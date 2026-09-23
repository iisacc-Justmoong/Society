#pragma once
#include <iiSocietyClient/AccountSession.h>
#include <QtQml/qqmlregistration.h>
class AccountController : public AccountSession {
    Q_OBJECT
    QML_ELEMENT
public:
    using AccountSession::AccountSession;
};
