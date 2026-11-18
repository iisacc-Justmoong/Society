#pragma once
#include <QObject>
#include <QLocalSocket>
#include <QTimer>
#include <QHash>
#include <QThreadPool>
#include <iiMultimodalAgent>
#include <functional>
#include <memory>

// Serves window requests only over the authenticated daemon connection.
class UiBridgeRegistration final : public QObject {
public:
    using PeerVerifier = std::function<bool(QLocalSocket *)>;
    UiBridgeRegistration(std::shared_ptr<iiMultimodalAgent::agent::ToolRunner> runner,
                         QObject *parent = nullptr, PeerVerifier verifier = {});
    ~UiBridgeRegistration() override;
    void start();
    void stop();
private:
    void connectIfNeeded();
    void readMessages();
    void scheduleRetry();
    void send(const QJsonObject &message);
    void cancelPending();
    QLocalSocket m_socket;
    QTimer m_retry;
    QByteArray m_input;
    std::shared_ptr<iiMultimodalAgent::agent::ToolRunner> m_runner;
    QHash<QString,iiMultimodalAgent::CancellationToken> m_pending;
    QThreadPool m_questionPool, m_actionPool;
    PeerVerifier m_verifyDaemon;
    QString m_requestId;
    bool m_enabled = false;
    qint64 m_lastActivationMs = 0;
};
