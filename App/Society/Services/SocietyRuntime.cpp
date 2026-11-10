#include "SocietyRuntime.h"
#include "ContainerRuntimePath.h"
#include <QCoreApplication>
#include <QDebug>
#include <QPromise>
#include <thread>

SocietyRuntime::SocietyRuntime(bool embeddedReceiver, const iiSocietyHelper::ObservationOptions &options,
                               QObject *parent, Resolver resolver, iiSocietyHelper::HelperLifecycleHooks hooks)
    : QObject(parent), m_embeddedReceiver(embeddedReceiver), m_options(options),
      m_helper(iiSocietyHelper::FileSystem::Initialization::Deferred),
      m_resolver(resolver ? std::move(resolver) : Resolver([](const QString &requested) {
          const auto directory = requested.isEmpty() ? iiSocietyHelper::Helper::defaultDirectory() : requested;
          return std::pair{directory, societyContainerRuntimePath(directory)};
      })), m_hooks(std::move(hooks))
{
    // The desktop owner controls final shutdown; Helper's compatibility slot
    // otherwise calls its synchronous stop before this object's quit handler.
    if (!m_embeddedReceiver)
        disconnect(qApp, &QCoreApplication::aboutToQuit, &m_helper, &iiSocietyHelper::Helper::stop);
    m_retry.setInterval(1000);
    connect(&m_retry, &QTimer::timeout, this, &SocietyRuntime::reconcile);
    connect(qApp, &QCoreApplication::aboutToQuit, this, [this] {
        if (!m_embeddedReceiver) m_helper.fileSystem()->prepareForShutdown();
        stop();
    });
    connect(&m_validation, &QFutureWatcherBase::finished, this, [this] {
        m_validationPending = false;
        if (!m_enabled) return;
        if (m_validationGeneration != m_generation) { reconcile(); return; }
        const auto [directory, valid] = m_validation.result();
        if (m_helper.isStopping()) return;
        if ((m_helper.isRunning() && (!valid || directory != m_helper.directory()))
            || (m_helper.isStarting() && (!valid || directory != m_startDirectory))) {
            m_inbox.stop();
            if (m_embeddedReceiver) { m_helper.stop(); m_receiver.stop(); }
            else { m_helper.stopAsync(); return; }
        }
        if (!valid) {
            m_inbox.stop(); m_receiver.stop(); return;
        }
        if (!m_helper.isRunning()) {
            if (m_helper.isStarting()) return;
            m_inbox.stop(); m_receiver.stop();
            auto options = m_options;
            options.directory = directory;
            if (m_embeddedReceiver) {
                if (!m_helper.start({"com.iisacc.society", "Society", SOCIETY_APP_VERSION}, options)) return;
            } else {
                m_startDirectory = directory;
                m_helper.startAsync({"com.iisacc.society", "Society", SOCIETY_APP_VERSION}, options, m_hooks);
                return;
            }
        }
        connectRunningHelper();
    });
    connect(&m_helper, &iiSocietyHelper::Helper::startFinished, this, [this](bool success) {
        if (success && m_enabled) connectRunningHelper();
    });
    connect(&m_helper, &iiSocietyHelper::Helper::stopFinished, this, [this] {
        if (m_enabled) reconcile();
    });
}
SocietyRuntime::~SocietyRuntime() { stop(); }
void SocietyRuntime::connectRunningHelper()
{
    if (!m_enabled || !m_helper.isRunning()) return;
    m_helper.setActivity(m_state == Qt::ApplicationActive ? iiSocietyHelper::Activity::Foreground
                                                         : iiSocietyHelper::Activity::Background);
    if (m_embeddedReceiver && !m_receiver.isRunning())
        m_receiver.start(m_helper.directory(), m_options.heartbeatIntervalMs, m_options.peerTimeoutMs);
    if (!m_inbox.connected() && !m_inbox.starting()) m_inbox.start(m_helper.directory());
}
void SocietyRuntime::setApplicationState(Qt::ApplicationState state)
{
    m_state = state;
    // Qt iOS maps UIApplicationStateBackground to ApplicationSuspended.
    // Inactive also occurs while presenting a picker or Control Center.
    if (m_embeddedReceiver && (state == Qt::ApplicationSuspended || state == Qt::ApplicationHidden)) {
        stop();
        return;
    }
    m_enabled = true;
    if (m_helper.isRunning())
        m_helper.setActivity(state == Qt::ApplicationActive ? iiSocietyHelper::Activity::Foreground
                                                           : iiSocietyHelper::Activity::Background);
    reconcile();
    m_retry.start();
}
void SocietyRuntime::reconcile()
{
    if (!m_enabled || m_validationPending) return;
    m_validationPending = true;
    m_validationGeneration = m_generation;
    auto promise = std::make_shared<QPromise<std::pair<QString, bool>>>();
    promise->start();
    m_validation.setFuture(promise->future());
    std::thread([requested = m_options.directory, resolver = m_resolver, promise] {
        try { promise->addResult(resolver(requested)); }
        catch (const std::exception &error) {
            qWarning() << "Society runtime validation failed:" << error.what();
            promise->addResult({QString(), false});
        }
        catch (...) { promise->addResult({QString(), false}); }
        promise->finish();
    }).detach();
}
void SocietyRuntime::stop()
{
    ++m_generation;
    m_enabled = false;
    m_retry.stop();
    if (m_embeddedReceiver) {
        // The mobile receiver retains its lease until the last drain completes.
        m_helper.stop();
        m_receiver.stop();
    } else {
        m_helper.stopAsync();
    }
    m_inbox.stop();
}
