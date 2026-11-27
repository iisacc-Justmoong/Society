#include "ContentIndexScheduler.h"
#include "Society/Content/FileDiscovery.h"

#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>
#include <utility>

namespace {
constexpr qsizetype maximumWatchPaths=1024;

// The path lists stay inside the host; callers only receive aggregate status.
struct WorkResult final {
    society::content::Result result;
    society::content::FileDiscovery::Batch discovery;
};

// Hosts may tune the poll cadence without changing the indexing policy. An
// invalid setting retains the conservative default rather than disabling work.
int pollInterval(int requested) {
    if (requested>0) return std::clamp(requested,100,60000);
    bool valid=false;
    const auto configured=qEnvironmentVariableIntValue("SOCIETY_CONTENT_INDEX_POLL_MS",&valid);
    return valid && configured>=100 && configured<=60000 ? configured : 5000;
}
}

ContentIndexScheduler::ContentIndexScheduler(Provider provider,ForegroundBusy foregroundBusy,
                                             int intervalMs,QObject *parent)
    : QObject(parent),m_provider(std::move(provider)),m_foregroundBusy(std::move(foregroundBusy)) {
    m_timer.setInterval(pollInterval(intervalMs));
    connect(&m_timer,&QTimer::timeout,this,[this] { tick(); });
    const auto changed=[this](const QString &) {
        m_discoveryHint=true;
        if (!m_active && !m_policyChanges) QTimer::singleShot(0,this,[this] { tick(); });
    };
    connect(&m_fileWatcher,&QFileSystemWatcher::directoryChanged,this,changed);
    connect(&m_fileWatcher,&QFileSystemWatcher::fileChanged,this,changed);
    m_timer.start();
}

ContentIndexScheduler::~ContentIndexScheduler() {
    m_timer.stop(); m_stop.request_stop(); m_selection.request_stop();
}

void ContentIndexScheduler::selectContainer(const QString &container) {
    m_stop.request_stop(); m_selection.request_stop(); m_selection=std::stop_source{};
    clearWatches(); m_discoveryHint=false;
    m_container=container; ++m_generation; m_disabled=false;
    const bool hashOnly=!society::content::fileContentEmbeddingEnabled();
    m_discovery=container.isEmpty()?nullptr:std::make_shared<society::content::FileDiscovery>(
        container,[hashOnly](const QString &path,quint64 size) {
            return hashOnly || society::content::isTextCandidate(path,size);
        },hashOnly?society::content::FileDiscovery::Mode::HashMetadata:society::content::FileDiscovery::Mode::Observe);
    m_status={{"state",container.isEmpty()?QStringLiteral("disabled"):QStringLiteral("checking")}};
    if (!container.isEmpty()) QTimer::singleShot(0,this,[this] { tick(); });
}

std::stop_token ContentIndexScheduler::selectionToken() const { return m_selection.get_token(); }

void ContentIndexScheduler::beginPolicyChange() {
    ++m_policyChanges; m_stop.request_stop();
}

void ContentIndexScheduler::endPolicyChange() {
    if (m_policyChanges>0) --m_policyChanges;
    m_disabled=false;
    // Begin the fallback interval from this policy decision; the immediate
    // tick below handles it without waiting for the periodic timer.
    m_timer.start();
    QTimer::singleShot(0,this,[this] { tick(); });
}

ContentIndexScheduler::Admission ContentIndexScheduler::yieldTo(std::function<void()> continuation) {
    if (!m_active) return Admission::ready;
    if (m_waiters.size()>=16) return Admission::full;
    m_waiters.push_back(std::move(continuation)); m_stop.request_stop();
    return Admission::deferred;
}

QJsonObject ContentIndexScheduler::status() const { return m_status; }

// Revoke old native event subscriptions without touching the source entries.
void ContentIndexScheduler::clearWatches() {
    const auto files=m_fileWatcher.files(),directories=m_fileWatcher.directories();
    if (!files.isEmpty()) m_fileWatcher.removePaths(files);
    if (!directories.isEmpty()) m_fileWatcher.removePaths(directories);
    m_watchIncomplete=false;
}

// Bound native subscriptions; failures remain visible and the independent
// periodic walk continues even when this event source cannot watch a path.
void ContentIndexScheduler::addWatches(const QStringList &directories,const QStringList &files) {
    auto watched=m_fileWatcher.directories(); watched.append(m_fileWatcher.files());
    for (const auto &path:directories+files) {
        if (watched.contains(path)) continue;
        if (watched.size()>=maximumWatchPaths || !m_fileWatcher.addPath(path)) { m_watchIncomplete=true; continue; }
        watched.append(path);
    }
}

