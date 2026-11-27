#include "Society/Environment/EnvironmentAppsModel.h"
#include "CTestSupport.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>

// The Apps catalogue must expose the exact published macOS packages while
// preserving launch actions for applications already present on the device.
class EnvironmentDistributionTests final {
    using Platform = EnvironmentAppsModel::DistributionPlatform;

    static QVariantMap entry(const EnvironmentAppsModel &model, const QString &id)
    {
        for (const auto &value : model.apps()) {
            const auto app = value.toMap();
            if (app.value("id").toString() == id) return app;
        }
        return {};
    }

    static QByteArray packagedCatalog()
    {
        QFile file(":/iisacc/distribution/macos-arm64.json");
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
    }

public:
    void uninstalledProductsOpenTheirPublishedDownloads()
    {
        QList<QUrl> opened;
        EnvironmentAppsModel model({}, packagedCatalog(), Platform::MacOSArm64,
            [&opened](const QUrl &url) { opened.append(url); return true; },
            [](const QString &) { return QString{}; });
        for (const auto &id : {QStringLiteral("congregation"), QStringLiteral("dreamscapes")}) {
            const auto app = entry(model, id);
            CTEST_CHECK(!app.value("installed").toBool());
            CTEST_CHECK(app.value("actionEnabled").toBool());
            CTEST_COMPARE(app.value("action").toString(), QString("Download"));
            CTEST_COMPARE(app.value("license").toString(), QString("License not verified"));
            CTEST_CHECK(model.open(id));
        }
        CTEST_COMPARE(opened, (QList<QUrl>{
            QUrl("https://downloads.iisacc.com/archive/congregation/macos-arm64/0.1.1/iisacc-Congregation-Beta-0.1.1-20261124.pkg"),
            QUrl("https://downloads.iisacc.com/archive/dreamscapes/macos-arm64/0.1.1/iisacc-Dreamscapes-Beta-0.1.1-20261124.pkg")}));
    }

    void installedProductsRetainOpenAndOfferSeparateDownloads()
    {
        QList<QUrl> opened;
        EnvironmentAppsModel model({}, packagedCatalog(), Platform::MacOSArm64,
            [&opened](const QUrl &url) { opened.append(url); return true; },
            [](const QString &name) { return QStringLiteral("/Applications/") + name + QStringLiteral(".app"); });
        for (const auto &id : {QStringLiteral("congregation"), QStringLiteral("dreamscapes")}) {
            CTEST_CHECK(entry(model, id).value("installed").toBool());
            CTEST_COMPARE(entry(model, id).value("action").toString(), QString("Open"));
            const auto installer = entry(model, id + QStringLiteral(".installer"));
            CTEST_CHECK(!installer.isEmpty());
            CTEST_CHECK(!installer.value("installed").toBool());
            CTEST_CHECK(installer.value("actionEnabled").toBool());
            CTEST_COMPARE(installer.value("action").toString(), QString("Download"));
            CTEST_CHECK(installer.value("status").toString().contains(QStringLiteral("0.1.1")));
            CTEST_CHECK(model.open(id)); // The injected opener records the path; no app is launched.
            CTEST_CHECK(model.open(id + QStringLiteral(".installer")));
        }
        CTEST_COMPARE(opened.size(), 4);
        CTEST_COMPARE(opened[0], QUrl::fromLocalFile("/Applications/Congregation.app"));
        CTEST_CHECK(opened[1].toString().contains(QStringLiteral("/congregation/")));
        CTEST_COMPARE(opened[2], QUrl::fromLocalFile("/Applications/Dreamscapes.app"));
        CTEST_CHECK(opened[3].toString().contains(QStringLiteral("/dreamscapes/")));
    }

