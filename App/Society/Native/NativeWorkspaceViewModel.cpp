#include "NativeWorkspaceViewModel.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <unordered_set>
#include <utility>

namespace {
std::vector<lvui::TableColumn> columns(bool suggested) {
    std::vector<lvui::TableColumn> values={{"name", "Name", 0, true, false}, {"modified", suggested ? "Date modified" : "Last activity", 196, true, false},
            {"kind", "Kind", 188, true, false}, {"size", "Size", 120, true, false}};
    if (suggested) values.push_back({"reason","Reason",140,true,false});
    return values;
}
std::string folded(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return c < 128 ? static_cast<char>(std::tolower(c)) : static_cast<char>(c);
    });
    return value;
}
bool validName(const std::string &name) {
    return !name.empty() && name.front()!='.' && name.find_first_of("/\\:")==std::string::npos && name.find('\0')==std::string::npos;
}
bool filesDestination(const std::string &value) {
    if (value!="Files" && !value.starts_with("Files/")) return false;
    if (value.find('\\')!=std::string::npos || value.find('\0')!=std::string::npos) return false;
    std::size_t first=0;
    for (;;) {
        const auto last=value.find('/',first); const auto part=value.substr(first,last==std::string::npos?last:last-first);
        if (part.empty() || part=="." || part=="..") return false;
        if (last==std::string::npos) return true; first=last+1;
    }
}
}

NativeWorkspaceViewModel::NativeWorkspaceViewModel()
    : source(std::make_shared<lvui::TableMemory>(columns(true))),
      controller(std::make_shared<lvui::TableController>(source, lvui::TableSelectionMode::range)) { rebuildRibbon(); }

