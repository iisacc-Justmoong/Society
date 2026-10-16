#pragma once
#include <iiSocietyHelper/AccountSession.h>
#include <QtQmlIntegration/qqmlintegration.h>
class AccountController : public AccountSession {
    Q_OBJECT
    QML_ELEMENT
public:
    using AccountSession::AccountSession;
};
