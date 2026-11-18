#include "NativeFilesViewModel.h"
#include <LVUI>
#include <iiNetwork>
#include <algorithm>
#include <filesystem>
#include <limits>
#include <utility>

namespace {
using ii::core::Value;
const Value *field(const Value &object, std::string_view key) {
    auto found=object.find(key); return found ? &found->get() : nullptr;
}
std::string string(const Value &object, std::string_view key) {
    const auto *v=field(object,key); if (!v) return {};
    auto result=v->get<std::string>(); return result ? result->get() : std::string{};
}
std::optional<std::size_t> number(const Value &object, std::string_view key) {
    const auto *v=field(object,key); if (!v) return {};
    if (auto x=v->get<std::int64_t>(); x && x->get() >= 0) return static_cast<std::size_t>(x->get());
    if (auto x=v->get<std::uint64_t>(); x && x->get() <= std::numeric_limits<std::size_t>::max()) return static_cast<std::size_t>(x->get());
    return {};
}
std::optional<bool> boolean(const Value &object, std::string_view key) {
    const auto *v=field(object,key); if (!v) return {};
    auto result=v->get<bool>(); if (!result) return {}; return result->get();
}
std::vector<lvui::TableColumn> columns() {
    return {{"name","Name",0,true,false}, {"modified","Date modified",220,true,false},
            {"kind","Kind",130,true,false}, {"size","Size",120,true,false}};
}
bool validName(const std::string &value) {
    return !value.empty() && value.front()!='.' && value.find_first_of("/\\:")==std::string::npos;
}
bool validRelative(const std::string &value) {
    if (value.empty() || value.size()>4096 || value.front()=='/' || value.find('\\')!=std::string::npos
        || value.find('\0')!=std::string::npos) return false;
    std::size_t begin=0;
    for (;;) {
        const auto end=value.find('/',begin);
        const auto part=value.substr(begin,end==std::string::npos?end:end-begin);
        if (part.empty() || part=="." || part=="..") return false;
        if (end==std::string::npos) return true;
        begin=end+1;
    }
}
}

NativeFilesViewModel::NativeFilesViewModel()
    : source(std::make_shared<lvui::TableMemory>(columns())),
      controller(std::make_shared<lvui::TableController>(source,lvui::TableSelectionMode::range)),
      m_requestPrefix("native-files-"+ii::network::wire::uuid()+"-") {}

std::string NativeFilesViewModel::request(std::string_view op, Value::Object fields) {
    if (!onRequest || m_container.empty()) { status.set("Society backend unavailable"); return {}; }
    const auto id=m_requestPrefix+std::to_string(++m_sequence);
    fields.emplace("id",Value(id)); fields.emplace("op",Value(op)); fields.emplace("container",Value(m_container));
    if (!onRequest(Value(std::move(fields)))) { status.set("Could not send request to Society"); return {}; }
    return id;
}

void NativeFilesViewModel::open(std::string value) {
    if (value!=m_wantedPath) { m_search.clear(); searchDraft.set(""); }
    m_wantedPath=std::move(value); path.set(m_wantedPath); m_generation.clear(); m_directoryRevision.clear();
    m_directoryReady=false; closeCreateFolder();
    m_loadedRevision.clear(); m_pageRequest.clear(); m_stateRequest.clear(); m_activateRequest.clear(); m_openingFile.clear(); m_offset=m_count=0;
    clearRows(); status.set("Loading files"); updateControls();
    m_openRequest=request("directoryOpen",{{"path",Value(m_wantedPath)},{"query",Value(m_search)},
                                          {"sort",Value(m_sort)},{"descending",Value(m_descending)}});
}

