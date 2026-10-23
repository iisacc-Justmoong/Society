#include "SocietyRuntime.h"
#include "ContainerRuntimePath.h"
#include <QCoreApplication>

SocietyRuntime::SocietyRuntime(bool embeddedReceiver, const iiSocietyHelper::ObservationOptions &options, QObject *parent)
    : QObject(parent), m_embeddedReceiver(embeddedReceiver), m_options(options)
{
    m_retry.setInterval(1000);
    connect(&m_retry, &QTimer::timeout, this, &SocietyRuntime::reconcile);
    connect(qApp, &QCoreApplication::aboutToQuit, this, &SocietyRuntime::stop);
}
SocietyRuntime::~SocietyRuntime() { stop(); }
void SocietyRuntime::setApplicationState(Qt::ApplicationState state)
{
    m_state = state;
    // Qt iOS maps UIApplicationStateBackground to ApplicationSuspended.
    // Inactive also occurs while presenting a picker or Control Center.
    if (m_embeddedReceiver && (state == Qt::ApplicationSuspended || state == Qt::ApplicationHidden)) {
        stop();
        return;
    }
    reconcile();
    m_retry.start();
}
void SocietyRuntime::reconcile()
{
    if (m_validation.valid()
        && m_validation.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        const auto [directory, valid] = m_validation.get();
        if (m_validationGeneration == m_generation && (!valid || directory != m_helper.directory())) {
            m_helper.stop(); m_inbox.stop(); m_receiver.stop();
        }
    }
    if (!m_helper.isRunning()) {
        const auto directory = m_options.directory.isEmpty() ? iiSocietyHelper::Helper::defaultDirectory() : m_options.directory;
        if (!societyContainerRuntimePath(directory)) {
            m_inbox.stop(); m_receiver.stop(); return;
        }
        m_inbox.stop();
        m_receiver.stop();
        if (!m_helper.start({"com.iisacc.society", "Society", SOCIETY_APP_VERSION}, m_options)) return;
    }
    m_helper.setActivity(m_state == Qt::ApplicationActive ? iiSocietyHelper::Activity::Foreground
                                                        : iiSocietyHelper::Activity::Background);
    if (m_embeddedReceiver && !m_receiver.isRunning())
        m_receiver.start(m_helper.directory(), m_options.heartbeatIntervalMs, m_options.peerTimeoutMs);
    if (!m_inbox.connected()) m_inbox.start(m_helper.directory());
    if (!m_validation.valid()) {
        m_validationGeneration = m_generation;
        m_validation = std::async(std::launch::async, [requested = m_options.directory] {
            const auto directory = requested.isEmpty() ? iiSocietyHelper::Helper::defaultDirectory() : requested;
            return std::pair{directory, societyContainerRuntimePath(directory)};
        });
    }
}
void SocietyRuntime::stop()
{
    ++m_generation;
    m_retry.stop();
    // Commit the final Helper event before the receiver's last bounded drain.
    m_helper.stop();
    m_receiver.stop();
    m_inbox.stop();
}
