#include <iiFileProvider>
#pragma once
#include <QObject>
#include <QTimer>
#include <memory>
#include <QFuture>
#include <functional>

// One network identity across the desktop window and its login service.
// The foreground lease asks the daemon to finish releasing the owner lease.
class SyncOwnership final : public QObject {
    Q_OBJECT
public:
    using Validator = std::function<bool(const QString &)>;
    SyncOwnership(QString directory, bool foreground, QObject *parent = nullptr, Validator validator = {});
    ~SyncOwnership() override;
    bool start();
    bool setDirectory(const QString &directory);
    QString directory() const { return m_directory; }
    void stop();
    // Only for final process exit. Retain the leases until the OS ends this
    // process while its network controller is being shut down asynchronously.
    void retainUntilProcessExit();
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
    bool m_foreground, m_owned = false, m_retired = false;
    QTimer m_timer;
    QFuture<bool> m_validation;
    Validator m_validator;
    bool m_validating = false;
    quint64 m_generation = 0, m_validationGeneration = 0;
    std::unique_ptr<iiFileProvider::LockFile> m_priority, m_owner;
};