void NativeFilesViewModel::receive(const Value &message) {
    const auto type=string(message,"type");
    if (type=="state") {
        const auto *state=field(message,"state"); if (!state) return;
        const auto container=string(*state,"selectedContainer");
        canReconnect.set(false);
        if (container==m_container) {
            applyModelState(*state);
            if (container.empty()) status.set("No Society drive selected"); return;
        }
        m_container=container; m_openRequest.clear(); m_pageRequest.clear(); m_stateRequest.clear();
        if (!m_importJob.empty() && m_importContainer!=container)
            modelImportStatus.set("Model import "+m_importJob+" belongs to another Society drive");
        if (m_container.empty()) { disconnected(); canReconnect.set(false); status.set("No Society drive selected"); return; }
        open(m_root);
        applyModelState(*state);
        queryJobs();
        return;
    }
    if (type=="directoryChanged") {
        const auto *state=field(message,"directory");
        if (state && m_openRequest.empty()) applyState(*state);
        return;
    }
    if (type=="directoryActivated") {
        if (string(message,"id")!=m_activateRequest || string(message,"generation")!=m_generation
            || string(message,"relative")!=m_openingFile) return;
        m_activateRequest.clear(); m_openingFile.clear(); updateControls();
        const auto error=string(message,"error"), target=string(message,"path");
        if (!error.empty()) { activity.set(error); return; }
        if (target.empty() || target.size()>4096 || !std::filesystem::path(target).is_absolute() || !onOpenFile) {
            activity.set("The selected file cannot be opened on this device"); return;
        }
        if (const auto failure=onOpenFile(target)) activity.set(*failure);
        else activity.set("Opening file");
        return;
    }
    if (type=="fileJobChanged") {
        const auto jobId=string(message,"jobId"); const auto found=m_jobs.find(jobId);
        if (found==m_jobs.end() || string(message,"container")!=found->second) return;
        const auto state=string(message,"state");
        if (state=="recorded") activity.set("File job recorded");
        else if (state=="finished") {
            const auto result=string(message,"path"), error=string(message,"error");
            activity.set(error.empty() ? (result.empty()?"File operation completed":"File operation completed: "+result)
                                      : (result.empty()?error:"Result: "+result+". "+error)+" (job "+jobId+")");
            if (!error.empty()) recordError(m_jobTargets[jobId],error+" (job "+jobId+")");
            // A status request sent before this event may report an older state.
            // Keep failed jobs until a fresh inspection establishes their outcome.
            std::erase_if(m_jobQueries,[&](const auto &query){return query.second==jobId;});
            if (error.empty()) { m_jobs.erase(found); m_jobTargets.erase(jobId); }
        }
        updateControls();
        return;
    }
    if (type!="result") return;
    const auto id=string(message,"id");
    if (!id.empty() && id==m_importRequest) {
        m_importRequest.clear();
        const auto requestedContainer=std::exchange(m_importRequestContainer,std::string{});
        if (boolean(message,"accepted")!=true || string(message,"jobId").empty()) {
            const auto error=string(message,"error");
            activity.set(error.empty()?"Model import was rejected":error); recordError("Model import",activity.get());
        } else {
            m_importJob=string(message,"jobId"); m_importContainer=requestedContainer;
            activity.set("Model import queued (job "+m_importJob+")");
            if (m_importContainer!=m_container)
                modelImportStatus.set("Model import "+m_importJob+" belongs to another Society drive");
            if (const auto *state=field(message,"state")) applyModelState(*state);
        }
        updateControls(); return;
    }
    if (!id.empty() && id==m_activateRequest) {
        if (boolean(message,"accepted")!=true) {
            const auto error=string(message,"error"); activity.set(error.empty()?"Could not prepare the selected file":error);
            m_activateRequest.clear(); m_openingFile.clear(); updateControls();
        } else activity.set("Preparing file");
        return;
    }
    if (const auto operation=m_operationRequests.find(id);operation!=m_operationRequests.end()) {
        const auto container=operation->second; m_operationRequests.erase(operation);
        const auto target=m_requestTargets[id]; m_requestTargets.erase(id);
        if (boolean(message,"accepted")!=true || string(message,"jobId").empty()) {
            const auto error=string(message,"error"); const auto reason=error.empty()?"File operation was rejected":error;
            activity.set(reason); recordError(target,reason); updateControls(); return;
        }
        m_jobs.emplace(string(message,"jobId"),container); m_jobTargets[string(message,"jobId")]=target;
        activity.set("File operation queued"); updateControls(); return;
    }
    if (const auto query=m_jobQueries.find(id);query!=m_jobQueries.end()) {
        const auto jobId=query->second; m_jobQueries.erase(query);
        if (!m_jobs.contains(jobId)) return;
        if (boolean(message,"accepted")!=true) { activity.set(string(message,"error")); return; }
        if (const auto *job=field(message,"job")) updateJob(*job,jobId);
        updateControls();
        return;
    }
    const bool opened=!id.empty() && id==m_openRequest;
    const bool stateReply=!id.empty() && id==m_stateRequest;
    const bool pageReply=!id.empty() && id==m_pageRequest;
    if (!opened && !stateReply && !pageReply) return;
    if (opened) m_openRequest.clear(); if (stateReply) m_stateRequest.clear(); if (pageReply) m_pageRequest.clear();
    if (boolean(message,"accepted")!=true) {
        const auto error=string(message,"error"); status.set(error.empty() ? "Society rejected the request" : error);
        updateControls(); return;
    }
    if (opened || stateReply) { if (const auto *state=field(message,"directory")) applyState(*state); return; }
    const auto offset=number(message,"offset"), count=number(message,"count");
    const auto *entries=field(message,"entries"); if (!entries) return;
    auto array=entries->get<Value::Array>();
    if (string(message,"generation")!=m_generation || string(message,"revision")!=m_directoryRevision
        || !offset || *offset!=m_offset || !count || *count!=m_count || m_offset>m_count || !array
        || array->get().size()>64 || array->get().size()>m_count-m_offset) return;
    std::vector<lvui::TableRow> rows; std::vector<Entry> parsed; std::vector<NativeFileGridRow> grid;
    for (const auto &entry : array->get()) {
        auto name=string(entry,"name"), locator=string(entry,"path"), size=string(entry,"size");
        auto directory=boolean(entry,"directory"); const auto prefix=m_wantedPath+"/";
        if (!directory || name.empty() || name.size()>4096 || locator!=prefix+name || name.find('/')!=std::string::npos) {
            status.set("The Society backend returned an invalid directory entry"); return;
        }
        const auto suffix=string(entry,"suffix");
        const auto kind=*directory ? "Folder" : (suffix.empty() ? "File" : suffix);
        rows.push_back({.key=locator, .cells={{name,"text",true,false},{string(entry,"modified"),"text",true,false},
            {kind,"text",true,false},{*directory ? std::string{} : size,"text",true,false}}});
        grid.push_back({id+"-"+std::to_string(grid.size()),locator,name,kind,*directory ? std::string{} : size});
        parsed.push_back({std::move(locator),*directory});
    }
    auto old=source->snapshot(); if (!old) { status.set("Could not read the table state"); return; }
    auto replaced=source->replace(columns(),std::move(rows),(*old)->revision);
    if (!replaced) { status.set(replaced.error().message); return; }
    m_entries=std::move(parsed); m_selected.clear(); m_selectedPaths.clear(); gridRows.set(std::move(grid));
    revision.set(static_cast<std::int64_t>(*replaced));
    m_loadedRevision=m_directoryRevision; status.set(m_count ? std::to_string(m_count)+" items" : "No items"); updateControls();
}

