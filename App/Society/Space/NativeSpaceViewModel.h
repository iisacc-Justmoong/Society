#pragma once

#include <SpaceStore.h>
#include <iiGUI/Image.h>
#include <iiSDKCore>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

/// A session choice projected from the current container's persistent store.
struct NativeSpaceSession final {
    std::string id, title;
    bool selected = false;
    friend bool operator==(const NativeSpaceSession &, const NativeSpaceSession &) = default;
};

/// A displayable idea, retaining the stable identifier used by the store.
struct NativeSpaceIdea final {
    std::string id, title, content;
    bool selected = false;
    friend bool operator==(const NativeSpaceIdea &, const NativeSpaceIdea &) = default;
};

/// One directed relation between stored idea IDs.
struct NativeSpaceConnection final {
    std::string id, fromId, toId;
    friend bool operator==(const NativeSpaceConnection &, const NativeSpaceConnection &) = default;
};

/// One saved relationship or agent activity, never an invented agent response.
struct NativeSpaceLine final {
    std::string id, title, detail;
    friend bool operator==(const NativeSpaceLine &, const NativeSpaceLine &) = default;
};

/// Owner-thread presentation state for the Space LVUI view.
/// Persistence and identifiers remain the responsibility of its host callbacks.
class NativeSpaceViewModel final {
public:
    ii::core::Observable<std::vector<NativeSpaceSession>> sessions;
    ii::core::Observable<std::vector<NativeSpaceIdea>> leftIdeas, rightIdeas;
    ii::core::Observable<std::vector<NativeSpaceLine>> activities, relationships;
    /// Displayed conversation and the current session's explicitly chosen attachments.
    ii::core::Observable<std::vector<NativeSpaceLine>> messages, attachments;
    ii::core::Observable<std::vector<NativeSpaceConnection>> connections;
    ii::core::Observable<std::shared_ptr<const ii::gui::RasterImage>> orbImage, connectionsImage;
    ii::core::Observable<std::string> currentSessionId{""}, sessionTitle{""}, sessionGoal{""};
    ii::core::Observable<std::string> sessionState{"No session"}, sessionSummary{"No saved session"};
    ii::core::Observable<std::string> selectedTitle{""}, selectedContent{""}, selectedProvenance{""}, selectedEvaluation{""};
    ii::core::Observable<std::string> sessionDraft{""}, ideaDraft{""}, status{"Connect a Society container to open Space."};
    /// Unsaved text for the current Space session; no agent submission is implied.
    ii::core::Observable<std::string> promptDraft{""};
    ii::core::Observable<std::string> agentStatus{"Connecting to the Space agent…"}, reasoningLabel{"Reasoning · Standard"};
    ii::core::Observable<std::string> microphoneStatus{"Starting microphone…"};
    ii::core::Observable<bool> agentAvailable{false}, agentBusy{false}, reasoningOpen{false}, recording{false}, hasAttachments{false}, hasMessages{false};
    ii::core::Observable<bool> hasSession{false}, hasSelection{false}, hasIdeas{false}, creatingSession{false}, storeAvailable{false};
    ii::core::Observable<bool> canCreateSession{false}, canAddIdea{false}, panelVisible{true};
    /// Whether the separate session-management controls are expanded.
    ii::core::Observable<bool> sessionManagerVisible{false};
    ii::core::Observable<int> ideaCount{0}, connectionCount{0};
    ii::core::Observable<double> viewportWidth{1440}, viewportHeight{810};
    ii::core::Observable<double> mainWidth{900}, panelWidth{320}, panelHeight{700}, activityHeight{500},
        graphHeight{480}, columnWidth{210}, columnHeight{430}, fieldWidth{500};
    std::function<void(const std::string &)> onSelectSession, onCreateSession, onAddIdea;
    /// Host adapters own transport, picker and audio lifetimes; the model remains testable.
    std::function<bool(const std::string &,const std::vector<std::string> &,const std::string &)> onSend;
    std::function<void()> onAttach, onMicrophone;

    /// Replace a complete snapshot, preserving selection only if its ID still exists.
    void setSnapshot(const std::vector<iiSocietyContainer::SpaceSession> &all,
                     std::optional<iiSocietyContainer::SpaceGraph> graph);
    /// Clear a previous container before attempting to open another one.
    void clear(const std::string &message);
    /// Recalculate the LVUI layout for the host's current logical size.
    void resize(double width, double height);
    /// Select a stored session by stable ID through the owning store adapter.
    void selectSession(const std::string &id);
    /// Select a stored idea within the current graph; unknown IDs clear selection.
    void selectIdea(const std::string &id);
    /// Return callbacks capturing stable IDs for LVUI repeated rows.
    [[nodiscard]] std::function<void()> sessionAction(const std::string &id);
    [[nodiscard]] std::function<void()> ideaAction(const std::string &id);
    /// Open or dismiss the explicit new-session form.
    void beginSession();
    void cancelSession();
    /// Expand or collapse session management without discarding any text draft.
    void toggleSessionManager();
    /// Update user-entered text without changing persisted data.
    void sessionEdited(std::string text);
    void ideaEdited(std::string text);
    /// Keep the unsent conversation draft in memory for the current session.
    void promptEdited(std::string text);
    /// Apply current session service state and settle a completed submission.
    void applyAgentState(std::vector<NativeSpaceLine> entries, std::string state, bool available, bool busy, bool retryable);
    void setAgentAvailable(bool available);
    /// Add and remove only explicit user choices, preserving each session's draft.
    void addAttachments(const std::vector<std::string> &paths);
    /// Bind a removal action to a stable selected path.
    [[nodiscard]] std::function<void()> removeAttachmentAction(const std::string &path);
    /// Remove one selected path from this session's draft.
    void removeAttachment(const std::string &path);
    /// Delegate file selection to the current native host.
    void attach();
    /// Toggle the host's native dictation service.
    void microphone();
    /// Show or hide the three supported generation budgets.
    void toggleReasoning();
    /// Select a validated budget without making a model-quality claim.
    void selectReasoning(const std::string &value);
    /// Select the light generation budget.
    void reasoningLow();
    /// Select the standard generation budget.
    void reasoningStandard();
    /// Select the deep generation budget.
    void reasoningHigh();
    /// Submit the current draft, retaining it until a confirmed successful run.
    void send();
    /// Ask the host to persist a nonempty, user-entered title.
    void createSession();
    void addIdea();
    /// Dismiss the selected idea inspector and return to saved activity.
    void clearSelection();

private:
    void rebuildIdeas();
    void rebuildSelection();
    /// Unsaved drafts are isolated by session and never written to the container.
    std::unordered_map<std::string, std::string> promptDrafts_;
    std::unordered_map<std::string, std::vector<std::string>> attachmentDrafts_;
    struct Submission { std::string prompt; std::vector<std::string> paths; bool observedBusy = false; };
    std::unordered_map<std::string, Submission> submissions_;
    std::string reasoning_ = "standard";
    std::optional<iiSocietyContainer::SpaceGraph> graph_;
    std::string selectedId_;
};
