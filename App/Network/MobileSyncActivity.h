#pragma once
#include <Qt>
#include <QObject>
#include <functional>

class MobileSyncActivity final : public QObject {
    Q_OBJECT
public:
    using Begin = std::function<bool(std::function<void()>)>;
    using End = std::function<void()>;
    explicit MobileSyncActivity(QObject *parent = nullptr);
    ~MobileSyncActivity() override;
    void setBackend(Begin begin, End end);
    bool retain();
    void release();
    bool active() const { return m_active; }
signals:
    void expired();
private:
    Begin m_begin; End m_end;
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
