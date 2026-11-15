#pragma once

#include <LVUI/IconCatalog.h>
#include <LVUI/Table.h>
#include <iiSDKCore/Core/Property/Observable.h>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// Locators identify rows only within this snapshot; they are not resource IDs.
struct NativeWorkspaceFile final {
    std::string path, name, modified, kind, size;
    bool directory = false;
    std::int64_t modifiedTime = 0, bytes = 0;
    std::string reason, recommendationSource, resourceId, workingVersion;
    friend bool operator==(const NativeWorkspaceFile &, const NativeWorkspaceFile &) = default;
};

struct NativeWorkspaceGroup final {
    std::string id, name;
    friend bool operator==(const NativeWorkspaceGroup &, const NativeWorkspaceGroup &) = default;
};

struct NativeAgentModel final {
    std::string id, name;
    friend bool operator==(const NativeAgentModel &, const NativeAgentModel &) = default;
};

struct NativeAgentMessage final {
    std::string id, role, text;
    double height = 48;
    friend bool operator==(const NativeAgentMessage &, const NativeAgentMessage &) = default;
};

struct NativeRibbonAction final {
    std::string id, label, icon;
    bool enabled = false;
    friend bool operator==(const NativeRibbonAction &, const NativeRibbonAction &) = default;
};
struct NativeRibbonGroup final {
    std::string id, label;
    std::vector<NativeRibbonAction> actions;
    friend bool operator==(const NativeRibbonGroup &, const NativeRibbonGroup &) = default;
};

struct NativeWorkspaceViewModel final {
    enum class Page { Suggested, Recent, Projects, Calendar, Activity };
    std::shared_ptr<lvui::TableMemory> source;
    ii::core::Observable<std::shared_ptr<lvui::TableController>> controller;
    ii::core::Observable<std::int64_t> revision{0};
    ii::core::Observable<std::vector<NativeWorkspaceFile>> rows, context;
    ii::core::Observable<std::vector<NativeWorkspaceGroup>> guilds, organizations;
    ii::core::Observable<std::vector<NativeAgentModel>> agentModels;
    ii::core::Observable<std::vector<NativeAgentMessage>> agentMessages;
    ii::core::Observable<std::vector<NativeRibbonGroup>> ribbonGroups;
    ii::core::Observable<bool> suggestedActive{true}, recentActive{false}, projectsActive{false};
    ii::core::Observable<bool> calendarActive{false}, activityActive{false}, contentVisible{true};
    ii::core::Observable<bool> gridActive{false}, hasSelection{false}, canAddContext{false}, canUp{false}, canSend{false}, agentAvailable{false};
    ii::core::Observable<bool> sortMenuVisible{false};
    ii::core::Observable<bool> agentBusy{false}, agentModelMenuVisible{false}, agentRetryable{false};
    ii::core::Observable<bool> agentEmpty{true};
    ii::core::Observable<bool> canSortSize{true};
    ii::core::Observable<bool> ribbonHomeActive{true}, ribbonShareActive{false}, ribbonViewActive{false}, ribbonManageActive{false};
    ii::core::Observable<bool> ribbonExpanded{true}, fileMenuOpen{false}, fileActionsAvailable{false};
    ii::core::Observable<bool> canOpenSelection{false}, canModifySelection{false}, renameOpen{false}, moveOpen{false}, folderOpen{false}, pasteOpen{false}, detailsOpen{false};
    ii::core::Observable<bool> detailsHasResourceId{false}, detailsHasReason{false};
    ii::core::Observable<bool> clipboardHasFiles{false};
    ii::core::Observable<std::int64_t> selectionCount{0};
    ii::core::Observable<std::string> renameDraft{""}, moveDraft{"Files"}, moveTitle{"Move to"}, folderDraft{""}, pasteDraft{"Files"}, ribbonToggleLabel{"Collapse"};
    ii::core::Observable<std::string> detailsName{""}, detailsPath{""}, detailsKind{""}, detailsSize{""}, detailsModified{""}, detailsResourceId{""}, detailsReason{""};
    ii::core::Observable<bool> hasGuilds{false}, hasOrganizations{false}, hasContext{false}, showAgent{true};
    ii::core::Observable<std::string> query{""}, prompt{""}, status{"Reading Society…"}, operationStatus{""}, operationError{""};
    ii::core::Observable<bool> hasOperationError{false};
    ii::core::Observable<std::string> agentStatus{"Agent session is not connected."};
    ii::core::Observable<std::string> agentModelLabel{"Select model"};
    ii::core::Observable<double> fileWidth{958}, ribbonGroupWidth{180}, tableHeight{650}, agentWidth{296}, viewHeight{772}, agentTranscriptHeight{570};
    std::shared_ptr<const lvui::IconSource> iconSource;
    std::function<void(Page)> onNavigate;
    std::function<void(const std::string &, const std::string &)> onGroup;
    std::function<void(const std::string &)> onOpen;
    std::function<void()> onUp;
    std::function<bool(const std::string &, const std::vector<NativeWorkspaceFile> &)> onSend;
    std::function<void(const std::string &)> onSelectModel;
    std::function<void()> onNewConversation, onStopAgent;
    std::function<void()> onRefresh;
    std::function<void(bool)> onFileMenuChanged;
    std::function<void(const std::vector<std::string> &)> onGridSetSelection;
    std::function<void(const std::vector<std::string> &)> onCopyPaths;
    std::function<void(const std::vector<std::string> &)> onCopyFiles;
    std::function<bool(const std::string &)> onPasteFiles;
    std::function<bool(const std::string &, const std::string &, const std::string &)> onFileAction;

