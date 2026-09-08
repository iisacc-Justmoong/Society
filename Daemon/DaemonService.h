#pragma once

#include <iiSocietyHelper.h>
#include <DeliveryStore.h>
#include <QElapsedTimer>
#include <QLockFile>
#include <QObject>
#include <QTimer>
#include <memory>

class SocietyDaemonService final : public QObject {
    Q_OBJECT
public:
    explicit SocietyDaemonService(QObject *parent = nullptr);
    ~SocietyDaemonService() override;
    bool start(const QString &directory = {}, int heartbeatMs = 1000, int timeoutMs = 5000);
    void stop();
    bool isRunning() const { return m_running; }
    QString errorString() const { return m_error; }
    QString directory() const { return m_helper.directory(); }
signals:
    void dataAccepted(int count);
    void errorOccurred(const QString &message);
private:
    void drain();
    bool fail(const QString &message);
    iiSocietyHelper::Helper m_helper;
    iiSocietyHelper::DeliveryStore m_store;
    std::unique_ptr<QLockFile> m_lock;
    QTimer m_timer;
    QElapsedTimer m_snapshotAge;
    QString m_error;
    quint64 m_heartbeat = 0;
    bool m_running = false;
    bool m_snapshotDirty = true;
};
