#include "App/State/GroupSessionStore.h"
#include "App/State/StateCrypto.h"
#include "MemorySessionStore.h"
#include <QCryptographicHash>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>
#include <optional>

using Store = iisacc::accounts::SessionStore;
static Store::Result complete(std::function<void(Store::Completion)> action) {
    std::optional<Store::Result> result;
    QEventLoop loop;
    QTimer timeout; timeout.setSingleShot(true); timeout.start(3000);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    action([&](auto value) { result = value; loop.quit(); });
    if (!result) loop.exec();
    return result.value_or(Store::Result{Store::Error::Unavailable, {}});
}
static QString recordPath(const QString &root, const QString &key) {
    return QDir(root).filePath(QString::fromLatin1(QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha256).toHex()) + ".state");
}
static QByteArray bytes(const QString &path) { QFile f(path); return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray(); }
static bool replace(const QString &path, const QByteArray &data) { QFile f(path); return f.open(QIODevice::WriteOnly) && f.write(data) == data.size(); }

class GroupStateTests : public QObject {
    Q_OBJECT
private slots:
    void boundedLargeAccountSnapshotSurvivesAndOversizeDoesNotReplaceIt() {
        QTemporaryDir dir(QString(SOCIETY_TEST_DIRECTORY) + "/group-state-XXXXXX"); QVERIFY(dir.isValid());
        MemorySessionStore keys, legacy;
        const QByteArray fullRecord(MaximumSocietyStateBytes, 'x');
        GroupSessionStore store(dir.path(), &keys, &legacy);
        QCOMPARE(complete([&](auto done) { store.write("account-snapshot", fullRecord, done); }).error, Store::Error::None);
        QCOMPARE(complete([&](auto done) { store.write("account-snapshot", fullRecord + 'x', done); }).error, Store::Error::Unavailable);
        GroupSessionStore reopened(dir.path(), &keys, &legacy);
        QCOMPARE(complete([&](auto done) { reopened.read("account-snapshot", done); }).data, fullRecord);
    }
    void ciphertextSurvivesRestartAndSeparatesRecords() {
        QTemporaryDir dir(QString(SOCIETY_TEST_DIRECTORY) + "/group-state-XXXXXX"); QVERIFY(dir.isValid());
        MemorySessionStore keys, legacy;
        const QByteArray login = "fixture-refresh-cookie-and-session", pairing = "fixture-pairing-proof-and-peers";
        {
            GroupSessionStore store(dir.path(), &keys, &legacy);
            QCOMPARE(complete([&](auto done) { store.write("login", login, done); }).error, Store::Error::None);
            QCOMPARE(complete([&](auto done) { store.write("pairing", pairing, done); }).error, Store::Error::None);
        }
        const auto ciphertext = bytes(recordPath(dir.path(), "login"));
        QVERIFY(ciphertext.startsWith("SGS1")); QVERIFY(!ciphertext.contains(login));
        QVERIFY(!ciphertext.contains(keys.values.value("group-state-aes256-v1")));
        GroupSessionStore restored(dir.path(), &keys, &legacy);
        QCOMPARE(complete([&](auto done) { restored.read("login", done); }).data, login);
        QCOMPARE(complete([&](auto done) { restored.read("pairing", done); }).data, pairing);
        QCOMPARE(QFile::permissions(recordPath(dir.path(), "login")) & (QFileDevice::ReadGroup | QFileDevice::ReadOther | QFileDevice::WriteOther), QFileDevice::Permissions{});
    }
    void migratesOnlyAfterSuccessfulEncryptedWrite() {
        QTemporaryDir dir(QString(SOCIETY_TEST_DIRECTORY) + "/group-state-XXXXXX"); QVERIFY(dir.isValid());
        MemorySessionStore keys, legacy; legacy.values["login"] = "legacy-fixture";
        keys.unavailable = true;
        GroupSessionStore store(dir.path(), &keys, &legacy);
        QCOMPARE(complete([&](auto done) { store.read("login", done); }).error, Store::Error::Unavailable);
        QVERIFY(legacy.values.contains("login")); QVERIFY(!QFile::exists(recordPath(dir.path(), "login")));
        keys.unavailable = false;
        QCOMPARE(complete([&](auto done) { store.read("login", done); }).data, QByteArray("legacy-fixture"));
        QVERIFY(!legacy.values.contains("login"));
        QVERIFY(bytes(recordPath(dir.path(), "login")).startsWith("SGS1"));
    }
    void damagedOrSubstitutedCiphertextNeverFallsBack() {
        QTemporaryDir dir(QString(SOCIETY_TEST_DIRECTORY) + "/group-state-XXXXXX"); QVERIFY(dir.isValid());
        MemorySessionStore keys, legacy;
        GroupSessionStore store(dir.path(), &keys, &legacy);
        QCOMPARE(complete([&](auto done) { store.write("login", "fixture", done); }).error, Store::Error::None);
        auto cipher = bytes(recordPath(dir.path(), "login"));
        legacy.values["login"] = "stale-fixture";
        QVERIFY(replace(recordPath(dir.path(), "different-origin"), cipher));
        QCOMPARE(complete([&](auto done) { store.read("different-origin", done); }).error, Store::Error::Unavailable);
        cipher[cipher.size() - 1] ^= 1; QVERIFY(replace(recordPath(dir.path(), "login"), cipher));
        QCOMPARE(complete([&](auto done) { store.read("login", done); }).error, Store::Error::Unavailable);
        QVERIFY(legacy.values.contains("login"));
    }
    void logoutInvalidatesPendingWriteFromAnotherStore() {
        QTemporaryDir dir(QString(SOCIETY_TEST_DIRECTORY) + "/group-state-XXXXXX"); QVERIFY(dir.isValid());
        MemorySessionStore keys, legacy; legacy.values["login"] = "stale"; legacy.failRemove = true;
        GroupSessionStore first(dir.path(), &keys, &legacy), second(dir.path(), &keys, &legacy);
        keys.holdRead = true; bool written = false;
        first.write("login", "pending-fixture", [&](auto result) { written = true; QCOMPARE(result.error, Store::Error::Missing); });
        QTRY_VERIFY(bool(keys.releaseRead));
        // Revocation is durable immediately, even while another process owns the lock.
        second.remove("login", [](auto) {});
        QVERIFY(QFile::exists(recordPath(dir.path(), "login") + ".revocation"));
        keys.releaseRead(); QTRY_VERIFY(written);
        GroupSessionStore restarted(dir.path(), &keys, &legacy);
        QCOMPARE(complete([&](auto done) { restarted.read("login", done); }).error, Store::Error::Missing);
        QCOMPARE(complete([&](auto done) { restarted.write("login", "new-sign-in", done); }).error, Store::Error::None);
        QCOMPARE(complete([&](auto done) { restarted.read("login", done); }).data, QByteArray("new-sign-in"));
    }
    void refusesRedirectedDirectoriesAndFiles() {
        QTemporaryDir dir(QString(SOCIETY_TEST_DIRECTORY) + "/group-state-XXXXXX"); QVERIFY(dir.isValid());
        MemorySessionStore keys, legacy;
        QVERIFY(QDir(dir.path()).mkdir("real"));
        QVERIFY(QFile::link(dir.path() + "/real", dir.path() + "/alias"));
        GroupSessionStore redirected(dir.path() + "/alias/state", &keys, &legacy);
        QCOMPARE(complete([&](auto done) { redirected.write("login", "fixture", done); }).error, Store::Error::Unavailable);
        GroupSessionStore store(dir.path() + "/real", &keys, &legacy);
        const auto outside = dir.path() + "/outside"; QVERIFY(replace(outside, "untouched"));
        QVERIFY(QFile::link(outside, recordPath(store.directory(), "login")));
        QCOMPARE(complete([&](auto done) { store.write("login", "fixture", done); }).error, Store::Error::Unavailable);
        QCOMPARE(bytes(outside), QByteArray("untouched"));
    }
    void anOlderProcessCannotSaveAgainAfterAnotherProcessLogsOut() {
        QTemporaryDir dir(QString(SOCIETY_TEST_DIRECTORY) + "/group-state-XXXXXX"); QVERIFY(dir.isValid());
        MemorySessionStore keys, legacy;
        GroupSessionStore older(dir.path(), &keys, &legacy), logout(dir.path(), &keys, &legacy);
        QCOMPARE(complete([&](auto done) { older.write("login", "old-session", done); }).error, Store::Error::None);
        QCOMPARE(complete([&](auto done) { logout.remove("login", done); }).error, Store::Error::None);
        QCOMPARE(complete([&](auto done) { older.write("login", "late-refresh", done); }).error, Store::Error::Missing);
        GroupSessionStore restarted(dir.path(), &keys, &legacy);
        QCOMPARE(complete([&](auto done) { restarted.read("login", done); }).error, Store::Error::Missing);
    }
    void cryptoUsesFreshNoncesAndAuthenticatesContext() {
        const QByteArray key(32, 'k'), plain("fixture"), aad("account-and-device");
        const auto first = sealSocietyState(plain, key, aad), second = sealSocietyState(plain, key, aad);
        QVERIFY(!first.isEmpty()); QVERIFY(first != second);
        QCOMPARE(openSocietyState(first, key, aad), plain);
        QVERIFY(openSocietyState(first, QByteArray(32, 'x'), aad).isEmpty());
        QVERIFY(openSocietyState(first, key, "wrong-account").isEmpty());
    }
};
QTEST_GUILESS_MAIN(GroupStateTests)
#include "tst_groupstate.moc"