    void rejectsUnpublishedInvalidAndCrossPlatformPackages()
    {
        const auto valid = QJsonDocument::fromJson(packagedCatalog()).object();
        CTEST_CHECK(!valid.isEmpty());
        const auto original = valid.value("products").toArray().at(0).toObject();
        QList<QJsonObject> invalid;
        for (const QString &url : {
                 QStringLiteral("http://downloads.iisacc.com/archive/congregation/macos-arm64/0.1.1/a.pkg"),
                 QStringLiteral("https://example.com/archive/congregation/macos-arm64/0.1.1/a.pkg"),
                 QStringLiteral("https://user@downloads.iisacc.com/archive/congregation/macos-arm64/0.1.1/a.pkg"),
                 QStringLiteral("https://downloads.iisacc.com:443/archive/congregation/macos-arm64/0.1.1/a.pkg"),
                 QStringLiteral("https://downloads.iisacc.com/archive/dreamscapes/macos-arm64/0.1.1/a.pkg"),
                 QStringLiteral("https://downloads.iisacc.com/archive/congregation/macos-arm64/0.1.2/a.pkg"),
                 QStringLiteral("https://downloads.iisacc.com/archive/congregation/macos-arm64/0.1.1/a.pkg?redirect=elsewhere"),
                 QStringLiteral("https://downloads.iisacc.com/archive/congregation/macos-arm64/0.1.1/a.pkg#fragment"),
                 QStringLiteral("https://downloads.iisacc.com/archive/congregation/macos-arm64/0.1.1%2Fa.pkg"),
                 QStringLiteral("https://downloads.iisacc.com/archive/congregation/macos-arm64/0.1.1/a.zip")}) {
            auto item = original; item.insert("url", url); invalid.append(item);
        }
        auto unpublished = original; unpublished.insert("published", false); invalid.append(unpublished);
        auto badHash = original; badHash.insert("sha256", "bad"); invalid.append(badHash);
        auto badSize = original; badSize.insert("sizeBytes", 0); invalid.append(badSize);
        int calls = 0;
        for (const auto &item : invalid) {
            auto root = valid; root.insert("products", QJsonArray{item});
            EnvironmentAppsModel model({}, QJsonDocument(root).toJson(), Platform::MacOSArm64,
                [&calls](const QUrl &) { ++calls; return true; }, [](const QString &) { return QString{}; });
            CTEST_CHECK(!entry(model, "congregation").value("actionEnabled").toBool());
            CTEST_CHECK(!model.open("congregation"));
        }
        EnvironmentAppsModel unsupported({}, packagedCatalog(), Platform::Other,
            [&calls](const QUrl &) { ++calls; return true; }, [](const QString &) { return QString{}; });
        CTEST_CHECK(!unsupported.open("congregation"));
        EnvironmentAppsModel windows({}, packagedCatalog(), Platform::WindowsStore,
            [&calls](const QUrl &) { ++calls; return true; }, [](const QString &) { return QString{}; });
        CTEST_CHECK(!windows.open("congregation"));
        CTEST_COMPARE(calls, 0);
    }

    void packagedMetadataMatchesVerifiedReleases()
    {
        const auto root = QJsonDocument::fromJson(packagedCatalog()).object();
        QHash<QString, QJsonObject> products;
        for (const auto &value : root.value("products").toArray()) {
            const auto product = value.toObject();
            products.insert(product.value("id").toString(), product);
        }
        CTEST_COMPARE(products.size(), 2);
        CTEST_COMPARE(products.value("congregation").value("sizeBytes").toInteger(), qint64(42781857));
        CTEST_COMPARE(products.value("congregation").value("sha256").toString(), QString("33e9b11d537b84dca800c26b2065daf0568118c3fe47312b856981feaa220dfa"));
        CTEST_COMPARE(products.value("dreamscapes").value("sizeBytes").toInteger(), qint64(47629527));
        CTEST_COMPARE(products.value("dreamscapes").value("sha256").toString(), QString("e713ac69803aa63c556fe2a4dea0c860d0f2775a8f01199f1565cfa295522f5e"));
    }

    void nativeModelIncludesDownloadRegardlessOfInstallation()
    {
#if defined(Q_OS_MACOS) && defined(Q_PROCESSOR_ARM_64)
        EnvironmentAppsModel model;
        for (const auto &id : {QStringLiteral("congregation"), QStringLiteral("dreamscapes")}) {
            const auto app = entry(model, id);
            const auto download = app.value("installed").toBool() ? entry(model, id + QStringLiteral(".installer")) : app;
            CTEST_COMPARE(download.value("action").toString(), QString("Download"));
            CTEST_CHECK(download.value("actionEnabled").toBool());
        }
#endif
    }

    void reportsBrowserOpenFailure()
    {
        EnvironmentAppsModel model({}, packagedCatalog(), Platform::MacOSArm64,
            [](const QUrl &) { return false; }, [](const QString &) { return QString{}; });
        CTEST_CHECK(!model.open("congregation"));
    }
};

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    EnvironmentDistributionTests tests;
    society_ctest::Runner runner;
    runner.run("uninstalledProductsOpenTheirPublishedDownloads", [&] { tests.uninstalledProductsOpenTheirPublishedDownloads(); });
    runner.run("installedProductsRetainOpenAndOfferSeparateDownloads", [&] { tests.installedProductsRetainOpenAndOfferSeparateDownloads(); });
    runner.run("rejectsUnpublishedInvalidAndCrossPlatformPackages", [&] { tests.rejectsUnpublishedInvalidAndCrossPlatformPackages(); });
    runner.run("packagedMetadataMatchesVerifiedReleases", [&] { tests.packagedMetadataMatchesVerifiedReleases(); });
    runner.run("nativeModelIncludesDownloadRegardlessOfInstallation", [&] { tests.nativeModelIncludesDownloadRegardlessOfInstallation(); });
    runner.run("reportsBrowserOpenFailure", [&] { tests.reportsBrowserOpenFailure(); });
    return runner.result();
}
