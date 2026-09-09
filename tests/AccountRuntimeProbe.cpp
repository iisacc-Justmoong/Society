// Opt-in, credential-free native APK/device verification. Disabled in normal builds.
#include "App/Account/AccountController.h"
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QQuickWindow>
#include <QSslConfiguration>
#include <QSslSocket>
#include <QStandardPaths>
#include <QTimer>

void societyAccountRuntimeProbe(QObject *root)
{
    auto *account = root->findChild<AccountController *>(QStringLiteral("societyAccount"));
    if (!account) { qWarning("Society account probe: missing controller"); return; }
    auto *network = new QNetworkAccessManager(root);
    QNetworkRequest request(QUrl("https://iisacc.com/Account/Session/App"));
    request.setTransferTimeout(20000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    auto *reply = network->get(request);
    QObject::connect(reply, &QNetworkReply::finished, root, [account, reply] {
        const auto device = account->manager()->deviceInfo();
        const QJsonObject report{{"supportsSsl", QSslSocket::supportsSsl()},
            {"sslVersion", QSslSocket::sslLibraryVersionString()},
            {"httpStatus", reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()},
            {"certificatePresent", !reply->sslConfiguration().peerCertificate().isNull()},
            {"deviceType", QJsonValue::fromVariant(device.value("type"))},
            {"appId", QJsonValue::fromVariant(device.value("appId"))}};
        qInfo().noquote() << "Society account probe:" << QJsonDocument(report).toJson(QJsonDocument::Compact);
        reply->deleteLater();
    });
    QTimer::singleShot(1500, root, [root] {
        QMetaObject::invokeMethod(root, "openAccount");
        QTimer::singleShot(1500, root, [root] {
            auto *window = qobject_cast<QQuickWindow *>(root);
            const QString directory = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
            QDir().mkpath(directory);
            if (window && window->grabWindow().save(directory + "/account-runtime.png"))
                qInfo().noquote() << "Society account probe screenshot:" << directory + "/account-runtime.png";
        });
    });
}
