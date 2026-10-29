#include "AccountController.h"
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QRegularExpression>
#ifndef SOCIETY_HEADLESS
#include <QCoreApplication>
#include <iiAccountManager/Quick/AccountViews.h>
static void registerSocietyAccountViews() { iisacc::accounts::registerAccountViews(); }
Q_COREAPP_STARTUP_FUNCTION(registerSocietyAccountViews)
#endif

AccountController::AccountController(QObject *parent) : AccountSession(parent) { mountLicenses(); }
AccountController::AccountController(const QUrl &serviceUrl, QObject *parent)
    : AccountSession(serviceUrl, parent)
{
    mountLicenses();
}
AccountController::AccountController(const QUrl &serviceUrl, iisacc::accounts::SessionStore *store,
                                     QObject *parent)
    : AccountSession(serviceUrl, store, parent)
{
    mountLicenses();
}
void AccountController::mountLicenses()
{
    manager()->licenseManager()->configure({"congregation", "dreamscapes", "vincent"});
    connect(manager(), &iisacc::accounts::AccountManager::authenticatedChanged, this, [this] {
        if(licenseBroker_.isListening()&&signedIn()) QTimer::singleShot(0, this, [this]{manager()->licenseManager()->ensure();});
    });
    connect(manager()->licenseManager(), &iisacc::accounts::LicenseManager::changed, this,
            &AccountController::licensesChanged);

    connectionsDeadline_.setSingleShot(true);
    connect(&connectionsDeadline_, &QTimer::timeout, this,
            [this]
            {
                clearConnections();
                connectionsCode_ = "request_timeout";
                emit connectionsChanged();
            });
    connect(this, &AccountSession::sessionEnding, this, &AccountController::clearConnections);
    connect(manager(), &iisacc::accounts::AccountManager::authenticatedChanged, this,
            &AccountController::clearConnections);
    connect(manager(), &iisacc::accounts::AccountManager::loginSessionChanged, this,
            &AccountController::clearConnections);
    connect(manager(), &iisacc::accounts::AccountManager::deviceInfoChanged, this,
            &AccountController::clearConnections);
}

AccountController::~AccountController() { clearConnections(); }
void AccountController::clearConnections()
{
    connectionsDeadline_.stop();
    if (auto *reply = connectionsReply_.data())
    {
        connectionsReply_ = nullptr;
        reply->disconnect(this);
        reply->abort();
        reply->deleteLater();
    }
    connectionsBytes_.clear();
    connections_.clear();
    connectionsCode_ = signedIn() ? "not_checked" : "sign_in_required";
    emit connectionsChanged();
}
bool AccountController::refreshConnections()
{
    return requestConnections({{"operation", "list"}}, false);
}
bool AccountController::disconnectApp(const QString &sessionId)
{
    bool allowed = false;
    for (const auto &value : connections_)
    {
        const auto row = value.toMap();
        if (row.value("id") == sessionId && !row.value("current").toBool())
            allowed = true;
    }
    if (!allowed || connectionsBusy())
        return false;
    return requestConnections({{"operation", "revoke"}, {"sessionId", sessionId}}, true);
}
bool AccountController::requestConnections(const QJsonObject &input, bool revoke)
{
    clearConnections();
    if (!signedIn())
        return false;
    const auto binding = manager()->sessionBinding();
    const auto body =
        QJsonDocument(QJsonObject{{"query", "mutation AccountConnections($input: JSON!) { "
                                            "accountConnections(input: $input) }"},
                                  {"variables", QJsonObject{{"input", input}}}})
            .toJson(QJsonDocument::Compact);
    auto *reply = manager()->postAuthenticated(
        manager()->serviceUrl().resolved(QUrl("/Account/GraphQL")), body);
    if (!reply)
    {
        connectionsCode_ = "request_unavailable";
        emit connectionsChanged();
        return false;
    }
    connectionsReply_ = reply;
    reply->setReadBufferSize(65537);
    connectionsCode_ = "checking";
    connect(reply, &QIODevice::readyRead, this,
            [this, reply]
            {
                if (connectionsReply_ != reply)
                    return;
                connectionsBytes_ += reply->readAll();
                if (connectionsBytes_.size() > 65536)
                {
                    clearConnections();
                    connectionsCode_ = "invalid_response";
                    emit connectionsChanged();
                }
            });
    connect(
        reply, &QNetworkReply::finished, this,
        [this, reply, binding, revoke]
        {
            if (connectionsReply_ != reply)
                return;
            connectionsDeadline_.stop();
            connectionsBytes_ += reply->readAll();
            connectionsReply_ = nullptr;
            reply->deleteLater();
            auto bytes = std::move(connectionsBytes_);
            connectionsBytes_.clear();
            if (!manager()->matchesSessionBinding(binding))
            {
                clearConnections();
                return;
            }
            const QPointer<AccountController> guard(this);
            int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            QJsonParseError error;
            const auto root = QJsonDocument::fromJson(bytes, &error).object();
            if (root.value("errors").isArray() && !root.value("errors").toArray().isEmpty())
            {
                const auto ext = root.value("errors")
                                     .toArray()
                                     .first()
                                     .toObject()
                                     .value("extensions")
                                     .toObject();
                status = ext.value("status").toInt(status);
            }
            if (manager()->handleSessionResponse(status, bytes,
                                                 binding.value("sessionId").toString()))
            {
                if (guard)
                    clearConnections();
                return;
            }
            if (!guard)
                return;
            const auto payload =
                root.value("data").toObject().value("accountConnections").toObject();
            const bool valid =
                reply->error() == QNetworkReply::NoError && status == 200 &&
                bytes.size() <= 65536 && error.error == QJsonParseError::NoError &&
                reply->header(QNetworkRequest::ContentTypeHeader).toString().split(';').first() ==
                    "application/json" &&
                !root.contains("errors");
            if (valid && revoke && payload.value("revoked").isDouble())
            {
                refreshConnections();
                return;
            }
            bool scoped =
                valid &&
                payload.value("subject").toString() == binding.value("subject").toString() &&
                payload.value("sessions").isArray();
            QVariantList rows;
            QStringList ids;
            const QRegularExpression idPattern("\\A[a-f0-9]{32}\\z");
            for (const auto &value : payload.value("sessions").toArray())
            {
                const auto row = value.toObject();
                const auto id = row.value("id").toString();
                const auto device = row.value("device").toObject();
                scoped =
                    scoped && idPattern.match(id).hasMatch() && !ids.contains(id) &&
                    row.value("current").isBool() &&
                    (row.value("client") == "app" || row.value("client") == "web") &&
                    QDateTime::fromString(row.value("expiresAt").toString(), Qt::ISODate).toUTC() >
                        QDateTime::currentDateTimeUtc();
                ids << id;
                rows << QVariantMap{{"id", id},
                                    {"current", row.value("current").toBool()},
                                    {"client", row.value("client").toString()},
                                    {"deviceName", device.value("name").toString()},
                                    {"appId", device.value("appId").toString()},
                                    {"expiresAt", row.value("expiresAt").toString()}};
            }
            if (rows.size() > 512)
                scoped = false;
            connections_ = scoped ? rows : QVariantList{};
            connectionsCode_ = scoped ? "verified" : "verification_unavailable";
            emit connectionsChanged();
        });
    connectionsDeadline_.start(10000);
    emit connectionsChanged();
    return true;
}
