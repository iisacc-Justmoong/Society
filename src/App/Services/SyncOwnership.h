#include <iiFileProviderDevice.h>
#pragma once
#include <QObject>
#include <QTimer>
#include <memory>
#include <future>

// One network identity across the desktop window and its login service.
// The foreground lease asks the daemon to finish releasing the owner lease.
class SyncOwnership final : public QObject {
    Q_OBJECT
public:
    SyncOwnership(QString directory, bool foreground, QObject *parent = nullptr);
    ~SyncOwnership() override;
    bool start();
    bool setDirectory(const QString &directory);
    QString directory() const { return m_directory; }
    void stop();
    bool owned() const { return m_owned; }
    QString errorString() const { return m_error; }
    bool rememberContainer(const QString &path);
    QString storedContainer() const;
signals:
    void ownershipChanged(bool owned);
private:
    void reconcile();
    void updateOwnership();
    void release();
    QString m_directory, m_error;
    bool m_foreground, m_owned = false;
    QTimer m_timer;
    std::future<bool> m_validation;
    quint64 m_generation = 0, m_validationGeneration = 0;
    std::unique_ptr<iiFileProvider::LockFile> m_priority, m_owner;
};
