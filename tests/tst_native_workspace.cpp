#include "Society/Native/NativeWorkspaceSurface.h"
#include "Society/Native/LegacyNavigationBridge.h"
#include "Society/Native/LegacyWorkspaceBridge.h"
#include "Society/Native/NativeNavigationSurface.h"
#include <QGuiApplication>
#include <QImage>
#include <QKeyEvent>
#include <QKeySequence>
#include <QMouseEvent>
#include <QPainter>
#include <QSignalSpy>
#include <QQuickWindow>
#include <QtTest>

class WorkspaceShell final : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(QString selectedTab MEMBER selectedTab NOTIFY selectedTabChanged)
public:
    using QQuickItem::QQuickItem;
    QString selectedTab = QStringLiteral("Tools");
signals:
    void selectedTabChanged();
    void tabRequested(const QString &page);
};

class WorkspaceDashboard final : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(bool projectsPageVisible MEMBER projects NOTIFY projectsPageVisibleChanged)
    Q_PROPERTY(bool calendarPageVisible MEMBER calendar NOTIFY calendarPageVisibleChanged)
    Q_PROPERTY(bool activityPageVisible MEMBER activity NOTIFY activityPageVisibleChanged)
    Q_PROPERTY(QString filesRoot MEMBER root NOTIFY filesRootChanged)
    Q_PROPERTY(QString dashboardPath MEMBER path NOTIFY dashboardPathChanged)
public:
    using QQuickItem::QQuickItem;
    bool projects = false, calendar = false, activity = false;
    QString root, path;
    Q_INVOKABLE void goHome() {
        projects = calendar = activity = false;
        emit projectsPageVisibleChanged(); emit calendarPageVisibleChanged(); emit activityPageVisibleChanged();
    }
    Q_INVOKABLE void showProjects() { goHome(); projects = true; emit projectsPageVisibleChanged(); }
    Q_INVOKABLE void showCalendar(const QString &section) {
        goHome(); calendar = section.isEmpty(); activity = !section.isEmpty();
        emit calendarPageVisibleChanged(); emit activityPageVisibleChanged();
    }
signals:
    void projectsPageVisibleChanged(); void calendarPageVisibleChanged(); void activityPageVisibleChanged();
    void filesRootChanged(); void dashboardPathChanged();
    void fileRequested(const QString &path);
};