void NativeWorkspaceViewModel::setPage(Page page, bool notify) {
    closeFileMenu();
    if (page == m_page) { if (notify && onNavigate) onNavigate(page); return; }
    m_page = page;
    if (page == Page::Suggested) { m_sort = Sort::Recommended; m_descending = false; }
    if (page == Page::Recent) { m_sort = Sort::Date; m_descending = true; }
    canSortSize.set(page != Page::Recent);
    suggestedActive.set(page == Page::Suggested); recentActive.set(page == Page::Recent);
    projectsActive.set(page == Page::Projects); calendarActive.set(page == Page::Calendar);
    activityActive.set(page == Page::Activity);
    contentVisible.set(page == Page::Suggested || page == Page::Recent);
    clearSelection();
    if (contentVisible.get()) rebuild();
    if (notify && onNavigate) onNavigate(page);
}
void NativeWorkspaceViewModel::showSuggested() { setPage(Page::Suggested, true); }
void NativeWorkspaceViewModel::showRecent() { setPage(Page::Recent, true); }
void NativeWorkspaceViewModel::showProjects() { setPage(Page::Projects, true); }
void NativeWorkspaceViewModel::showCalendar() { setPage(Page::Calendar, true); }
void NativeWorkspaceViewModel::showActivity() { setPage(Page::Activity, true); }
void NativeWorkspaceViewModel::setSuggestedFiles(std::vector<NativeWorkspaceFile> files) {
    if (m_suggested == files) return;
    m_suggested = std::move(files); if (m_page == Page::Suggested) rebuild();
}
void NativeWorkspaceViewModel::setRecentFiles(std::vector<NativeWorkspaceFile> files) {
    std::erase_if(files, [](const auto &file) { return file.directory || file.path.empty(); });
    std::stable_sort(files.begin(),files.end(),[](const auto &a,const auto &b) {
        return a.modifiedTime==b.modifiedTime ? a.path<b.path : a.modifiedTime>b.modifiedTime;
    });
    std::unordered_set<std::string> paths;
    std::erase_if(files,[&](const auto &file) { return !paths.insert(file.path).second; });
    if (files.size()>500) files.resize(500);
    if (m_recent == files) return;
    m_recent = std::move(files); if (m_page == Page::Recent) rebuild();
}
void NativeWorkspaceViewModel::setGroups(std::vector<NativeWorkspaceGroup> guildValues,
                                          std::vector<NativeWorkspaceGroup> organizationValues) {
    hasGuilds.set(!guildValues.empty()); hasOrganizations.set(!organizationValues.empty());
    guilds.set(std::move(guildValues)); organizations.set(std::move(organizationValues));
}
void NativeWorkspaceViewModel::setAgentAvailable(bool available) {
    agentAvailable.set(available);
    agentStatus.set(available ? "Start a conversation" : "Agent session is not connected.");
    updateSend();
}
void NativeWorkspaceViewModel::setAgentModels(std::vector<NativeAgentModel> models) {
    agentModels.set(std::move(models));
}
void NativeWorkspaceViewModel::setAgentState(const std::string &model, bool busy, const std::string &status,
                                            std::vector<NativeAgentMessage> messages) {
    for (auto &message : messages) {
        std::size_t lines=1, column=0;
        for (const unsigned char c : message.text) {
            if (c=='\n') { ++lines; column=0; }
            else if ((c & 0xc0) != 0x80 && ++column >= 34) { ++lines; column=0; }
        }
        message.height=std::min(4096.0,std::max(48.0,static_cast<double>(lines)*20.0+20.0));
    }
    const auto &models = agentModels.get();
    const auto found = std::find_if(models.begin(), models.end(), [&](const auto &item) { return item.id == model; });
    agentModelLabel.set(found == models.end() ? "Select model" : "Automatic · " + found->name);
    agentAvailable.set(found != models.end());
    agentBusy.set(busy);
    agentStatus.set(status);
    agentEmpty.set(messages.empty() && !busy);
    agentMessages.set(std::move(messages));
    updateSend();
}
void NativeWorkspaceViewModel::toggleAgentModels() { agentModelMenuVisible.set(!agentModelMenuVisible.get()); }
std::function<void()> NativeWorkspaceViewModel::selectAgentModel(const std::string &id) {
    return [this, id] { agentModelMenuVisible.set(false); if (onSelectModel && !agentBusy.get()) onSelectModel(id); };
}
void NativeWorkspaceViewModel::stopAgent() { if (agentBusy.get() && onStopAgent) onStopAgent(); }
void NativeWorkspaceViewModel::retryAgent() {
    if (!agentRetryable.get() || agentBusy.get()) return;
    const auto &messages=agentMessages.get();
    for (auto it=messages.rbegin();it!=messages.rend();++it)
        if (it->role=="You") { promptEdited(it->text); break; }
}
void NativeWorkspaceViewModel::updateSend() {
    const auto &value = prompt.get();
    canSend.set(agentAvailable.get() && !agentBusy.get() && std::any_of(value.begin(), value.end(), [](unsigned char c) { return !std::isspace(c); }));
}
void NativeWorkspaceViewModel::promptEdited(const std::string &value) { prompt.set(value); updateSend(); }
void NativeWorkspaceViewModel::searchEdited(const std::string &value) { query.set(value); rebuild(); }
void NativeWorkspaceViewModel::showList() { if (gridActive.get()) { clearSelection(); gridActive.set(false); rebuildRibbon(); } }
void NativeWorkspaceViewModel::showGrid() { if (!gridActive.get()) { clearSelection(); gridActive.set(true); rebuildRibbon(); } }
void NativeWorkspaceViewModel::setRibbon(Ribbon tab) {
    ribbonHomeActive.set(tab == Ribbon::Home); ribbonShareActive.set(tab == Ribbon::Share);
    ribbonViewActive.set(tab == Ribbon::View); ribbonManageActive.set(tab == Ribbon::Manage);
    closeFileMenu(); ribbonExpanded.set(true); ribbonToggleLabel.set("Collapse"); rebuildRibbon();
    resize(fileWidth.get() + 178.0 + (showAgent.get() ? 304.0 : 0.0), viewHeight.get());
}
void NativeWorkspaceViewModel::showRibbonHome() { setRibbon(Ribbon::Home); }
void NativeWorkspaceViewModel::showRibbonShare() { setRibbon(Ribbon::Share); }
void NativeWorkspaceViewModel::showRibbonView() { setRibbon(Ribbon::View); }
void NativeWorkspaceViewModel::showRibbonManage() { setRibbon(Ribbon::Manage); }
void NativeWorkspaceViewModel::toggleRibbon() {
    ribbonExpanded.set(!ribbonExpanded.get()); ribbonToggleLabel.set(ribbonExpanded.get()?"Collapse":"Expand");
    closeFileMenu();
    resize(fileWidth.get() + 178.0 + (showAgent.get() ? 304.0 : 0.0), viewHeight.get());
}
void NativeWorkspaceViewModel::toggleFileMenu() { fileMenuOpen.set(!fileMenuOpen.get()); if (onFileMenuChanged) onFileMenuChanged(fileMenuOpen.get()); }
void NativeWorkspaceViewModel::closeFileMenu() { const bool wasOpen=fileMenuOpen.get(); fileMenuOpen.set(false); if (wasOpen && onFileMenuChanged) onFileMenuChanged(false); }
void NativeWorkspaceViewModel::refreshFiles() { closeFileMenu(); if (onRefresh) onRefresh(); }
void NativeWorkspaceViewModel::recordOperationError(const std::string &message) {
    if (message.empty()) return;
    auto current=operationError.get();
    if (current.find(message)!=std::string::npos) return;
    if (!current.empty()) current+="\n";
    if (current.size()+message.size()>8000) current="Earlier file errors were omitted.\n";
    current+=message.substr(0,4000); operationError.set(std::move(current)); hasOperationError.set(true);
}
void NativeWorkspaceViewModel::clearOperationError() { operationError.set(""); hasOperationError.set(false); }
void NativeWorkspaceViewModel::toggleAgent() {
    m_agentRequested=!m_agentRequested;
    const auto width=fileWidth.get()+178.0+(showAgent.get()?304.0:0.0);
    resize(width,viewHeight.get()); rebuildRibbon();
}
void NativeWorkspaceViewModel::setFileActionsAvailable(bool available) { fileActionsAvailable.set(available); rebuildRibbon(); }
void NativeWorkspaceViewModel::setClipboardHasFiles(bool available) { clipboardHasFiles.set(available); rebuildRibbon(); }
void NativeWorkspaceViewModel::resetDriveContext() {
    closeFileMenu(); clearSelection(); closeRename(); closeMove(); closeCreateFolder(); closePaste(); closeDetails();
    operationStatus.set("");
}
void NativeWorkspaceViewModel::rebuildRibbon() {
    using A=NativeRibbonAction; using G=NativeRibbonGroup;
    const bool single=canOpenSelection.get(), selected=canModifySelection.get(), writable=selected && fileActionsAvailable.get();
    const auto item=std::find_if(rows.get().begin(),rows.get().end(),[&](const auto &file){return file.path==m_selected;});
    const bool identified=single && item!=rows.get().end() && !item->resourceId.empty();
    bool invert=gridActive.get() || m_selectedPaths.empty() || m_selectedPaths.size()==rows.get().size();
    if (!invert && !rows.get().empty()) invert=m_selectedPaths.front()==rows.get().front().path || m_selectedPaths.back()==rows.get().back().path;
    std::vector<G> groups;
    if (ribbonHomeActive.get()) groups={
        {"clipboard","Clipboard",{{"paste","Paste","generalpaste",clipboardHasFiles.get() && fileActionsAvailable.get()},{"cut","Cut","generalcut",false},{"copy","Copy","generalcopy",selected},{"copy-path","Copy path","generalcopy",selected}}},
        {"organize","Organize",{{"move","Move to","move",writable},{"copy-to","Copy to","copyOfFolder",writable},{"rename","Rename","rename-asset",writable && single},{"trash","Delete","generaldelete",writable}}},
        {"new","New",{{"new-folder","New folder","nodesnewFolder",fileActionsAvailable.get()},{"import","Import","import-document",false},{"duplicate","Duplicate","save-copy",writable}}},
        {"open","Open",{{"open","Open","open-document",single},{"open-with","Open with","panel-file-open",false},{"properties","Properties","document-properties",single}}},
        {"select","Select",{{"select-all","Select all","selectAll",!rows.get().empty()},{"clear","Clear","generalclose",selected},{"invert","Invert","direct-select",invert && !rows.get().empty()}}}
    };
    else if (ribbonShareActive.get()) groups={
        {"share","Share",{{"share","Share","cwmShare",false},{"copy-link","Copy link","generalcopy",false},{"send-copy","Send copy","linked-document",false},{"recipients","Recipients","cwmAccess",false}}},
        {"access","Access",{{"manage-access","Manage access","lock",false},{"add-people","Add people","cwmAccess",false},{"permissions","Permissions","cwmPermissionsGranted",false},{"revoke","Revoke access","generalclose",false}}},
        {"publish","Publish",{{"publish","Publish post","generalupload",false},{"view-posts","View posts","generalopen",false},{"license","License","panel-license-version",false}}},
        {"release","Release",{{"release","Create release","package-file",false},{"export-package","Export package","package-file",false},{"export-files","Export files","generalexport",false}}},
        {"information","Information",{{"resource-id","Resource ID","panel-metadata",identified},{"activity","Activity","document-history",true},{"details","Details","document-properties",single}}}
    };
    else if (ribbonViewActive.get()) groups={
        {"layout","Layout",{{"details-layout","Details","panel-eye-detail",true},{"list","List","list",true},{"grid","Grid","grid",true},{"compact","Compact","list",false}}},
        {"arrange","Arrange",{{"sort","Sort by","sort-assets",true},{"group","Group by","group",false},{"filter","Filter","filter-assets",false},{"refresh","Refresh","generalrefresh",true}}},
        {"panes","Panes",{{"preview","Preview","asset-preview",false},{"inspector","Inspector","webInspector",false},{"agent","Agent","codeAssistantGlobal",true}}},
        {"show","Show",{{"extensions","Extensions","document-properties",false},{"locations","Locations","asset-location",false},{"status","Status","cwmVerified",false}}},
        {"navigate","Navigate",{{"back","Back","generalchevronLeft",false},{"forward","Forward","generalchevronRight",false},{"up","Up","generalchevronUp",canUp.get()}}}
    };
    else groups={
        {"resource","Resource",{{"properties","Properties","document-properties",single},{"metadata","Metadata","panel-metadata",false},{"copy-id","Copy ID","generalcopy",identified},{"open-with","Open with","generalopen",false}}},
        {"revision","Revision",{{"history","View history","document-history",false},{"compare","Compare","document-history",false},{"commit","Commit revision","generalsave",false},{"restore","Restore revision","generalundo",false}}},
        {"locations","Locations",{{"locations","View locations","asset-location",false},{"replicate","Replicate","cloud-save",false},{"offline","Keep offline","cloud-save",false}}},
        {"maintenance","Maintenance",{{"verify","Verify","cwmVerified",false},{"reindex","Reindex","generalrefresh",false},{"preview","Rebuild preview","asset-preview",false}}},
        {"project","Project",{{"release","Create release","package-file",false},{"project","Add to project","projectProperties",false},{"activity","Activity","document-history",true}}}
    };
    ribbonGroups.set(std::move(groups));
}
std::function<void()> NativeWorkspaceViewModel::invokeRibbon(const std::string &id) {
    return [this,id] {
        const auto &groups=ribbonGroups.get();
        const bool enabled=std::any_of(groups.begin(),groups.end(),[&](const auto &group){return std::any_of(group.actions.begin(),group.actions.end(),[&](const auto &action){return action.id==id && action.enabled;});});
        if (!enabled) return;
        if (id=="open") openSelected(); else if (id=="copy-path") copyPaths(); else if (id=="copy") copyFiles();
        else if (id=="paste") showPaste(); else if (id=="new-folder") showCreateFolder();
        else if (id=="move") showMove(); else if (id=="copy-to") showCopyTo(); else if (id=="rename") showRename();
        else if (id=="trash") trashSelected(); else if (id=="duplicate") duplicateSelected();
        else if (id=="select-all") selectAll(); else if (id=="clear") clearSelection(); else if (id=="invert") invertSelection();
        else if (id=="details-layout" || id=="list") showList(); else if (id=="grid") showGrid();
        else if (id=="sort") toggleSort(); else if (id=="refresh") refreshFiles();
        else if (id=="agent") toggleAgent(); else if (id=="up") up(); else if (id=="activity") showActivity();
        else if (id=="properties" || id=="details") showDetails(); else if (id=="resource-id" || id=="copy-id") copyResourceId();
    };
}
void NativeWorkspaceViewModel::selectAll() {
    if (rows.get().empty()) return;
    std::vector<std::string> paths; paths.reserve(rows.get().size());
    for (const auto &file : rows.get()) paths.push_back(file.path);
    if (!gridActive.get()) {
        (void)controller.get()->select({paths.front(), "name"});
        (void)controller.get()->select({paths.back(), "name"}, true);
    } else if (onGridSetSelection) onGridSetSelection(paths);
    setSelection(std::move(paths));
}
void NativeWorkspaceViewModel::clearSelection() {
    (void)controller.get()->clear_selection();
    if (gridActive.get() && onGridSetSelection) onGridSetSelection({});
    setSelection({});
}
void NativeWorkspaceViewModel::invertSelection() {
    std::vector<std::string> result;
    for (const auto &file:rows.get()) if (std::find(m_selectedPaths.begin(),m_selectedPaths.end(),file.path)==m_selectedPaths.end()) result.push_back(file.path);
    if (gridActive.get()) { if (onGridSetSelection) onGridSetSelection(result); setSelection(std::move(result)); return; }
    if (result.empty()) { clearSelection(); return; }
    const auto first=std::find_if(rows.get().begin(),rows.get().end(),[&](const auto &f){return f.path==result.front();});
    const auto last=std::find_if(rows.get().begin(),rows.get().end(),[&](const auto &f){return f.path==result.back();});
    if (first==rows.get().end() || last-first+1!=static_cast<std::ptrdiff_t>(result.size())) return;
    (void)controller.get()->select({result.front(),"name"}); (void)controller.get()->select({result.back(),"name"},true); setSelection(std::move(result));
}
void NativeWorkspaceViewModel::setSelection(std::vector<std::string> paths) {
    if (m_selectedPaths != paths) { renameOpen.set(false); moveOpen.set(false); detailsOpen.set(false); m_dialogSelection.clear(); }
    m_selectedPaths = std::move(paths);
    m_selected = m_selectedPaths.size() == 1 ? m_selectedPaths.front() : std::string{};
    selectionCount.set(static_cast<std::int64_t>(m_selectedPaths.size()));
    hasSelection.set(!m_selectedPaths.empty()); canModifySelection.set(!m_selectedPaths.empty());
    canOpenSelection.set(!m_selected.empty());
    const auto found=std::find_if(rows.get().begin(), rows.get().end(), [&](const auto &f) { return f.path == m_selected; });
    canAddContext.set(found != rows.get().end() && !found->directory);
    rebuildRibbon();
}
void NativeWorkspaceViewModel::copyPaths() { if (!m_selectedPaths.empty() && onCopyPaths) onCopyPaths(m_selectedPaths); }
void NativeWorkspaceViewModel::copyFiles() { if (!m_selectedPaths.empty() && onCopyFiles) onCopyFiles(m_selectedPaths); }
void NativeWorkspaceViewModel::showDetails() {
    const auto item=std::find_if(rows.get().begin(),rows.get().end(),[&](const auto &file){return file.path==m_selected;});
    if (m_selected.empty() || item==rows.get().end()) return;
    detailsName.set(item->name); detailsPath.set(item->path); detailsKind.set(item->kind); detailsSize.set(item->size);
    detailsModified.set(item->modified); detailsResourceId.set(item->resourceId); detailsReason.set(item->reason);
    detailsHasResourceId.set(!item->resourceId.empty()); detailsHasReason.set(!item->reason.empty()); detailsOpen.set(true);
}
void NativeWorkspaceViewModel::closeDetails() { detailsOpen.set(false); }
void NativeWorkspaceViewModel::copyResourceId() {
    const auto item=std::find_if(rows.get().begin(),rows.get().end(),[&](const auto &file){return file.path==m_selected;});
    if (item!=rows.get().end() && !item->resourceId.empty() && onCopyPaths) onCopyPaths({item->resourceId});
}
void NativeWorkspaceViewModel::showCreateFolder() { folderDraft.set(""); folderOpen.set(true); closeFileMenu(); }
void NativeWorkspaceViewModel::closeCreateFolder() { folderOpen.set(false); }
void NativeWorkspaceViewModel::folderEdited(const std::string &value) { folderDraft.set(value); }
void NativeWorkspaceViewModel::createFolder() {
    if (!folderOpen.get()) return;
    if (!validName(folderDraft.get())) { recordOperationError("Enter a valid folder name."); return; }
    if (onFileAction && onFileAction("create-directory","",folderDraft.get())) closeCreateFolder();
}
void NativeWorkspaceViewModel::showPaste() { pasteDraft.set("Files"); pasteOpen.set(true); }
void NativeWorkspaceViewModel::closePaste() { pasteOpen.set(false); }
void NativeWorkspaceViewModel::pasteEdited(const std::string &value) { pasteDraft.set(value); }
void NativeWorkspaceViewModel::pasteFiles() {
    if (!pasteOpen.get()) return;
    if (!filesDestination(pasteDraft.get())) { recordOperationError("Choose a destination inside Society/Files."); return; }
    if (onPasteFiles && onPasteFiles(pasteDraft.get())) closePaste();
}
void NativeWorkspaceViewModel::showRename() {
    if (m_selected.empty()) return;
    const auto found=std::find_if(rows.get().begin(),rows.get().end(),[&](const auto &f){return f.path==m_selected;});
    if (found==rows.get().end()) return;
    m_dialogSelection=m_selectedPaths; renameDraft.set(found->name); renameOpen.set(true);
}
void NativeWorkspaceViewModel::closeRename() { renameOpen.set(false); m_dialogSelection.clear(); }
void NativeWorkspaceViewModel::renameEdited(const std::string &value) { renameDraft.set(value); }
void NativeWorkspaceViewModel::renameSelected() {
    const auto &name=renameDraft.get();
    if (!renameOpen.get() || m_dialogSelection != m_selectedPaths || m_selected.empty()) return;
    if (!validName(name)) { recordOperationError("Enter a valid file name."); return; }
    if (onFileAction && onFileAction("rename",m_selected,name)) closeRename();
}
void NativeWorkspaceViewModel::showMove() {
    if (m_selectedPaths.empty()) return;
    m_moveAction="move"; moveTitle.set("Move to"); moveDraft.set("Files"); m_dialogSelection=m_selectedPaths; moveOpen.set(true);
}
void NativeWorkspaceViewModel::showCopyTo() {
    showMove(); if (moveOpen.get()) { m_moveAction="copy"; moveTitle.set("Copy to"); }
}
void NativeWorkspaceViewModel::closeMove() { moveOpen.set(false); m_dialogSelection.clear(); }
void NativeWorkspaceViewModel::moveEdited(const std::string &value) { moveDraft.set(value); }
void NativeWorkspaceViewModel::moveSelected() {
    if (!moveOpen.get() || m_dialogSelection!=m_selectedPaths) return;
    if (!filesDestination(moveDraft.get())) { recordOperationError("Choose a destination inside Society/Files."); return; }
    const auto destination=moveDraft.get();
    submitSelection(m_moveAction,destination); closeMove();
}
void NativeWorkspaceViewModel::submitSelection(const std::string &action, const std::string &argument) {
    if (!onFileAction) return;
    const auto paths=m_selectedPaths;
    for (const auto &path:paths) if (!onFileAction(action,path,argument)) break;
}
void NativeWorkspaceViewModel::trashSelected() { submitSelection("trash"); }
void NativeWorkspaceViewModel::duplicateSelected() { submitSelection("duplicate"); }
void NativeWorkspaceViewModel::toggleSort() { sortMenuVisible.set(!sortMenuVisible.get()); updateTableHeight(); }
void NativeWorkspaceViewModel::setSort(Sort sort) {
    if (m_sort == sort) m_descending = !m_descending; else { m_sort = sort; m_descending = false; }
    sortMenuVisible.set(false); updateTableHeight();
    rebuild();
}
void NativeWorkspaceViewModel::sortName() { setSort(Sort::Name); }
void NativeWorkspaceViewModel::sortDate() { setSort(Sort::Date); }
void NativeWorkspaceViewModel::sortKind() { setSort(Sort::Kind); }
void NativeWorkspaceViewModel::sortSize() { if (canSortSize.get()) setSort(Sort::Size); }

