#pragma once
#include <QObject>
#include <QLockFile>
#include <QTimer>
#include <memory>

// One network identity across the desktop window and its login service.
// The foreground lease asks the daemon to finish releasing the owner lease.
class SyncOwnership final : public QObject {
    Q_OBJECT
public:
    SyncOwnership(QString directory, bool foreground, QObject *parent = nullptr);
    ~SyncOwnership() override;
    bool start();
    void stop();
    bool owned() const { return m_owned; }
    QString errorString() const { return m_error; }
    bool rememberContainer(const QString &path);
    QString storedContainer() const;
signals:
    void ownershipChanged(bool owned);
private:
    void reconcile();
    void release();
    QString m_directory, m_error;
    bool m_foreground, m_owned = false;
    QTimer m_timer;
    std::unique_ptr<QLockFile> m_priority, m_owner;
};
