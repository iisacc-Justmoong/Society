#include "Society/Native/NativeWorkspaceSurface.h"
#include "Society/Native/LegacyNavigationBridge.h"
#include "Society/Native/LegacyWorkspaceBridge.h"
#include "Society/Native/NativeNavigationSurface.h"
#include "Society/Native/NativeNoticeSurface.h"
#include "Society/Native/NativeErrorNotificationSurface.h"
#include <LVUI/Popover.h>
#include <QAbstractListModel>
#include <iiGUI>
#include <QGuiApplication>
#include <QAccessible>
#include <QClipboard>
#include <QImage>
#include <QDir>
#include <QFile>
#include <QLocalServer>
#include <QLocalSocket>
#include <QJsonArray>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QKeySequence>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QMimeData>
#include <QPainter>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QSignalSpy>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QUrl>
#include <QUuid>
#include <QScopeGuard>
#include <QScopedPointer>
#include <QtEndian>
#include <QtMath>
#include <QtTest>
#include <algorithm>

namespace {
constexpr int ribbonCommandHeight = 22;
constexpr int ribbonCommandAreaHeight = 4 * ribbonCommandHeight;
constexpr int ribbonCaptionGap = 4;
constexpr int ribbonGroupGap = 32;
}

class WorkspaceShell final : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(QString selectedTab MEMBER selectedTab NOTIFY selectedTabChanged)
public:
    using QQuickItem::QQuickItem;
    QString selectedTab = QStringLiteral("Tools");
signals:
    void selectedTabChanged();
    void tabRequested(const QString &page);
    void featureRequested(const QString &feature);
};

// Represents the asynchronously loaded Environment page and its public app route.
class EnvironmentFixture final : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(QString page READ page WRITE setPage NOTIFY pageChanged)
public:
    using QQuickItem::QQuickItem;
    QString page() const { return m_page; }
    void setPage(const QString &page) { if (m_page != page) { m_page = page; emit pageChanged(); } }
    Q_INVOKABLE void showApps(const QVariant &collection) { appCollection = collection.toInt(); setPage(QStringLiteral("Apps")); }
    int appCollection = -1;
signals:
    void pageChanged();
private:
    QString m_page = QStringLiteral("Overview");
};

// Provides the same completion signal as the shell's Environment loader.
class EnvironmentLoaderFixture final : public QQuickItem {
    Q_OBJECT
public:
    using QQuickItem::QQuickItem;
signals:
    void loaded();
};

// A legacy notice transport for exercising the shell bridge without launching the app.
class LegacyNoticeFixture final : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(bool open READ isOpen WRITE setOpen NOTIFY openChanged)
    Q_PROPERTY(QString title MEMBER title)
    Q_PROPERTY(QString message MEMBER message)
    Q_PROPERTY(QString primaryText MEMBER primaryText)
public:
    using QQuickItem::QQuickItem;
    bool isOpen() const { return m_open; }
    void setOpen(bool value) { if (m_open != value) { m_open = value; emit openChanged(); } }
    QString title, message, primaryText;
signals:
    void openChanged();
private:
    bool m_open = false;
};

class WorkspaceDashboard final : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(bool projectsPageVisible MEMBER projects NOTIFY projectsPageVisibleChanged)
    Q_PROPERTY(bool calendarPageVisible MEMBER calendar NOTIFY calendarPageVisibleChanged)
    Q_PROPERTY(bool activityPageVisible MEMBER activity NOTIFY activityPageVisibleChanged)
    Q_PROPERTY(QString filesRoot MEMBER root NOTIFY filesRootChanged)
    Q_PROPERTY(QString dashboardPath MEMBER path NOTIFY dashboardPathChanged)
    Q_PROPERTY(QObject* viewModel MEMBER fileModel)
public:
    using QQuickItem::QQuickItem;
    bool projects = false, calendar = false, activity = false;
    QString root, path;
    QObject *fileModel = nullptr;
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

class WorkspaceFileModel final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString containerPath MEMBER containerPath NOTIFY containerPathChanged)
public:
    QString containerPath;
signals:
    void containerPathChanged(); void filesChanged(); void loadingChanged();
};

// Supplies recorded errors using the same role contract as the notification center.
class ErrorNotificationFixture final : public QAbstractListModel {
    Q_OBJECT
public:
    using QAbstractListModel::QAbstractListModel;
    int rowCount(const QModelIndex &parent = {}) const override { return parent.isValid() ? 0 : int(rows.size()); }
    QVariant data(const QModelIndex &index,int role) const override {
        if (!index.isValid() || index.row()<0 || index.row()>=int(rows.size())) return {};
        if (role==Qt::UserRole+1) return rows[index.row()].first;
        if (role==Qt::UserRole+2) return rows[index.row()].second;
        return {};
    }
    QHash<int,QByteArray> roleNames() const override {
        return {{Qt::UserRole+1,"source"},{Qt::UserRole+2,"message"}};
    }
    void append(QString source,QString message) {
        const auto row=int(rows.size());beginInsertRows({},row,row);
        rows.emplace_back(std::move(source),std::move(message));endInsertRows();
    }
    Q_INVOKABLE void record(const QVariant &source,const QVariant &message) { append(source.toString(),message.toString()); }
    std::vector<std::pair<QString,QString>> rows;
};