void NativeWorkspaceViewModel::rebuild() {
    auto files = m_page == Page::Recent ? m_recent : m_suggested;
    const auto needle = folded(query.get());
    std::erase_if(files, [&](const auto &item) { return item.path.empty() || folded(item.name).find(needle) == std::string::npos; });
    if (m_sort!=Sort::Recommended) std::stable_sort(files.begin(), files.end(), [&](const auto &a, const auto &b) {
        if (a.directory != b.directory) return a.directory;
        int comparison = 0;
        switch (m_sort) {
        case Sort::Recommended: break;
        case Sort::Name: comparison = folded(a.name).compare(folded(b.name)); break;
        case Sort::Date: comparison = (a.modifiedTime > b.modifiedTime) - (a.modifiedTime < b.modifiedTime); break;
        case Sort::Kind: comparison = folded(a.kind).compare(folded(b.kind)); break;
        case Sort::Size: comparison = (a.bytes > b.bytes) - (a.bytes < b.bytes); break;
        }
        if (!comparison) comparison = folded(a.name).compare(folded(b.name));
        return m_descending ? comparison > 0 : comparison < 0;
    });
    const auto oldRows=rows.get();
    bool selectionStale=!m_selectedPaths.empty() && (oldRows.size()!=files.size() || !std::equal(oldRows.begin(),oldRows.end(),files.begin(),
        [](const auto &a,const auto &b){ return a.path==b.path; }));
    if (!m_selectedPaths.empty() && !selectionStale) {
        const std::unordered_set<std::string> selected(m_selectedPaths.begin(),m_selectedPaths.end());
        for (std::size_t i=0;i<files.size();++i) {
            if (!selected.contains(files[i].path)) continue;
            const auto &before=oldRows[i], &after=files[i];
            if (before.resourceId!=after.resourceId || (before.resourceId.empty() &&
                (before.modifiedTime!=after.modifiedTime || before.bytes!=after.bytes))) { selectionStale=true; break; }
        }
    }
    if (selectionStale) clearSelection();
    std::vector<lvui::TableRow> table;
    table.reserve(files.size());
    for (const auto &f : files) {
        lvui::TableRow row{.key=f.path, .cells={{f.name,"text",true,false},
            {f.modified,"text",true,false}, {f.kind,"text",true,false}, {f.size,"text",true,false}}};
        if (m_page==Page::Suggested) row.cells.push_back({f.reason,"text",true,false});
        table.push_back(std::move(row));
    }
    if (auto previous = source->snapshot()) {
        if (auto next = source->replace(columns(m_page==Page::Suggested), std::move(table), (*previous)->revision)) {
            rows.set(std::move(files)); revision.set(static_cast<std::int64_t>(*next)); rebuildRibbon();
        }
    }
}
void NativeWorkspaceViewModel::selectPath(const std::string &path) {
    const auto &files = rows.get();
    const auto found = std::find_if(files.begin(), files.end(), [&](const auto &f) { return f.path == path; });
    setSelection(found == files.end() ? std::vector<std::string>{} : std::vector<std::string>{path});
}
void NativeWorkspaceViewModel::selected(const std::optional<lvui::TableSelection> &value) {
    if (!value) { setSelection({}); return; }
    const auto &files=rows.get();
    const auto first=std::find_if(files.begin(),files.end(),[&](const auto &f){return f.path==value->anchor.row;});
    const auto last=std::find_if(files.begin(),files.end(),[&](const auto &f){return f.path==value->current.row;});
    if (first==files.end() || last==files.end()) { setSelection({}); return; }
    const auto bounds=std::minmax(first,last); std::vector<std::string> paths;
    for (auto it=bounds.first;it!=bounds.second+1;++it) paths.push_back(it->path);
    setSelection(std::move(paths));
}
void NativeWorkspaceViewModel::gridSelected(const std::vector<std::string> &keys) {
    std::vector<std::string> paths;
    for (const auto &f:rows.get()) if (std::find(keys.begin(),keys.end(),f.path)!=keys.end()) paths.push_back(f.path);
    setSelection(std::move(paths));
}
void NativeWorkspaceViewModel::gridActivated(const std::string &key) { selectPath(key); openSelected(); }
void NativeWorkspaceViewModel::openSelected() { if (!m_selected.empty() && onOpen) onOpen(m_selected); }
void NativeWorkspaceViewModel::up() { if (canUp.get() && onUp) onUp(); }
void NativeWorkspaceViewModel::addSelectedContext() {
    const auto &files = rows.get();
    const auto found = std::find_if(files.begin(), files.end(), [&](const auto &f) { return f.path == m_selected && !f.directory; });
    if (found == files.end()) return;
    auto values = context.get();
    if (std::none_of(values.begin(), values.end(), [&](const auto &f) { return f.path == found->path; })) values.push_back(*found);
    hasContext.set(!values.empty()); context.set(std::move(values));
}
std::function<void()> NativeWorkspaceViewModel::removeContext(const std::string &path) {
    return [this, path] { auto values = context.get(); std::erase_if(values, [&](const auto &f) { return f.path == path; }); hasContext.set(!values.empty()); context.set(std::move(values)); };
}
std::function<void()> NativeWorkspaceViewModel::openGuild(const std::string &id) { return [this, id] { if (onGroup) onGroup("guild", id); }; }
std::function<void()> NativeWorkspaceViewModel::openOrganization(const std::string &id) { return [this, id] { if (onGroup) onGroup("organization", id); }; }
void NativeWorkspaceViewModel::send() { if (canSend.get() && onSend && onSend(prompt.get(), context.get())) promptEdited(""); }
void NativeWorkspaceViewModel::newConversation() {
    if (agentBusy.get()) return;
    if (onNewConversation) onNewConversation();
    promptEdited(""); hasContext.set(false); context.set({});
}
void NativeWorkspaceViewModel::resize(double width, double height) {
    if (!std::isfinite(width) || !std::isfinite(height)) return;
    viewHeight.set(std::max(1.0, height));
    agentTranscriptHeight.set(std::max(100.0, height - 270.0));
    showAgent.set(width >= 900 && m_agentRequested);
    // The hidden agent controls remain mounted so their view state survives
    // navigation. Keep their authored extent valid even in narrow windows.
    agentWidth.set(296.0);
    fileWidth.set(std::max(200.0, width - 178.0 - (showAgent.get() ? 304.0 : 0.0)));
    ribbonGroupWidth.set(std::max(78.0, (fileWidth.get()-32.0)/5.0));
    updateTableHeight();
}
void NativeWorkspaceViewModel::updateTableHeight() {
    tableHeight.set(std::max(100.0,viewHeight.get()-(ribbonExpanded.get()?250.0:128.0)-(sortMenuVisible.get()?36.0:0.0)));
}
