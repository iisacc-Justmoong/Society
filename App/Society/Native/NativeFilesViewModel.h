#pragma once
#include <LVUI/Table.h>
#include <iiJson/Json.h>
#include <iiSDKCore/Core/Property/Observable.h>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

// Per-view state; paths are locators and never substituted for resource IDs.
struct NativeFileGridRow final {
    std::string id, path, name, kind, size;
    friend bool operator==(const NativeFileGridRow &, const NativeFileGridRow &) = default;
};
struct NativeFilesViewModel final {
    std::shared_ptr<lvui::TableMemory> source;
    ii::core::Observable<std::shared_ptr<lvui::TableController>> controller;
    ii::core::Observable<std::vector<NativeFileGridRow>> gridRows;
    ii::core::Observable<std::int64_t> revision{0};
    ii::core::Observable<std::string> path{"Files"}, status{"Connecting to Society"};
    ii::core::Observable<std::string> activity{""}, errors{""}, renameDraft{""}, folderDraft{""}, moveDraft{""}, removePrompt{""};
    ii::core::Observable<std::string> currentError{""}, errorCountLabel{""}, modelImportStatus{""};
    ii::core::Observable<std::string> searchDraft{""}, sortLabel{"Sort: Name (ascending)"};
    ii::core::Observable<bool> canUp{false}, canOpen{false}, canPrevious{false}, canNext{false};
    ii::core::Observable<bool> canRename{false}, canTrash{false}, canRestore{false};
    ii::core::Observable<bool> canDuplicate{false}, canMove{false}, canRemove{false}, canCreateFolder{false}, canImportModels{false}, canImportFiles{false};
    ii::core::Observable<bool> filesActive{true}, modelsActive{false}, deletedActive{false}, renameOpen{false};
    ii::core::Observable<bool> folderOpen{false}, moveOpen{false}, removeOpen{false};
    ii::core::Observable<bool> canReconnect{false}, hasErrors{false}, canPreviousError{false}, canNextError{false}, gridActive{false};
    ii::core::Observable<double> tableWidth{900}, tableHeight{500};
    ii::core::Observable<double> navigationWidth{232};
    std::function<bool(ii::core::Value)> onRequest;
    std::function<void()> onReconnect;
    std::function<void()> onChooseModels;
    std::function<void()> onChooseFiles;
    std::function<std::optional<std::string>(const std::string &)> onOpenFile;

    NativeFilesViewModel();
    void receive(const ii::core::Value &message);
    void disconnected();
    void backendError(std::string message);
    void selected(const std::optional<lvui::TableSelection> &selection);
    void showList();
    void showGrid();
    void gridSelected(const std::vector<std::string> &keys);
    void gridActivated(const std::string &key);
    void chooseModels();
    void modelsChosen(const std::vector<std::string> &paths);
    void modelChooserFailed(const std::string &message);
    void chooseFiles();
    void filesChosen(const std::vector<std::string> &paths);
    void fileChooserFailed(const std::string &message);
    void openSelected();
    void up();
    void previous();
    void next();
    void refresh();
    void reconnect();
    void searchEdited(const std::string &value);
    void search();
    void sortName();
    void sortDate();
    void sortKind();
    void sortSize();
    void showFiles();
    void showModels();
    void showDeleted();
    void showRename();
    void closeRename();
    void renameEdited(const std::string &value);
    void renameSelected();
    void trashSelected();
    void restoreSelected();
    void duplicateSelected();
    void showMove();
    void closeMove();
    void moveEdited(const std::string &value);
    void moveSelected();
    void showCreateFolder();
    void closeCreateFolder();
    void folderEdited(const std::string &value);
    void createFolder();
    void showRemove();
    void closeRemove();
    void removeSelected();
    void clearErrors();
    void previousError();
    void nextError();
    void resize(double width, double height);
    // Attach services only after the view has placed this model in its final scope.
    void retainServices(std::shared_ptr<void> services) { m_services = std::move(services); }
private:
    struct Entry { std::string path; bool directory; };
    std::string request(std::string_view op, ii::core::Value::Object fields = {});
    void open(std::string value);
    void applyState(const ii::core::Value &state);
    void requestPage();
    void clearRows();
    void updateControls();
    void applyModelState(const ii::core::Value &state);
    void setSelection(std::vector<std::string> paths);
    bool submitOperation(const std::string &action, const std::string &argument = {}, const std::string &source = {});
    void submitSelection(const std::string &action);
    void recordError(const std::string &path, const std::string &message);
    void updateErrors();
    void updateJob(const ii::core::Value &job, const std::string &jobId);
    void queryJobs();
    void selectSection(std::string root);
    void setSort(const std::string &field, const std::string &label);
    std::string m_container, m_root{"Files"}, m_wantedPath{"Files"}, m_generation, m_directoryRevision;
    std::string m_openRequest, m_pageRequest, m_stateRequest, m_activateRequest, m_openingFile, m_selected;
    std::string m_importRequest, m_importRequestContainer, m_importJob, m_importContainer, m_chooseContainer;
    std::string m_chooseFilesContainer, m_chooseFilesPath, m_chooseFilesGeneration;
    std::string m_loadedRevision;
    std::string m_search, m_sort{"name"};
    std::vector<std::string> m_selectedPaths, m_moveTargets, m_removeTargets, m_errors;
    std::vector<Entry> m_entries;
    std::string m_requestPrefix;
    std::unordered_map<std::string,std::string> m_operationRequests, m_jobQueries, m_jobs;
    std::unordered_map<std::string,std::string> m_requestTargets, m_jobTargets;
    std::size_t m_omittedErrors = 0;
    std::size_t m_errorCursor = 0;
    std::uint64_t m_sequence = 0;
    bool m_directoryReady = false;
    bool m_choosingModels = false, m_choosingFiles = false, m_modelBusy = false;
    bool m_descending = false;
    std::size_t m_offset = 0, m_count = 0;
    // Destroy services before the fields and callbacks they reference.
    std::shared_ptr<void> m_services;
};
