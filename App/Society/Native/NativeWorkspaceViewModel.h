#pragma once

#include <LVUI>
#include <iiSDKCore>
#include <array>
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
    std::string reason, recommendationSource, resourceId, workingVersion, previewSource, displayName, iconName;
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

struct NativeWorkspaceAddRequest final {
    std::string action;
    std::array<std::string, 4> fields;
    std::vector<std::string> selection;
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
    ii::core::Observable<bool> gridActive{false}, hasSelection{false}, canAddContext{false}, canUp{false}, canNavigateBack{false}, canNavigateForward{false}, canSend{false}, agentAvailable{false};
    ii::core::Observable<bool> sortMenuVisible{false};
    ii::core::Observable<bool> filterMenuVisible{false}, statusVisible{true}, showExtensions{true}, compactActive{false};
    ii::core::Observable<bool> agentBusy{false}, agentModelMenuVisible{false}, agentRetryable{false};
    ii::core::Observable<bool> agentEmpty{true};
    ii::core::Observable<bool> canSortSize{true};
    ii::core::Observable<bool> ribbonHomeActive{true}, ribbonShareActive{false}, ribbonViewActive{false}, ribbonManageActive{false}, ribbonAddActive{false}, ribbonSearchActive{false};
    ii::core::Observable<int> ribbonTabIndex{0};
    ii::core::Observable<bool> fileActionsAvailable{false};
    ii::core::Observable<bool> canOpenSelection{false}, canModifySelection{false}, renameOpen{false}, moveOpen{false}, folderOpen{false}, pasteOpen{false}, detailsOpen{false}, importOpen{false}, previewOpen{false};
    ii::core::Observable<bool> detailsHasResourceId{false}, detailsHasReason{false};
    ii::core::Observable<bool> clipboardHasFiles{false};
    ii::core::Observable<std::int64_t> selectionCount{0};
    ii::core::Observable<std::string> renameDraft{""}, moveDraft{"Files"}, moveTitle{"Move to"}, folderDraft{""}, pasteDraft{"Files"};
    ii::core::Observable<std::string> importDraft{""}, importDestination{"Files"};
    ii::core::Observable<bool> addOpen{false}, addSecondVisible{false}, addThirdVisible{false}, addFourthVisible{false}, addMultiline{false};
    ii::core::Observable<std::string> addTitle{""}, addLabel1{""}, addLabel2{""}, addLabel3{""}, addLabel4{""};
    ii::core::Observable<std::string> addDraft1{""}, addDraft2{""}, addDraft3{""}, addDraft4{""};
    ii::core::Observable<std::string> detailsName{""}, detailsPath{""}, detailsKind{""}, detailsSize{""}, detailsModified{""}, detailsResourceId{""}, detailsReason{""};
    ii::core::Observable<std::string> detailsInspection{""}, detailsPreviewSource{""};
    ii::core::Observable<bool> hasGuilds{false}, hasOrganizations{false}, hasContext{false}, showAgent{true};
    ii::core::Observable<std::string> query{""}, prompt{""}, status{"Reading Society…"}, operationStatus{""}, operationError{""};
    ii::core::Observable<std::string> searchScope{"Current view: Suggested"}, searchFilter{"Type: All files"};
    ii::core::Observable<bool> hasOperationError{false};
    ii::core::Observable<std::string> agentStatus{"Agent session is not connected."};
    ii::core::Observable<std::string> agentModelLabel{"Select model"};
    ii::core::Observable<double> fileWidth{958}, tableHeight{650}, agentWidth{296}, viewHeight{772};
    ii::core::Observable<double> fileNameWidth{300}, fileDateWidth{196}, fileKindWidth{188}, fileSizeWidth{120}, fileReasonWidth{140};
    ii::core::Observable<double> listRowHeight{32};
    std::shared_ptr<const lvui::IconSource> iconSource;
    std::function<void(Page)> onNavigate;
    std::function<void(const std::string &, const std::string &)> onGroup;
    std::function<void(const std::string &)> onOpen;
    std::function<void()> onUp;
    std::function<bool(const std::string &, const std::vector<NativeWorkspaceFile> &)> onSend;
    std::function<void(const std::string &)> onSelectModel;
    std::function<void()> onNewConversation, onStopAgent;
    std::function<void()> onRefresh;
    std::function<void(const std::vector<std::string> &)> onGridSetSelection;
    std::function<void(const std::vector<std::string> &)> onCopyPaths;
    std::function<void(const std::vector<std::string> &)> onCopyFiles;
    std::function<void(const std::vector<std::string> &)> onCutFiles;
    std::function<bool(const std::string &)> onPasteFiles;
    std::function<bool(const std::string &,const std::string &)> onImportFile;
    std::function<bool(const NativeWorkspaceAddRequest &)> onWorkspaceAdd;
    std::function<void(const std::string &)> onInspectResource;
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
    void resetSearch();
    void promptEdited(const std::string &value);
    void showList(); void showGrid();
    void showRibbonHome(); void showRibbonShare(); void showRibbonView(); void showRibbonManage(); void showRibbonAdd(); void showRibbonSearch();
    void selectRibbonTab(int index);
    void selectAll(); void clearSelection(); void invertSelection(); void copyPaths(); void refreshFiles(); void toggleAgent();
    void recordOperationError(const std::string &message); void clearOperationError();
    void setFileActionsAvailable(bool available);
    void setClipboardHasFiles(bool available);
    void resetDriveContext();
    std::function<void()> invokeRibbon(const std::string &id);
    void showRename(); void closeRename(); void renameEdited(const std::string &value); void renameSelected();
    void showCreateFolder(); void closeCreateFolder(); void folderEdited(const std::string &value); void createFolder();
    void showPaste(); void closePaste(); void pasteEdited(const std::string &value); void pasteFiles(); void copyFiles();
    void cutFiles(); void showImport(); void closeImport(); void importEdited(const std::string &value);
    void importDestinationEdited(const std::string &value); void importFile();
    void closeAdd(); void submitAdd();
    void addEdited1(const std::string &); void addEdited2(const std::string &);
    void addEdited3(const std::string &); void addEdited4(const std::string &);
    void showDetails(); void closeDetails(); void copyResourceId();
    void setDetailsInspection(const std::string &path,const std::string &value);
    void showPreview(); void closePreview();
    void showMove(); void showCopyTo(); void closeMove(); void moveEdited(const std::string &value); void moveSelected();
    void trashSelected(); void duplicateSelected();
    void toggleSort(); void sortName(); void sortDate(); void sortKind(); void sortSize();
    void toggleFilter(); void filterAll(); void filterImages(); void filterMedia(); void filterDocuments(); void filterOther();
    void toggleExtensions(); void toggleCompact(); void toggleStatus();
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
    enum class Filter { All, Images, Media, Documents, Other };
    enum class Ribbon { Home, Share, View, Manage, Add, Search };
    void rebuild(); void updateSend(); void updateTableHeight(); void selectPath(const std::string &path); void setSort(Sort sort);
    void setRibbon(Ribbon tab); void rebuildRibbon(); void setSelection(std::vector<std::string> paths); void submitSelection(const std::string &action, const std::string &argument = {});
    void showAdd(const std::string &action);
    void setFilter(Filter filter);
    std::string m_addAction;
    std::vector<std::string> m_addSelection;
    void navigateBack(); void navigateForward(); void updateHistoryAvailability();
    Page m_page = Page::Suggested;
    std::vector<Page> m_pageHistory{Page::Suggested};
    std::size_t m_historyIndex = 0;
    Filter m_filter = Filter::All;
    Sort m_sort = Sort::Recommended;
    bool m_descending = false;
    bool m_agentRequested = true;
    std::string m_selected;
    std::vector<std::string> m_selectedPaths, m_dialogSelection;
    std::string m_moveAction{"move"};
    std::vector<NativeWorkspaceFile> m_suggested, m_recent;
};