class NativeWorkspaceTest final : public QObject {
    Q_OBJECT
private slots:
    void recordedErrorsAppearInUpperRightWithoutDeletingHistory() {
        QQuickWindow window;window.resize(1000,700);window.contentItem()->setSize({1000,700});
        WorkspaceShell shell(window.contentItem());shell.setObjectName(QStringLiteral("societyContent"));
        shell.setSize({1000,700});
        QQuickItem viewport(&shell);viewport.setObjectName(QStringLiteral("dashboardTabViewport"));
        QQuickItem legacy(&viewport);legacy.setObjectName(QStringLiteral("dashboardTabs"));
        QQuickItem body(&shell);body.setPosition({35,70});body.setSize({920,610});
        WorkspaceDashboard dashboard(&body);dashboard.setObjectName(QStringLiteral("dashboardView"));dashboard.setSize(body.size());
        QQuickItem sidebar(&dashboard);sidebar.setObjectName(QStringLiteral("dashboardSidebar"));
        QQuickItem content(&dashboard);QQuickItem scroll(&content);scroll.setObjectName(QStringLiteral("dashboardScroll"));
        ErrorNotificationFixture center(&window);center.setObjectName(QStringLiteral("notificationCenter"));
        center.append(QStringLiteral("Storage"),QStringLiteral("First failure"));
        QVERIFY(LegacyNavigationBridge::install(&window));
        auto *notice=window.findChild<NativeErrorNotificationSurface *>(QStringLiteral("societyErrorNotification"));
        QVERIFY(notice);QVERIFY2(notice->ready(),qPrintable(notice->errorString()));
        QVERIFY(notice->isVisible());QCOMPARE(notice->message(),QStringLiteral("Storage: First failure"));
        QCOMPARE(notice->parentItem(),&body);
        const auto margin=lvui::PopoverConfiguration{}.margins;
        QVERIFY(notice->x()>=0 && notice->x()+notice->width()<=dashboard.width());
        QCOMPARE(notice->y(),margin);
        auto *accessible=QAccessible::queryAccessibleInterface(notice);QVERIFY(accessible);
        QCOMPARE(accessible->role(),QAccessible::AlertMessage);
        QCOMPARE(accessible->text(QAccessible::Description),QStringLiteral("Storage: First failure"));
        center.append(QStringLiteral("Account"),QStringLiteral("Second failure"));
        QVERIFY(notice->dismiss());
        QTRY_COMPARE(notice->message(),QStringLiteral("Account: Second failure"));
        QCOMPARE(center.rowCount(),2);
        const auto popup=notice->controlBounds(QStringLiteral("society-error-notification"));
        const auto close=notice->controlBounds(QStringLiteral("dismiss-error-notification"));
        QVERIFY2(popup.contains(close) && !close.isEmpty(),"The close control must stay inside the notification.");
        const auto closePos=close.center();
        QMouseEvent press(QEvent::MouseButtonPress,closePos,closePos,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
        QMouseEvent release(QEvent::MouseButtonRelease,closePos,closePos,Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
        QCoreApplication::sendEvent(notice,&press);QCoreApplication::sendEvent(notice,&release);
        QTRY_VERIFY(!notice->isVisible());
        QCOMPARE(center.rowCount(),2);
        auto *workspace=LegacyWorkspaceBridge::install(&window);QVERIFY(workspace);
        workspace->model()->recordOperationError("Native file failure");
        QTRY_VERIFY(notice->isVisible());
        QCOMPARE(notice->message(),QStringLiteral("Workspace: Native file failure"));
        QCOMPARE(center.rowCount(),3);
    }
    // Exercise the actual retained controls, including a resize and a message
    // that exceeds the available content height, without launching Society.
    void errorNotificationTracksContentAndWrapsLongMessages() {
        QQuickWindow window;window.resize(1100,800);window.contentItem()->setSize({1100,800});
        WorkspaceShell shell(window.contentItem());shell.setObjectName(QStringLiteral("societyContent"));
        shell.setPosition({30,20});shell.setSize({1030,740});
        QQuickItem navigation(&shell);navigation.setObjectName(QStringLiteral("dashboardTabViewport"));
        QQuickItem legacy(&navigation);legacy.setObjectName(QStringLiteral("dashboardTabs"));
        QQuickItem body(&shell);body.setPosition({25,75});body.setSize({900,600});
        WorkspaceDashboard anchor(&body);anchor.setObjectName(QStringLiteral("dashboardView"));
        anchor.setPosition({20,10});anchor.setSize({850,540});
        QQuickItem onboarding(window.contentItem());onboarding.setObjectName(QStringLiteral("containerOnboarding"));
        onboarding.setPosition({40,65});onboarding.setSize({760,610});onboarding.setVisible(false);
        ErrorNotificationFixture center(&window);center.setObjectName(QStringLiteral("notificationCenter"));
        center.append(QStringLiteral("Storage"),QStringLiteral("The Society source must be an existing, unredirected .societycontainer directory package without legacy disk-image markers."));
        QVERIFY(LegacyNavigationBridge::install(&window));
        auto *notice=window.findChild<NativeErrorNotificationSurface *>(QStringLiteral("societyErrorNotification"));
        QVERIFY(notice);QVERIFY2(notice->ready(),qPrintable(notice->errorString()));
        QCOMPARE(notice->parentItem(),&body);
        const auto margin=lvui::PopoverConfiguration{}.margins;
        QCOMPARE(notice->y(),anchor.y()+margin);
        QCOMPARE(notice->x()+notice->width(),anchor.x()+anchor.width()-margin);
        auto popup=notice->controlBounds(QStringLiteral("society-error-notification"));
        auto text=notice->controlBounds(QStringLiteral("error-message"));
        auto viewport=notice->controlBounds(QStringLiteral("error-message-scroll"));
        QVERIFY2(!popup.isEmpty() && !text.isEmpty() && !viewport.isEmpty(),"Notification controls must have layout bounds.");
        QVERIFY2(text.height()>15,"The supplied error must wrap onto multiple lines.");
        QVERIFY2(viewport.contains(text),"The complete wrapped message must fit its viewport when space is available.");
        QVERIFY2(popup.contains(viewport),"The message viewport must stay inside the notification panel.");
        QVERIFY(notice->contains(popup.center()));
        QVERIFY2(!notice->contains(QPointF(popup.center().x(),notice->height()-1)),"The transparent host must not intercept content input.");

        shell.setVisible(false);onboarding.setVisible(true);
        QTRY_COMPARE(notice->parentItem(),window.contentItem());
        QTRY_COMPARE(notice->y(),onboarding.y()+margin);
        QVERIFY2(notice->isVisible(),"Errors must remain visible on the onboarding content view.");
        onboarding.setVisible(false);shell.setVisible(true);
        QTRY_COMPARE(notice->parentItem(),&body);
        QTRY_COMPARE(notice->y(),anchor.y()+margin);

        anchor.setPosition({55,35});anchor.setSize({250,150});
        QTRY_COMPARE(notice->y(),anchor.y()+margin);
        QTRY_COMPARE(notice->x()+notice->width(),anchor.x()+anchor.width()-margin);
        QVERIFY(notice->width()<=anchor.width() && notice->height()<=anchor.height());
        popup=notice->controlBounds(QStringLiteral("society-error-notification"));
        viewport=notice->controlBounds(QStringLiteral("error-message-scroll"));
        QVERIFY(popup.right()<=notice->width() && popup.bottom()<=notice->height());
        QVERIFY(popup.contains(viewport));

        center.append(QStringLiteral("Storage"),QStringLiteral("A very long error message that must remain readable. ").repeated(30));
        QVERIFY(notice->dismiss());
        QTRY_VERIFY(notice->message().contains(QStringLiteral("very long")));
        viewport=notice->controlBounds(QStringLiteral("error-message-scroll"));
        const auto before=notice->controlScrollPosition(QStringLiteral("error-message-scroll"));
        QVERIFY2(before.y()>0,"A message taller than the available area must be scrollable.");
        const auto pos=viewport.center();
        QWheelEvent wheel(pos,pos,QPoint{},QPoint{0,-120},Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);
        QCoreApplication::sendEvent(notice,&wheel);
        QTRY_VERIFY(notice->controlScrollPosition(QStringLiteral("error-message-scroll")).x()>before.x());
        QVERIFY(notice->dismiss());
        QTRY_VERIFY(!notice->isVisible());
        QCOMPARE(center.rowCount(),2);
    }
    void agentTranscriptFollowsNewMessagesUntilUserScrollsUp() {
        NativeWorkspaceSurface surface; surface.setSize({1440,772}); surface.refresh();
        auto *vm=surface.model(); QVERIFY(vm);
        vm->setAgentModels({{"model://small","iisaccson"}});
        vm->setAgentState("model://small",false,"Ready",{}); surface.refresh();
        std::vector<NativeAgentMessage> messages;
        for (int i=0;i<16;++i)
            messages.push_back({"message-"+std::to_string(i),"Agent",std::string(120,'a')});
        vm->setAgentState("model://small",true,"Responding…",messages); surface.refresh();
        const auto viewport=surface.controlBounds(QStringLiteral("workspace-agent-history"));
        auto scroll=surface.controlScrollPosition(QStringLiteral("workspace-agent-history"));
        QVERIFY(!viewport.isEmpty());
        QVERIFY2(scroll.y()>1.0,"The conversation must extend beyond the viewport.");
        QVERIFY2(scroll.x()+1.0>=scroll.y(),"The latest reply must be visible when following the conversation.");
        messages.back().text+=std::string(250,'b');
        vm->setAgentState("model://small",true,"Responding…",messages); surface.refresh();
        scroll=surface.controlScrollPosition(QStringLiteral("workspace-agent-history"));
        QVERIFY2(scroll.x()+1.0>=scroll.y(),"A growing streamed reply must remain visible.");
        const auto position=viewport.center();
        // Qt reports wheel-up as positive, while iiGUI scroll offsets grow downwards.
        QWheelEvent wheel(position,position,QPoint{},QPoint{0,9600},Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);
        QCoreApplication::sendEvent(&surface,&wheel);
        scroll=surface.controlScrollPosition(QStringLiteral("workspace-agent-history"));
        QVERIFY2(scroll.x()+1.0<scroll.y(),"Scrolling up must leave the latest message below the viewport.");
        const auto beforeDown=scroll.x();
        QWheelEvent down(position,position,QPoint{0,-20},QPoint{0,-120},Qt::NoButton,Qt::NoModifier,Qt::ScrollUpdate,false);
        QCoreApplication::sendEvent(&surface,&down);
        scroll=surface.controlScrollPosition(QStringLiteral("workspace-agent-history"));
        QVERIFY2(scroll.x()>beforeDown,"A negative precise Qt delta must scroll down.");
        QVERIFY2(scroll.x()+1.0<scroll.y(),"A small downward scroll must not jump to the end.");
        messages.push_back({"message-16","Agent","A newer message"});
        vm->setAgentState("model://small",false,"Ready",messages); surface.refresh();
        const auto newer=surface.controlScrollPosition(QStringLiteral("workspace-agent-history"));
        QVERIFY2(newer.x()+1.0<newer.y(),"New replies must not force a reader back to the bottom.");
    }
    void agentWaitStateAndComposerRemainVisibleWithAttachments() {
        NativeWorkspaceSurface surface; surface.setSize({1440,772});
        auto *vm=surface.model(); QVERIFY(vm);
        std::vector<NativeWorkspaceFile> files; std::vector<std::string> paths;
        for (int i=0;i<8;++i) {
            const auto path="/Files/attachment-"+std::to_string(i)+".txt";
            files.push_back({path,path,"","Text","1 B",false,i,1}); paths.push_back(path);
        }
        vm->setSuggestedFiles(std::move(files)); vm->gridSelected(paths); vm->addSelectedContext();
        QCOMPARE(vm->context.get().size(),std::size_t(8));
        vm->setAgentModels({{"model://small","iisaccson"}});
        vm->setAgentState("model://small",true,"Waiting for memory to load iisaccson…",{{"1","You","Hello"}});
        surface.refresh(); QVERIFY2(surface.ready(),qPrintable(surface.errorString()));
        const auto panel=surface.controlBounds(QStringLiteral("workspace-agent"));
        const auto status=surface.controlBounds(QStringLiteral("workspace-agent-status"));
        const auto history=surface.controlBounds(QStringLiteral("workspace-agent-history"));
        const auto prompt=surface.controlBounds(QStringLiteral("workspace-prompt"));
        const auto stop=surface.controlBounds(QStringLiteral("workspace-agent-stop"));
        QVERIFY(!panel.isEmpty() && !status.isEmpty() && !history.isEmpty() && !prompt.isEmpty() && !stop.isEmpty());
        QVERIFY(status.bottom() <= history.top());
        QVERIFY(prompt.bottom() <= panel.bottom());
        QVERIFY2(stop.bottom() <= panel.bottom(),"Stop must remain reachable during a memory wait.");
        int cancellations=0; vm->onStopAgent=[&] { ++cancellations; };
        QVERIFY(surface.activateControl(QStringLiteral("workspace-agent-stop")));
        QCOMPARE(cancellations,1);
    }
    void ribbonTabsSupportKeyboardFocusAndActivation() {
        NativeWorkspaceSurface surface; surface.setSize({1440,772}); surface.refresh();
        auto *vm=surface.model(); QVERIFY(vm); QVERIFY(vm->ribbonHomeActive.get());
        QVERIFY(!surface.controlBounds(QStringLiteral("workspace-ribbon-tabbar")).isEmpty());
        const auto homeBounds=surface.controlBounds(QStringLiteral("workspace-ribbon-home"));
        const auto manageBounds=surface.controlBounds(QStringLiteral("workspace-ribbon-manage"));
        const auto addBounds=surface.controlBounds(QStringLiteral("workspace-ribbon-add"));
        const auto searchBounds=surface.controlBounds(QStringLiteral("workspace-ribbon-search"));
        QVERIFY(!homeBounds.isEmpty() && !manageBounds.isEmpty() && !addBounds.isEmpty() && !searchBounds.isEmpty());
        QVERIFY(searchBounds.left() >= addBounds.right());
        surface.setSize({1800,772}); surface.refresh();
        const auto wideHome=surface.controlBounds(QStringLiteral("workspace-ribbon-home"));
        const auto wideManage=surface.controlBounds(QStringLiteral("workspace-ribbon-manage"));
        QVERIFY2(qAbs(wideHome.left()-homeBounds.left())<1.0 && qAbs(wideManage.right()-manageBounds.right())<1.0,
                 "Ribbon tabs must remain packed at the left as the workspace widens.");
        surface.setSize({1440,772}); surface.refresh();
        QVERIFY(vm->iconSource);
        for (const auto *name : {"ribbon-tab-home", "ribbon-tab-share", "ribbon-tab-view", "ribbon-tab-manage", "generaladd", "generalsearch"}) {
            QVERIFY(QFile::exists(QStringLiteral(":/society/workspace/") + QString::fromLatin1(name) + QStringLiteral(".svg")));
            const auto asset=vm->iconSource->resolve(name); QVERIFY(asset.has_value());
            const auto decoded=ii::gui::CachedSvgDecoder::decode(*asset,(*asset)->svg,18);
            QVERIFY2(decoded.has_value(), decoded ? "" : decoded.error().message.c_str());
            const auto rgba=(*decoded)->rgba(); bool visible=false;
            for (std::size_t i=3;i<rgba.size();i+=4) if (rgba[i]) { visible=true; break; }
            QVERIFY2(visible, "Each Figma tab icon must render visible pixels.");
        }
        QVERIFY(surface.focusControl(QStringLiteral("workspace-ribbon-home")));
        const auto key=[&](int value) {
            QKeyEvent press(QEvent::KeyPress,value,Qt::NoModifier);
            QCoreApplication::sendEvent(&surface,&press);
            QKeyEvent release(QEvent::KeyRelease,value,Qt::NoModifier);
            QCoreApplication::sendEvent(&surface,&release);
        };
        key(Qt::Key_Right);
        QVERIFY2(vm->ribbonHomeActive.get(), "Moving tab focus must not change the selected ribbon.");
        key(Qt::Key_Return);
        QVERIFY2(vm->ribbonShareActive.get(), "Enter must activate the tab reached with the arrow key.");
        QCOMPARE(vm->ribbonTabIndex.get(),1);
        key(Qt::Key_Right);
        QVERIFY(vm->ribbonShareActive.get());
        key(Qt::Key_Space);
        QVERIFY2(vm->ribbonViewActive.get(), "Space must activate the focused tab.");
        QCOMPARE(vm->ribbonTabIndex.get(),2);
        QVERIFY(surface.activateControl(QStringLiteral("workspace-ribbon-view")));
        QVERIFY2(vm->ribbonViewActive.get(), "Activating the selected tab keeps its commands active.");
        QVERIFY2(!surface.controlBounds(QStringLiteral("workspace-ribbon-groups")).isEmpty(), "The selected ribbon remains visible.");
    }
    void ribbonRemainsVisibleAcrossTabsAndWorkspacePages() {
        NativeWorkspaceSurface surface; surface.setSize({1440, 772}); surface.refresh();
        QVERIFY2(surface.controlBounds(QStringLiteral("workspace-ribbon-collapse")).isEmpty(),
                 "An always-expanded ribbon must not offer a collapse control.");
        const auto verifyTabs = [&] {
            for (const auto *tab : {"home", "share", "view", "manage", "add", "search"}) {
                const auto id = QStringLiteral("workspace-ribbon-") + QString::fromLatin1(tab);
                for (int activation = 0; activation < 2; ++activation) {
                    QVERIFY(surface.activateControl(id));
                    QVERIFY2(!surface.controlBounds(QStringLiteral("workspace-ribbon-groups")).isEmpty(),
                             "Ribbon commands must remain visible after switching or reselecting a tab.");
                }
            }
        };
        verifyTabs();
        QVERIFY(surface.activateControl(QStringLiteral("workspace-recent")));
        verifyTabs();
        QVERIFY(surface.activateControl(QStringLiteral("workspace-suggested")));
        verifyTabs();
    }
    void ribbonGroupsKeepTheirContentWidthWhenWindowChanges() {
        NativeWorkspaceSurface surface; surface.setSize({1200,772});
        auto *vm=surface.model(); QVERIFY(vm);
        const auto checkTab=[&] {
            surface.setSize({1200,772}); surface.refresh();
            const auto groups=vm->ribbonGroups.get();
            const auto ribbon=surface.controlBounds(QStringLiteral("workspace-ribbon-groups"));
            QVERIFY(!ribbon.isEmpty());
            std::vector<QRectF> narrow;
            for (const auto &item : groups) {
                const auto bounds=surface.keyedControlBounds(QStringLiteral("workspace-ribbon-group-list"),QString::fromStdString(item.id));
                QVERIFY2(!bounds.isEmpty(), "Every ribbon group must have visible, nonzero bounds.");
                QVERIFY2(bounds.right() <= ribbon.right()+1.0,
                         qPrintable(QStringLiteral("Ribbon group %1 exceeds the available width (%2 > %3).")
                             .arg(QString::fromStdString(item.id)).arg(bounds.right()).arg(ribbon.right())));
                if (!narrow.empty()) {
                    const auto gap=bounds.left()-narrow.back().right();
                    QVERIFY2(qAbs(gap - ribbonGroupGap) <= 1.0, "Adjacent ribbon groups must be separated by the LVUI 32px spacing.");
                }
                narrow.push_back(bounds);
            }
            surface.setSize({1800,772}); surface.refresh();
            for (std::size_t i=0;i<groups.size();++i) {
                const auto bounds=surface.keyedControlBounds(QStringLiteral("workspace-ribbon-group-list"),QString::fromStdString(groups[i].id));
                QVERIFY2(!bounds.isEmpty(), "Ribbon groups must remain visible after a resize.");
                QVERIFY2(qAbs(bounds.left()-narrow[i].left())<=1.0 && qAbs(bounds.width()-narrow[i].width())<=1.0,
                         "Widening the window must not stretch or redistribute ribbon groups.");
            }
        };
        vm->showRibbonHome(); checkTab();
        vm->showRibbonShare(); checkTab();
        vm->showRibbonView(); checkTab();
        vm->showRibbonManage(); checkTab();
        vm->selectRibbonTab(4); checkTab();
        vm->selectRibbonTab(5); checkTab();
    }
    void ribbonCommandsAreBorderlessWhenEnabledAndDisabled() {
        NativeWorkspaceSurface surface; surface.setSize({1440, 772});
        auto *vm = surface.model(); QVERIFY(vm);
        vm->setFileActionsAvailable(true); vm->showRibbonHome(); surface.refresh();
        QVERIFY2(surface.ready(), qPrintable(surface.errorString()));
        QImage frame(1440, 772, QImage::Format_ARGB32_Premultiplied); frame.fill(Qt::transparent);
        QPainter painter(&frame); surface.paint(&painter); painter.end();
        for (const auto *name : {"new", "clipboard"}) {
            const auto bounds = surface.keyedControlBounds(QStringLiteral("workspace-ribbon-group-list"), QString::fromLatin1(name));
            QVERIFY(!bounds.isEmpty());
            const auto x = qRound(bounds.center().x());
            const auto command = frame.pixelColor(x, qRound(bounds.top() + 2));
            const auto background = frame.pixelColor(x, qRound(bounds.top() + ribbonCommandAreaHeight + ribbonCaptionGap / 2));
            QVERIFY2(command == background,
                     qPrintable(QStringLiteral("The %1 ribbon command has a background (%2 versus %3)")
                         .arg(QString::fromLatin1(name), command.name(QColor::HexArgb), background.name(QColor::HexArgb))));
        }
    }
    void ribbonCommandIconsShareTheirGroupsLeadingEdge() {
        const QSize size(1440, 772);
        NativeWorkspaceSurface surface;
        surface.setSize(size);
        QVERIFY(surface.model());
        const auto verifyClipboard = [&] {
            surface.refresh();
            QVERIFY2(surface.ready(), qPrintable(surface.errorString()));
            const auto group = surface.keyedControlBounds(QStringLiteral("workspace-ribbon-group-list"), QStringLiteral("clipboard"));
            QVERIFY(!group.isEmpty());
            QImage frame(size, QImage::Format_ARGB32_Premultiplied);
            frame.fill(Qt::transparent);
            QPainter painter(&frame);
            surface.paint(&painter);
            painter.end();
            const auto leadingInk = [&](int row) {
                const auto top = qRound(group.top()) + row * ribbonCommandHeight;
                const auto area = QRect(qCeil(group.left()), top, qFloor(group.width()), ribbonCommandHeight).intersected(frame.rect());
                for (int x = area.left(); x <= area.right(); ++x)
                    for (int y = area.top(); y <= area.bottom(); ++y)
                        if (frame.pixelColor(x, y).alpha() > 20) return x;
                return -1;
            };
            // Both commands use the same icon, so their leading ink should align.
            const auto copy = leadingInk(2);
            const auto copyPath = leadingInk(3);
            QVERIFY(copy >= 0 && copyPath >= 0);
            QCOMPARE(copy, copyPath);
        };
        verifyClipboard();
        QVERIFY(surface.activateControl(QStringLiteral("workspace-recent")));
        verifyClipboard();
    }
    void ribbonGroupCaptionsRenderBeneathCommands() {
        NativeWorkspaceSurface surface; surface.setSize({1800, 772});
        auto *vm = surface.model(); QVERIFY(vm);
        const auto check = [&] {
            surface.refresh(); QVERIFY2(surface.ready(), qPrintable(surface.errorString()));
            QImage image(1800, 772, QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent);
            QPainter painter(&image); surface.paint(&painter); painter.end();
            if (const auto prefix = qEnvironmentVariable("SOCIETY_RIBBON_CAPTION_TEST_OUTPUT"); !prefix.isEmpty())
                QVERIFY(image.save(prefix + QString::number(vm->ribbonTabIndex.get()) + QStringLiteral(".png")));
            const auto checkCaption = [&](const QRectF &bounds, const QString &group) {
                QVERIFY2(!bounds.isEmpty(), qPrintable(QStringLiteral("Ribbon group %1 has no layout bounds.").arg(group)));
                const auto top = qCeil(bounds.top() + ribbonCommandAreaHeight + ribbonCaptionGap);
                const auto area = QRect(qCeil(bounds.left()), top, qFloor(bounds.width()), qFloor(bounds.bottom()) - top)
                                      .intersected(image.rect());
                int left = area.right()+1, right = area.left()-1;
                for (int y = area.top(); y <= area.bottom(); ++y)
                    for (int x = area.left(); x <= area.right(); ++x)
                        if (image.pixelColor(x,y).alpha() > 20) { left = qMin(left,x); right = qMax(right,x); }
                QVERIFY2(right >= left, qPrintable(QStringLiteral("Ribbon group %1 has no visible caption below its commands.")
                    .arg(group)));
                QVERIFY2(qAbs((left+right)/2.0-bounds.center().x()) <= 4.0,
                    qPrintable(QStringLiteral("Ribbon group %1 caption is not centered (ink %2–%3; bounds %4,%5 %6x%7).")
                        .arg(group).arg(left).arg(right)
                        .arg(bounds.x()).arg(bounds.y()).arg(bounds.width()).arg(bounds.height())));
            };
            for (const auto &group : vm->ribbonGroups.get()) {
                const auto id = QString::fromStdString(group.id);
                checkCaption(surface.keyedControlBounds(QStringLiteral("workspace-ribbon-group-list"), id), id);
            }
            if (vm->ribbonSearchActive.get()) {
                const auto ribbon = surface.controlBounds(QStringLiteral("workspace-ribbon-groups"));
                const auto media = surface.keyedControlBounds(QStringLiteral("workspace-ribbon-group-list"), QStringLiteral("search-media"));
                const auto searchWidth = media.left() - ribbon.left() - ribbonGroupGap;
                checkCaption(QRectF(ribbon.topLeft(), QSizeF(searchWidth, media.height())), QStringLiteral("search"));
            }
        };
        vm->showRibbonHome(); check();
        vm->showRibbonShare(); check();
        vm->showRibbonView(); check();
        vm->showRibbonManage(); check();
        vm->selectRibbonTab(4); check();
        vm->selectRibbonTab(5); check();
    }
    void ribbonGroupCaptionsMatchFigma() {
        NativeWorkspaceSurface surface; surface.setSize({1440, 772});
        auto *vm = surface.model(); QVERIFY(vm);
        const auto check = [&](const QStringList &expected) {
            QStringList actual;
            for (const auto &group : vm->ribbonGroups.get()) actual.append(QString::fromStdString(group.label));
            QCOMPARE(actual, expected);
        };
        vm->showRibbonHome(); check({"Clipboard", "Organize", "New", "Open", "Select"});
        vm->showRibbonShare(); check({"Share", "Access", "Publication", "Release", "Review"});
        vm->showRibbonView(); check({"Layout", "Arrange", "Panels", "Display", "Navigate"});
        vm->showRibbonManage(); check({"Resource", "Revision", "Copies", "Integrity", "Workflow"});
        vm->selectRibbonTab(4); check({"Create", "Import", "Reference", "Project"});
        vm->selectRibbonTab(5); check({"Media", "Type"});
    }
    void ribbonReplacesTheRedundantFileMenu() {
        NativeWorkspaceSurface surface; surface.setSize({1440, 772});
        auto *vm = surface.model(); QVERIFY(vm);
        vm->setFileActionsAvailable(true); surface.refresh();
        QVERIFY2(surface.controlBounds(QStringLiteral("workspace-file-button")).isEmpty(),
                 "The ribbon must not show a redundant File menu button.");
        const auto group = surface.keyedControlBounds(QStringLiteral("workspace-ribbon-group-list"), QStringLiteral("new"));
        QVERIFY(!group.isEmpty());
        const auto click = [&](int row) {
            const QPointF position(group.x() + 10, group.y() + row * 22 + 11);
            QMouseEvent press(QEvent::MouseButtonPress, position, position, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QCoreApplication::sendEvent(&surface, &press);
            QMouseEvent release(QEvent::MouseButtonRelease, position, position, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            QCoreApplication::sendEvent(&surface, &release);
        };
        click(0); QVERIFY2(vm->folderOpen.get(), "New folder remains available directly on Home.");
        vm->closeCreateFolder(); surface.refresh();
        click(1); QVERIFY2(vm->importOpen.get(), "Import remains available directly on Home.");
    }
    void searchRibbonFiltersAndReplacesControlsRow() {
        NativeWorkspaceSurface surface; surface.setSize({1440, 772});
        auto *vm=surface.model(); QVERIFY(vm);
        vm->setSuggestedFiles({{"/Files/picture.png", "picture.png", "", "Image", "1 B", false, 1, 1},
                               {"/Files/notes.txt", "notes.txt", "", "Text", "1 B", false, 1, 1}});
        surface.refresh();
        QVERIFY(surface.controlBounds(QStringLiteral("workspace-list")).isEmpty());
        QVERIFY(surface.controlBounds(QStringLiteral("workspace-grid")).isEmpty());
        QVERIFY(surface.activateControl(QStringLiteral("workspace-ribbon-search")));
        QVERIFY(vm->ribbonSearchActive.get());
        const auto ribbon=surface.controlBounds(QStringLiteral("workspace-ribbon-groups"));
        QVERIFY(!ribbon.isEmpty());
        const auto collection=surface.controlBounds(QStringLiteral("workspace-collection"));
        QVERIFY2(collection.bottom()<=surface.height()-32, "The file list must leave room for the status and action footer.");
        const auto click = [&](QPointF point) {
            QMouseEvent press(QEvent::MouseButtonPress,point,point,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
            QCoreApplication::sendEvent(&surface,&press);
            QMouseEvent release(QEvent::MouseButtonRelease,point,point,Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
            QCoreApplication::sendEvent(&surface,&release);
        };
        const auto media=surface.keyedControlBounds(QStringLiteral("workspace-ribbon-group-list"),QStringLiteral("search-media"));
        QVERIFY(!media.isEmpty());
        click({media.x()+10,media.y()+11});
        QCOMPARE(vm->searchFilter.get(),std::string("Type: Images"));
        QCOMPARE(vm->rows.get().size(),std::size_t(1));
        click({ribbon.x()+40,ribbon.y()+11});
        QKeyEvent character(QEvent::KeyPress,Qt::Key_P,Qt::NoModifier,QStringLiteral("p"));
        QCoreApplication::sendEvent(&surface,&character);
        QCOMPARE(vm->query.get(),std::string("p"));
        click({ribbon.x()+10,ribbon.y()+55});
        QCOMPARE(vm->searchFilter.get(),std::string("Type: All files"));
        QVERIFY(vm->query.get().empty());
        QCOMPARE(vm->rows.get().size(),std::size_t(2));
    }
    void ribbonIconAndLabelShareOneHitArea() {
        NativeWorkspaceSurface surface; surface.setSize({1440,772});
        auto *vm=surface.model(); QVERIFY(vm); vm->setFileActionsAvailable(true);
        vm->setSuggestedFiles({{"/Files/a.txt","a.txt","","Text","1 B",false,1,1}});
        vm->gridSelected({"/Files/a.txt"}); surface.refresh();
        const auto group=surface.keyedControlBounds(QStringLiteral("workspace-ribbon-group-list"),QStringLiteral("organize"));
        QVERIFY(!group.isEmpty());
        const QPointF origin(group.x(),group.y()+2*22);
        const auto click=[&](double x) {
            const QPointF position=origin+QPointF(x,11);
            QMouseEvent press(QEvent::MouseButtonPress,position,position,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
            QCoreApplication::sendEvent(&surface,&press);
            QMouseEvent release(QEvent::MouseButtonRelease,position,position,Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
            QCoreApplication::sendEvent(&surface,&release);
        };
        for (const auto x : {24.0, 10.0, 40.0}) {
            click(x);
            QVERIFY2(vm->renameOpen.get(), "Icon, gap, and label must each activate the same ribbon command.");
            vm->closeRename(); surface.refresh();
        }
        vm->clearSelection(); surface.refresh();
        for (const auto x : {10.0, 24.0, 40.0}) {
            click(x);
            QVERIFY2(!vm->renameOpen.get(), "A disabled ribbon command must not respond in any part of its hit area.");
        }
    }
    void ribbonButtonDispatchesTheSelectedFile() {
        NativeWorkspaceSurface surface; surface.setSize({1440,772});
        auto *vm=surface.model(); QVERIFY(vm); vm->setFileActionsAvailable(true);
        vm->setSuggestedFiles({{"/Files/a.txt","a.txt","","Text","1 B",false,1,1}});
        vm->gridSelected({"/Files/a.txt"}); surface.refresh();
        const auto group=surface.keyedControlBounds(QStringLiteral("workspace-ribbon-group-list"),QStringLiteral("organize"));
        QVERIFY(!group.isEmpty());
        const QPointF position(group.x()+11,group.y()+2*22+11);
        QMouseEvent press(QEvent::MouseButtonPress,position,position,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
        QCoreApplication::sendEvent(&surface,&press);
        QMouseEvent release(QEvent::MouseButtonRelease,position,position,Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
        QCoreApplication::sendEvent(&surface,&release);
        QVERIFY2(vm->renameOpen.get(),"The Rename ribbon button must open the selected file's editor.");
        std::string action,path,name;
        vm->onFileAction=[&](const auto &a,const auto &p,const auto &n) { action=a; path=p; name=n; return true; };
        vm->renameEdited("new.txt"); vm->renameSelected();
        QCOMPARE(QString::fromStdString(action),QStringLiteral("rename"));
        QCOMPARE(QString::fromStdString(path),QStringLiteral("/Files/a.txt"));
        QCOMPARE(QString::fromStdString(name),QStringLiteral("new.txt"));
    }
    void gridUsesIconEvenWhenPreviewExists() {
        QTemporaryDir fixture(QStringLiteral(SOCIETY_TEST_DIRECTORY)+"/workspace-preview-XXXXXX");
        QVERIFY(fixture.isValid());
        const auto path=fixture.filePath("preview.png");
        QImage source(12,12,QImage::Format_RGBA8888); source.fill(QColor(243,7,195)); QVERIFY(source.save(path));
        NativeWorkspaceSurface surface; surface.setSize({1440,772});
        NativeWorkspaceFile file{path.toStdString(),"preview.png","","PNG image","1 B",false,1,1};
        file.previewSource=QUrl::fromLocalFile(path).toString(QUrl::FullyEncoded).toStdString();
        surface.model()->setSuggestedFiles({file}); surface.model()->showGrid(); surface.refresh();
        QVERIFY2(surface.ready(),qPrintable(surface.errorString()));
        const auto hasPreview=[&] {
            surface.refresh();
            QImage frame(1440,772,QImage::Format_ARGB32_Premultiplied); frame.fill(Qt::transparent);
            QPainter painter(&frame); surface.paint(&painter); painter.end();
            const auto grid=surface.controlBounds(QStringLiteral("workspace-collection")).toAlignedRect().intersected(frame.rect());
            for (int y=grid.top();y<qMin(grid.bottom(),grid.top()+140);y+=2)
                for (int x=grid.left();x<qMin(grid.right(),grid.left()+140);x+=2) {
                    const auto color=frame.pixelColor(x,y);
                    if (color.red()>210 && color.green()<40 && color.blue()>160) return true;
                }
            return false;
        };
        QTest::qWait(1000);
        QVERIFY2(!hasPreview(), "Grid files must use type icons even when a thumbnail is available.");
        QCOMPARE(surface.model()->rows.get().front().previewSource, file.previewSource);
        const auto hasImageIcon=[&] {
            surface.refresh();
            QImage frame(1440,772,QImage::Format_ARGB32_Premultiplied); frame.fill(Qt::transparent);
            QPainter painter(&frame); surface.paint(&painter); painter.end();
            const auto area=surface.controlBounds(QStringLiteral("workspace-collection")).toAlignedRect().intersected(frame.rect());
            for (int y=area.top();y<qMin(area.bottom(),area.top()+140);++y)
                for (int x=area.left();x<qMin(area.right(),area.left()+140);++x) {
                    const auto c=frame.pixelColor(x,y);
                    if (c.red()<90 && c.green()>70 && c.green()<160 && c.blue()>160) return true;
                }
            return false;
        };
        QVERIFY2(hasImageIcon(), "The LVUI image file icon is visible in grid view.");
        surface.model()->showList();
        QVERIFY2(hasImageIcon(), "The LVUI image file icon is visible in list view.");
        const auto &catalog=surface.model()->iconSource;
        QVERIFY(catalog);
        for (const auto *name : {"nodesfolder", "fileTypesdiagram", "fileTypesimage", "audio-track", "import-video",
                                  "fileTypestext", "fileTypesarchive", "fileTypesunknown"}) {
            QVERIFY2(QFile::exists(QStringLiteral(":/society/workspace/") + QString::fromLatin1(name) + QStringLiteral(".svg")), name);
            QVERIFY2(catalog->resolve(name).has_value(), name);
        }
        QVERIFY2(surface.ready(),qPrintable(surface.errorString()));
    }
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
        QVERIFY(surface.activateControl(QStringLiteral("workspace-ribbon-view")));
        const auto layoutGroup = surface.keyedControlBounds(QStringLiteral("workspace-ribbon-group-list"), QStringLiteral("layout"));
        QVERIFY(!layoutGroup.isEmpty());
        const auto activateLayoutRow = [&](int row) {
            const QPointF position(layoutGroup.x()+10, layoutGroup.y()+row*22+11);
            QMouseEvent press(QEvent::MouseButtonPress,position,position,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
            QCoreApplication::sendEvent(&surface,&press);
            QMouseEvent release(QEvent::MouseButtonRelease,position,position,Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
            QCoreApplication::sendEvent(&surface,&release);
        };
        activateLayoutRow(2);
        QVERIFY(surface.model()->gridActive.get());
        QVERIFY2(surface.ready(), qPrintable(surface.errorString()));
        activateLayoutRow(1);
        QVERIFY(!surface.model()->gridActive.get());
        QVERIFY(surface.activateControl(QStringLiteral("workspace-ribbon-share")));
        QVERIFY(surface.model()->ribbonShareActive.get());
        QVERIFY(surface.activateControl(QStringLiteral("workspace-ribbon-view")));
        QVERIFY(surface.model()->ribbonViewActive.get());
        QVERIFY(surface.activateControl(QStringLiteral("workspace-ribbon-manage")));
        QVERIFY(surface.model()->ribbonManageActive.get());
        QVERIFY(surface.activateControl(QStringLiteral("workspace-ribbon-home")));
        QVERIFY(surface.model()->ribbonHomeActive.get());
        QVERIFY(!surface.controlBounds(QStringLiteral("workspace-ribbon-groups")).isEmpty());
        QVERIFY2(surface.ready(), qPrintable(surface.errorString()));
        QVERIFY(surface.controlBounds(QStringLiteral("workspace-file-button")).isEmpty());

        surface.model()->setSuggestedFiles({{"/Files/a.txt", "a.txt", "", "Text", "1 B", false, 1, 1},
                                            {"/Files/b.txt", "b.txt", "", "Text", "2 B", false, 2, 2},
                                            {"/Files/c.txt", "c.txt", "", "Text", "3 B", false, 3, 3}});
        surface.refresh();
        QVERIFY2(surface.ready(),qPrintable(surface.errorString()));
        const auto table=surface.controlBounds(QStringLiteral("workspace-collection"));
        QVERIFY(!table.isEmpty());
        const auto click=[&](QPointF position,Qt::KeyboardModifiers modifiers) {
            QMouseEvent press(QEvent::MouseButtonPress,position,position,Qt::LeftButton,Qt::LeftButton,modifiers);
            QCoreApplication::sendEvent(&surface,&press);
            QMouseEvent release(QEvent::MouseButtonRelease,position,position,Qt::LeftButton,Qt::NoButton,modifiers);
            QCoreApplication::sendEvent(&surface,&release);
        };
        click({table.x()+40,table.y()+36},Qt::NoModifier);
        QCOMPARE(surface.model()->selectionCount.get(),std::int64_t(1));
        click({table.x()+40,table.y()+100},Qt::MetaModifier);
        QCOMPARE(surface.model()->selectionCount.get(),std::int64_t(2));
        QVERIFY(surface.activateControl(QStringLiteral("workspace-agent-add")));
        QCOMPARE(surface.model()->context.get().size(),std::size_t(2));
        surface.model()->invertSelection();
        QCOMPARE(surface.model()->selectionCount.get(),std::int64_t(1));
        click({table.x()+40,table.y()+36},Qt::NoModifier);
        click({table.x()+40,table.y()+100},Qt::ShiftModifier);
        QCOMPARE(surface.model()->selectionCount.get(),std::int64_t(3));
        surface.model()->showGrid(); surface.refresh();
        QCOMPARE(surface.model()->selectionCount.get(),std::int64_t(3));
        surface.model()->selectAll();
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
        const auto agentStatus=surface.controlBounds(QStringLiteral("workspace-agent-status"));
        const auto agentHistory=surface.controlBounds(QStringLiteral("workspace-agent-history"));
        const auto agentPrompt=surface.controlBounds(QStringLiteral("workspace-prompt"));
        const auto agentPanel=surface.controlBounds(QStringLiteral("workspace-agent"));
        QVERIFY(!agentStatus.isEmpty());
        QVERIFY(agentStatus.bottom() <= agentHistory.top());
        QVERIFY(!agentPrompt.isEmpty());
        QVERIFY2(agentPrompt.bottom() <= agentPanel.bottom(), "The agent composer must remain visible with attachments.");

        QVERIFY(surface.controlBounds(QStringLiteral("workspace-old-controls")).isEmpty());
        QVERIFY(surface.activateControl(QStringLiteral("workspace-ribbon-search")));
        const auto searchRibbon=surface.controlBounds(QStringLiteral("workspace-ribbon-groups"));
        QVERIFY(!searchRibbon.isEmpty());
        click({searchRibbon.x()+40,searchRibbon.y()+11},Qt::NoModifier);
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
        QCOMPARE(surface->accessibleItemName(0), QStringLiteral("Environment"));
        QSignalSpy requested(&shell, &WorkspaceShell::tabRequested);
        surface->requested(QStringLiteral("Environment"));
        QCOMPARE(requested.size(), 1);
        QCOMPARE(requested.takeFirst().at(0).toString(), QStringLiteral("Dashboard"));
        surface->requested(QStringLiteral("Hosting"));
        QCOMPARE(requested.size(), 1);
        QCOMPARE(requested.takeFirst().at(0).toString(), QStringLiteral("Environment"));
    }
    void appsSegmentWaitsForEnvironmentAndTracksItsSubpage() {
        QQuickWindow window;
        WorkspaceShell shell(window.contentItem()); shell.setObjectName(QStringLiteral("societyContent"));
        QQuickItem viewport(&shell); viewport.setObjectName(QStringLiteral("dashboardTabViewport"));
        QQuickItem legacy(&viewport); legacy.setObjectName(QStringLiteral("dashboardTabs"));
        EnvironmentLoaderFixture loader(&shell); loader.setObjectName(QStringLiteral("environmentLoader"));
        connect(&shell, &WorkspaceShell::tabRequested, &shell, [&](const QString &page) {
            if (shell.selectedTab != page) { shell.selectedTab = page; emit shell.selectedTabChanged(); }
        });
        auto *surface = LegacyNavigationBridge::install(&window); QVERIFY(surface);
        surface->requested(QStringLiteral("Apps"));
        QCOMPARE(shell.selectedTab, QStringLiteral("Environment"));
        EnvironmentFixture environment(&shell); environment.setObjectName(QStringLiteral("environmentView"));
        emit loader.loaded();
        QCOMPARE(environment.page(), QStringLiteral("Apps"));
        QCOMPARE(environment.appCollection, 1);
        QCOMPARE(surface->currentPage(), QStringLiteral("Apps"));
        environment.setPage(QStringLiteral("Devices"));
        QCOMPARE(surface->currentPage(), QStringLiteral("Environment"));
        environment.setPage(QStringLiteral("Apps"));
        QCOMPARE(surface->currentPage(), QStringLiteral("Apps"));
        surface->requested(QStringLiteral("Environment"));
        QCOMPARE(shell.selectedTab, QStringLiteral("Dashboard"));
        surface->requested(QStringLiteral("Apps"));
        QCOMPARE(surface->currentPage(), QStringLiteral("Apps"));
        surface->requested(QStringLiteral("Hosting"));
        QCOMPARE(environment.page(), QStringLiteral("Overview"));
        QCOMPARE(surface->currentPage(), QStringLiteral("Hosting"));
        environment.setPage(QStringLiteral("Devices"));
        QCOMPARE(surface->currentPage(), QStringLiteral("Environment"));
    }
    void pendingAppsRequestIsCancelledWhenAnotherPageIsSelected() {
        QQuickWindow window;
        WorkspaceShell shell(window.contentItem()); shell.setObjectName(QStringLiteral("societyContent"));
        QQuickItem viewport(&shell); viewport.setObjectName(QStringLiteral("dashboardTabViewport"));
        QQuickItem legacy(&viewport); legacy.setObjectName(QStringLiteral("dashboardTabs"));
        EnvironmentLoaderFixture loader(&shell); loader.setObjectName(QStringLiteral("environmentLoader"));
        connect(&shell, &WorkspaceShell::tabRequested, &shell, [&](const QString &page) {
            if (shell.selectedTab != page) { shell.selectedTab = page; emit shell.selectedTabChanged(); }
        });
        auto *surface = LegacyNavigationBridge::install(&window); QVERIFY(surface);
        surface->requested(QStringLiteral("Apps"));
        surface->requested(QStringLiteral("Tools"));
        EnvironmentFixture environment(&shell); environment.setObjectName(QStringLiteral("environmentView"));
        emit loader.loaded();
        QCOMPARE(environment.appCollection, -1);
        QCOMPARE(surface->currentPage(), QStringLiteral("Tools"));
    }
    void appsRouteInvokesAnUntypedQmlFunction() {
        QQuickWindow window;
        WorkspaceShell shell(window.contentItem()); shell.setObjectName(QStringLiteral("societyContent"));
        QQuickItem viewport(&shell); viewport.setObjectName(QStringLiteral("dashboardTabViewport"));
        QQuickItem legacy(&viewport); legacy.setObjectName(QStringLiteral("dashboardTabs"));
        EnvironmentLoaderFixture loader(&shell); loader.setObjectName(QStringLiteral("environmentLoader"));
        connect(&shell, &WorkspaceShell::tabRequested, &shell, [&](const QString &page) {
            if (shell.selectedTab != page) { shell.selectedTab = page; emit shell.selectedTabChanged(); }
        });
        auto *surface = LegacyNavigationBridge::install(&window); QVERIFY(surface);
        surface->requested(QStringLiteral("Apps"));
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData(R"qml(import QtQuick
Item {
    objectName: "environmentView"
    property string page: "Overview"
    property int appCollection: -1
    function showApps(collection) { page = "Apps"; appCollection = collection }
})qml", QUrl());
        QScopedPointer<QObject> environment(component.create());
        QVERIFY2(environment, qPrintable(component.errorString()));
        environment->setParent(&shell);
        emit loader.loaded();
        QCOMPARE(environment->property("page").toString(), QStringLiteral("Apps"));
        QCOMPARE(environment->property("appCollection").toInt(), 1);
        QCOMPARE(surface->currentPage(), QStringLiteral("Apps"));
    }
    void unavailableSegmentsKeepTheCurrentPage() {
        QQuickWindow window;
        WorkspaceShell shell(window.contentItem()); shell.setObjectName(QStringLiteral("societyContent"));
        QQuickItem viewport(&shell); viewport.setObjectName(QStringLiteral("dashboardTabViewport"));
        QQuickItem legacy(&viewport); legacy.setObjectName(QStringLiteral("dashboardTabs"));
        auto *surface = LegacyNavigationBridge::install(&window); QVERIFY(surface);
        QSignalSpy features(&shell, &WorkspaceShell::featureRequested);
        QSignalSpy tabs(&shell, &WorkspaceShell::tabRequested);
        for (const auto &feature : {QStringLiteral("Service"), QStringLiteral("Community"), QStringLiteral("Learning")}) {
            surface->requested(feature);
            QCOMPARE(features.takeFirst().at(0).toString(), feature);
            QCOMPARE(surface->currentPage(), QStringLiteral("Tools"));
        }
        QVERIFY(tabs.isEmpty());
    }
    void unavailableViewUsesNativeFigmaAlert_data() {
        QTest::addColumn<QSize>("viewportSize");
        QTest::newRow("desktop") << QSize(1440, 900);
        QTest::newRow("compact") << QSize(800, 600);
        QTest::newRow("mobile") << QSize(390, 844);
        QTest::newRow("short") << QSize(360, 320);
    }
    void unavailableViewUsesNativeFigmaAlert() {
        QFETCH(QSize, viewportSize);
        QQuickWindow window; window.resize(viewportSize); window.contentItem()->setSize(viewportSize);
        WorkspaceShell shell(window.contentItem()); shell.setObjectName(QStringLiteral("societyContent"));
        QQuickItem viewport(&shell); viewport.setObjectName(QStringLiteral("dashboardTabViewport"));
        QQuickItem legacyNavigation(&viewport); legacyNavigation.setObjectName(QStringLiteral("dashboardTabs"));
        LegacyNoticeFixture legacy(window.contentItem()); legacy.setObjectName(QStringLiteral("dashboardNotice"));
        legacy.primaryText = QStringLiteral("OK");
        connect(&shell,&WorkspaceShell::featureRequested,&legacy,[&](const QString &feature) {
            legacy.title=feature;
            legacy.message=feature+QStringLiteral(" is not connected to a workspace service yet.");
            legacy.setOpen(true);
        });
        auto *navigation=LegacyNavigationBridge::install(&window);QVERIFY(navigation);
        const auto selected=navigation->currentPage();
        window.contentItem()->setSize({0,0});
        navigation->requested(QStringLiteral("Community"));
        QVERIFY2(legacy.isOpen(),"The legacy notice must remain available when the native surface cannot be shown.");
        legacy.setOpen(false);window.contentItem()->setSize(viewportSize);
        navigation->requested(QStringLiteral("Community"));
        auto *notice = window.findChild<NativeNoticeSurface *>(QStringLiteral("societyNativeNotice"));
        QVERIFY(notice);
        QVERIFY2(notice->ready(),qPrintable(notice->errorString()));
        QVERIFY(notice->isVisible());
        QVERIFY(!legacy.isOpen());
        QCOMPARE(notice->title(), legacy.title);
        QCOMPARE(notice->message(), legacy.message);
        const QRectF bounds = notice->alertBounds();
        QCOMPARE(bounds.width(), qMin(500.0, double(viewportSize.width())-48.0));
        QVERIFY(bounds.height() > 0 && bounds.height() < 379.0);
        QVERIFY(QRectF(QPointF(), window.size()).contains(bounds));
        QCOMPARE(navigation->currentPage(),selected);
        auto *accessible=QAccessible::queryAccessibleInterface(notice);QVERIFY(accessible);
        QCOMPARE(accessible->role(),QAccessible::AlertMessage);
        QCOMPARE(accessible->text(QAccessible::Name),QStringLiteral("Community"));
        const auto point=notice->primaryActionBounds().center();QVERIFY(!notice->primaryActionBounds().isEmpty());
        QMouseEvent press(QEvent::MouseButtonPress,point,point,point,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
        QCoreApplication::sendEvent(notice,&press);
        QMouseEvent release(QEvent::MouseButtonRelease,point,point,point,Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
        QCoreApplication::sendEvent(notice,&release);
        QTRY_VERIFY(!notice->isVisible());
        legacy.title = QStringLiteral("Another view");
        legacy.setOpen(true);
        QVERIFY(notice->isVisible());
        QCOMPARE(notice->title(), legacy.title);
        auto *action=accessible->actionInterface();QVERIFY(action);
        action->doAction(QAccessibleActionInterface::pressAction());
        legacy.title=QStringLiteral("Newest view");legacy.setOpen(true);
        QCoreApplication::processEvents();
        QVERIFY2(notice->isVisible(),"An older queued dismissal hid a newer notice.");
        QCOMPARE(notice->title(),legacy.title);
        QVERIFY(notice->activatePrimary());
        QTRY_VERIFY(!notice->isVisible());
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
    void cutPasteAndImportUseIndependentBackendJobs() {
        // Keep the Unix-domain socket path below macOS's sockaddr_un limit.
        QTemporaryDir fixture(QDir::cleanPath(QStringLiteral(SOCIETY_TEST_DIRECTORY)+"/../../.workspace-jobs-XXXXXX")); QVERIFY(fixture.isValid());
        const auto broker=fixture.filePath("broker"); QVERIFY(QDir().mkdir(broker));
        QLocalServer server; QVERIFY(server.listen(broker+"/backend.sock"));
        const auto previous=qgetenv("SOCIETY_LOCAL_BROKER_DIR"); const bool had=qEnvironmentVariableIsSet("SOCIETY_LOCAL_BROKER_DIR");
        qputenv("SOCIETY_LOCAL_BROKER_DIR",broker.toUtf8());
        const auto restore=qScopeGuard([&] { if (had) qputenv("SOCIETY_LOCAL_BROKER_DIR",previous); else qunsetenv("SOCIETY_LOCAL_BROKER_DIR"); });
        QJsonArray jobs; QByteArray input;
        connect(&server,&QLocalServer::newConnection,this,[&] {
            auto *peer=server.nextPendingConnection();
            connect(peer,&QLocalSocket::readyRead,this,[&,peer] {
                input+=peer->readAll();
                while (input.size()>=4) {
                    const auto size=qFromBigEndian<quint32>(input.constData());
                    if (input.size()<qsizetype(size+4)) break;
                    const auto request=QJsonDocument::fromJson(input.mid(4,size)).object(); input.remove(0,size+4);
                    const auto operation=request.value("op").toString();
                    QJsonObject response{{"type","result"},{"id",request.value("id")},{"accepted",true}};
                    if (operation=="fileOperation") { jobs.append(request); response.insert("jobId",QString::number(jobs.size())); }
                    if (operation=="fileJobStatus") response.insert("job",QJsonObject{{"state","succeeded"}});
                    const auto body=QJsonDocument(response).toJson(QJsonDocument::Compact);
                    QByteArray frame(4,'\0'); qToBigEndian(quint32(body.size()),frame.data()); peer->write(frame+body); peer->flush();
                }
            });
        });
        QQuickWindow window;
        WorkspaceDashboard dashboard(window.contentItem()); dashboard.setObjectName("dashboardView"); dashboard.setSize({1200,760});
        WorkspaceFileModel files; files.containerPath=fixture.filePath("drive.societycontainer"); dashboard.fileModel=&files;
        dashboard.root=files.containerPath+"/Files";
        QQuickItem sidebar(&dashboard); sidebar.setObjectName("dashboardSidebar"); sidebar.setWidth(170);
        QQuickItem content(&dashboard); QQuickItem scroll(&content); scroll.setObjectName("dashboardScroll");
        auto *surface=LegacyWorkspaceBridge::install(&window); QVERIFY(surface);
        QTRY_VERIFY_WITH_TIMEOUT(surface->model()->fileActionsAvailable.get(),10000);
        const auto a=dashboard.root+"/a.txt",b=dashboard.root+"/b.txt";
        surface->model()->setSuggestedFiles({{a.toStdString(),"a.txt","","Text","1 B",false,1,1},
                                             {b.toStdString(),"b.txt","","Text","1 B",false,1,1}});
        surface->model()->gridSelected({a.toStdString(),b.toStdString()});
        surface->model()->cutFiles(); surface->model()->showPaste(); surface->model()->pasteEdited("Files/Target"); surface->model()->pasteFiles();
        QTRY_COMPARE_WITH_TIMEOUT(jobs.size(),2,10000);
        QCOMPARE(jobs[0].toObject().value("action").toString(),QStringLiteral("move"));
        QCOMPARE(jobs[0].toObject().value("path").toString(),QStringLiteral("Files/a.txt"));
        QCOMPARE(jobs[1].toObject().value("path").toString(),QStringLiteral("Files/b.txt"));
        surface->model()->gridSelected({a.toStdString()});
        surface->model()->copyFiles(); surface->model()->showPaste(); surface->model()->pasteEdited("Files/Target"); surface->model()->pasteFiles();
        QTRY_COMPARE_WITH_TIMEOUT(jobs.size(),3,10000);
        QCOMPARE(jobs[2].toObject().value("action").toString(),QStringLiteral("copy"));
        QCOMPARE(jobs[2].toObject().value("path").toString(),a);
        surface->model()->showCopyTo(); surface->model()->moveEdited("Files/Target"); surface->model()->moveSelected();
        QTRY_COMPARE_WITH_TIMEOUT(jobs.size(),4,10000);
        QCOMPARE(jobs[3].toObject().value("action").toString(),QStringLiteral("copy"));
        QCOMPARE(jobs[3].toObject().value("path").toString(),a);
        surface->model()->showImport(); surface->model()->importEdited(fixture.filePath("outside.txt").toStdString());
        surface->model()->importDestinationEdited("Files/Target"); surface->model()->importFile();
        QTRY_COMPARE_WITH_TIMEOUT(jobs.size(),5,10000);
        QCOMPARE(jobs[4].toObject().value("action").toString(),QStringLiteral("copy"));
        QCOMPARE(jobs[4].toObject().value("argument").toString(),QStringLiteral("Files/Target"));
    }
    void openWaitsForTheSelectedDriveDaemon() {
        QTemporaryDir fixture(QDir::cleanPath(QStringLiteral(SOCIETY_TEST_DIRECTORY)+"/../../.workspace-open-XXXXXX")); QVERIFY(fixture.isValid());
        const auto broker=fixture.filePath("broker"); QVERIFY(QDir().mkdir(broker));
        QLocalServer server; QVERIFY(server.listen(broker+"/backend.sock"));
        const auto previous=qgetenv("SOCIETY_LOCAL_BROKER_DIR"); const bool had=qEnvironmentVariableIsSet("SOCIETY_LOCAL_BROKER_DIR");
        qputenv("SOCIETY_LOCAL_BROKER_DIR",broker.toUtf8());
        const auto restore=qScopeGuard([&] { if (had) qputenv("SOCIETY_LOCAL_BROKER_DIR",previous); else qunsetenv("SOCIETY_LOCAL_BROKER_DIR"); });
        QPointer<QLocalSocket> peer; QByteArray input; QJsonArray opens, activations;
        const auto send=[&](const QJsonObject &object) {
            const auto body=QJsonDocument(object).toJson(QJsonDocument::Compact);
            QByteArray frame(4,'\0'); qToBigEndian(quint32(body.size()),frame.data());
            if (peer) { peer->write(frame+body); peer->flush(); }
        };
        connect(&server,&QLocalServer::newConnection,this,[&] {
            peer=server.nextPendingConnection();
            connect(peer,&QLocalSocket::readyRead,this,[&] {
                input+=peer->readAll();
                while (input.size()>=4) {
                    const auto size=qFromBigEndian<quint32>(input.constData());
                    if (input.size()<qsizetype(size+4)) break;
                    const auto request=QJsonDocument::fromJson(input.mid(4,size)).object(); input.remove(0,size+4);
                    const auto operation=request.value("op").toString();
                    QJsonObject response{{"type","result"},{"id",request.value("id")},{"accepted",true}};
                    if (operation=="directoryOpen") {
                        opens.append(request);
                        response.insert("directory",QJsonObject{{"path",request.value("path")},{"generation",QStringLiteral("generation-%1").arg(opens.size())},
                                                               {"revision","revision-1"},{"status","ready"},{"count",1}});
                    } else if (operation=="directoryActivate") activations.append(request);
                    send(response);
                }
            });
        });
        QQuickWindow window;
        WorkspaceDashboard dashboard(window.contentItem()); dashboard.setObjectName("dashboardView"); dashboard.setSize({1200,760});
        WorkspaceFileModel files; files.containerPath=fixture.filePath("drive.societycontainer"); dashboard.fileModel=&files;
        dashboard.root=files.containerPath+"/Files";
        QQuickItem sidebar(&dashboard); sidebar.setObjectName("dashboardSidebar"); sidebar.setWidth(170);
        QQuickItem content(&dashboard); QQuickItem scroll(&content); scroll.setObjectName("dashboardScroll");
        QSignalSpy opened(&dashboard,SIGNAL(fileRequested(QString)));
        auto *surface=LegacyWorkspaceBridge::install(&window); QVERIFY(surface);
        QTRY_VERIFY_WITH_TIMEOUT(surface->model()->fileActionsAvailable.get(),10000);
        const auto path=dashboard.root+"/Drafts/a.txt";
        surface->model()->setSuggestedFiles({{path.toStdString(),"a.txt","","Text","1 B",false,1,1}});
        surface->model()->gridSelected({path.toStdString()}); surface->model()->openSelected();
        QTRY_COMPARE_WITH_TIMEOUT(opens.size(),1,5000);
        QCOMPARE(opens[0].toObject().value("path").toString(),QStringLiteral("Files/Drafts"));
        QTRY_COMPARE_WITH_TIMEOUT(activations.size(),1,5000);
        const auto first=activations[0].toObject();
        QCOMPARE(first.value("path").toString(),QStringLiteral("Files/Drafts/a.txt"));
        QCOMPARE(opened.size(),0);
        files.containerPath=fixture.filePath("other.societycontainer"); emit files.containerPathChanged();
        files.containerPath=fixture.filePath("drive.societycontainer"); emit files.containerPathChanged();
        send({{"type","directoryActivated"},{"id",first.value("id")},{"generation",first.value("generation")},{"path",path}});
        QTest::qWait(50); QCOMPARE(opened.size(),0);
        surface->model()->setSuggestedFiles({{path.toStdString(),"a.txt","","Text","1 B",false,1,1}});
        surface->model()->gridSelected({path.toStdString()}); surface->model()->openSelected();
        QTRY_COMPARE_WITH_TIMEOUT(activations.size(),2,5000);
        const auto second=activations[1].toObject();
        send({{"type","directoryActivated"},{"id",first.value("id")},{"generation",first.value("generation")},{"path",path}});
        QTest::qWait(50); QCOMPARE(opened.size(),0);
        send({{"type","directoryActivated"},{"id",second.value("id")},{"generation",second.value("generation")},{"path",path}});
        QTRY_COMPARE_WITH_TIMEOUT(opened.size(),1,5000);
        QCOMPARE(opened.first().first().toString(),path);
        QVERIFY(surface->model()->status.get()!="Preparing file…");
    }
    void cutKeepsFailedAndPendingFilesUntilCompletion() {
        QTemporaryDir fixture(QDir::cleanPath(QStringLiteral(SOCIETY_TEST_DIRECTORY)+"/../../.workspace-cut-XXXXXX")); QVERIFY(fixture.isValid());
        const auto broker=fixture.filePath("broker"); QVERIFY(QDir().mkdir(broker));
        QLocalServer server; QVERIFY(server.listen(broker+"/backend.sock"));
        const auto previous=qgetenv("SOCIETY_LOCAL_BROKER_DIR"); const bool had=qEnvironmentVariableIsSet("SOCIETY_LOCAL_BROKER_DIR");
        qputenv("SOCIETY_LOCAL_BROKER_DIR",broker.toUtf8());
        const auto restore=qScopeGuard([&] { if (had) qputenv("SOCIETY_LOCAL_BROKER_DIR",previous); else qunsetenv("SOCIETY_LOCAL_BROKER_DIR"); });
        const auto jobB=QUuid::createUuid().toString(QUuid::WithoutBraces),jobA=QUuid::createUuid().toString(QUuid::WithoutBraces);
        QPointer<QLocalSocket> peer; QByteArray input; QJsonArray moves, polls;
        const auto send=[&](const QJsonObject &object) {
            const auto body=QJsonDocument(object).toJson(QJsonDocument::Compact);
            QByteArray frame(4,'\0'); qToBigEndian(quint32(body.size()),frame.data());
            if (peer) { peer->write(frame+body); peer->flush(); }
        };
        connect(&server,&QLocalServer::newConnection,this,[&] {
            peer=server.nextPendingConnection();
            connect(peer,&QLocalSocket::readyRead,this,[&] {
                input+=peer->readAll();
                while (input.size()>=4) {
                    const auto size=qFromBigEndian<quint32>(input.constData());
                    if (input.size()<qsizetype(size+4)) break;
                    const auto request=QJsonDocument::fromJson(input.mid(4,size)).object(); input.remove(0,size+4);
                    const auto operation=request.value("op").toString();
                    QJsonObject response{{"type","result"},{"id",request.value("id")},{"accepted",true}};
                    if (operation=="fileOperation") {
                        moves.append(request);
                        if (moves.size()==1) { response.insert("accepted",false); response.insert("error","Destination temporarily unavailable"); }
                        else if (moves.size()==4) { peer->disconnectFromServer(); return; }
                        else response.insert("jobId",moves.size()==2?jobB:jobA);
                    } else if (operation=="fileJobStatus") {
                        polls.append(request);
                        if (polls.size()==1) { response.insert("accepted",false); response.insert("error","The file job was not found."); }
                        else response.insert("job",QJsonObject{{"state","running"}});
                    }
                    send(response);
                }
            });
        });
        QQuickWindow window;
        WorkspaceDashboard dashboard(window.contentItem()); dashboard.setObjectName("dashboardView"); dashboard.setSize({1200,760});
        WorkspaceFileModel files; files.containerPath=fixture.filePath("drive.societycontainer"); dashboard.fileModel=&files;
        dashboard.root=files.containerPath+"/Files";
        QQuickItem sidebar(&dashboard); sidebar.setObjectName("dashboardSidebar"); sidebar.setWidth(170);
        QQuickItem content(&dashboard); QQuickItem scroll(&content); scroll.setObjectName("dashboardScroll");
        auto *surface=LegacyWorkspaceBridge::install(&window); QVERIFY(surface);
        QTRY_VERIFY_WITH_TIMEOUT(surface->model()->fileActionsAvailable.get(),10000);
        const auto a=dashboard.root+"/a.txt",b=dashboard.root+"/b.txt";
        surface->model()->setSuggestedFiles({{a.toStdString(),"a.txt","","Text","1 B",false,1,1},
                                             {b.toStdString(),"b.txt","","Text","1 B",false,1,1}});
        surface->model()->gridSelected({a.toStdString(),b.toStdString()}); surface->model()->cutFiles();
        surface->model()->showPaste(); surface->model()->pasteEdited("Files/Target"); surface->model()->pasteFiles();
        QTRY_COMPARE_WITH_TIMEOUT(moves.size(),2,5000);
        QTRY_VERIFY_WITH_TIMEOUT(surface->model()->hasOperationError.get() &&
            surface->model()->operationStatus.get().find("queued")!=std::string::npos,5000);
        QCOMPARE(polls.size(),0);
        surface->model()->refreshFiles();
        QTRY_COMPARE_WITH_TIMEOUT(polls.size(),1,5000);
        QTRY_VERIFY_WITH_TIMEOUT(surface->model()->operationStatus.get().find("not available yet")!=std::string::npos,5000);
        QVERIFY(surface->model()->operationError.get().find("Could not check job")==std::string::npos);
        auto paths=[] { QStringList result; if (const auto *mime=QGuiApplication::clipboard()->mimeData())
            for (const auto &url:mime->urls()) result.push_back(url.toLocalFile()); return result; };
        QCOMPARE(paths(),QStringList({a,b}));
        send({{"type","fileJobChanged"},{"jobId",jobB},{"container",files.containerPath},{"state","finished"},{"path","Files/Target/b.txt"},{"error",""}});
        QTRY_COMPARE_WITH_TIMEOUT(paths(),QStringList({a}),5000);
        surface->model()->showPaste(); surface->model()->pasteEdited("Files/Target"); surface->model()->pasteFiles();
        QTRY_COMPARE_WITH_TIMEOUT(moves.size(),3,5000);
        QCOMPARE(moves[2].toObject().value("path").toString(),QStringLiteral("Files/a.txt"));
        QTRY_VERIFY_WITH_TIMEOUT(surface->model()->operationStatus.get().find("queued")!=std::string::npos,5000);
        send({{"type","fileJobChanged"},{"jobId",jobA},{"container",files.containerPath},{"state","finished"},{"path","Files/Target/a.txt"},{"error",""}});
        QTRY_VERIFY_WITH_TIMEOUT(paths().isEmpty(),5000);
        surface->model()->gridSelected({a.toStdString()}); surface->model()->cutFiles();
        surface->model()->showPaste(); surface->model()->pasteEdited("Files/Target"); surface->model()->pasteFiles();
        QTRY_COMPARE_WITH_TIMEOUT(moves.size(),4,5000);
        QTRY_VERIFY_WITH_TIMEOUT(surface->model()->fileActionsAvailable.get() &&
            surface->model()->operationError.get().find("Connection interrupted")!=std::string::npos,10000);
        QCOMPARE(paths(),QStringList({a}));
        surface->model()->showPaste(); surface->model()->pasteEdited("Files/Target"); surface->model()->pasteFiles();
        QTest::qWait(100); QCOMPARE(moves.size(),4);
        QVERIFY(surface->model()->operationError.get().find("uncertain outcome")!=std::string::npos);
        surface->model()->setSuggestedFiles({{a.toStdString(),"a.txt","","Text","1 B",false,1,1}});
        surface->model()->gridSelected({a.toStdString()}); surface->model()->cutFiles();
        surface->model()->showPaste(); surface->model()->pasteEdited("Files/Target"); surface->model()->pasteFiles();
        QTRY_COMPARE_WITH_TIMEOUT(moves.size(),5,5000);
        QTRY_VERIFY_WITH_TIMEOUT(surface->model()->operationStatus.get().find("queued")!=std::string::npos,5000);
        send({{"type","fileJobChanged"},{"jobId",jobA},{"container",files.containerPath},{"state","finished"},{"path","Files/Target/a.txt"},{"error",""}});
        QTRY_VERIFY_WITH_TIMEOUT(paths().isEmpty(),5000);
    }
};

QTEST_MAIN(NativeWorkspaceTest)
#include "tst_native_workspace.moc"