void ContentIndexScheduler::tick() {
    const bool hashOnly=!society::content::fileContentEmbeddingEnabled();
    if (m_container.isEmpty() || m_active || m_policyChanges ||
        (m_foregroundBusy && m_foregroundBusy())) return;
    society::content::EmbedderAccess access;
    try { if (!hashOnly && m_provider) access=m_provider(); }
    catch (const std::exception &error) {
        m_status={{"state","error"},{"error",QString::fromUtf8(error.what())}}; return;
    } catch (...) { m_status={{"state","error"},{"error","The local embedding service is unavailable."}}; return; }
    m_stop=std::stop_source{};
    const auto stop=m_stop.get_token(),selection=m_selection.get_token();
    const auto container=m_container; const auto generation=m_generation;
    const auto discovery=m_discovery;
    const auto hint=std::exchange(m_discoveryHint,false);
    // Preserve the previous observation while the next potentially slow scan
    // is running, so a failure does not disappear during every polling tick.
    m_active=true; m_status.insert("busy",true);
    auto *watcher=new QFutureWatcher<WorkResult>(this);
    connect(watcher,&QFutureWatcher<WorkResult>::finished,this,[this,watcher,generation,stop] {
        auto work=watcher->result(); watcher->deleteLater(); m_active=false;
        auto &result=work.result;
        if (generation==m_generation && !stop.stop_requested()) {
            if (!result.error.isEmpty()) m_status={{"state","error"},{"error",result.error}};
            else if (result.value.contains("enabled") && !result.value.value("enabled").toBool()) {
                clearWatches(); m_disabled=true; m_status={{"state","disabled"}};
            } else {
                addWatches(work.discovery.directories,work.discovery.files);
                auto intake=work.discovery.status;
                if (!work.discovery.error.isEmpty())
                    intake={{"state","error"},{"error",work.discovery.error},{"completeness","unknown"}};
                intake.insert("watchedPaths",m_fileWatcher.directories().size()+m_fileWatcher.files().size());
                intake.insert("watchIncomplete",m_watchIncomplete);
                result.value.insert("discovery",intake);
                m_disabled=false; m_status=result.value;
            }
        }
        if (generation==m_generation) m_status.insert("busy",false);
        auto waiters=std::move(m_waiters); m_waiters.clear();
        for (auto &waiter:waiters) if (waiter) waiter();
        if (generation==m_generation && !stop.stop_requested() && !m_disabled &&
            (m_discoveryHint || m_status.value("state")=="working" ||
             m_status.value("discovery").toObject().value("state")=="working"))
            QTimer::singleShot(std::min(m_timer.interval(),250),this,[this] { tick(); });
    });
    watcher->setFuture(QtConcurrent::run([container,access,stop,selection,discovery,hint,hashOnly] {
        WorkResult work;
        std::stop_source linked;
        std::stop_callback onStop(stop,[&linked] { linked.request_stop(); });
        std::stop_callback onSelection(selection,[&linked] { linked.request_stop(); });
        std::stop_callback onProvider(access.cancellation,[&linked] { linked.request_stop(); });
        const auto token=linked.get_token();
        if (hashOnly) {
            if (!discovery || token.stop_requested()) {
                work.result={{},QStringLiteral("File intake was cancelled."),society::content::Result::Failure::retryable};
                return work;
            }
            work.discovery=discovery->step(hint,token);
            if (!work.discovery.error.isEmpty()) work.result.error=work.discovery.error;
            else work.result.value={{"state",work.discovery.status.value("state")},{"mode","hash_metadata_only"}};
            return work;
        }
        const auto policy=society::content::trackedTextPolicy(container,token);
        if (!policy.error.isEmpty() || !policy.value.value("enabled").toBool()) { work.result=policy; return work; }
        if (!access.embedder || !discovery || token.stop_requested()) {
            work.result={{},QStringLiteral("The selected container's local embedding service is unavailable."),
                         society::content::Result::Failure::retryable};
            return work;
        }
        const society::content::Profile profile{policy.value.value("model").toString(),
            policy.value.value("documentPrefix").toString(),policy.value.value("queryPrefix").toString()};
        work.discovery=discovery->step(hint,token);
        if (token.stop_requested()) {
            work.result={{},QStringLiteral("File intake was cancelled."),society::content::Result::Failure::retryable};
            return work;
        }
        // At most one admitted file is read per slice, after directory intake
        // returns. The legacy journal is advanced independently below.
        if (!work.discovery.logicalFiles.isEmpty()) {
            const auto indexed=society::content::indexText(container,work.discovery.logicalFiles.first(),
                                                            profile,*access.embedder,{},token);
            if (!indexed.error.isEmpty()) {
                work.discovery.status.insert("indexError",indexed.error);
                work.discovery.status.insert("indexPath",work.discovery.logicalFiles.first());
            }
        }
        society::content::BatchOptions options; options.scanLimit=32; options.workLimit=1;
        work.result=society::content::indexTrackedTextBatch(container,profile,*access.embedder,options,token);
        return work;
    }));
}
