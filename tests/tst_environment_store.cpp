#include "Society/Environment/EnvironmentAppsModel.h"
#include "CTestSupport.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>

// Contract: only published, configured Windows Store products can be opened;
// opening a Store page does not assert that the application is installed.
class EnvironmentStoreTests final {
private:
    static QVariantMap entry(const EnvironmentAppsModel &model, const QString &id)
    {
        for (const auto &value : model.apps()) {
            const auto app = value.toMap();
            if (app.value("id").toString() == id) return app;
        }
        return {};
    }

public:
    void opensOnlyPublishedStoreLinks()
    {
        const QByteArray config = R"({"schema":1,"products":[
            {"id":"dreamscapes","url":"https://apps.microsoft.com/detail/9NXV9RQDVLQ5","published":true},
            {"id":"congregation","url":"https://apps.microsoft.com/detail/9N5QVRW92QQT","published":false}
        ]})";
        QList<QUrl> opened;
        EnvironmentAppsModel model(config, true, [&opened](const QUrl &url) { opened.append(url); return true; });
        const auto dreamscapes = entry(model, "dreamscapes");
        CTEST_CHECK(!dreamscapes.value("installed").toBool());
        CTEST_CHECK(dreamscapes.value("actionEnabled").toBool());
        CTEST_COMPARE(dreamscapes.value("license").toString(), QString("License not verified"));
        CTEST_CHECK(model.open("dreamscapes"));
        CTEST_COMPARE(opened, QList<QUrl>{QUrl("https://apps.microsoft.com/detail/9NXV9RQDVLQ5")});
        CTEST_CHECK(!entry(model, "congregation").value("actionEnabled").toBool());
        CTEST_CHECK(!model.open("congregation"));
        CTEST_CHECK(!model.open("unknown"));
        CTEST_COMPARE(opened.size(), 1);
    }

    void rejectsUntrustedOrUnavailableLinks()
    {
        const QByteArray config = R"({"schema":1,"products":[
            {"id":"canvas","url":"https://example.com/detail/9NXV9RQDVLQ5","published":true},
            {"id":"motion","url":"http://apps.microsoft.com/detail/9NXV9RQDVLQ5","published":true},
            {"id":"notes","url":"https://apps.microsoft.com/detail/9NXV9RQDVLQ5?redirect=other","published":true}
        ]})";
        int opened = 0;
        EnvironmentAppsModel model(config, true, [&opened](const QUrl &) { ++opened; return true; });
        for (const auto id : {"canvas", "motion", "notes"}) {
            CTEST_CHECK(!entry(model, id).value("actionEnabled").toBool());
            CTEST_CHECK(!model.open(id));
        }
        CTEST_COMPARE(opened, 0);
        const QByteArray valid = R"({"schema":1,"products":[
            {"id":"canvas","url":"https://apps.microsoft.com/detail/9NXV9RQDVLQ5","published":true}
        ]})";
        EnvironmentAppsModel unsupported(valid, false, [&opened](const QUrl &) { ++opened; return true; });
        CTEST_CHECK(!unsupported.open("canvas"));
        const QByteArray unknownSchema = R"({"schema":2,"products":[
            {"id":"canvas","url":"https://apps.microsoft.com/detail/9NXV9RQDVLQ5","published":true}
        ]})";
        EnvironmentAppsModel unknown(unknownSchema, true, [&opened](const QUrl &) { ++opened; return true; });
        CTEST_CHECK(!unknown.open("canvas"));
        CTEST_COMPARE(opened, 0);
    }

    void packagedLinksStayDisabledBeforePublication()
    {
        QFile file(":/iisacc/distribution/windows-store.json");
        CTEST_CHECK(file.open(QIODevice::ReadOnly));
        const auto config = file.readAll();
        const auto root = QJsonDocument::fromJson(config).object();
        CTEST_COMPARE(root.value("schema").toInt(), 1);
        QHash<QString, QString> configured;
        for (const auto &value : root.value("products").toArray()) {
            const auto product = value.toObject();
            CTEST_CHECK(product.value("published").isBool());
            CTEST_CHECK(!product.value("published").toBool());
            configured.insert(product.value("id").toString(), product.value("url").toString());
        }
        CTEST_COMPARE(configured.size(), 2);
        CTEST_COMPARE(configured.value("dreamscapes"), QString("https://apps.microsoft.com/detail/9NXV9RQDVLQ5"));
        CTEST_COMPARE(configured.value("congregation"), QString("https://apps.microsoft.com/detail/9N5QVRW92QQT"));
        int opened = 0;
        EnvironmentAppsModel model(config, true, [&opened](const QUrl &) { ++opened; return true; });
        for (const auto id : {"dreamscapes", "congregation"}) {
            CTEST_CHECK(!entry(model, id).value("actionEnabled").toBool());
            CTEST_CHECK(!model.open(id));
        }
        CTEST_COMPARE(opened, 0);
    }

    void reportsFailureToOpenTheStore()
    {
        const QByteArray config = R"({"schema":1,"products":[
            {"id":"dreamscapes","url":"https://apps.microsoft.com/detail/9NXV9RQDVLQ5","published":true}
        ]})";
        EnvironmentAppsModel model(config, true, [](const QUrl &) { return false; });
        CTEST_CHECK(!model.open("dreamscapes"));
    }
};

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    EnvironmentStoreTests tests;
    society_ctest::Runner runner;
    runner.run("opensOnlyPublishedStoreLinks", [&] { tests.opensOnlyPublishedStoreLinks(); });
    runner.run("rejectsUntrustedOrUnavailableLinks", [&] { tests.rejectsUntrustedOrUnavailableLinks(); });
    runner.run("packagedLinksStayDisabledBeforePublication", [&] { tests.packagedLinksStayDisabledBeforePublication(); });
    runner.run("reportsFailureToOpenTheStore", [&] { tests.reportsFailureToOpenTheStore(); });
    return runner.result();
}
