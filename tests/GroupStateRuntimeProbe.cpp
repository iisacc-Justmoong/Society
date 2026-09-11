// Opt-in device validation. Uses one synthetic record and never exports credentials.
#include "App/State/GroupSessionStore.h"
#include "App/Account/AccountController.h"
#include "App/Network/PairingCredentials.h"
#include "App/Network/NetworkDriveController.h"
#include "App/Drive/DriveController.h"
#include <QCryptographicHash>
#include <QDir>
#include <QGuiApplication>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimer>
#include <QRegularExpression>

void societyGroupStateRuntimeProbe(QObject *root) {
    const auto stage = qEnvironmentVariable("SOCIETY_GROUP_STATE_PROBE_STAGE");
    if (stage == "mirror-inspect" || stage == "mirror-write" || stage == "mirror-remove") {
        auto *timer = new QTimer(root); timer->setInterval(3000);
        QObject::connect(timer, &QTimer::timeout, root, [root, timer, stage, runs = 0, operated = false]() mutable {
            auto *drive = root->findChild<DriveController *>("driveController");
            auto *network = root->findChild<NetworkDriveController *>("networkDriveController");
            auto *account = root->findChild<AccountController *>("societyAccount");
            if (!drive || !network || !account) { timer->stop(); return; }
            if (runs == 0 && qEnvironmentVariableIntValue("SOCIETY_MIRROR_PROBE_RESUME") == 1)
                network->resumeAutomaticPairing();
            int verified = 0;
            for (const auto &peer : network->nearbyDevices()) if (peer.toMap().value("verified").toBool()) ++verified;
            const auto name = qEnvironmentVariable("SOCIETY_MIRROR_PROBE_NAME");
            const bool validName = QRegularExpression("\\ASociety-mirror-check-[a-z0-9-]{1,64}\\.txt\\z").match(name).hasMatch();
            const QByteArray expected = "Society mirror verification: " + name.toUtf8() + '\n';
            QString error; QByteArray bytes;
            if (validName && drive->contentsAvailable()) {
                QFile file(QDir(drive->rootPath()).filePath("Files/" + name));
                if (!operated && stage == "mirror-write") {
                    if (file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
                        operated = file.write(expected) == expected.size(); file.close();
                    } else error = file.errorString();
                }
                if (file.open(QIODevice::ReadOnly)) { bytes = file.read(1024 * 1024); file.close(); }
                if (!operated && stage == "mirror-remove" && bytes == expected) {
                    operated = file.remove(); if (operated) bytes.clear(); else error = file.errorString();
                }
            }
            auto output = qEnvironmentVariable("SOCIETY_GROUP_STATE_PROBE_REPORT");
            if (!QDir::isAbsolutePath(output)) output = QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)).filePath("mirror-probe.json");
            QSaveFile report(output);
            const QJsonObject metadata{{"stage", stage}, {"metadataOnly", true}, {"sample", ++runs}, {"timestampMs", QDateTime::currentMSecsSinceEpoch()},
                {"signedIn", account->signedIn()}, {"container", drive->identifier()}, {"root", drive->rootPath()},
                {"ready", drive->contentsAvailable()}, {"connected", network->connected()}, {"hosting", network->hosting()},
                {"nearbyCount", network->nearbyDevices().size()}, {"synchronizing", network->synchronizing()},
                {"verifiedNearbyCount", verified}, {"automaticEnabled", network->automaticPairingEnabled()},
                {"automaticStatus", network->automaticPairingStatus()}, {"authenticatedDiscovery", network->discovery()->authenticated()},
                {"automaticHost", network->discovery()->automaticHostAvailable()}, {"peerPhase", network->localPeer()->phase()},
                {"connectionStatus", network->status()}, {"screenActive", QGuiApplication::applicationState() == Qt::ApplicationActive},
                {"status", network->synchronizationStatus()}, {"probeOperated", operated}, {"probeError", error},
                {"probePresent", !bytes.isEmpty()}, {"probeMatches", validName && bytes == expected},
                {"probeHash", QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex())}};
            if (report.open(QIODevice::WriteOnly)) { report.write(QJsonDocument(metadata).toJson()); report.commit(); }
            if (runs >= 2000) timer->stop();
        });
        timer->start(); return;
    }
    if (stage != "write" && stage != "read" && stage != "inspect-cache") return;
    QTimer::singleShot(5000, root, [root, stage] {
        auto *store = GroupSessionStore::create(root);
        if (stage == "inspect-cache") {
            auto *account = root->findChild<AccountController *>("societyAccount");
            if (!account) return;
            const auto origin = account->manager()->serviceUrl().adjusted(QUrl::RemovePath | QUrl::RemoveQuery | QUrl::RemoveFragment | QUrl::RemoveUserInfo);
            const auto device = account->manager()->deviceInfo().value("id").toString();
            const auto cacheKey = "pairing-v1-" + QString::fromLatin1(QCryptographicHash::hash(origin.toEncoded() + '\n' + device.toUtf8(), QCryptographicHash::Sha256).toHex());
            store->read(cacheKey, [account = QPointer<AccountController>(account), origin, device, store](auto result) {
                if (!account) return;
                const auto record = QJsonDocument::fromJson(result.data).object();
                const auto proof = record.value("credentials").toObject();
                auto binding = record.value("binding").toObject().toVariantMap();
                if (record.value("schemaVersion").toInt() < 3) {
                    for (const auto* field : {"origin", "subject", "userId", "deviceId"}) binding.insert(field, record.value(field).toVariant());
                    binding.insert("sessionId", proof.value("sessionId").toVariant());
                }
                const auto output = qEnvironmentVariable("SOCIETY_GROUP_STATE_PROBE_REPORT");
                if (!QDir::isAbsolutePath(output)) return;
                const QJsonObject metadata{{"metadataOnly", true}, {"storageResult", int(result.error)}, {"storageError", store->errorString()},
                    {"schema", record.value("schemaVersion")}, {"credentialVersion", proof.value("version")},
                    {"seeds", proof.value("seeds").toObject().size()}, {"validGrant", SocietyPairingCredentials::valid(proof)},
                    {"bindingMatches", account->manager()->matchesSessionBinding(binding)},
                    {"credentialAccepted", account->manager()->acceptsSessionCredential(proof.toVariantMap())},
                    {"legacySnapshotPresent", record.contains("snapshot")},
                    {"requestBlocked", record.value("requestBlocked")}, {"signedIn", account->signedIn()},
                    {"accountError", int(account->manager()->error())}};
                QSaveFile file(output);
                if (file.open(QIODevice::WriteOnly)) { file.write(QJsonDocument(metadata).toJson()); file.commit(); }
            });
            return;
        }
        const QString key = "runtime-probe-v1";
        const QByteArray fixture = "Society synthetic group-container restart check";
        const auto report = [stage, store](bool ok) {
            auto output = qEnvironmentVariable("SOCIETY_GROUP_STATE_PROBE_REPORT");
            if (!QDir::isAbsolutePath(output)) output = QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)).filePath("group-state-probe.json");
            const auto directory = QFileInfo(output).absolutePath();
            QDir().mkpath(directory);
            QSaveFile file(output);
            const auto bytes = QJsonDocument(QJsonObject{{"stage", stage}, {"ok", ok},
                {"groupDirectory", store->directory()}, {"storageError", store->errorString()}, {"syntheticOnly", true}}).toJson();
            if (file.open(QIODevice::WriteOnly)) { file.write(bytes); file.commit(); }
            qInfo("Society group-state probe: stage=%s ok=%d", qPrintable(stage), ok);
        };
        if (stage == "write") store->write(key, fixture, [report](auto result) { report(result.error == GroupSessionStore::Error::None); });
        else store->read(key, [store, key, fixture, report](auto result) {
            const bool ok = result.error == GroupSessionStore::Error::None && result.data == fixture;
            store->remove(key, [report, ok](auto removed) { report(ok && removed.error == GroupSessionStore::Error::None); });
        });
    });
}
