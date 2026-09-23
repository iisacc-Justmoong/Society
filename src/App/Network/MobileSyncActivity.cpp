#include "MobileSyncActivity.h"
#include <QPointer>
#include <QTimer>
#include <QThread>
#include <algorithm>
#include <limits>
#include <utility>

MobileSyncActivity::MobileSyncActivity(QObject *parent) : QObject(parent) {
#if defined(Q_OS_IOS) || defined(Q_OS_ANDROID)
    setBackend(societyBeginBackgroundSync, societyEndBackgroundSync);
#endif
#if defined(Q_OS_IOS)
    setContinuedBackend(societyBeginContinuedSync, societyCompleteContinuedSync, societyUpdateContinuedSync);
#endif
}
MobileSyncActivity::~MobileSyncActivity() { release(); }
void MobileSyncActivity::setBackend(Begin begin, End end) {
    release(); m_begin = std::move(begin); m_end = std::move(end);
}
void MobileSyncActivity::setContinuedBackend(Begin begin, Complete complete, Progress progress) {
    release(); m_beginContinued = std::move(begin); m_complete = std::move(complete); m_progress = std::move(progress);
}
bool MobileSyncActivity::retain() { return start(false); }
bool MobileSyncActivity::retainContinued() { return m_beginContinued ? start(true) : retain(); }
bool MobileSyncActivity::start(bool continued) {
    if (m_active && (m_continued || !continued)) return true;
    release();
    const auto &begin = continued ? m_beginContinued : m_begin;
    if (!begin || (continued ? !m_complete : !m_end)) return false;
    m_continued = continued; m_fileProgress.clear(); m_remainingWork.clear(); m_completedBytes = 0;
    if (continued) m_batchActive = true;
    const auto generation = ++m_generation; const QPointer<MobileSyncActivity> guard(this);
    m_active = begin([guard, generation] {
        if (!guard) return;
        const auto stop = [guard, generation] {
            if (!guard || guard->m_generation != generation || !guard->m_active) return;
            // Cancellation must drain file workers before returning the grant;
            // iOS kills a suspended process that still owns a shared file lock.
            emit guard->expired();
            if (guard && guard->m_generation == generation) guard->release();
        };
        if (QThread::currentThread() == guard->thread()) stop();
        else QTimer::singleShot(0, guard, stop);
    });
    return m_active;
}
void MobileSyncActivity::update(const QString &path, qint64 done, qint64 total) {
    if (!m_batchActive || !m_progress || path.isEmpty() || done < 0 || total <= 0 || done > total) return;
    const auto previous = m_fileProgress.value(path);
    const auto delta = done >= previous ? done - previous : done;
    const auto maximum = std::numeric_limits<qint64>::max();
    m_completedBytes += std::min(delta, maximum - 1 - m_completedBytes);
    m_fileProgress[path] = done;
    if (done == total) m_remainingWork.remove(path); else m_remainingWork[path] = total - done;
    // File synchronization, photo indexing, and original transfer can overlap.
    // Count each path's delta once and retain every known unfinished resource.
    qint64 remaining = 0;
    for (const auto pending : m_remainingWork)
        remaining += std::min(pending, maximum - m_completedBytes - remaining);
    // One unit remains until both queues confirm the complete batch.
    remaining = std::max<qint64>(1, remaining);
    m_progress(m_completedBytes, m_completedBytes + remaining);
}
void MobileSyncActivity::release(bool success) {
    ++m_generation;
    // Finishing actual work is independent of an execution grant that expired.
    const bool finishedBatch = success && std::exchange(m_batchActive, false);
    if (finishedBatch && !continued() && m_complete) m_complete(true);
    if (!m_active) return;
    m_active = false;
    if (m_continued) { m_continued = false; if (m_complete) m_complete(success); }
    else if (m_end) m_end();
}
