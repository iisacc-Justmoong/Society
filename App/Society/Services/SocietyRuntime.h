#pragma once

#include "SocietyInbox.h"
#include "Daemon/DaemonService.h"
#include <QFutureWatcher>
#include <functional>

// One owner for the app's Helper, inbox, and mobile receiver. The desktop
// launchd service remains independent of this object's lifetime.
class SocietyRuntime final : public QObject {
    Q_OBJECT
public:
    using Resolver = std::function<std::pair<QString, bool>(const QString &)>;
    explicit SocietyRuntime(bool embeddedReceiver, const iiSocietyHelper::ObservationOptions &options = {},
                            QObject *parent = nullptr, Resolver resolver = {},
                            iiSocietyHelper::HelperLifecycleHooks hooks = {});
    ~SocietyRuntime() override;
    void setApplicationState(Qt::ApplicationState state);
    void stop();
    iiSocietyHelper::Helper *helper() { return &m_helper; }
    SocietyInbox *inbox() { return &m_inbox; }
private:
    void reconcile();
    void connectRunningHelper();
    const bool m_embeddedReceiver;
    const iiSocietyHelper::ObservationOptions m_options;
    iiSocietyHelper::Helper m_helper;
    SocietyInbox m_inbox;
    SocietyDaemonService m_receiver;
    QTimer m_retry;
    QFutureWatcher<std::pair<QString, bool>> m_validation;
    Resolver m_resolver;
    iiSocietyHelper::HelperLifecycleHooks m_hooks;
    QString m_startDirectory;
    quint64 m_generation = 0, m_validationGeneration = 0;
    bool m_enabled = false, m_validationPending = false;
    Qt::ApplicationState m_state = Qt::ApplicationSuspended;
};
