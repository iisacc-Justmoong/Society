#include "Society/Environment/EnvironmentAppsModel.h"
#include "CTestSupport.h"

#include <QCoreApplication>
#include <QFile>

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QFile file(":/iisacc/distribution/macos-arm64.json");
    CTEST_CHECK(file.open(QIODevice::ReadOnly));
    const auto catalog = file.readAll();
    using Platform = EnvironmentAppsModel::DistributionPlatform;
    EnvironmentAppsModel available({}, catalog, Platform::MacOSArm64, {}, [](const QString &) { return QString{}; });
    const auto release = available.installRelease("congregation");
    CTEST_CHECK(!release.isEmpty());
    CTEST_COMPARE(release.value("version").toString(), QString("0.1.1"));
    CTEST_COMPARE(release.value("sizeBytes").toLongLong(), qint64(42781857));
    CTEST_COMPARE(release.value("sha256").toByteArray(), QByteArray("33e9b11d537b84dca800c26b2065daf0568118c3fe47312b856981feaa220dfa"));
    CTEST_CHECK(available.installRelease("unknown").isEmpty());
    CTEST_CHECK(available.installRelease("congregation.installer").isEmpty());
    EnvironmentAppsModel installed({}, catalog, Platform::MacOSArm64, {}, [](const QString &) { return QStringLiteral("/Applications/Existing.app"); });
    CTEST_CHECK(installed.installRelease("congregation").isEmpty());
    EnvironmentAppsModel other({}, catalog, Platform::Other, {}, [](const QString &) { return QString{}; });
    CTEST_CHECK(other.installRelease("congregation").isEmpty());
    return 0;
}
