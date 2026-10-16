#pragma once
#include "AccountServer.h"
#include "MemorySessionStore.h"
#include "App/Account/AccountController.h"
#include <SocietyDrive.h>
#include <QElapsedTimer>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

inline bool registerFixtureContainer(AccountServer &authority, const QString &root, const QString &hostId)
{
    const auto drive = iiSocietyContainer::SocietyDrive::open(root);
    if (!drive) return false;
    authority.profile.insert("societyContainerDrive", QJsonObject{{"hostDeviceId", hostId},
        {"containerId", drive->identifier()}, {"revision", QUuid::createUuid().toString(QUuid::WithoutBraces)},
        {"imagePath", root}});
    return true;
}

// Exercise the real account response and host-registration contract while
// keeping synthetic credentials in memory and all package state in build/.
struct AuthenticatedNetworkFixture
{
    QTemporaryDir container{QString(QT_TESTCASE_BUILDDIR) + "/network-account-XXXXXX.societycontainer"};
    AccountServer authority;
    MemorySessionStore hostStore, clientStore;
    std::unique_ptr<AccountController> hostAccount, clientAccount;

    bool start(QString root = {}, bool pairing = false)
    {
        if (root.isEmpty()) {
            root = container.path();
            if (!iiSocietyContainer::SocietyDrive::create(root)) return false;
        }
        if (!authority.server.listen(QHostAddress::LocalHost)
            || !registerFixtureContainer(authority, root, QString(64, 'a'))) return false;
        hostAccount = std::make_unique<AccountController>(authority.url(), &hostStore, nullptr);
        clientAccount = std::make_unique<AccountController>(authority.url(), &clientStore, nullptr);
        char identity = 'a';
        for (auto *account : {hostAccount.get(), clientAccount.get()}) {
            QSignalSpy restored(account, &AccountController::pairingStateRestored);
            auto device = account->manager()->deviceInfo(); device["id"] = QString(64, identity++);
            if (!account->manager()->setDeviceInfo(device)
                || !account->login("builder@example.com", "FixtureOnly1!")) return false;
            QElapsedTimer elapsed; elapsed.start();
            while ((!account->signedIn() || account->busy() || restored.isEmpty())
                && elapsed.elapsed() < 5000) QTest::qWait(10);
            if (!account->signedIn() || account->busy() || restored.isEmpty()) return false;
            account->setAutomaticPairingEnabled(false);
            if (pairing) {
                account->requestPairingCredentials(); elapsed.restart();
                while (account->pairingCredentials().isEmpty() && elapsed.elapsed() < 5000) QTest::qWait(10);
                if (account->pairingCredentials().isEmpty()) return false;
            }
        }
        return true;
    }
    QString principal() const { return authority.profile.value("sub").toString(); }
};
