#include "NativeSpaceViewModel.h"
#include <algorithm>
#include <cctype>
#include <utility>

namespace {
/// Remove surrounding ASCII whitespace without altering the stored inner text.
std::string trimmed(std::string text) {
    auto space = [](unsigned char c) { return std::isspace(c) != 0; };
    text.erase(text.begin(), std::find_if_not(text.begin(), text.end(), space));
    text.erase(std::find_if_not(text.rbegin(), text.rend(), space).base(), text.end());
    return text;
}
/// Extract a display name without converting UTF-8 through a platform code page.
std::string fileName(const std::string &path) {
    const auto separator=path.find_last_of("/\\");
    return (separator==std::string::npos ? path : path.substr(separator+1)) + " ×";
}
}

void NativeSpaceViewModel::setSnapshot(const std::vector<iiSocietyContainer::SpaceSession> &all,
                                       std::optional<iiSocietyContainer::SpaceGraph> graph) {
    const auto old = currentSessionId.get();
    graph_ = std::move(graph);
    const auto current = graph_ ? graph_->session.id : std::string{};
    if (old != current) {
        selectedId_.clear();
        const auto draft = promptDrafts_.find(current);
        promptDraft.set(draft == promptDrafts_.end() ? std::string{} : draft->second);
        std::vector<NativeSpaceLine> files;
        for (const auto &path : attachmentDrafts_[current])
            files.push_back({path,fileName(path),{}});
        attachments.set(std::move(files));
        hasAttachments.set(!attachmentDrafts_[current].empty());
        messages.set({}); hasMessages.set(false); agentAvailable.set(false); agentBusy.set(false); reasoningOpen.set(false);
        agentStatus.set(current.empty() ? "Select a Space session." : "Connecting to the Space agent…");
    }
    currentSessionId.set(current);
    std::vector<NativeSpaceSession> rows;
    for (const auto &s : all) rows.push_back({s.id, s.title, s.id == current});
    sessions.set(std::move(rows));
    hasSession.set(bool(graph_));
    sessionTitle.set(graph_ ? graph_->session.title : "No session");
    sessionGoal.set(graph_ ? graph_->session.goal : "");
    sessionState.set(graph_ ? graph_->session.status : "No session");
    ideaCount.set(graph_ ? int(graph_->ideas.size()) : 0);
    connectionCount.set(graph_ ? int(graph_->connections.size()) : 0);
    hasIdeas.set(ideaCount.get() > 0);
    std::vector<NativeSpaceConnection> links;
    if (graph_) for (const auto &edge : graph_->connections) links.push_back({edge.id, edge.fromId, edge.toId});
    connections.set(std::move(links));
    sessionSummary.set(graph_ ? graph_->session.title + " · " + std::to_string(ideaCount.get()) + " ideas · " +
        std::to_string(connectionCount.get()) + " connections · Saved in Space" : "No saved session");
    std::vector<NativeSpaceLine> log;
    if (graph_) for (const auto &entry : graph_->activities) {
        std::string name = entry.agentId;
        const auto agent = std::find_if(graph_->agents.begin(), graph_->agents.end(),
            [&](const auto &a) { return a.id == entry.agentId; });
        if (agent != graph_->agents.end()) name = agent->name;
        log.push_back({entry.id, name, entry.summary});
    }
    activities.set(std::move(log));
    canAddIdea.set(hasSession.get() && !trimmed(ideaDraft.get()).empty());
    rebuildIdeas(); rebuildSelection();
}
void NativeSpaceViewModel::clear(const std::string &message) {
    selectedId_.clear(); graph_.reset(); storeAvailable.set(false);
    promptDrafts_.clear(); attachmentDrafts_.clear(); submissions_.clear(); attachments.set({}); hasAttachments.set(false); messages.set({}); hasMessages.set(false);
    sessionDraft.set(""); ideaDraft.set(""); promptDraft.set(""); creatingSession.set(false);
    sessionManagerVisible.set(false); canCreateSession.set(false);
    setSnapshot({}, std::nullopt); status.set(message);
}
void NativeSpaceViewModel::resize(double width, double height) {
    viewportWidth.set(std::clamp(width, 1.0, 8192.0));
    viewportHeight.set(std::clamp(height, 1.0, 8192.0));
    const bool wide = width >= 760;
    panelVisible.set(wide);
    const double panel = wide ? std::clamp(width * 0.25, 240.0, 356.0) : 0.0;
    const double main = std::max(260.0, width - (wide ? panel + 72.0 : 48.0));
    panelWidth.set(wide ? panel : 240.0); mainWidth.set(main);
    panelHeight.set(std::max(200.0, height - 32.0));
    activityHeight.set(std::max(80.0, panelHeight.get() - 200.0));
    // Reserve a compact composer; expanded session management is a separate panel.
    const double reserve = (sessionManagerVisible.get() ? (creatingSession.get() ? 390.0 : 360.0) : 180.0)
        + (wide ? 0.0 : 32.0 + (hasMessages.get() ? 152.0 : 0.0))
        + (hasAttachments.get() ? 30.0 : 0.0) + (reasoningOpen.get() ? 30.0 : 0.0) + (recording.get() ? 22.0 : 0.0);
    const double graph = std::max(140.0, height - reserve);
    graphHeight.set(graph); columnHeight.set(std::max(140.0, graph - 32.0));
    columnWidth.set(std::max(60.0, (main - 210.0) / 2.0));
    fieldWidth.set(std::max(80.0, main - 210.0));
}
void NativeSpaceViewModel::selectSession(const std::string &id) { if (onSelectSession) onSelectSession(id); }
void NativeSpaceViewModel::selectIdea(const std::string &id) {
    selectedId_ = id; rebuildIdeas(); rebuildSelection();
}
std::function<void()> NativeSpaceViewModel::sessionAction(const std::string &id) { return [this, id] { selectSession(id); }; }
std::function<void()> NativeSpaceViewModel::ideaAction(const std::string &id) { return [this, id] { selectIdea(id); }; }
void NativeSpaceViewModel::beginSession() { creatingSession.set(true); }
void NativeSpaceViewModel::cancelSession() { creatingSession.set(false); sessionEdited(""); }
void NativeSpaceViewModel::toggleSessionManager() { sessionManagerVisible.set(!sessionManagerVisible.get()); }
void NativeSpaceViewModel::sessionEdited(std::string text) {
    sessionDraft.set(std::move(text)); canCreateSession.set(storeAvailable.get() && !trimmed(sessionDraft.get()).empty());
}
void NativeSpaceViewModel::ideaEdited(std::string text) {
    ideaDraft.set(std::move(text)); canAddIdea.set(hasSession.get() && !trimmed(ideaDraft.get()).empty());
}
void NativeSpaceViewModel::promptEdited(std::string text) {
    promptDraft.set(text);
    promptDrafts_[currentSessionId.get()] = std::move(text);
}
void NativeSpaceViewModel::setAgentAvailable(bool available) { agentAvailable.set(available); }
void NativeSpaceViewModel::applyAgentState(std::vector<NativeSpaceLine> entries,std::string state,bool available,bool busy,bool retryable) {
    hasMessages.set(!entries.empty()); messages.set(std::move(entries)); agentStatus.set(std::move(state));
    agentAvailable.set(available); agentBusy.set(busy);
    const auto id=currentSessionId.get();
    auto pending=submissions_.find(id);
    if (pending==submissions_.end()) return;
    if (busy) { pending->second.observedBusy=true; return; }
    if (!pending->second.observedBusy) return;
    if (!retryable) {
        if (promptDraft.get()==pending->second.prompt) promptEdited("");
        if (attachmentDrafts_[id]==pending->second.paths) {
            attachmentDrafts_[id].clear(); attachments.set({}); hasAttachments.set(false);
        }
    }
    submissions_.erase(pending);
}
void NativeSpaceViewModel::addAttachments(const std::vector<std::string> &paths) {
    if (!hasSession.get()) return;
    auto &saved=attachmentDrafts_[currentSessionId.get()];
    for (const auto &path : paths) {
        if (path.empty() || std::find(saved.begin(),saved.end(),path)!=saved.end()) continue;
        if (saved.size()>=8) { agentStatus.set("Attach at most eight files."); break; }
        saved.push_back(path);
    }
    std::vector<NativeSpaceLine> files;
    for (const auto &path : saved) files.push_back({path,fileName(path),{}});
    attachments.set(std::move(files));
    hasAttachments.set(!saved.empty());
}
void NativeSpaceViewModel::removeAttachment(const std::string &path) {
    auto &saved=attachmentDrafts_[currentSessionId.get()];
    saved.erase(std::remove(saved.begin(),saved.end(),path),saved.end());
    std::vector<NativeSpaceLine> files;
    for (const auto &item : saved) files.push_back({item,fileName(item),{}});
    attachments.set(std::move(files));
    hasAttachments.set(!saved.empty());
}
std::function<void()> NativeSpaceViewModel::removeAttachmentAction(const std::string &path) { return [this,path] { removeAttachment(path); }; }
void NativeSpaceViewModel::attach() { if (hasSession.get() && onAttach) onAttach(); }
void NativeSpaceViewModel::microphone() { if (hasSession.get() && onMicrophone) onMicrophone(); }
void NativeSpaceViewModel::toggleReasoning() { reasoningOpen.set(!reasoningOpen.get()); }
void NativeSpaceViewModel::selectReasoning(const std::string &value) {
    if (value!="low" && value!="standard" && value!="high") return;
    reasoning_=value; reasoningOpen.set(false);
    reasoningLabel.set(value=="low" ? "Reasoning · Light" : value=="high" ? "Reasoning · Deep" : "Reasoning · Standard");
}
void NativeSpaceViewModel::reasoningLow() { selectReasoning("low"); }
void NativeSpaceViewModel::reasoningStandard() { selectReasoning("standard"); }
void NativeSpaceViewModel::reasoningHigh() { selectReasoning("high"); }
void NativeSpaceViewModel::send() {
    if (!hasSession.get() || !agentAvailable.get() || agentBusy.get() || trimmed(promptDraft.get()).empty() || !onSend) return;
    const auto id=currentSessionId.get(), draft=promptDraft.get(); const auto paths=attachmentDrafts_[id];
    if (onSend(draft,paths,reasoning_)) {
        submissions_[id]={draft,paths,false}; agentAvailable.set(false);
        agentStatus.set("Sending…");
    }
}
void NativeSpaceViewModel::createSession() {
    const auto title = trimmed(sessionDraft.get());
    if (!title.empty() && onCreateSession) onCreateSession(title);
}
void NativeSpaceViewModel::addIdea() {
    const auto title = trimmed(ideaDraft.get());
    if (hasSession.get() && !title.empty() && onAddIdea) onAddIdea(title);
}
void NativeSpaceViewModel::clearSelection() { selectIdea(""); }
void NativeSpaceViewModel::rebuildIdeas() {
    std::vector<NativeSpaceIdea> left, right;
    if (graph_) for (std::size_t i = 0; i < graph_->ideas.size(); ++i) {
        const auto &idea = graph_->ideas[i];
        (i % 2 ? right : left).push_back({idea.id, idea.title, idea.content, idea.id == selectedId_});
    }
    leftIdeas.set(std::move(left)); rightIdeas.set(std::move(right));
}
void NativeSpaceViewModel::rebuildSelection() {
    const iiSocietyContainer::SpaceIdea *idea = nullptr;
    if (graph_) for (const auto &candidate : graph_->ideas)
        if (candidate.id == selectedId_) { idea = &candidate; break; }
    if (!idea) selectedId_.clear();
    hasSelection.set(idea != nullptr);
    selectedTitle.set(idea ? idea->title : ""); selectedContent.set(idea ? idea->content : "");
    selectedProvenance.set(idea ? idea->provenance : ""); selectedEvaluation.set(idea ? idea->evaluation : "");
    std::vector<NativeSpaceLine> edges;
    if (idea && graph_) for (const auto &edge : graph_->connections) {
        if (edge.fromId != idea->id && edge.toId != idea->id) continue;
        const auto otherId = edge.fromId == idea->id ? edge.toId : edge.fromId;
        const auto other = std::find_if(graph_->ideas.begin(), graph_->ideas.end(), [&](const auto &i) { return i.id == otherId; });
        const auto name = other == graph_->ideas.end() ? otherId : other->title;
        edges.push_back({edge.id, edge.relation + " · " + name, edge.evidence});
    }
    relationships.set(std::move(edges));
}
