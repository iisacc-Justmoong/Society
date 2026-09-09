#include "AccountController.h"
#include "DeviceInfo.h"
#include <iiAcountManager/Quick/AccountViews.h>
#include <QCoreApplication>
#include <QNetworkCookie>
#include <QNetworkCookieJar>

using iisacc::accounts::AccountManager;

static void registerSocietyAccountViews() { iisacc::accounts::registerAccountViews(); }
Q_COREAPP_STARTUP_FUNCTION(registerSocietyAccountViews)

AccountController::AccountController(QObject *parent)
    : AccountController(QUrl(qEnvironmentVariable("SOCIETY_ACCOUNT_URL", "https://iisacc.com")), parent) {}

AccountController::AccountController(const QUrl &serviceUrl, QObject *parent)
    : QObject(parent), m_manager(serviceUrl, &m_network) {
    m_manager.setDeviceInfo(societyAccountDevice(m_manager.deviceInfo()));
    connect(m_manager.account(), &iisacc::accounts::Account::changed, this, &AccountController::changed);
    connect(&m_manager, &AccountManager::loadingChanged, this, &AccountController::changed);
    connect(&m_manager, &AccountManager::errorChanged, this, &AccountController::changed);
    connect(&m_manager, &AccountManager::loginSessionChanged, this, &AccountController::changed);
    connect(&m_manager, &AccountManager::loginChallengeChanged, this, &AccountController::changed);
    connect(&m_manager, &AccountManager::sessionEnding, this, &AccountController::sessionEnding);
}

bool AccountController::login(const QString &email, const QString &password) {
    return !busy() && m_manager.loginWithPassword(email.trimmed(), password);
}
bool AccountController::refresh() { return !busy() && signedIn() && m_manager.refresh(); }
bool AccountController::logout() {
    if (busy()) return false;
    return m_manager.logout();
}
void AccountController::cancelLogin() { if (!signedIn()) m_manager.clear(); }
QByteArray AccountController::relayCredential() const {
    if (!signedIn()) return {};
    QByteArray credential;
    const auto endpoint = m_manager.serviceUrl().resolved(QUrl("/Account/Session"));
    for (const auto &cookie : m_network.cookieJar()->cookiesForUrl(endpoint)) {
        if (cookie.name() != "iisacc_auth_id" && cookie.name() != "iisacc_auth_refresh" && cookie.name() != "iisacc_login_session") continue;
        if (!credential.isEmpty()) credential += "; ";
        credential += cookie.toRawForm(QNetworkCookie::NameAndValueOnly);
    }
    return credential;
}
