#include "Society/Native/LegacyNavigationBridge.h"
#include "Society/Native/NativeAppsSurface.h"
#include "Society/Native/NativeAppsViewModel.h"
#include "Society/Native/NativeNavigationSurface.h"

#include <QQuickItem>
#include <QQuickWindow>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QSignalSpy>
#include <QTest>

// The fixture supplies data and records actions; it never launches an app.
class EnvironmentAppsModel final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList apps READ apps NOTIFY changed)
public:
    using QObject::QObject;
    QVariantList apps() const { return {QVariantMap{{"id", "dreamscapes"}, {"name", "Dreamscapes"},
        {"description", "Images"}, {"status", "Installed"}, {"action", "Open"},
        {"publisher", "iisacc"}, {"installed", true}, {"actionEnabled", true}}}; }
    Q_INVOKABLE void refresh() {}
    Q_INVOKABLE bool open(const QString &id) { opened = id; return true; }
    QString opened;
signals:
    void changed();
};

class AppsEnvironmentFixture final : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(QString page READ page WRITE setPage NOTIFY pageChanged)
    Q_PROPERTY(QVariantList webEntries READ webEntries NOTIFY webEntriesChanged)
    Q_PROPERTY(QVariantList customWebApps READ customWebApps NOTIFY customWebAppsChanged)
public:
    explicit AppsEnvironmentFixture(QQuickItem *parent) : QQuickItem(parent), catalogue(this) {}
    QString page() const { return page_; }
    void setPage(const QString &value) { if (page_ != value) { page_ = value; emit pageChanged(); } }
    QVariantList webEntries() const { return {QVariantMap{{"id", "account"}, {"name", "Account"}, {"url", "https://iisacc.com/Account"}}}; }
    QVariantList customWebApps() const { return {}; }
    Q_INVOKABLE void showApps(const QVariant &) { setPage("Apps"); }
    Q_INVOKABLE void openApp(const QVariant &value) { webOpened = value.toMap().value("id").toString(); }
    EnvironmentAppsModel catalogue;
    QString webOpened;
signals:
    void pageChanged(); void webEntriesChanged(); void customWebAppsChanged();
private:
    QString page_ = "Overview";
};

class AppsShellFixture final : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(QString selectedTab MEMBER selectedTab NOTIFY selectedTabChanged)
public:
    using QQuickItem::QQuickItem;
    QString selectedTab = "Dashboard";
signals:
    void selectedTabChanged(); void tabRequested(const QString &page); void featureRequested(const QString &feature);
};

class NativeAppsSurfaceTests final : public QObject {
    Q_OBJECT
private slots:
    void routesToNativeSurfaceAndExistingProviders() {
        QQuickWindow window;
        AppsShellFixture shell(window.contentItem()); shell.setObjectName("societyContent");
        QQuickItem viewport(&shell); viewport.setObjectName("dashboardTabViewport");
        QQuickItem legacy(&viewport); legacy.setObjectName("dashboardTabs");
        QQuickItem anchor(&shell); anchor.setObjectName("dashboardView"); anchor.setSize({1440, 844});
        AppsEnvironmentFixture environment(&shell); environment.setObjectName("environmentView");
        connect(&shell, &AppsShellFixture::tabRequested, &shell, [&](const QString &page) {
            if (shell.selectedTab != page) { shell.selectedTab = page; emit shell.selectedTabChanged(); }
        });
        QSignalSpy unsupported(&shell, &AppsShellFixture::featureRequested);
        auto *navigation = LegacyNavigationBridge::install(&window); QVERIFY(navigation);
        navigation->requested("Apps");
        auto *surface = shell.findChild<NativeAppsSurface *>("nativeAppsSurface");
        QVERIFY(surface); QVERIFY2(surface->ready(), qPrintable(surface->errorString()));
        QVERIFY(surface->isVisible()); QCOMPARE(navigation->currentPage(), QString("Apps"));
        QCOMPARE(surface->model()->visibleApps.get().size(), std::size_t(1));
        surface->model()->performAction();
        QTRY_COMPARE(environment.catalogue.opened, QString("dreamscapes"));
        surface->model()->showWebApps();
        QCOMPARE(surface->model()->visibleApps.get().size(), std::size_t(1));
        surface->model()->performAction();
        QTRY_COMPARE(environment.webOpened, QString("account"));
        environment.setPage("Devices");
        QVERIFY(!surface->isVisible());
        environment.setPage("Apps");
        QVERIFY(surface->isVisible());
        QCOMPARE(navigation->currentPage(), QString("Apps"));
        QCOMPARE(unsupported.size(), 0);
        navigation->requested("Environment");
        QVERIFY(!surface->isVisible());
        QCOMPARE(navigation->currentPage(), QString("Dashboard"));
    }
    void readsQmlWebShortcutArrays() {
        QQuickWindow window;
        AppsShellFixture shell(window.contentItem()); shell.setObjectName("societyContent");
        QQuickItem viewport(&shell); viewport.setObjectName("dashboardTabViewport");
        QQuickItem legacy(&viewport); legacy.setObjectName("dashboardTabs");
        QQuickItem anchor(&shell); anchor.setObjectName("dashboardView"); anchor.setSize({1440, 844});
        connect(&shell, &AppsShellFixture::tabRequested, &shell, [&](const QString &page) {
            if (shell.selectedTab != page) { shell.selectedTab = page; emit shell.selectedTabChanged(); }
        });
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData(QByteArrayLiteral(
            "import QtQuick\n"
            "Item {\n"
            "  objectName: 'environmentView'\n"
            "  property string page: 'Overview'\n"
            "  property var webEntries: [{id: 'account', name: 'Account', url: 'https://iisacc.com/Account'}]\n"
            "  property var customWebApps: [{id: 'personal', name: 'Personal', url: 'https://example.org/'}]\n"
            "  property string opened: ''\n"
            "  function showApps(collection) { page = 'Apps' }\n"
            "  function openApp(entry) { opened = entry.id }\n"
            "}\n"), QUrl());
        QScopedPointer<QObject> environment(component.create());
        QVERIFY2(environment, qPrintable(component.errorString()));
        environment->setParent(&shell);
        auto *environmentItem = qobject_cast<QQuickItem *>(environment.data()); QVERIFY(environmentItem);
        environmentItem->setParentItem(&shell);
        new EnvironmentAppsModel(environment.data());
        auto *navigation = LegacyNavigationBridge::install(&window); QVERIFY(navigation);
        navigation->requested("Apps");
        auto *surface = shell.findChild<NativeAppsSurface *>("nativeAppsSurface"); QVERIFY(surface);
        surface->model()->showWebApps();
        QCOMPARE(surface->model()->visibleApps.get().size(), std::size_t(2));
        surface->model()->selectApp("web:personal")();
        surface->model()->performAction();
        QTRY_COMPARE(environment->property("opened").toString(), QString("personal"));
    }
};
QTEST_MAIN(NativeAppsSurfaceTests)
#include "tst_native_apps_surface.moc"
