#include "native_link.h"
#include "Society/State/GroupSessionStore.h"
#include "Society/State/StateCrypto.h"
#include "MemorySessionStore.h"
#include <QCryptographicHash>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QTemporaryDir>
#include "CTestSupport.h"
#include "QtEventWait.h"
#include <QCoreApplication>
#include <QEvent>
#include <QPointer>
#include <QTimer>
#include <memory>
#include <optional>
#ifdef Q_OS_WIN
#include <windows.h>
#include <aclapi.h>
#endif

using Store = iisacc::accounts::SessionStore;
static Store::Result complete(std::function<void(Store::Completion)> action) {
    struct State { std::optional<Store::Result> result; QPointer<QEventLoop> loop; };
    const auto state = std::make_shared<State>();
    QEventLoop loop; state->loop = &loop;
    QTimer timeout; timeout.setSingleShot(true); timeout.start(3000);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    action([state](auto value) { state->result = value; if (state->loop) state->loop->quit(); });
    if (!state->result) loop.exec();
    return state->result.value_or(Store::Result{Store::Error::Unavailable, {}});
}
static QString recordPath(const QString &root, const QString &key) {
    return QDir(root).filePath(QString::fromLatin1(QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha256).toHex()) + ".state");
}
static QByteArray bytes(const QString &path) { QFile f(path); return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray(); }
static bool replace(const QString &path, const QByteArray &data) { QFile f(path); return f.open(QIODevice::WriteOnly) && f.write(data) == data.size(); }

