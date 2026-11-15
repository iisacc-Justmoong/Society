#pragma once

#include <QJsonObject>
#include <QJsonArray>
#include <QObject>
#include <QThread>
#include <memory>
namespace iiMultimodalAgent::agent { class Model; struct DecisionOptions; }
struct WorkspaceAgentToolTransport;

// Owns the agent independently of window/socket lifetimes. All model and
// transcript I/O is performed by its worker, not the daemon event loop.
class WorkspaceAgentBackend final : public QObject {
    Q_OBJECT
public:
    explicit WorkspaceAgentBackend(QObject *parent = nullptr,
        std::shared_ptr<iiMultimodalAgent::agent::Model> modelOverride = {}, QString modelOverrideId = {},
        std::shared_ptr<const iiMultimodalAgent::agent::DecisionOptions> decisionOverride = {},
        std::shared_ptr<const WorkspaceAgentToolTransport> transportOverride = {});
    ~WorkspaceAgentBackend() override;
    void selectContainer(const QString &container);
    void setBrokerDirectory(const QString &directory);
    QJsonObject snapshot() const;
    bool selectModel(const QString &id, QString *error);
    bool submit(const QString &prompt, QString *error);
    bool newConversation(QString *error);
    bool cancel(QString *error);
    // Low-priority, bounded local model estimate; user chat may cancel it.
    bool assessSuggested(const QJsonArray &evidence, const QString &ticket);
    void cancelSuggested();
signals:
    void changed(const QJsonObject &state);
    void suggestedAssessed(const QString &ticket, const QJsonArray &estimates, const QString &error);
private:
    struct Worker;
    void publish();
    void initialized(quint64 epoch, const QJsonObject &state);
    void progress(quint64 epoch, quint64 run, const QString &status);
    void delta(quint64 epoch, quint64 run, const QString &text);
    void finished(quint64 epoch, quint64 run, const QString &status, const QString &text, const QString &error, const QJsonObject &details = {});
    void suggestedFinished(quint64 epoch, quint64 sequence, const QString &ticket, const QJsonArray &estimates, const QString &error);
    QThread m_thread;
    QObject *m_dispatch = nullptr;
    std::shared_ptr<Worker> m_worker;
    QJsonObject m_state;
    QString m_container, m_model;
    quint64 m_epoch = 0, m_run = 0;
    bool m_busy = false, m_deltaScheduled = false;
    quint64 m_suggestionSequence = 0;
    QString m_suggestionTicket;
    bool m_suggestionBusy = false;
};