void NativeFilesViewModel::applyState(const Value &state) {
    const auto incomingPath=string(state,"path"), generation=string(state,"generation"), rev=string(state,"revision");
    const auto count=number(state,"count");
    if (incomingPath!=m_wantedPath || generation.empty() || rev.empty() || !count) return;
    if (!m_generation.empty() && generation!=m_generation) return;
    const bool changed=m_generation!=generation || m_directoryRevision!=rev;
    m_generation=generation; m_directoryRevision=rev; m_count=*count;
    if (changed) { m_loadedRevision.clear(); m_pageRequest.clear(); m_offset=0; clearRows(); }
    m_directoryReady=string(state,"status")=="ready";
    if (!m_directoryReady) { status.set("Loading files"); updateControls(); return; }
    if (m_loadedRevision==rev) status.set(m_count ? std::to_string(m_count)+" items" : "No items");
    else if (m_pageRequest.empty()) requestPage();
    updateControls();
}

void NativeFilesViewModel::requestPage() {
    if (m_generation.empty() || m_directoryRevision.empty()) return;
    m_pageRequest=request("directoryPage",{{"generation",Value(m_generation)},{"revision",Value(m_directoryRevision)},
                                           {"offset",Value(m_offset)},{"limit",Value(64)}});
}
void NativeFilesViewModel::clearRows() {
    m_entries.clear(); m_selected.clear(); m_selectedPaths.clear(); gridRows.set({}); renameOpen.set(false); closeMove(); closeRemove();
    if (auto old=source->snapshot()) if (auto replaced=source->replace(columns(),{},(*old)->revision)) revision.set(static_cast<std::int64_t>(*replaced));
    canOpen.set(false);
}
void NativeFilesViewModel::updateControls() {
    canUp.set(!m_generation.empty() && m_wantedPath!=m_root && m_wantedPath.starts_with(m_root+"/"));
    canPrevious.set(!m_generation.empty() && m_offset>0 && m_pageRequest.empty());
    canNext.set(!m_generation.empty() && m_offset+64<m_count && m_pageRequest.empty());
    const bool selected=!m_selectedPaths.empty(), single=m_selectedPaths.size()==1;
    canOpen.set(single && m_activateRequest.empty());
    const bool deleted=m_wantedPath=="Deleted" || m_wantedPath.starts_with("Deleted/");
    canRename.set(single); canTrash.set(selected && !deleted); canRestore.set(selected && deleted);
    canDuplicate.set(selected && !deleted); canMove.set(selected && !deleted); canRemove.set(selected && deleted);
    canCreateFolder.set(m_directoryReady && !m_generation.empty() && !deleted);
    canImportModels.set(modelsActive.get() && !m_container.empty() && !m_choosingModels && m_importRequest.empty() && !m_modelBusy);
    canImportFiles.set(filesActive.get() && m_directoryReady && !m_container.empty() && !m_generation.empty()
                       && !m_choosingFiles && m_operationRequests.size()+m_jobs.size()<128);
}
void NativeFilesViewModel::applyModelState(const Value &state) {
    if (string(state,"selectedContainer")!=m_container) return;
    if (const auto busy=boolean(state,"busy")) m_modelBusy=*busy;
    const auto job=string(state,"currentJobId"), stateText=string(state,"status"), error=string(state,"errorString");
    if (!job.empty() && job==m_importJob && m_importContainer==m_container) {
        auto label=stateText;
        if (m_modelBusy) if (const auto *progress=field(state,"progress")) if (const auto fraction=progress->get<double>();
            fraction && fraction->get()>=0 && fraction->get()<=1)
            label+=" ("+std::to_string(int(fraction->get()*100))+"%)";
        if (!label.empty()) {
            if (m_modelBusy) modelImportStatus.set(label);
            else { modelImportStatus.set(""); activity.set(label); }
        }
        if (!m_modelBusy) {
            if (!error.empty()) recordError("Model import "+job,error);
            m_importJob.clear(); m_importContainer.clear();
        }
    }
    updateControls();
}
void NativeFilesViewModel::selected(const std::optional<lvui::TableSelection> &selection) {
    if (gridActive.get()) return;
    std::vector<std::string> paths;
    if (selection) {
        const auto first=std::find_if(m_entries.begin(),m_entries.end(),[&](const auto &e){return e.path==selection->anchor.row;});
        const auto last=std::find_if(m_entries.begin(),m_entries.end(),[&](const auto &e){return e.path==selection->current.row;});
        if (first!=m_entries.end() && last!=m_entries.end()) {
            const auto bounds=std::minmax(first,last);
            for (auto it=bounds.first;it!=bounds.second+1;++it) paths.push_back(it->path);
        }
    }
    setSelection(std::move(paths));
}
void NativeFilesViewModel::setSelection(std::vector<std::string> paths) {
    if (paths!=m_selectedPaths) { closeMove(); closeRemove(); renameOpen.set(false); }
    m_selectedPaths=std::move(paths);
    m_selected=m_selectedPaths.size()==1 ? m_selectedPaths.front() : std::string{};
    const auto slash=m_selected.find_last_of('/'); renameDraft.set(slash==std::string::npos?m_selected:m_selected.substr(slash+1));
    updateControls();
}
void NativeFilesViewModel::showList() {
    if (!gridActive.get()) return;
    gridActive.set(false); (void)controller.get()->clear_selection(); setSelection({});
}
void NativeFilesViewModel::showGrid() {
    if (gridActive.get()) return;
    (void)controller.get()->clear_selection(); setSelection({}); gridActive.set(true);
}
void NativeFilesViewModel::gridSelected(const std::vector<std::string> &keys) {
    if (!gridActive.get()) return;
    std::vector<std::string> paths;
    for (const auto &row:gridRows.get())
        if (std::find(keys.begin(),keys.end(),row.id)!=keys.end()) paths.push_back(row.path);
    setSelection(std::move(paths));
}
void NativeFilesViewModel::gridActivated(const std::string &key) {
    if (!gridActive.get()) return;
    const auto &rows=gridRows.get();
    const auto row=std::find_if(rows.begin(),rows.end(),[&](const auto &item){return item.id==key;});
    if (row==rows.end() || !canOpen.get() || m_selected!=row->path) return;
    openSelected();
}
void NativeFilesViewModel::chooseModels() {
    if (!canImportModels.get() || !onChooseModels) return;
    m_choosingModels=true; m_chooseContainer=m_container; updateControls(); onChooseModels();
}
void NativeFilesViewModel::chooseFiles() {
    if (!canImportFiles.get() || !onChooseFiles) return;
    m_choosingFiles=true; m_chooseFilesContainer=m_container; m_chooseFilesPath=m_wantedPath;
    m_chooseFilesGeneration=m_generation; updateControls(); onChooseFiles();
}
void NativeFilesViewModel::fileChooserFailed(const std::string &message) {
    if (!m_choosingFiles) return;
    m_choosingFiles=false; m_chooseFilesContainer.clear(); m_chooseFilesPath.clear(); m_chooseFilesGeneration.clear();
    if (!message.empty()) { activity.set(message); recordError("File import",message); }
    updateControls();
}
void NativeFilesViewModel::filesChosen(const std::vector<std::string> &paths) {
    if (!m_choosingFiles) return;
    m_choosingFiles=false;
    const auto container=std::exchange(m_chooseFilesContainer,{}), destination=std::exchange(m_chooseFilesPath,{});
    const auto generation=std::exchange(m_chooseFilesGeneration,{});
    if (paths.empty()) { updateControls(); return; }
    if (container!=m_container || destination!=m_wantedPath || generation!=m_generation || !m_directoryReady) {
        activity.set("The destination changed; choose files again"); updateControls(); return;
    }
    if (paths.size()>128 || paths.size()+m_operationRequests.size()+m_jobs.size()>128) {
        activity.set("Too many file operations are pending"); updateControls(); return;
    }
    std::vector<std::string> unique;
    for (const auto &path:paths) {
        const std::filesystem::path input(path);
        if (path.empty() || path.size()>4096 || path.find('\0')!=std::string::npos || !input.is_absolute()
            || input.lexically_normal()!=input) {
            activity.set("Invalid file path"); updateControls(); return;
        }
        if (std::find(unique.begin(),unique.end(),path)==unique.end()) unique.push_back(path);
    }
    for (const auto &path:unique) submitOperation("copy",destination,path);
    updateControls();
}
void NativeFilesViewModel::modelChooserFailed(const std::string &message) {
    if (!m_choosingModels) return;
    m_choosingModels=false; m_chooseContainer.clear();
    if (!message.empty()) { activity.set(message); recordError("Model import",message); }
    updateControls();
}
void NativeFilesViewModel::modelsChosen(const std::vector<std::string> &paths) {
    if (!m_choosingModels) return;
    m_choosingModels=false;
    const auto chosenContainer=std::exchange(m_chooseContainer,{});
    if (paths.empty()) { updateControls(); return; }
    if (m_container.empty() || m_container!=chosenContainer) {
        activity.set("The selected Society drive changed; choose models again"); updateControls(); return;
    }
    if (paths.size()>512) { activity.set("Too many models selected"); updateControls(); return; }
    Value::Array sources;
    for (const auto &path:paths) {
        if (path.empty() || path.size()>4096 || path.find('\0')!=std::string::npos || !std::filesystem::path(path).is_absolute()) {
            activity.set("Invalid model path"); updateControls(); return;
        }
        sources.emplace_back(path);
    }
    m_importRequest=request("import",{{"sources",Value(std::move(sources))}});
    if (!m_importRequest.empty()) { m_importRequestContainer=chosenContainer; activity.set("Submitting model import"); }
    else recordError("Model import","Could not submit models");
    updateControls();
}
void NativeFilesViewModel::openSelected() {
    if (!canOpen.get()) return;
    const auto found=std::find_if(m_entries.begin(),m_entries.end(),[&](const auto &entry){return entry.path==m_selected;});
    if (found==m_entries.end()) return;
    if (found->directory) { open(m_selected); return; }
    m_openingFile=m_selected;
    m_activateRequest=request("directoryActivate",{{"generation",Value(m_generation)},{"revision",Value(m_directoryRevision)},{"path",Value(m_openingFile)}});
    if (m_activateRequest.empty()) m_openingFile.clear();
    else activity.set("Preparing file");
    updateControls();
}
void NativeFilesViewModel::up() { if (canUp.get()) open(m_wantedPath.substr(0,m_wantedPath.find_last_of('/'))); }
void NativeFilesViewModel::previous() { if (canPrevious.get()) { m_offset-=std::min<std::size_t>(m_offset,64); m_loadedRevision.clear(); clearRows(); requestPage(); updateControls(); } }
void NativeFilesViewModel::next() { if (canNext.get()) { m_offset+=64; m_loadedRevision.clear(); clearRows(); requestPage(); updateControls(); } }
void NativeFilesViewModel::refresh() {
    if (!m_generation.empty() && m_stateRequest.empty()) m_stateRequest=request("directoryState");
    queryJobs();
}
void NativeFilesViewModel::queryJobs() {
    for (const auto &[jobId,container]:m_jobs) {
        if (container!=m_container || std::any_of(m_jobQueries.begin(),m_jobQueries.end(),[&](const auto &query){return query.second==jobId;})) continue;
        const auto query=request("fileJobStatus",{{"jobId",Value(jobId)}});
        if (!query.empty()) m_jobQueries.emplace(query,jobId);
    }
}
void NativeFilesViewModel::reconnect() {
    if (!canReconnect.get() || !onReconnect) return;
    canReconnect.set(false); status.set("Connecting to Society"); onReconnect();
}
void NativeFilesViewModel::searchEdited(const std::string &value) { searchDraft.set(value); }
void NativeFilesViewModel::search() {
    if (searchDraft.get().size()>1024) { status.set("Search text is too long"); return; }
    m_search=searchDraft.get(); open(m_wantedPath);
}
void NativeFilesViewModel::setSort(const std::string &field,const std::string &label) {
    if (m_sort==field) m_descending=!m_descending;
    else { m_sort=field; m_descending=false; }
    sortLabel.set("Sort: "+label+(m_descending?" (descending)":" (ascending)"));
    open(m_wantedPath);
}
void NativeFilesViewModel::sortName() { setSort("name","Name"); }
void NativeFilesViewModel::sortDate() { setSort("modified","Date"); }
void NativeFilesViewModel::sortKind() { setSort("kind","Kind"); }
void NativeFilesViewModel::sortSize() { setSort("size","Size"); }
void NativeFilesViewModel::selectSection(std::string root) {
    m_root=std::move(root); filesActive.set(m_root=="Files"); modelsActive.set(m_root=="Models"); deletedActive.set(m_root=="Deleted");
    if (m_container.empty()) { m_wantedPath=m_root; path.set(m_root); clearRows(); updateControls(); return; }
    open(m_root);
}
void NativeFilesViewModel::showFiles() { selectSection("Files"); }
void NativeFilesViewModel::showModels() { selectSection("Models"); }
void NativeFilesViewModel::showDeleted() { selectSection("Deleted"); }
void NativeFilesViewModel::showRename() { if (canRename.get()) renameOpen.set(true); }
void NativeFilesViewModel::closeRename() { renameOpen.set(false); }
void NativeFilesViewModel::renameEdited(const std::string &value) { renameDraft.set(value); }
void NativeFilesViewModel::renameSelected() {
    const auto &name=renameDraft.get();
    if (!canRename.get() || !validName(name)) return;
    submitOperation("rename",name); renameOpen.set(false);
}
void NativeFilesViewModel::trashSelected() { if (canTrash.get()) submitSelection("trash"); }
void NativeFilesViewModel::restoreSelected() { if (canRestore.get()) submitSelection("restore"); }
void NativeFilesViewModel::duplicateSelected() { if (canDuplicate.get()) submitSelection("duplicate"); }
void NativeFilesViewModel::showMove() {
    if (!canMove.get()) return;
    m_moveTargets=m_selectedPaths; moveDraft.set(m_root); moveOpen.set(true);
}
void NativeFilesViewModel::closeMove() { moveOpen.set(false); m_moveTargets.clear(); }
void NativeFilesViewModel::moveEdited(const std::string &value) { moveDraft.set(value); }
void NativeFilesViewModel::moveSelected() {
    if (!moveOpen.get() || !canMove.get() || m_selectedPaths!=m_moveTargets || !validRelative(moveDraft.get())) return;
    const auto paths=m_moveTargets;
    const auto destination=moveDraft.get();
    for (const auto &path:paths) submitOperation("move",destination,path);
    closeMove();
}
void NativeFilesViewModel::showCreateFolder() { if (canCreateFolder.get()) { folderDraft.set(""); folderOpen.set(true); } }
void NativeFilesViewModel::closeCreateFolder() { folderOpen.set(false); }
void NativeFilesViewModel::folderEdited(const std::string &value) { folderDraft.set(value); }
void NativeFilesViewModel::createFolder() {
    if (!folderOpen.get() || !canCreateFolder.get() || !validName(folderDraft.get())) return;
    submitOperation("create-directory",folderDraft.get(),m_wantedPath); closeCreateFolder();
}
void NativeFilesViewModel::showRemove() {
    if (!canRemove.get()) return;
    m_removeTargets=m_selectedPaths;
    removePrompt.set(m_removeTargets.size()==1 ? "Delete permanently: "+m_removeTargets.front()+"?"
                                              : "Delete "+std::to_string(m_removeTargets.size())+" selected items permanently?");
    removeOpen.set(true);
}
void NativeFilesViewModel::closeRemove() { removeOpen.set(false); m_removeTargets.clear(); }
void NativeFilesViewModel::removeSelected() {
    if (!removeOpen.get() || !canRemove.get() || m_selectedPaths!=m_removeTargets) return;
    submitSelection("remove"); closeRemove();
}
void NativeFilesViewModel::submitSelection(const std::string &action) {
    const auto paths=m_selectedPaths;
    for (const auto &path:paths) submitOperation(action,{},path);
}
bool NativeFilesViewModel::submitOperation(const std::string &action,const std::string &argument,const std::string &source) {
    const auto target=source.empty()?m_selected:source;
    if (m_operationRequests.size()+m_jobs.size()>=128) {
        activity.set("Too many file operations are pending"); recordError(target,activity.get()); return false;
    }
    Value::Object args{{"action",Value(action)},{"path",Value(target)}};
    if (!argument.empty()) args.emplace("argument",Value(argument));
    const auto id=request("fileOperation",std::move(args));
    if (!id.empty()) { m_operationRequests.emplace(id,m_container); m_requestTargets.emplace(id,target); activity.set("Submitting file operation"); return true; }
    recordError(target,"Could not send file operation"); return false;
}
void NativeFilesViewModel::recordError(const std::string &path,const std::string &message) {
    const auto entry=path.empty()?message:path+": "+message;
    if (std::find(m_errors.begin(),m_errors.end(),entry)==m_errors.end()) m_errors.push_back(entry);
    if (m_errors.size()>128) { m_errors.erase(m_errors.begin()); ++m_omittedErrors; }
    m_errorCursor=m_errors.size()-1;
    updateErrors();
}
void NativeFilesViewModel::updateErrors() {
    std::string joined=m_omittedErrors ? std::to_string(m_omittedErrors)+" older errors omitted" : std::string{};
    for (const auto &error:m_errors) { if (!joined.empty()) joined+="\n"; joined+=error; }
    errors.set(joined); hasErrors.set(!m_errors.empty());
    currentError.set(m_errors.empty()?std::string{}:m_errors[m_errorCursor]);
    errorCountLabel.set(m_errors.empty()?std::string{}:std::to_string(m_errorCursor+1)+" / "+std::to_string(m_errors.size())
                        +(m_omittedErrors?" ("+std::to_string(m_omittedErrors)+" older omitted)":""));
    canPreviousError.set(!m_errors.empty() && m_errorCursor>0);
    canNextError.set(!m_errors.empty() && m_errorCursor+1<m_errors.size());
}
void NativeFilesViewModel::clearErrors() { m_errors.clear(); m_omittedErrors=0; m_errorCursor=0; updateErrors(); }
void NativeFilesViewModel::previousError() { if (canPreviousError.get()) { --m_errorCursor; updateErrors(); } }
void NativeFilesViewModel::nextError() { if (canNextError.get()) { ++m_errorCursor; updateErrors(); } }
void NativeFilesViewModel::updateJob(const Value &job,const std::string &id) {
    const auto state=string(job,"state"), error=string(job,"error"), result=string(job,"path");
    if (state=="succeeded") { activity.set(result.empty()?"File operation completed":"File operation completed: "+result); m_jobs.erase(id); }
    else if (state=="attention") {
        const auto message=error.empty()?"File job "+id+" needs verification":error+" (job "+id+")";
        activity.set(result.empty()?message:"Result: "+result+". "+message);
        recordError(m_jobTargets[id],message);
    }
    else if (state=="failed" || state=="partial" || state=="cancelled") {
        const auto message=error.empty()?"File operation needs attention":error;
        activity.set(result.empty()?message:"Result: "+result+". "+message);
        recordError(m_jobTargets[id],message+" (job "+id+")"); m_jobs.erase(id); m_jobTargets.erase(id);
    } else activity.set("File job "+state);
}
void NativeFilesViewModel::disconnected() {
    if (!m_operationRequests.empty()) activity.set("A submitted operation may still be running; check its result before retrying");
    else if (!m_activateRequest.empty()) activity.set("File preparation was interrupted; reconnect and try again");
    for (const auto &[id,target]:m_requestTargets) { (void)id; recordError(target,"Submission outcome unknown; verify before retrying"); }
    if (!m_importRequest.empty()) {
        recordError("Model import","Submission outcome unknown; verify before retrying"); m_importRequest.clear();
        m_importRequestContainer.clear();
    }
    if (!m_importJob.empty()) modelImportStatus.set("Model import "+m_importJob+" may still be running; reconnect to check");
    m_modelBusy=false;
    m_operationRequests.clear(); m_requestTargets.clear(); m_jobQueries.clear();
    m_container.clear(); m_generation.clear(); m_directoryRevision.clear(); m_openRequest.clear(); m_pageRequest.clear(); m_stateRequest.clear(); m_activateRequest.clear(); m_openingFile.clear();
    m_directoryReady=false; closeCreateFolder();
    clearRows(); updateControls(); canReconnect.set(true); status.set("Society backend disconnected");
}
void NativeFilesViewModel::backendError(std::string message) {
    disconnected(); status.set(message.empty() ? "Society backend error" : std::move(message));
}
void NativeFilesViewModel::resize(double width, double height) {
    const auto &tokens=lvui::default_style_tokens();
    const auto pad=tokens.number("--lvrs-theme-metrics-space-18").value();
    const auto row=tokens.number("--lvrs-theme-metrics-size-56").value();
    const auto minimum=tokens.number("--lvrs-theme-metrics-size-140").value();
    const auto side=tokens.number("--lvrs-theme-metrics-size-256").value()-2*tokens.number("--lvrs-theme-metrics-space-12").value();
    navigationWidth.set(side);
    tableWidth.set(std::max(minimum,width-side-3*pad)); tableHeight.set(std::max(minimum,height-2*pad-3*row));
}
