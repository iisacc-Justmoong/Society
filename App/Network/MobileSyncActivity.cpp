#include "MobileSyncActivity.h"
#include <QPointer>
#include <QTimer>

MobileSyncActivity::MobileSyncActivity(QObject *parent) : QObject(parent) {
#if defined(Q_OS_IOS) || defined(Q_OS_ANDROID)
    setBackend(societyBeginBackgroundSync, societyEndBackgroundSync);
#endif
}
MobileSyncActivity::~MobileSyncActivity() { release(); }
void MobileSyncActivity::setBackend(Begin begin, End end) {
    release(); m_begin = std::move(begin); m_end = std::move(end);
}
bool MobileSyncActivity::retain() {
    if (m_active) return true;
    if (!m_begin || !m_end) return false;
    const auto generation = ++m_generation; const QPointer<MobileSyncActivity> guard(this);
    m_active = m_begin([guard, generation] {
        if (!guard) return;
        QTimer::singleShot(0, guard, [guard, generation] {
            if (!guard || guard->m_generation != generation || !guard->m_active) return;
            guard->release(); emit guard->expired();
        });
    });
    return m_active;
}
void MobileSyncActivity::release() {
    ++m_generation;
    if (!m_active) return;
    m_active = false; if (m_end) m_end();
}
