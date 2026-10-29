#pragma once
#include <Qt>
#include <QObject>
#include <QHash>
#include <QString>
#include <functional>

class MobileSyncActivity final : public QObject {
    Q_OBJECT
public:
    using Begin = std::function<bool(std::function<void()>)>;
    using End = std::function<void()>;
    using Complete = std::function<void(bool)>;
    using Progress = std::function<void(qint64, qint64)>;
    explicit MobileSyncActivity(QObject *parent = nullptr);
    ~MobileSyncActivity() override;
    void setBackend(Begin begin, End end);
    void setContinuedBackend(Begin begin, Complete complete, Progress progress);
    bool retain();
    bool retainContinued();
    void update(const QString &path, qint64 done, qint64 total);
    void release(bool success = false);
    bool active() const { return m_active; }
    bool continued() const { return m_active && m_continued; }
    bool batchActive() const { return m_batchActive; }
signals:
    void expired();
private:
    Begin m_begin; End m_end;
    Begin m_beginContinued; Complete m_complete; Progress m_progress;
    bool start(bool continued);
    bool m_continued = false;
    bool m_batchActive = false;
    QHash<QString, qint64> m_fileProgress, m_remainingWork;
    qint64 m_completedBytes = 0;
    bool m_active = false;
    quint64 m_generation = 0;
};

// Foreground transfer progress must remain visible until this batch finishes.
inline bool societySyncNeedsScreen(bool synchronizing, bool connected, Qt::ApplicationState state) {
    return synchronizing && connected && state == Qt::ApplicationActive;
}
void societySetSyncScreenActive(bool active);
#if defined(Q_OS_IOS) || defined(Q_OS_ANDROID)
bool societyBeginBackgroundSync(std::function<void()> expired);
void societyEndBackgroundSync();
#endif
#if defined(Q_OS_IOS)
bool societyBeginContinuedSync(std::function<void()> expired);
void societyCompleteContinuedSync(bool success);
void societyUpdateContinuedSync(qint64 completed, qint64 total);
void societyRestartSyncPresentation();
#endif
