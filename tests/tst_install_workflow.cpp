#include "Society/Environment/InstallWorkflow.h"
#include "CTestSupport.h"
#include <QStringList>

int main() {
    QStringList calls;
    bool present = false;
    FirstInstallPorts ports{
        [&] { calls << "present"; return present; },
        [&] { calls << "download"; return QString{}; },
        [&] { calls << "verify"; return QString{}; },
        [&] { calls << "handoff"; return QString{}; }};
    auto result = runFirstInstall(ports);
    CTEST_CHECK(result.handedOff);
    CTEST_COMPARE(calls, (QStringList{"present", "download", "verify", "present", "handoff"}));
    calls.clear(); present = true;
    result = runFirstInstall(ports);
    CTEST_CHECK(!result.handedOff && calls == QStringList{"present"});
    present = false; calls.clear();
    ports.downloadAndCheck = [&] { calls << "download"; return QStringLiteral("hash mismatch"); };
    result = runFirstInstall(ports);
    CTEST_CHECK(!result.handedOff && result.error == "hash mismatch");
    CTEST_COMPARE(calls, (QStringList{"present", "download"}));
    calls.clear(); ports.downloadAndCheck = [&] { calls << "download"; return QString{}; };
    ports.verifyIdentity = [&] { calls << "verify"; return QStringLiteral("untrusted signer"); };
    result = runFirstInstall(ports);
    CTEST_CHECK(!result.handedOff && result.error == "untrusted signer");
    CTEST_COMPARE(calls, (QStringList{"present", "download", "verify"}));
    calls.clear(); ports.verifyIdentity = [&] { calls << "verify"; present = true; return QString{}; };
    result = runFirstInstall(ports);
    CTEST_CHECK(!result.handedOff);
    CTEST_COMPARE(calls, (QStringList{"present", "download", "verify", "present"}));
    calls.clear(); present = false; ports.verifyIdentity = [&] { calls << "verify"; return QString{}; };
    ports.handoff = [&] { calls << "handoff"; return QStringLiteral("launch refused"); };
    result = runFirstInstall(ports);
    CTEST_CHECK(!result.handedOff && result.error == "launch refused");
    return 0;
}
