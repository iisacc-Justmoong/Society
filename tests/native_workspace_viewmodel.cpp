#include "Society/Native/NativeWorkspaceViewModel.h"
#include "Society/Native/NativeWorkspaceClipboard.h"
#include <stdexcept>
#include <vector>

static void check(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}

int main() {
    using Page = NativeWorkspaceViewModel::Page;
    NativeWorkspaceViewModel errorModel;
    std::vector<std::string> reportedErrors;
    errorModel.onOperationError=[&](const std::string &message) { reportedErrors.push_back(message); };
    errorModel.recordOperationError("File operation failed");
    errorModel.recordOperationError("File operation failed");
    check(reportedErrors.size()==2 && errorModel.operationError.get()=="File operation failed",
          "workspace errors must reach the shell without replacing the original error state");
    NativeWorkspaceViewModel icons;
    NativeWorkspaceFile image{"/Files/photo.PNG", "photo.PNG", "", "Image", "1 B", false, 1, 1};
    image.previewSource="file:///preview.png";
    icons.setSuggestedFiles({{"/Files/folder.png", "folder.png", "", "Folder", "", true, 0, 0},
                             {"/Files/canvas.IISC", "canvas.IISC", "", "Canvas", "", false, 0, 0}, image,
                             {"/Files/sound.wav", "sound.wav", "", "Audio", "", false, 0, 0},
                             {"/Files/video.mp4", "video.mp4", "", "Video", "", false, 0, 0},
                             {"/Files/note.txt", "note.txt", "", "Text", "", false, 0, 0},
                             {"/Files/archive.zip", "archive.zip", "", "Archive", "", false, 0, 0},
                             {"/Files/untitled", "untitled", "", "Unknown", "", false, 0, 0},
                             {"/Files/unknown.xyz", "unknown.xyz", "", "Unknown", "", false, 0, 0}});
    const auto typeIcons=icons.rows.get();
    check(typeIcons.size()==9, "all files are retained for icon presentation");
    const std::vector<std::string> expectedIcons={"nodesfolder", "fileTypesdiagram", "fileTypesimage", "audio-track",
        "import-video", "fileTypestext", "fileTypesarchive", "fileTypesunknown", "fileTypesunknown"};
    for (std::size_t i=0; i<expectedIcons.size(); ++i)
        check(typeIcons[i].iconName==expectedIcons[i], "each row has its LVUI type icon, with a fallback");
    icons.toggleExtensions();
    check(icons.rows.get()[1].displayName=="canvas" && icons.rows.get()[1].iconName=="fileTypesdiagram",
          "hiding extensions does not change file classification");
    icons.filterImages();
    check(icons.rows.get().size()==2 && icons.rows.get().back().iconName=="fileTypesimage",
          "filtering retains the file icon even when a preview is available");
    icons.filterAll(); icons.setRecentFiles({image}); icons.showRecent();
    check(icons.rows.get().size()==1 && icons.rows.get()[0].iconName=="fileTypesimage",
          "Recent applies the same unconditional icon presentation");

    NativeWorkspaceViewModel vm;
    std::vector<Page> requests;
    vm.onNavigate = [&](Page page) { requests.push_back(page); };
    vm.showRecent();
    check(vm.recentActive.get() && !vm.suggestedActive.get(), "Recent is a distinct Workspace route");
    vm.showSuggested();
    check(vm.suggestedActive.get() && !vm.recentActive.get(), "Suggested is a distinct Workspace route");
    vm.showProjects(); vm.showCalendar(); vm.showActivity();
    check(requests == std::vector<Page>{Page::Recent, Page::Suggested, Page::Projects, Page::Calendar, Page::Activity},
          "sidebar requests the correct existing destinations");

    vm.setSuggestedFiles({{"/Files/b.png", "b.png", "2026-01-02", "PNG image", "2 B", false, 2, 2},
                          {"/Files/a", "a", "2026-01-01", "Folder", "", true, 1, 0}});
    vm.setRecentFiles({{"/Files/old.txt", "old.txt", "2026-01-01", "Text document", "1 B", false, 1, 1},
                       {"/Files/new.txt", "new.txt", "2026-01-03", "Text document", "3 B", false, 3, 3}});
    vm.showSuggested();
    check(vm.rows.get().size() == 2 && vm.rows.get()[0].name == "b.png", "Suggested preserves backend recommendation order");
    auto suggestion=vm.rows.get()[0]; suggestion.reason="AI estimate"; suggestion.recommendationSource="model_estimate";
    suggestion.resourceId="resource-1"; suggestion.workingVersion="version-1";
    vm.setSuggestedFiles({suggestion});
    check(vm.rows.get().size()==1 && vm.rows.get()[0].reason=="AI estimate" &&
          vm.rows.get()[0].resourceId=="resource-1", "Suggested retains its evidence and resource identity");
    vm.searchEdited("B.PN");
    check(vm.rows.get().size() == 1 && vm.rows.get()[0].name == "b.png", "search filters file names");
    vm.searchEdited(""); vm.showRecent();
    check(vm.rows.get().size() == 2 && vm.rows.get()[0].name == "new.txt", "Recent uses the recent snapshot, newest first");
    check(!vm.canSortSize.get(), "Recent does not sort by an unavailable numeric size");
    vm.selected(lvui::TableSelection{{"/Files/new.txt", "name"}, {"/Files/new.txt", "name"}});
    std::string opened;
    vm.onOpen = [&](const std::string &path) { opened = path; };
    vm.openSelected();
    check(opened == "/Files/new.txt", "open delegates the selected locator to the host");
    vm.addSelectedContext();
    check(vm.context.get().size() == 1 && vm.context.get()[0].path == opened, "selected file becomes context");
    check(vm.hasContext.get(), "context visibility follows selection");
    vm.addSelectedContext();
    check(vm.context.get().size() == 1, "context is not duplicated");
    vm.removeContext(opened)();
    check(vm.context.get().empty() && !vm.hasContext.get(), "context can be removed independently");
    vm.addSelectedContext();
    vm.promptEdited("Hello");
    check(!vm.canSend.get(), "a disconnected agent cannot send");
    vm.setAgentAvailable(true);
    check(vm.canSend.get(), "connected agent can send a nonempty message");
    std::string prompt;
    vm.onSend = [&](const std::string &value, const std::vector<NativeWorkspaceFile> &) { prompt = value; return true; };
    vm.send();
    check(prompt == "Hello" && vm.prompt.get().empty(), "submission delegates to the agent port");
    vm.setRecentFiles({{"/Files/old.txt", "old.txt", "2026-01-01", "Text document", "1 B", false, 1, 1}});
    check(!vm.hasSelection.get(), "a selection is cleared when its file leaves the snapshot");
    opened.clear(); vm.openSelected();
    check(opened.empty(), "a removed selection cannot be opened");
    vm.setGroups({{"guild-1", "Guild"}}, {{"org-1", "Organization"}});
    check(vm.hasGuilds.get() && vm.hasOrganizations.get(), "membership snapshots control group sections");
    std::string group, id;
    vm.onGroup = [&](const auto &kind, const auto &value) { group = kind; id = value; };
    vm.openOrganization("org-1")();
    check(group == "organization" && id == "org-1", "organization navigation retains its identity");
    vm.resize(600, 700);
    check(!vm.showAgent.get() && vm.agentWidth.get() > 0, "hidden agent keeps a valid mounted width");

    NativeWorkspaceViewModel recent;
    std::vector<NativeWorkspaceFile> many;
    for (int i=0;i<620;++i) {
        auto name=std::to_string(i)+".txt";
        many.push_back({"/Files/"+name,name,"", "Text document","1 B",false,i,1});
    }
    many.push_back({"/Files/folder","folder","","Folder","",true,9999,0});
    recent.setRecentFiles(std::move(many)); recent.showRecent();
    check(recent.rows.get().size()==500,"Recent is limited to the 500 most recent files");
    check(recent.rows.get().front().modifiedTime==619 && recent.rows.get().back().modifiedTime==120,
          "Recent chooses files by latest activity before truncating");
    const auto recentTable=recent.source->snapshot();
    check(recentTable && (*recentTable)->columns[1].title=="Last activity", "Recent labels its ordering timestamp");

    NativeWorkspaceViewModel agent;
    agent.setAgentModels({{"model://one", "One"}, {"model://two", "Two"}});
    check(!agent.agentAvailable.get(), "catalog discovery alone does not select a model");
    std::string selectedModel;
    agent.onSelectModel = [&](const std::string &id) { selectedModel = id; };
    agent.selectAgentModel("model://two")();
    check(selectedModel == "model://two", "model choice is delegated using the catalog identifier");
    agent.setAgentState("model://two", false, "Ready", {{"m1", "You", "Hello"}, {"m2", "Agent", "Hi"}});
    check(agent.agentAvailable.get() && agent.agentMessages.get().size() == 2, "backend snapshot drives availability and transcript");
    agent.promptEdited("Next");
    agent.setAgentState("model://two", true, "Thinking", agent.agentMessages.get());
    check(!agent.canSend.get() && agent.agentBusy.get(), "active run prevents duplicate submission");
    agent.setAgentState("model://two", false, "Ready", agent.agentMessages.get());
    check(agent.canSend.get(), "completion restores the draft for submission");
    agent.promptEdited(""); agent.agentRetryable.set(true); agent.retryAgent();
    check(agent.prompt.get()=="Hello", "retry restores the last user message for review");
    std::vector<NativeWorkspaceFile> attachmentFiles;
    std::vector<std::string> attachmentPaths;
    for (int i=0;i<8;++i) {
        auto path="/Files/attachment-"+std::to_string(i)+".txt";
        attachmentFiles.push_back({path,path,"","Text","1 B",false,i,1});
        attachmentPaths.push_back(path);
    }
    agent.setSuggestedFiles(std::move(attachmentFiles)); agent.gridSelected(attachmentPaths); agent.addSelectedContext();
    check(agent.context.get().size()==8,"the agent accepts its full attachment limit");
    agent.removeContext(attachmentPaths.front())();
    check(agent.context.get().size()==7,"attachments can be removed independently at the limit");

    NativeWorkspaceViewModel ribbon;
    ribbon.setSuggestedFiles({{"/Files/a.txt", "a.txt", "", "Text", "1 B", false, 1, 1},
                              {"/Files/b.txt", "b.txt", "", "Text", "2 B", false, 2, 2},
                              {"/Files/c.txt", "c.txt", "", "Text", "3 B", false, 3, 3}});
    ribbon.resize(1440, 772);
    const auto expandedTableHeight = ribbon.tableHeight.get();
    check(ribbon.ribbonHomeActive.get() && ribbon.ribbonTabIndex.get()==0, "Home ribbon is initially active");
    ribbon.selectRibbonTab(1);
    check(ribbon.ribbonShareActive.get() && !ribbon.ribbonHomeActive.get() && ribbon.ribbonTabIndex.get()==1, "Share replaces Home commands");
    ribbon.selectRibbonTab(-1); ribbon.selectRibbonTab(6);
    check(ribbon.ribbonShareActive.get() && ribbon.ribbonTabIndex.get()==1, "Invalid tab indices leave the selection unchanged");
    ribbon.showRibbonView();
    check(ribbon.ribbonViewActive.get(), "changing tabs selects View commands");
    check(ribbon.ribbonViewActive.get() && ribbon.tableHeight.get()==expandedTableHeight, "reselecting the active tab keeps the ribbon layout");
    ribbon.showRibbonManage();
    check(ribbon.ribbonManageActive.get() && ribbon.ribbonTabIndex.get()==3 && ribbon.tableHeight.get()==expandedTableHeight, "selecting a tab keeps the ribbon layout");
    ribbon.selectRibbonTab(4);
    check(ribbon.ribbonTabIndex.get()==4 && ribbon.ribbonGroups.get().size()==4 && ribbon.ribbonGroups.get()[0].label=="Create", "Add exposes the designed command groups");
    NativeWorkspaceViewModel search;
    search.setSuggestedFiles({{"/Files/photo.png", "photo.png", "", "Image", "1 B", false, 1, 1},
                              {"/Files/notes.txt", "notes.txt", "", "Text", "1 B", false, 1, 1},
                              {"/Files/song.wav", "song.wav", "", "Audio", "1 B", false, 1, 1},
                              {"/Files/archive.zip", "archive.zip", "", "Archive", "1 B", false, 1, 1}});
    search.setRecentFiles({{"/Files/recent.png", "recent.png", "", "Image", "1 B", false, 2, 1}});
    search.selectRibbonTab(5);
    check(search.ribbonSearchActive.get() && search.ribbonTabIndex.get()==5 && search.searchScope.get()=="Current view: Suggested",
          "Search is the sixth ribbon tab and names its actual scope");
    check(search.ribbonGroups.get().size()==2 && search.ribbonGroups.get()[0].label=="Media" && search.ribbonGroups.get()[1].label=="Type",
          "Search exposes the designed media and type filters");
    search.invokeRibbon("search-images")();
    check(search.rows.get().size()==1 && search.rows.get()[0].name=="photo.png" && search.searchFilter.get()=="Type: Images",
          "Search image command filters the current snapshot and reports its type");
    search.searchEdited("PHOTO");
    check(search.rows.get().size()==1, "query and type filters work together case-insensitively");
    search.showRecent();
    check(search.rows.get().empty() && search.searchScope.get()=="Current view: Recent", "switching pages searches only that page's snapshot");
    search.showRibbonHome(); search.selectRibbonTab(5);
    check(search.query.get()=="PHOTO" && search.searchFilter.get()=="Type: Images", "switching tabs retains search state");
    search.resetSearch();
    check(search.query.get().empty() && search.searchFilter.get()=="Type: All files" && search.rows.get().size()==1,
          "Reset clears the query and type filter in the current view");
    search.showSuggested();
    search.invokeRibbon("search-media")(); check(search.rows.get().size()==1 && search.rows.get()[0].name=="song.wav", "media filter");
    search.invokeRibbon("search-documents")(); check(search.rows.get().size()==1 && search.rows.get()[0].name=="notes.txt", "document filter");
    search.invokeRibbon("search-other")(); check(search.rows.get().size()==1 && search.rows.get()[0].name=="archive.zip", "other filter");
    search.invokeRibbon("search-all")(); check(search.rows.get().size()==4, "all files removes the type filter");
    NativeWorkspaceAddRequest addRequest;
    ribbon.onWorkspaceAdd=[&](const auto &request) { addRequest=request; return true; };
    ribbon.setFileActionsAvailable(true);
    ribbon.invokeRibbon("new-project")();
    check(ribbon.addOpen.get(), "New project opens its input dialog");
    ribbon.addEdited1("Test project"); ribbon.addEdited2("Complete the work"); ribbon.submitAdd();
    check(!ribbon.addOpen.get() && addRequest.action=="new-project" && addRequest.fields[1]=="Complete the work", "New project submits the name and goal");
    ribbon.invokeRibbon("new-canvas")();
    ribbon.addEdited1("Draft"); ribbon.addEdited2("1024"); ribbon.addEdited3("768"); ribbon.submitAdd();
    check(addRequest.action=="new-canvas" && addRequest.fields[2]=="768", "New canvas submits user chosen dimensions");
    ribbon.invokeRibbon("add-import-folder")();
    ribbon.addEdited1("/external/folder"); ribbon.addEdited2("Files"); ribbon.submitAdd();
    check(addRequest.action=="add-import-folder" && addRequest.fields[0]=="/external/folder", "Folder import preserves the selected source");
    ribbon.invokeRibbon("add-import-files")(); ribbon.addEdited1("/external/one\n/external/two"); ribbon.submitAdd();
    check(addRequest.action=="add-import-files" && addRequest.fields[1]=="Files", "File import preserves its destination");
    ribbon.invokeRibbon("add-import-models")(); ribbon.addEdited1("/external/model.safetensors"); ribbon.submitAdd();
    check(addRequest.action=="add-import-models", "Model import is submitted as a separate model job");
    ribbon.gridSelected({"/Files/a.txt"});
    for (const auto *id : {"link-resource", "link-external", "link-url"}) {
        ribbon.invokeRibbon(id)(); ribbon.addEdited1("target"); ribbon.addEdited2("example:references"); ribbon.submitAdd();
        check(addRequest.action==id && addRequest.selection==std::vector<std::string>{"/Files/a.txt"}, "Reference commands preserve the chosen source");
    }
    ribbon.invokeRibbon("project-add-selection")(); ribbon.addEdited1("Project"); ribbon.addEdited2("Purpose"); ribbon.submitAdd();
    check(addRequest.action=="project-add-selection" && addRequest.selection.size()==1, "Project selection is submitted explicitly");
    ribbon.invokeRibbon("project-add-resource")(); ribbon.addEdited1("Project"); ribbon.addEdited2("/asset"); ribbon.addEdited3("Purpose"); ribbon.submitAdd();
    check(addRequest.action=="project-add-resource", "Adding a resource submits its path and purpose");
    ribbon.invokeRibbon("project-create-relation")(); ribbon.addEdited1("Project"); ribbon.addEdited2("Type"); ribbon.addEdited3("Subject"); ribbon.addEdited4("Object"); ribbon.submitAdd();
    check(addRequest.action=="project-create-relation" && addRequest.fields[3]=="Object", "Project relation submits both endpoints");
    ribbon.invokeRibbon("link-url")(); ribbon.addEdited1("https://example.com"); ribbon.addEdited2("example:references");
    ribbon.gridSelected({"/Files/b.txt"}); ribbon.submitAdd();
    check(!ribbon.addOpen.get() && addRequest.action=="project-create-relation" && ribbon.hasOperationError.get(), "Changed selection never applies a stale reference");
    ribbon.clearOperationError(); ribbon.clearSelection();
    ribbon.selectRibbonTab(3);
    ribbon.selectAll();
    check(ribbon.selectionCount.get()==3 && !ribbon.canOpenSelection.get(), "select all selects the current snapshot");
    ribbon.clearSelection();
    check(ribbon.selectionCount.get()==0, "clear selection resets command availability");
    ribbon.gridSelected({"/Files/a.txt", "/Files/c.txt"});
    check(ribbon.selectionCount.get()==2, "list can represent non-contiguous selected files");
    ribbon.invertSelection();
    check(ribbon.selectionCount.get()==1, "list selection inversion retains the middle file");
    ribbon.invertSelection();
    check(ribbon.selectionCount.get()==2, "list inversion can produce a non-contiguous selection");
    ribbon.showGrid();
    check(ribbon.selectionCount.get()==2, "switching views preserves the selected files");
    ribbon.showList(); ribbon.clearSelection();
    NativeWorkspaceCutState cut;
    cut.begin("token", "drive-a", {"/Files/a.txt", "/Files/c.txt"});
    check(cut.matches("token", "drive-a", {"/Files/a.txt", "/Files/c.txt"}), "cut clipboard binds its drive and exact files");
    check(!cut.matches("token", "drive-b", {"/Files/a.txt", "/Files/c.txt"}), "cut clipboard cannot move files on a different drive");
    cut.completed("/Files/a.txt");
    check(cut.matches("token", "drive-a", {"/Files/c.txt"}), "only confirmed moves are removed from the cut batch");
    ribbon.setFileActionsAvailable(true);
    ribbon.gridSelected({"/Files/a.txt", "/Files/c.txt"});
    std::vector<std::string> cutPaths;
    ribbon.onCutFiles=[&](const auto &paths) { cutPaths=paths; };
    ribbon.showRibbonHome(); ribbon.invokeRibbon("cut")();
    check(cutPaths==std::vector<std::string>{"/Files/a.txt", "/Files/c.txt"}, "cut delegates every selected file");
    ribbon.invokeRibbon("import")(); ribbon.importEdited("/outside/source.txt"); ribbon.importDestinationEdited("Files/Drafts");
    std::string importPath, importDestination;
    ribbon.onImportFile=[&](const auto &path, const auto &destination) { importPath=path; importDestination=destination; return true; };
    ribbon.importFile();
    check(importPath=="/outside/source.txt" && importDestination=="Files/Drafts", "import delegates a local file and destination");
    ribbon.clearSelection();
    ribbon.selected(lvui::TableSelection{{"/Files/a.txt", "name"}, {"/Files/b.txt", "name"}});
    check(ribbon.selectionCount.get()==2 && ribbon.canModifySelection.get(), "range selection is available to file commands");
    ribbon.searchEdited("c.txt");
    check(ribbon.selectionCount.get()==0 && !ribbon.canModifySelection.get(), "filtered-out files cannot receive operations");
    ribbon.searchEdited("");
    ribbon.selected(lvui::TableSelection{{"/Files/c.txt", "name"}, {"/Files/c.txt", "name"}});
    std::string action, locator, argument;
    ribbon.onFileAction = [&](const auto &a, const auto &p, const auto &v) { action=a; locator=p; argument=v; return true; };
    ribbon.showRename(); ribbon.renameEdited("renamed.txt"); ribbon.renameSelected();
    check(action=="rename" && locator=="/Files/c.txt" && argument=="renamed.txt", "rename delegates to the container action port");
    ribbon.selected(lvui::TableSelection{{"/Files/a.txt", "name"}, {"/Files/b.txt", "name"}});
    std::vector<std::string> deleted;
    ribbon.onFileAction = [&](const auto &a, const auto &p, const auto &) { if (a=="trash") deleted.push_back(p); return true; };
    ribbon.trashSelected();
    check(deleted==std::vector<std::string>{"/Files/a.txt", "/Files/b.txt"}, "trash submits each selected resource independently");
    ribbon.invokeRibbon("new-folder")(); ribbon.folderEdited("Drafts");
    ribbon.onFileAction = [&](const auto &a, const auto &p, const auto &v) { action=a; locator=p; argument=v; return true; };
    ribbon.createFolder();
    check(action=="create-directory" && locator.empty() && argument=="Drafts", "new folder is created under the Files root");
    ribbon.showPaste(); ribbon.pasteEdited("Files/Drafts");
    std::string destination;
    ribbon.onPasteFiles = [&](const auto &value) { destination=value; return true; };
    ribbon.pasteFiles();
    check(destination=="Files/Drafts", "paste asks the host to copy clipboard files to the chosen destination");
    auto identified=ribbon.rows.get()[0]; identified.resourceId="asset:stable"; identified.previewSource="file:///Files/a.txt?version=1";
    ribbon.setSuggestedFiles({identified});
    ribbon.selected(lvui::TableSelection{{identified.path, "name"}, {identified.path, "name"}});
    std::vector<std::string> copied;
    ribbon.onCopyPaths=[&](const auto &values){ copied=values; };
    ribbon.copyResourceId();
    check(copied==std::vector<std::string>{"asset:stable"}, "resource ID uses the stable identifier, not a path or hash");
    ribbon.showDetails();
    check(ribbon.detailsOpen.get() && ribbon.detailsName.get()==identified.name && ribbon.detailsResourceId.get()==identified.resourceId,
          "properties show the selected snapshot and identity");
    ribbon.closeDetails();
    ribbon.showRibbonView(); ribbon.invokeRibbon("preview")();
    check(ribbon.previewOpen.get() && ribbon.detailsPreviewSource.get()==identified.previewSource, "preview opens the selected file's cached image");
    ribbon.closePreview();
    std::string inspected;
    ribbon.onInspectResource=[&](const auto &path) { inspected=path; };
    ribbon.showRibbonView(); ribbon.invokeRibbon("inspector")();
    check(ribbon.detailsOpen.get() && inspected==identified.path, "inspector requests the selected resource from the daemon");
    ribbon.setDetailsInspection("/Files/other", "stale");
    check(ribbon.detailsInspection.get()!="stale", "a stale inspection cannot replace the current resource");
    ribbon.setDetailsInspection(identified.path, "Locations: unknown");
    check(ribbon.detailsInspection.get()=="Locations: unknown", "the current inspection is displayed");
    auto replacement=identified; replacement.resourceId="asset:replacement";
    ribbon.setSuggestedFiles({replacement});
    check(!ribbon.hasSelection.get() && !ribbon.detailsOpen.get(), "a replacement at the same path does not inherit selection");
    ribbon.selected(lvui::TableSelection{{replacement.path, "name"}, {replacement.path, "name"}});
    ribbon.showDetails();
    ribbon.setSuggestedFiles({});
    check(!ribbon.detailsOpen.get(), "properties close when the selected resource leaves the snapshot");
    ribbon.showCreateFolder(); ribbon.showPaste(); ribbon.resetDriveContext();
    check(!ribbon.folderOpen.get() && !ribbon.pasteOpen.get() && ribbon.selectionCount.get()==0,
          "changing drives cancels pending dialogs and their targets");
    ribbon.showPaste(); ribbon.pasteEdited("Files/../outside"); ribbon.pasteFiles();
    check(ribbon.hasOperationError.get() && ribbon.pasteOpen.get(), "invalid destinations are reported without submission");
    ribbon.clearOperationError();
    check(!ribbon.hasOperationError.get(), "file errors can be dismissed");

    NativeWorkspaceViewModel view;
    view.setSuggestedFiles({{"/Files/a.png","a.png","","PNG image","1 B",false,1,1},
                            {"/Files/b.txt","b.txt","","Text document","2 B",false,2,2},
                            {"/Files/c.mp4","c.mp4","","MP4 file","3 B",false,3,3}});
    view.showRibbonView(); view.invokeRibbon("filter")();
    check(view.filterMenuVisible.get(), "filter opens the type choices");
    view.filterImages();
    check(view.rows.get().size()==1 && view.rows.get()[0].name=="a.png", "image filter excludes other file types");
    view.filterAll(); view.invokeRibbon("extensions")();
    check(view.rows.get()[0].displayName=="a" && view.rows.get()[0].path=="/Files/a.png", "hiding extensions changes only the displayed name");
    view.invokeRibbon("compact")();
    check(view.listRowHeight.get()==24 && !view.gridActive.get(), "compact view uses the list layout");
    view.invokeRibbon("status")();
    check(!view.statusVisible.get(), "status visibility can be toggled");
    view.showRecent();
    check(view.canNavigateBack.get(), "visited Workspace pages can be navigated back to");
    view.invokeRibbon("back")();
    check(view.suggestedActive.get() && view.canNavigateForward.get(), "back restores the previous Workspace page");
    view.invokeRibbon("forward")();
    check(view.recentActive.get(), "forward restores the next Workspace page");
    view.invokeRibbon("back")();
    view.showProjects();
    check(!view.canNavigateForward.get(), "a new destination discards the previous forward history");
    view.showSuggested();
    check(view.canNavigateBack.get(), "navigation from another application page remains in history");
}
