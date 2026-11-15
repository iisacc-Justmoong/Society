#include "Society/Native/NativeWorkspaceViewModel.h"
#include <stdexcept>
#include <vector>

static void check(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}

int main() {
    using Page = NativeWorkspaceViewModel::Page;
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

    NativeWorkspaceViewModel ribbon;
    ribbon.setSuggestedFiles({{"/Files/a.txt", "a.txt", "", "Text", "1 B", false, 1, 1},
                              {"/Files/b.txt", "b.txt", "", "Text", "2 B", false, 2, 2},
                              {"/Files/c.txt", "c.txt", "", "Text", "3 B", false, 3, 3}});
    check(ribbon.ribbonHomeActive.get() && ribbon.ribbonExpanded.get(), "Home ribbon starts expanded");
    ribbon.showRibbonShare();
    check(ribbon.ribbonShareActive.get() && !ribbon.ribbonHomeActive.get(), "Share replaces Home commands");
    ribbon.toggleFileMenu();
    check(ribbon.fileMenuOpen.get(), "File menu opens over the active ribbon");
    ribbon.showRibbonView();
    check(ribbon.ribbonViewActive.get() && !ribbon.fileMenuOpen.get(), "changing tabs closes File menu");
    ribbon.toggleRibbon();
    check(!ribbon.ribbonExpanded.get() && ribbon.ribbonViewActive.get(), "collapse retains the active tab");
    ribbon.showRibbonManage();
    check(ribbon.ribbonExpanded.get() && ribbon.ribbonManageActive.get(), "selecting a tab expands the ribbon");
    ribbon.selectAll();
    check(ribbon.selectionCount.get()==3 && !ribbon.canOpenSelection.get(), "select all selects the current snapshot");
    ribbon.clearSelection();
    check(ribbon.selectionCount.get()==0, "clear selection resets command availability");
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
    ribbon.showCreateFolder(); ribbon.folderEdited("Drafts");
    ribbon.onFileAction = [&](const auto &a, const auto &p, const auto &v) { action=a; locator=p; argument=v; return true; };
    ribbon.createFolder();
    check(action=="create-directory" && locator.empty() && argument=="Drafts", "new folder is created under the Files root");
    ribbon.showPaste(); ribbon.pasteEdited("Files/Drafts");
    std::string destination;
    ribbon.onPasteFiles = [&](const auto &value) { destination=value; return true; };
    ribbon.pasteFiles();
    check(destination=="Files/Drafts", "paste asks the host to copy clipboard files to the chosen destination");
    auto identified=ribbon.rows.get()[0]; identified.resourceId="asset:stable";
    ribbon.setSuggestedFiles({identified});
    ribbon.selected(lvui::TableSelection{{identified.path, "name"}, {identified.path, "name"}});
    std::vector<std::string> copied;
    ribbon.onCopyPaths=[&](const auto &values){ copied=values; };
    ribbon.copyResourceId();
    check(copied==std::vector<std::string>{"asset:stable"}, "resource ID uses the stable identifier, not a path or hash");
    ribbon.showDetails();
    check(ribbon.detailsOpen.get() && ribbon.detailsName.get()==identified.name && ribbon.detailsResourceId.get()==identified.resourceId,
          "properties show the selected snapshot and identity");
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
}
