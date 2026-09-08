#pragma once

#include "SocietyInbox.h"
#include "Daemon/DaemonService.h"

// One owner for the app's Helper, inbox, and mobile receiver. The desktop
// launchd service remains independent of this object's lifetime.
class SocietyRuntime final : public QObject {
    Q_OBJECT
public:
    explicit SocietyRuntime(bool embeddedReceiver, const iiSocietyHelper::ObservationOptions &options = {},
                            QObject *parent = nullptr);
    ~SocietyRuntime() override;
    void setApplicationState(Qt::ApplicationState state);
    void stop();
    iiSocietyHelper::Helper *helper() { return &m_helper; }
    SocietyInbox *inbox() { return &m_inbox; }
private:
    void reconcile();
    const bool m_embeddedReceiver;
    const iiSocietyHelper::ObservationOptions m_options;
    iiSocietyHelper::Helper m_helper;
    SocietyInbox m_inbox;
    SocietyDaemonService m_receiver;
    QTimer m_retry;
    Qt::ApplicationState m_state = Qt::ApplicationSuspended;
};
