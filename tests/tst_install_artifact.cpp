#include "Society/Environment/InstallArtifact.h"
#include "CTestSupport.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QCryptographicHash>

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    const QByteArray payload("a bounded package fixture");
    const auto hash = QCryptographicHash::hash(payload, QCryptographicHash::Sha256).toHex();
    QBuffer bytes; bytes.setData(payload); CTEST_CHECK(bytes.open(QIODevice::ReadOnly));
    CTEST_CHECK(verifyInstallArtifact(bytes, payload.size(), hash));
    CTEST_CHECK(bytes.seek(0));
    CTEST_CHECK(!verifyInstallArtifact(bytes, payload.size() - 1, hash));
    CTEST_CHECK(bytes.seek(0));
    CTEST_CHECK(!verifyInstallArtifact(bytes, payload.size() + 1, hash));
    CTEST_CHECK(bytes.seek(0));
    CTEST_CHECK(!verifyInstallArtifact(bytes, payload.size(), QByteArray(64, '0')));
    CTEST_CHECK(bytes.seek(0));
    CTEST_CHECK(!verifyInstallArtifact(bytes, payload.size(), QByteArray("not-a-hash")));
    return 0;
}