class GroupStateTests {
public:
    void previousEncryptedRecordsAreCopiedWithoutChangingTheSource() {
        QTemporaryDir dir(QString(SOCIETY_TEST_DIRECTORY) + "/state-migration-XXXXXX"); CTEST_CHECK(dir.isValid());
        MemorySessionStore keys, legacy;
        const auto oldRoot = dir.filePath("previous");
        // Prior versions stored only ciphertext. Produce it privately, then copy
        // the raw legacy fixture without scheduling current asset enrichment.
        const auto seedRoot = dir.filePath(".encrypted-seed");
        GroupSessionStore original(seedRoot, &keys, &legacy);
        CTEST_COMPARE(complete([&](auto done) { original.write("login", "preserved-login", done); }).error, Store::Error::None);
        CTEST_CHECK(QDir().mkpath(oldRoot));
        CTEST_CHECK(QFile::copy(recordPath(seedRoot, "login"), recordPath(oldRoot, "login")));
        const auto oldCipher = bytes(recordPath(oldRoot, "login"));
        const auto oldEntries = QDir(oldRoot).entryList(QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot);
        CTEST_COMPARE(oldEntries.size(), 1);
        const auto destination = dir.filePath("selected.societycontainer/.society-runtime/state");
        GroupSessionStore migrated(destination, &keys, nullptr, nullptr, oldRoot);
        CTEST_COMPARE(complete([&](auto done) { migrated.read("login", done); }).data, QByteArray("preserved-login"));
        CTEST_COMPARE(bytes(recordPath(destination, "login")), oldCipher);
        CTEST_COMPARE(bytes(recordPath(oldRoot, "login")), oldCipher);
        CTEST_COMPARE(QDir(oldRoot).entryList(QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot), oldEntries);
        CTEST_COMPARE(complete([&](auto done) { migrated.remove("login", done); }).error, Store::Error::None);
        GroupSessionStore reopened(destination, &keys, nullptr, nullptr, oldRoot);
        CTEST_COMPARE(complete([&](auto done) { reopened.read("login", done); }).error, Store::Error::Missing);
        CTEST_COMPARE(bytes(recordPath(oldRoot, "login")), oldCipher);
    }
    void boundedLargeAccountSnapshotSurvivesAndOversizeDoesNotReplaceIt() {
        QTemporaryDir dir(QString(SOCIETY_TEST_DIRECTORY) + "/group-state-XXXXXX"); CTEST_CHECK(dir.isValid());
        MemorySessionStore keys, legacy;
        const QByteArray fullRecord(MaximumSocietyStateBytes, 'x');
        GroupSessionStore store(dir.path(), &keys, &legacy);
        CTEST_COMPARE(complete([&](auto done) { store.write("account-snapshot", fullRecord, done); }).error, Store::Error::None);
        CTEST_COMPARE(complete([&](auto done) { store.write("account-snapshot", fullRecord + 'x', done); }).error, Store::Error::Unavailable);
        GroupSessionStore reopened(dir.path(), &keys, &legacy);
        CTEST_COMPARE(complete([&](auto done) { reopened.read("account-snapshot", done); }).data, fullRecord);
    }
    void ciphertextSurvivesRestartAndSeparatesRecords() {
        QTemporaryDir dir(QString(SOCIETY_TEST_DIRECTORY) + "/group-state-XXXXXX"); CTEST_CHECK(dir.isValid());
        MemorySessionStore keys, legacy;
        const QByteArray login = "fixture-refresh-cookie-and-session", pairing = "fixture-pairing-proof-and-peers";
        {
            GroupSessionStore store(dir.path(), &keys, &legacy);
            CTEST_COMPARE(complete([&](auto done) { store.write("login", login, done); }).error, Store::Error::None);
            CTEST_COMPARE(complete([&](auto done) { store.write("pairing", pairing, done); }).error, Store::Error::None);
        }
        const auto ciphertext = bytes(recordPath(dir.path(), "login"));
        CTEST_CHECK(ciphertext.startsWith("SGS1")); CTEST_CHECK(!ciphertext.contains(login));
        CTEST_CHECK(!ciphertext.contains(keys.values.value("group-state-aes256-v1")));
        GroupSessionStore restored(dir.path(), &keys, &legacy);
        CTEST_COMPARE(complete([&](auto done) { restored.read("login", done); }).data, login);
        CTEST_COMPARE(complete([&](auto done) { restored.read("pairing", done); }).data, pairing);
#ifdef Q_OS_WIN
        const auto path = recordPath(dir.path(), "login");
        PSECURITY_DESCRIPTOR descriptor = nullptr; PACL acl = nullptr; PSID owner = nullptr;
        CTEST_COMPARE(GetNamedSecurityInfoW(const_cast<LPWSTR>(reinterpret_cast<LPCWSTR>(path.utf16())),
            SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
            &owner, nullptr, &acl, nullptr, &descriptor), DWORD(ERROR_SUCCESS));
        CTEST_CHECK(acl != nullptr); CTEST_COMPARE(acl->AceCount, WORD(1));
        void *entry = nullptr; CTEST_CHECK(GetAce(acl, 0, &entry));
        const auto allowed = static_cast<ACCESS_ALLOWED_ACE *>(entry);
        CTEST_COMPARE(allowed->Header.AceType, BYTE(ACCESS_ALLOWED_ACE_TYPE));
        CTEST_CHECK(EqualSid(owner, &allowed->SidStart));
        CTEST_CHECK((allowed->Mask & FILE_ALL_ACCESS) == FILE_ALL_ACCESS);
        SECURITY_DESCRIPTOR_CONTROL control{}; DWORD revision{};
        CTEST_CHECK(GetSecurityDescriptorControl(descriptor, &control, &revision));
        CTEST_CHECK(control & SE_DACL_PROTECTED); LocalFree(descriptor);
#else
        CTEST_COMPARE(QFile::permissions(recordPath(dir.path(), "login")) & (QFileDevice::ReadGroup | QFileDevice::ReadOther | QFileDevice::WriteOther), QFileDevice::Permissions{});
#endif
    }
    void migratesOnlyAfterSuccessfulEncryptedWrite() {
        QTemporaryDir dir(QString(SOCIETY_TEST_DIRECTORY) + "/group-state-XXXXXX"); CTEST_CHECK(dir.isValid());
        MemorySessionStore keys, legacy; legacy.values["login"] = "legacy-fixture";
        keys.unavailable = true;
        GroupSessionStore store(dir.path(), &keys, &legacy);
        CTEST_COMPARE(complete([&](auto done) { store.read("login", done); }).error, Store::Error::Unavailable);
        CTEST_CHECK(legacy.values.contains("login")); CTEST_CHECK(!QFile::exists(recordPath(dir.path(), "login")));
        keys.unavailable = false;
        CTEST_COMPARE(complete([&](auto done) { store.read("login", done); }).data, QByteArray("legacy-fixture"));
        CTEST_CHECK(!legacy.values.contains("login"));
        CTEST_CHECK(bytes(recordPath(dir.path(), "login")).startsWith("SGS1"));
    }
    void damagedOrSubstitutedCiphertextNeverFallsBack() {
        QTemporaryDir dir(QString(SOCIETY_TEST_DIRECTORY) + "/group-state-XXXXXX"); CTEST_CHECK(dir.isValid());
        MemorySessionStore keys, legacy;
        GroupSessionStore store(dir.path(), &keys, &legacy);
        CTEST_COMPARE(complete([&](auto done) { store.write("login", "fixture", done); }).error, Store::Error::None);
        auto cipher = bytes(recordPath(dir.path(), "login"));
        legacy.values["login"] = "stale-fixture";
        CTEST_CHECK(replace(recordPath(dir.path(), "different-origin"), cipher));
        CTEST_COMPARE(complete([&](auto done) { store.read("different-origin", done); }).error, Store::Error::Unavailable);
        cipher[cipher.size() - 1] ^= 1; CTEST_CHECK(replace(recordPath(dir.path(), "login"), cipher));
        CTEST_COMPARE(complete([&](auto done) { store.read("login", done); }).error, Store::Error::Unavailable);
        CTEST_CHECK(legacy.values.contains("login"));
    }
    void logoutInvalidatesPendingWriteFromAnotherStore() {
        QTemporaryDir dir(QString(SOCIETY_TEST_DIRECTORY) + "/group-state-XXXXXX"); CTEST_CHECK(dir.isValid());
        MemorySessionStore keys, legacy; legacy.values["login"] = "stale"; legacy.failRemove = true;
        GroupSessionStore first(dir.path(), &keys, &legacy), second(dir.path(), &keys, &legacy);
        keys.holdRead = true; auto writtenResult = std::make_shared<std::optional<Store::Error>>();
        first.write("login", "pending-fixture", [writtenResult](auto result) { *writtenResult = result.error; });
        CTEST_WAIT_DEFAULT(bool(keys.releaseRead));
        // Revocation is durable immediately, even while another process owns the lock.
        second.remove("login", [](auto) {});
        CTEST_CHECK(QFile::exists(recordPath(dir.path(), "login") + ".revocation"));
        keys.releaseRead(); CTEST_WAIT_DEFAULT(writtenResult->has_value());
        CTEST_COMPARE(writtenResult->value(), Store::Error::Missing);
        GroupSessionStore restarted(dir.path(), &keys, &legacy);
        CTEST_COMPARE(complete([&](auto done) { restarted.read("login", done); }).error, Store::Error::Missing);
        CTEST_COMPARE(complete([&](auto done) { restarted.write("login", "new-sign-in", done); }).error, Store::Error::None);
        CTEST_COMPARE(complete([&](auto done) { restarted.read("login", done); }).data, QByteArray("new-sign-in"));
    }
    void refusesRedirectedDirectoriesAndFiles() {
        QTemporaryDir dir(QString(SOCIETY_TEST_DIRECTORY) + "/group-state-XXXXXX"); CTEST_CHECK(dir.isValid());
        MemorySessionStore keys, legacy;
        CTEST_CHECK(QDir(dir.path()).mkdir("real"));
        CTEST_CHECK(createNativeTestLink(dir.path() + "/real", dir.path() + "/alias"));
        GroupSessionStore redirected(dir.path() + "/alias/state", &keys, &legacy);
        CTEST_COMPARE(complete([&](auto done) { redirected.write("login", "fixture", done); }).error, Store::Error::Unavailable);
        GroupSessionStore store(dir.path() + "/real", &keys, &legacy);
        const auto outside = dir.path() + "/outside"; CTEST_CHECK(replace(outside, "untouched"));
        CTEST_CHECK(createNativeTestLink(outside, recordPath(store.directory(), "login")));
        CTEST_COMPARE(complete([&](auto done) { store.write("login", "fixture", done); }).error, Store::Error::Unavailable);
        CTEST_COMPARE(bytes(outside), QByteArray("untouched"));
    }
    void anOlderProcessCannotSaveAgainAfterAnotherProcessLogsOut() {
        QTemporaryDir dir(QString(SOCIETY_TEST_DIRECTORY) + "/group-state-XXXXXX"); CTEST_CHECK(dir.isValid());
        MemorySessionStore keys, legacy;
        GroupSessionStore older(dir.path(), &keys, &legacy), logout(dir.path(), &keys, &legacy);
        CTEST_COMPARE(complete([&](auto done) { older.write("login", "old-session", done); }).error, Store::Error::None);
        CTEST_COMPARE(complete([&](auto done) { logout.remove("login", done); }).error, Store::Error::None);
        CTEST_COMPARE(complete([&](auto done) { older.write("login", "late-refresh", done); }).error, Store::Error::Missing);
        GroupSessionStore restarted(dir.path(), &keys, &legacy);
        CTEST_COMPARE(complete([&](auto done) { restarted.read("login", done); }).error, Store::Error::Missing);
    }
    void cryptoUsesFreshNoncesAndAuthenticatesContext() {
        const QByteArray key(32, 'k'), plain("fixture"), aad("account-and-device");
        const auto first = sealSocietyState(plain, key, aad), second = sealSocietyState(plain, key, aad);
        CTEST_CHECK(!first.isEmpty()); CTEST_CHECK(first != second);
        CTEST_COMPARE(openSocietyState(first, key, aad), plain);
        CTEST_CHECK(openSocietyState(first, QByteArray(32, 'x'), aad).isEmpty());
        CTEST_CHECK(openSocietyState(first, key, "wrong-account").isEmpty());
    }
};
int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    society_ctest::Runner runner(argc == 2 ? argv[1] : "");
    GroupStateTests tests;
    struct FlushDeferredDeletes {
        ~FlushDeferredDeletes() { QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); }
    };
    runner.run("previousEncryptedRecordsAreCopiedWithoutChangingTheSource", [&] { FlushDeferredDeletes cleanup; tests.previousEncryptedRecordsAreCopiedWithoutChangingTheSource(); });
    runner.run("boundedLargeAccountSnapshotSurvivesAndOversizeDoesNotReplaceIt", [&] { FlushDeferredDeletes cleanup; tests.boundedLargeAccountSnapshotSurvivesAndOversizeDoesNotReplaceIt(); });
    runner.run("ciphertextSurvivesRestartAndSeparatesRecords", [&] { FlushDeferredDeletes cleanup; tests.ciphertextSurvivesRestartAndSeparatesRecords(); });
    runner.run("migratesOnlyAfterSuccessfulEncryptedWrite", [&] { FlushDeferredDeletes cleanup; tests.migratesOnlyAfterSuccessfulEncryptedWrite(); });
    runner.run("damagedOrSubstitutedCiphertextNeverFallsBack", [&] { FlushDeferredDeletes cleanup; tests.damagedOrSubstitutedCiphertextNeverFallsBack(); });
    runner.run("logoutInvalidatesPendingWriteFromAnotherStore", [&] { FlushDeferredDeletes cleanup; tests.logoutInvalidatesPendingWriteFromAnotherStore(); });
    runner.run("refusesRedirectedDirectoriesAndFiles", [&] { FlushDeferredDeletes cleanup; tests.refusesRedirectedDirectoriesAndFiles(); });
    runner.run("anOlderProcessCannotSaveAgainAfterAnotherProcessLogsOut", [&] { FlushDeferredDeletes cleanup; tests.anOlderProcessCannotSaveAgainAfterAnotherProcessLogsOut(); });
    runner.run("cryptoUsesFreshNoncesAndAuthenticatesContext", [&] { FlushDeferredDeletes cleanup; tests.cryptoUsesFreshNoncesAndAuthenticatesContext(); });
    return runner.result();
}
