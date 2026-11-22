#pragma once

#include <QJsonObject>
#include <QJsonArray>
#include <QHash>
#include <QObject>
#include <QThread>
#include <QStringList>
#include <memory>
#include <functional>
#include <optional>
#include <stop_token>
namespace iiMultimodalAgent::agent { class Model; struct DecisionOptions; }
namespace iiLocalLLM { class Service; }
class ResidentGateway;
struct WorkspaceAgentToolTransport;

// Owns the agent independently of window/socket lifetimes. All model and
// transcript I/O is performed by its worker, not the daemon event loop.
class WorkspaceAgentBackend final : public QObject {
    Q_OBJECT
public:
    explicit WorkspaceAgentBackend(QObject *parent = nullptr,
        std::shared_ptr<iiMultimodalAgent::agent::Model> modelOverride = {}, QString modelOverrideId = {},
        std::shared_ptr<const iiMultimodalAgent::agent::DecisionOptions> decisionOverride = {},
        std::shared_ptr<const WorkspaceAgentToolTransport> transportOverride = {},
        std::function<void(const QString &, std::stop_token)> cachePrepareOverride = {},
        std::function<void(const QString &, std::stop_token)> residentPrepareOverride = {},
        int residentRetryIntervalMs = 15000);
    ~WorkspaceAgentBackend() override;
    void selectContainer(const QString &container);
    // Called by the model import service only when published files change.
    void modelFilesChanged(const QString &container, const QStringList &paths);
    void setBrokerDirectory(const QString &directory);
    QJsonObject snapshot() const;
    ResidentGateway *residentGateway() const { return m_resident.get(); }
    bool selectModel(const QString &id, QString *error);
    bool submit(const QString &prompt, QString *error);
    bool submit(const QString &prompt, const QJsonArray &attachments, QString *error);
    /// Load the conversation belonging to one persisted Space session asynchronously.
    bool observeSpace(const QString &spaceId, QString *error);
    /// Return the current Space state with shared runtime readiness and busy status.
    [[nodiscard]] QJsonObject spaceSnapshot(const QString &spaceId) const;
    /// Admit one isolated Space turn with explicitly selected attachments and effort.
    bool submitSpace(const QString &spaceId, const QString &prompt, const QJsonArray &attachments,
                     const QString &effort, QString *error);
    /// Cancel only a run owned by the specified Space session.
    bool cancelSpace(const QString &spaceId, QString *error);
    bool newConversation(QString *error);
    bool cancel(QString *error);
    // Low-priority, bounded local model estimate; user chat may cancel it.
    bool assessSuggested(const QJsonArray &evidence, const QString &ticket);
    void cancelSuggested();
signals:
    void changed(const QJsonObject &state);
    /// Published only for the corresponding Space session and selected container.
    void spaceChanged(const QString &spaceId, const QJsonObject &state);
    void suggestedAssessed(const QString &ticket, const QJsonArray &estimates, const QString &error);
private:
    struct Worker;
    void publish();
    bool needsResident(const QString &model) const;
    void refreshIdleStatus();
    void beginSelection(QString container, bool preserveConversation);
    void initialized(quint64 epoch, const QJsonObject &state);
    /// Install a worker-loaded transcript only while the container epoch still matches.
    void spaceLoaded(quint64 epoch, const QString &spaceId, const QJsonArray &messages, const QString &error);
    /// Dispatch transcript loading on the existing agent worker.
    void loadSpace(const QString &spaceId);
    void residentInitialized(quint64 epoch, const QString &model, std::shared_ptr<iiLocalLLM::Service> service,
                             const QString &error, bool waitForMemory, quint64 availableMemory, bool fixtureReady);
    void startResidentLoad(const QString &model, quint64 epoch);
    void retryResidentWhenMemoryChanges(const QString &model, quint64 epoch, quint64 availableMemory, int unchangedChecks = 0);
    void dispatchPending();
    void residentModelObserved(quint64 epoch, const QString &model);
    void scheduleModelCache(const QString &model, quint64 epoch);
    void modelCacheChanged(quint64 epoch, const QString &model, const QString &state,
                           const QString &error = {}, int percent = -1, bool hit = false);
    void progress(quint64 epoch, quint64 run, const QString &status);
    void delta(quint64 epoch, quint64 run, const QString &text);
    void finished(quint64 epoch, quint64 run, const QString &status, const QString &text, const QString &error, const QJsonObject &details = {});
    void suggestedFinished(quint64 epoch, quint64 sequence, const QString &ticket, const QJsonArray &estimates, const QString &error);
    QThread m_thread;
    QObject *m_dispatch = nullptr;
    std::shared_ptr<Worker> m_worker;
    std::unique_ptr<ResidentGateway> m_resident;
    QJsonObject m_state;
    QString m_container, m_model;
    struct PendingRequest { QString model, prompt; QJsonArray attachments; quint64 epoch, run; QString spaceId, effort; };
    std::optional<PendingRequest> m_pending;
    quint64 m_epoch = 0, m_run = 0;
    bool m_busy = false, m_deltaScheduled = false;
    bool m_requireResident = false, m_fixtureResident = false;
    int m_residentRetryIntervalMs = 15000;
    bool m_preserveConversation = false;
    bool m_modelChangeInterrupted = false;
    quint64 m_suggestionSequence = 0;
    QString m_suggestionTicket;
    bool m_suggestionBusy = false;
    QHash<QString, QJsonObject> m_spaceStates;
    QString m_activeSpace, m_workspaceStatusBeforeSpace;
};