class NativeWorkspaceTest final : public QObject {
    Q_OBJECT
private slots:
    void rendersAndRoutesWorkspacePages() {
        NativeWorkspaceSurface surface;
        surface.setSize({1440, 772});
        surface.refresh();
        QVERIFY2(surface.ready(), qPrintable(surface.errorString()));
        QVERIFY(surface.model());
        QImage image(1440, 772, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        QPainter painter(&image);
        surface.paint(&painter);
        painter.end();
        QVERIFY(!image.isNull());
        bool painted = false;
        for (int y = 0; y < image.height() && !painted; y += 8)
            for (int x = 0; x < image.width(); x += 8)
                if (image.pixelColor(x, y).alpha() != 0) { painted = true; break; }
        QVERIFY(painted);

        surface.model()->setSuggestedFiles({{"/Files/example.txt", "example.txt", "2026-11-12", "Text document", "2 B", false, 1, 2}});
        surface.refresh();
        QVERIFY(surface.activateControl(QStringLiteral("workspace-grid")));
        QVERIFY(surface.model()->gridActive.get());
        QVERIFY2(surface.ready(), qPrintable(surface.errorString()));
        QVERIFY(surface.activateControl(QStringLiteral("workspace-list")));
        QVERIFY(!surface.model()->gridActive.get());
        QVERIFY(surface.activateControl(QStringLiteral("workspace-ribbon-share")));
        QVERIFY(surface.model()->ribbonShareActive.get());
        QVERIFY(surface.activateControl(QStringLiteral("workspace-ribbon-view")));
        QVERIFY(surface.model()->ribbonViewActive.get());
        QVERIFY(surface.activateControl(QStringLiteral("workspace-ribbon-manage")));
        QVERIFY(surface.model()->ribbonManageActive.get());
        QVERIFY(surface.activateControl(QStringLiteral("workspace-ribbon-collapse")));
        QVERIFY(!surface.model()->ribbonExpanded.get());
        QVERIFY(surface.activateControl(QStringLiteral("workspace-ribbon-home")));
        QVERIFY(surface.model()->ribbonHomeActive.get() && surface.model()->ribbonExpanded.get());
        QVERIFY2(surface.ready(), qPrintable(surface.errorString()));
        QVERIFY(surface.activateControl(QStringLiteral("workspace-file-button")));
        QVERIFY(surface.model()->fileMenuOpen.get());
        QVERIFY(surface.controlOpened(QStringLiteral("workspace-file-menu")));
        QVERIFY(surface.activateControl(QStringLiteral("workspace-file-button")));
        QVERIFY(!surface.model()->fileMenuOpen.get());
        QVERIFY(!surface.controlOpened(QStringLiteral("workspace-file-menu")));

        surface.model()->setSuggestedFiles({{"/Files/a.txt", "a.txt", "", "Text", "1 B", false, 1, 1},
                                            {"/Files/b.txt", "b.txt", "", "Text", "2 B", false, 2, 2},
                                            {"/Files/c.txt", "c.txt", "", "Text", "3 B", false, 3, 3}});
        surface.refresh();
        const auto table=surface.controlBounds(QStringLiteral("workspace-table"));
        QVERIFY(!table.isEmpty());
        const auto click=[&](QPointF position,Qt::KeyboardModifiers modifiers) {
            QMouseEvent press(QEvent::MouseButtonPress,position,position,Qt::LeftButton,Qt::LeftButton,modifiers);
            QCoreApplication::sendEvent(&surface,&press);
            QMouseEvent release(QEvent::MouseButtonRelease,position,position,Qt::LeftButton,Qt::NoButton,modifiers);
            QCoreApplication::sendEvent(&surface,&release);
        };
        click({table.x()+30,table.y()+36},Qt::NoModifier);
        QCOMPARE(surface.model()->selectionCount.get(),std::int64_t(1));
        click({table.x()+30,table.y()+84},Qt::ShiftModifier);
        QCOMPARE(surface.model()->selectionCount.get(),std::int64_t(3));
        surface.model()->showGrid(); surface.refresh(); surface.model()->selectAll();
        QCOMPARE(surface.model()->selectionCount.get(),std::int64_t(3));
        surface.model()->invertSelection();
        QCOMPARE(surface.model()->selectionCount.get(),std::int64_t(0));
        surface.model()->showList(); surface.refresh();
        surface.model()->selected(lvui::TableSelection{{"/Files/a.txt", "name"}, {"/Files/a.txt", "name"}});
        surface.model()->showRename(); surface.refresh();
        QVERIFY(surface.model()->renameOpen.get()); QVERIFY2(surface.ready(), qPrintable(surface.errorString()));
        surface.model()->closeRename(); surface.refresh();
        surface.model()->showMove(); surface.refresh();
        QVERIFY(surface.model()->moveOpen.get()); QVERIFY2(surface.ready(), qPrintable(surface.errorString()));
        surface.model()->closeMove(); surface.refresh();
        surface.model()->showCreateFolder(); surface.refresh();
        QVERIFY(surface.model()->folderOpen.get()); QVERIFY2(surface.ready(), qPrintable(surface.errorString()));
        surface.model()->closeCreateFolder(); surface.refresh();
        surface.model()->showPaste(); surface.refresh();
        QVERIFY(surface.model()->pasteOpen.get()); QVERIFY2(surface.ready(), qPrintable(surface.errorString()));
        surface.model()->closePaste(); surface.refresh();
        surface.model()->showDetails(); surface.refresh();
        QVERIFY(surface.model()->detailsOpen.get()); QVERIFY2(surface.ready(), qPrintable(surface.errorString()));
        surface.model()->closeDetails(); surface.refresh();

        surface.model()->setAgentModels({{"model://small", "iisaccson"}});
        surface.model()->setAgentState("model://small", false, "Ready · uncalibrated routing",
                                       {{"1", "You", "Hello"}, {"2", "Agent", "A fixture response."}});
        surface.refresh();
        QVERIFY2(surface.ready(), qPrintable(surface.errorString()));
        QVERIFY(surface.activateControl(QStringLiteral("workspace-agent-model")));
        QVERIFY(surface.model()->agentModelMenuVisible.get());
        surface.model()->toggleAgentModels();
        surface.model()->setAgentState("model://small", true, "Responding…",
                                       {{"1", "You", "Hello"}, {"2", "Agent", "A longer streamed fixture response."}});
        surface.refresh();
        QVERIFY2(surface.ready(), qPrintable(surface.errorString()));
        QCOMPARE(surface.model()->agentMessages.get().back().text,std::string("A longer streamed fixture response."));

        QVERIFY(surface.focusControl(QStringLiteral("workspace-search")));
        QKeyEvent first(QEvent::KeyPress, Qt::Key_A, Qt::NoModifier, QStringLiteral("a"));
        QCoreApplication::sendEvent(&surface, &first);
        QKeyEvent space(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier, QStringLiteral(" "));
        QCoreApplication::sendEvent(&surface, &space);
        QKeyEvent second(QEvent::KeyPress, Qt::Key_B, Qt::NoModifier, QStringLiteral("b"));
        QCoreApplication::sendEvent(&surface, &second);
        QCOMPARE(QString::fromStdString(surface.model()->query.get()), QStringLiteral("a b"));
        const auto shortcut = QKeySequence(QKeySequence::SelectAll)[0];
        QKeyEvent selectAll(QEvent::KeyPress, shortcut.key(), shortcut.keyboardModifiers());
        QCoreApplication::sendEvent(&surface, &selectAll);
        QKeyEvent replacement(QEvent::KeyPress, Qt::Key_Z, Qt::NoModifier, QStringLiteral("z"));
        QCoreApplication::sendEvent(&surface, &replacement);
        QCOMPARE(QString::fromStdString(surface.model()->query.get()), QStringLiteral("z"));

        QSignalSpy navigation(&surface, &NativeWorkspaceSurface::pageRequested);
        QVERIFY(surface.activateControl(QStringLiteral("workspace-recent")));
        QTRY_COMPARE(navigation.size(), 1);
        QCOMPARE(navigation.takeFirst().at(0).toInt(), int(NativeWorkspaceViewModel::Page::Recent));
        surface.refresh();
        QVERIFY2(surface.ready(), qPrintable(surface.errorString()));
        QVERIFY(surface.activateControl(QStringLiteral("workspace-projects")));
        QTRY_COMPARE(navigation.size(), 1);
        QCOMPARE(navigation.takeFirst().at(0).toInt(), int(NativeWorkspaceViewModel::Page::Projects));
        QVERIFY(!surface.model()->contentVisible.get());
    }
    void firstSegmentReturnsToWorkspace() {
        QQuickWindow window;
        WorkspaceShell shell(window.contentItem()); shell.setObjectName(QStringLiteral("societyContent"));
        QQuickItem viewport(&shell); viewport.setObjectName(QStringLiteral("dashboardTabViewport"));
        QQuickItem legacy(&viewport); legacy.setObjectName(QStringLiteral("dashboardTabs"));
        auto *surface = LegacyNavigationBridge::install(&window);
        QVERIFY(surface);
        QCOMPARE(surface->accessibleItemName(0), QStringLiteral("Workspace"));
        QSignalSpy requested(&shell, &WorkspaceShell::tabRequested);
        surface->requested(QStringLiteral("Environment"));
        QCOMPARE(requested.size(), 1);
        QCOMPARE(requested.takeFirst().at(0).toString(), QStringLiteral("Dashboard"));
        surface->requested(QStringLiteral("Hosting"));
        QCOMPARE(requested.size(), 1);
        QCOMPARE(requested.takeFirst().at(0).toString(), QStringLiteral("Environment"));
    }
    void workspaceBridgeConnectsSidebarAndEmptyDrive() {
        QQuickWindow window;
        WorkspaceDashboard dashboard(window.contentItem());
        dashboard.setObjectName(QStringLiteral("dashboardView")); dashboard.setSize({1200, 760});
        QQuickItem sidebar(&dashboard); sidebar.setObjectName(QStringLiteral("dashboardSidebar")); sidebar.setWidth(170);
        QQuickItem content(&dashboard);
        QQuickItem scroll(&content); scroll.setObjectName(QStringLiteral("dashboardScroll"));
        auto *surface = LegacyWorkspaceBridge::install(&window);
        QVERIFY(surface);
        QCOMPARE(QString::fromStdString(surface->model()->status.get()), QStringLiteral("Society drive is not available."));
        QVERIFY(surface->activateControl(QStringLiteral("workspace-recent")));
        QTRY_VERIFY(surface->model()->recentActive.get());
        QVERIFY(!dashboard.projects);
        QVERIFY(surface->activateControl(QStringLiteral("workspace-projects")));
        QTRY_VERIFY(dashboard.projects);
        QVERIFY(surface->model()->projectsActive.get());
        QVERIFY(surface->activateControl(QStringLiteral("workspace-suggested")));
        QTRY_VERIFY(!dashboard.projects);
        QVERIFY(surface->model()->suggestedActive.get());
        surface->openRequested(QStringLiteral("/outside/file"));
        QCOMPARE(QString::fromStdString(surface->model()->status.get()), QStringLiteral("The file is not in the current Society drive."));
    }
};

QTEST_MAIN(NativeWorkspaceTest)
#include "tst_native_workspace.moc"