    NativeWorkspaceViewModel();
    void showSuggested(); void showRecent(); void showProjects(); void showCalendar(); void showActivity();
    void setPage(Page page, bool notify = false);
    void setSuggestedFiles(std::vector<NativeWorkspaceFile> files);
    void setRecentFiles(std::vector<NativeWorkspaceFile> files);
    void setGroups(std::vector<NativeWorkspaceGroup> guildValues, std::vector<NativeWorkspaceGroup> organizationValues);
    void setAgentAvailable(bool available);
    void setAgentModels(std::vector<NativeAgentModel> models);
    void setAgentState(const std::string &model, bool busy, const std::string &status, std::vector<NativeAgentMessage> messages);
    void toggleAgentModels();
    std::function<void()> selectAgentModel(const std::string &id);
    void stopAgent();
    void retryAgent();
    void searchEdited(const std::string &value);
    void promptEdited(const std::string &value);
    void showList(); void showGrid();
    void showRibbonHome(); void showRibbonShare(); void showRibbonView(); void showRibbonManage();
    void toggleRibbon(); void toggleFileMenu(); void closeFileMenu();
    void selectAll(); void clearSelection(); void invertSelection(); void copyPaths(); void refreshFiles(); void toggleAgent();
    void recordOperationError(const std::string &message); void clearOperationError();
    void setFileActionsAvailable(bool available);
    void setClipboardHasFiles(bool available);
    void resetDriveContext();
    std::function<void()> invokeRibbon(const std::string &id);
    void showRename(); void closeRename(); void renameEdited(const std::string &value); void renameSelected();
    void showCreateFolder(); void closeCreateFolder(); void folderEdited(const std::string &value); void createFolder();
    void showPaste(); void closePaste(); void pasteEdited(const std::string &value); void pasteFiles(); void copyFiles();
    void showDetails(); void closeDetails(); void copyResourceId();
    void showMove(); void showCopyTo(); void closeMove(); void moveEdited(const std::string &value); void moveSelected();
    void trashSelected(); void duplicateSelected();
    void toggleSort(); void sortName(); void sortDate(); void sortKind(); void sortSize();
    void selected(const std::optional<lvui::TableSelection> &selection);
    void gridSelected(const std::vector<std::string> &keys);
    void gridActivated(const std::string &key);
    void openSelected(); void up();
    void addSelectedContext();
    std::function<void()> removeContext(const std::string &path);
    std::function<void()> openGuild(const std::string &id);
    std::function<void()> openOrganization(const std::string &id);
    void send(); void newConversation();
    void resize(double width, double height);
private:
    enum class Sort { Recommended, Name, Date, Kind, Size };
    enum class Ribbon { Home, Share, View, Manage };
    void rebuild(); void updateSend(); void updateTableHeight(); void selectPath(const std::string &path); void setSort(Sort sort);
    void setRibbon(Ribbon tab); void rebuildRibbon(); void setSelection(std::vector<std::string> paths); void submitSelection(const std::string &action, const std::string &argument = {});
    Page m_page = Page::Suggested;
    Sort m_sort = Sort::Recommended;
    bool m_descending = false;
    bool m_agentRequested = true;
    std::string m_selected;
    std::vector<std::string> m_selectedPaths, m_dialogSelection;
    std::string m_moveAction{"move"};
    std::vector<NativeWorkspaceFile> m_suggested, m_recent;
};
