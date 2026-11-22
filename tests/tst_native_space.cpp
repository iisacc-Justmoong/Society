#include "Society/Space/NativeSpaceSurface.h"
#include "Society/Space/SpaceOrbGaze.h"
#include "Society/Space/SpaceDictation.h"
#include "Society/Space/NativeSpaceViewModel.h"
#include "Society/Native/LegacyNavigationBridge.h"
#include "Society/Native/NativeNavigationSurface.h"
#include <SpaceStore.h>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QQuickWindow>
#include <QMouseEvent>
#include <QHoverEvent>
#include <QKeyEvent>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocalServer>
#include <QLocalSocket>
#include <QPainter>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QtTest>
#include <QtEndian>
#include <cmath>
#include <limits>

/// Minimal shell and drive contracts for exercising the real navigation adapter.
class SpaceTestDrive final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString rootPath MEMBER rootPath NOTIFY locationChanged)
    Q_PROPERTY(bool contentsAvailable MEMBER contentsAvailable NOTIFY contentsChanged)
public:
    QString rootPath;
    bool contentsAvailable = false;
signals:
    void locationChanged();
    void contentsChanged();
};
class SpaceTestShell final : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(QString selectedTab MEMBER selectedTab NOTIFY selectedTabChanged)
    Q_PROPERTY(QObject *drive MEMBER drive)
public:
    using QQuickItem::QQuickItem;
    QString selectedTab = QStringLiteral("Dashboard");
    QObject *drive = nullptr;
signals:
    void selectedTabChanged();
    void tabRequested(const QString &page);
};

/// Deterministic speech boundary; late events exercise session-lifetime fencing.
class SpaceTestDictation final : public SpaceDictation {
public:
    Update update; int cancelled=0, stopped=0;
    void start(Update callback) override { update=std::move(callback); }
    void stop() override { ++stopped; }
    void cancel() override { ++cancelled; }
};

/// Exercise the Space view against a real, isolated SDK store and its retained LVUI surface.
class NativeSpaceTest final : public QObject {
    Q_OBJECT
private:
    static QString container(const QTemporaryDir &fixture, const QString &name) {
        const QString path = fixture.filePath(name + QStringLiteral(".societycontainer"));
        if (!QDir().mkpath(path)) return {};
        QFile manifest(path + QStringLiteral("/.society-drive.json"));
        if (!manifest.open(QIODevice::WriteOnly)) return {};
        manifest.write(R"({"type":"SocietyDrive","schemaVersion":1,"identifier":"11111111-2222-4333-8444-555555555555"})");
        return path;
    }
private slots:
    /// The native prompt sends its current session, attachment and budget to the service.
    void composerConnectsToScopedAgent() {
        QTemporaryDir fixture(QDir::temp().filePath("soc-XXXXXX")); QVERIFY(fixture.isValid()); const auto root=container(fixture,"agent");
        auto db=iiSocietyContainer::SpaceStore::open(root.toStdString()); QVERIFY(db);
        QVERIFY(db->createSession({"conversation","Conversation","","active"}));
        const auto broker=fixture.path();
        QLocalServer server; QVERIFY(server.listen(broker+"/backend.sock"));
        const auto previous=qgetenv("SOCIETY_LOCAL_BROKER_DIR"), previousAuto=qgetenv("SOCIETY_DISABLE_BACKEND_AUTOSTART");
        const bool had=qEnvironmentVariableIsSet("SOCIETY_LOCAL_BROKER_DIR"), hadAuto=qEnvironmentVariableIsSet("SOCIETY_DISABLE_BACKEND_AUTOSTART");
        const auto restore=qScopeGuard([&]{
            if(had)qputenv("SOCIETY_LOCAL_BROKER_DIR",previous);else qunsetenv("SOCIETY_LOCAL_BROKER_DIR");
            if(hadAuto)qputenv("SOCIETY_DISABLE_BACKEND_AUTOSTART",previousAuto);else qunsetenv("SOCIETY_DISABLE_BACKEND_AUTOSTART");
        });
        qputenv("SOCIETY_LOCAL_BROKER_DIR",broker.toUtf8());
        qputenv("SOCIETY_DISABLE_BACKEND_AUTOSTART","1");
        NativeSpaceSurface surface; surface.setSize({1100,700}); QVERIFY(surface.openContainer(root));
        QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(),5000); auto *peer=server.nextPendingConnection(); QVERIFY(peer);
        QByteArray input; QList<QJsonObject> received;
        const auto find=[&](const QString &operation) {
            input+=peer->readAll();
            while (input.size()>=4) {
                const auto n=qFromBigEndian<quint32>(input.constData()); if(input.size()<qsizetype(n+4)) break;
                received.append(QJsonDocument::fromJson(input.mid(4,n)).object()); input.remove(0,n+4);
            }
            for (auto i=received.crbegin();i!=received.crend();++i) if(i->value("op")==operation) return *i;
            return QJsonObject{};
        };
        const auto send=[&](const QJsonObject &object) {
            const auto body=QJsonDocument(object).toJson(QJsonDocument::Compact); QByteArray frame(4,'\0');
            qToBigEndian(quint32(body.size()),frame.data()); peer->write(frame+body); peer->flush();
        };
        QJsonObject subscription; QTRY_VERIFY_WITH_TIMEOUT(!(subscription=find("spaceSubscribe")).isEmpty(),5000);
        QCOMPARE(subscription.value("spaceId").toString(),QStringLiteral("conversation"));
        const auto token=subscription.value("subscription").toString();
        const QJsonObject ready{{"loaded",true},{"available",true},{"busy",false},{"status","Ready"},{"messages",QJsonArray{}}};
        send({{"type","result"},{"id",subscription.value("id")},{"accepted",true},{"space",ready}});
        auto *vm=surface.model(); QTRY_VERIFY_WITH_TIMEOUT(vm->agentAvailable.get(),5000);
        vm->addAttachments({"/tmp/example.png"}); vm->promptEdited("Explore this"); surface.refresh();
        QVERIFY(surface.controlEnabled(QStringLiteral("space-add-attachment")));
        QVERIFY(surface.controlEnabled(QStringLiteral("space-microphone")));
        QVERIFY(surface.activateControl(QStringLiteral("space-reasoning")));
        QVERIFY(surface.activateControl(QStringLiteral("space-reasoning-high")));
        const auto prompt=surface.controlBounds(QStringLiteral("space-agent-prompt")); QVERIFY(!prompt.isEmpty());
        QMouseEvent press(QEvent::MouseButtonPress,prompt.center(),prompt.center(),prompt.center(),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
        QMouseEvent release(QEvent::MouseButtonRelease,prompt.center(),prompt.center(),prompt.center(),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
        QCoreApplication::sendEvent(&surface,&press); QCoreApplication::sendEvent(&surface,&release);
        QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier); QCoreApplication::sendEvent(&surface,&enter);
        QJsonObject turn; QTRY_VERIFY_WITH_TIMEOUT(!(turn=find("spaceSend")).isEmpty(),5000);
        QCOMPARE(turn.value("spaceId").toString(),QStringLiteral("conversation"));
        QCOMPARE(turn.value("prompt").toString(),QStringLiteral("Explore this")); QCOMPARE(turn.value("effort").toString(),QStringLiteral("high"));
        QCOMPARE(turn.value("attachments").toArray(),QJsonArray{QStringLiteral("/tmp/example.png")});
        auto busy=ready; busy.insert("busy",true); busy.insert("available",false); busy.insert("status","Thinking…");
        send({{"type","spaceState"},{"container",root},{"spaceId","conversation"},{"subscription",token},{"space",busy}});
        QTRY_VERIFY_WITH_TIMEOUT(vm->agentBusy.get(),5000);
        auto completed=ready; completed.insert("messages",QJsonArray{QJsonObject{{"id","reply"},{"role","Agent"},{"text","A response"}}});
        send({{"type","spaceState"},{"container",root},{"spaceId","conversation"},{"subscription",token},{"space",completed}});
        QTRY_VERIFY_WITH_TIMEOUT(vm->promptDraft.get().empty(),5000);
        QCOMPARE(vm->messages.get().size(),std::size_t(1)); QCOMPARE(vm->messages.get().front().detail,std::string("A response"));
        QVERIFY(vm->attachments.get().empty());
        surface.setSize({700,700}); surface.refresh();
        QVERIFY2(!surface.controlBounds(QStringLiteral("space-compact-conversation")).isEmpty(),qPrintable(surface.errorString()));
    }

    /// Dictation edits only its original session and is cancelled when switching sessions.
    void dictationPreservesSessionAndIgnoresLateSpeech() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid());
        const auto root=container(fixture,"dictation");
        auto db=iiSocietyContainer::SpaceStore::open(root.toStdString()); QVERIFY(db);
        QVERIFY(db->createSession({"one","One","","active"}));
        QVERIFY(db->createSession({"two","Two","","active"}));
        NativeSpaceSurface surface; surface.setSize({1100,700}); QVERIFY(surface.openContainer(root));
        auto fake=std::make_unique<SpaceTestDictation>(); auto *speech=fake.get();
        surface.setDictationProvider(std::move(fake));
        auto *vm=surface.model(); vm->selectSession("one"); vm->promptEdited("Draft"); surface.refresh();
        QVERIFY(surface.activateControl(QStringLiteral("space-microphone")));
        QVERIFY(vm->recording.get()); QVERIFY(bool(speech->update));
        speech->update("spoken words",false,{}); QCOMPARE(vm->promptDraft.get(),std::string("Draft spoken words"));
        const auto stale=speech->update; vm->selectSession("two"); QVERIFY(!vm->recording.get()); QVERIFY(speech->cancelled>0);
        stale("late words",true,{}); QVERIFY(vm->promptDraft.get().empty());
        vm->selectSession("one"); QCOMPARE(vm->promptDraft.get(),std::string("Draft spoken words"));
    }

    /// Attachments, effort and a failed request remain scoped to the selected session.
    void composerSubmitsScopedDraftAndKeepsFailures() {
        NativeSpaceViewModel vm;
        iiSocietyContainer::SpaceGraph one; one.session={"one","One","","active"};
        iiSocietyContainer::SpaceGraph two; two.session={"two","Two","","active"};
        const std::vector<iiSocietyContainer::SpaceSession> sessions{one.session,two.session};
        vm.setSnapshot(sessions,one);
        vm.setAgentAvailable(true);
        vm.promptEdited("What is this?");
        vm.addAttachments({"/tmp/reference.png"});
        vm.selectReasoning("high");
        bool called=false; std::string submitted, strength; std::vector<std::string> attached;
        vm.onSend=[&](const std::string &prompt,const std::vector<std::string> &paths,const std::string &effort) {
            called=true; submitted=prompt; attached=paths; strength=effort;
            return false;
        };
        vm.send(); QVERIFY(called); QCOMPARE(submitted,std::string("What is this?"));
        QCOMPARE(attached,std::vector<std::string>({"/tmp/reference.png"})); QCOMPARE(strength,std::string("high"));
        QCOMPARE(vm.promptDraft.get(),std::string("What is this?"));
        QCOMPARE(vm.attachments.get().size(),std::size_t(1));
        vm.setSnapshot(sessions,two); QVERIFY(vm.promptDraft.get().empty()); QVERIFY(vm.attachments.get().empty());
        vm.setSnapshot(sessions,one); QCOMPARE(vm.promptDraft.get(),std::string("What is this?"));
        QCOMPARE(vm.attachments.get().size(),std::size_t(1));
        vm.setAgentAvailable(true); vm.onSend=[](const auto &,const auto &,const auto &){return true;};
        vm.send(); vm.applyAgentState({},"Thinking",false,true,false);
        vm.applyAgentState({},"Failed",true,false,true);
        QCOMPARE(vm.promptDraft.get(),std::string("What is this?")); QCOMPARE(vm.attachments.get().size(),std::size_t(1));
        vm.send(); vm.applyAgentState({},"Thinking",false,true,false);
        vm.promptEdited("A new draft"); vm.applyAgentState({},"Ready",true,false,false);
        QCOMPARE(vm.promptDraft.get(),std::string("A new draft")); QVERIFY(vm.attachments.get().empty());
    }

    /// The conversation composer exposes only the requested controls and keeps a local draft.
    void compactComposerAndSessionManager() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid());
        const auto root = container(fixture, "composer"); QVERIFY(!root.isEmpty());
        NativeSpaceSurface surface; surface.setSize({1100, 700});
        QVERIFY2(surface.ready(), qPrintable(surface.errorString()));
        QVERIFY2(surface.errorString().isEmpty(), qPrintable(surface.errorString()));
        QVERIFY(surface.openContainer(root));
        auto *vm = surface.model(); QVERIFY(vm);
        QVERIFY(!vm->sessionManagerVisible.get());
        const auto composer = surface.controlBounds(QStringLiteral("space-conversation"));
        const auto prompt = surface.controlBounds(QStringLiteral("space-agent-prompt"));
        const auto attach = surface.controlBounds(QStringLiteral("space-add-attachment"));
        const auto reasoning = surface.controlBounds(QStringLiteral("space-reasoning"));
        const auto mic = surface.controlBounds(QStringLiteral("space-microphone"));
        QVERIFY(!composer.isEmpty()); QVERIFY(!prompt.isEmpty()); QVERIFY(!attach.isEmpty());
        QVERIFY(!reasoning.isEmpty()); QVERIFY(!mic.isEmpty());
        QVERIFY(composer.contains(prompt)); QVERIFY(composer.contains(attach));
        QVERIFY(composer.contains(reasoning)); QVERIFY(composer.contains(mic));
        QVERIFY(composer.bottom() <= surface.height() + 1);
        QVERIFY(attach.right() <= reasoning.left()); QVERIFY(reasoning.right() <= mic.left());
        QVERIFY(surface.controlEnabled(QStringLiteral("space-agent-prompt")));
        QVERIFY(!surface.controlEnabled(QStringLiteral("space-add-attachment")));
        QVERIFY(!surface.controlEnabled(QStringLiteral("space-reasoning")));
        QVERIFY(!surface.controlEnabled(QStringLiteral("space-microphone")));
        QVERIFY(surface.controlBounds(QStringLiteral("space-agent-send")).isEmpty());
        QVERIFY(surface.controlBounds(QStringLiteral("space-agent-pause")).isEmpty());

        QMouseEvent press(QEvent::MouseButtonPress, prompt.center(), prompt.center(), prompt.center(), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QMouseEvent release(QEvent::MouseButtonRelease, prompt.center(), prompt.center(), prompt.center(), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QCoreApplication::sendEvent(&surface, &press); QCoreApplication::sendEvent(&surface, &release);
        QKeyEvent type(QEvent::KeyPress, Qt::Key_H, Qt::NoModifier, QStringLiteral("h"));
        QCoreApplication::sendEvent(&surface, &type);
        QCOMPARE(vm->promptDraft.get(), std::string("h"));
        QVERIFY(surface.activateControl(QStringLiteral("space-sessions-toggle")));
        QVERIFY(vm->sessionManagerVisible.get());
        QVERIFY(!surface.controlBounds(QStringLiteral("space-new-session")).isEmpty());
        QVERIFY(surface.activateControl(QStringLiteral("space-sessions-toggle")));
        QVERIFY(!vm->sessionManagerVisible.get());
        QCOMPARE(vm->promptDraft.get(), std::string("h"));
        surface.setSize({700, 600}); surface.refresh();
        QVERIFY(!vm->panelVisible.get());
        QVERIFY(!surface.controlBounds(QStringLiteral("space-agent-prompt")).isEmpty());
        auto bottom = surface.controlBounds(QStringLiteral("space-conversation")).bottom();
        QVERIFY2(bottom <= surface.height() + 1, qPrintable(QStringLiteral("Composer bottom %1 exceeds %2").arg(bottom).arg(surface.height())));
        QVERIFY(surface.activateControl(QStringLiteral("space-sessions-toggle")));
        bottom = surface.controlBounds(QStringLiteral("space-conversation")).bottom();
        QVERIFY2(bottom <= surface.height() + 1, qPrintable(QStringLiteral("Expanded composer bottom %1 exceeds %2").arg(bottom).arg(surface.height())));
        QCOMPARE(vm->promptDraft.get(), std::string("h"));
        surface.clearContainer();
        QVERIFY(vm->promptDraft.get().empty());
    }

    /// Verify the gaze direction and SVG layers without relying on a physical pointer.
    void gazeIsDirectionalBoundedAndKeepsTheFaceStill() {
        const QRectF bounds(30, 40, 168, 168);
        const auto center = bounds.center();
        QCOMPARE(SpaceOrbGaze::offsetForPointer(center, bounds), QPointF());
        const auto right = SpaceOrbGaze::offsetForPointer(center + QPointF(10000, 0), bounds);
        const auto left = SpaceOrbGaze::offsetForPointer(center + QPointF(-10000, 0), bounds);
        const auto up = SpaceOrbGaze::offsetForPointer(center + QPointF(0, -10000), bounds);
        const auto diagonal = SpaceOrbGaze::offsetForPointer(center + QPointF(10000, 10000), bounds);
        QVERIFY(right.x() > 0 && right.y() == 0);
        QVERIFY(left.x() < 0 && left.y() == 0);
        QVERIFY(up.y() < 0 && up.x() == 0);
        QVERIFY(diagonal.x() > 0 && diagonal.y() > 0);
        QVERIFY(std::hypot(diagonal.x(), diagonal.y()) <= bounds.width() * .08 + 1e-6);
        const auto raster = SpaceOrbGaze::rasterOffset(diagonal, bounds, QSize(504, 504));
        QVERIFY(std::hypot(raster.x(), raster.y()) <= 504 * .08);
        QCOMPARE(SpaceOrbGaze::offsetForPointer(center, {}), QPointF());
        QCOMPARE(SpaceOrbGaze::offsetForPointer(QPointF(std::numeric_limits<double>::infinity(), 0), bounds), QPointF());

        // Constructing the surface registers the packaged SVG resource.
        NativeSpaceSurface surface; QVERIFY2(surface.ready(), qPrintable(surface.errorString()));
        QFile file(QStringLiteral(":/society/space/minimal-cyan-orb.svg")); QVERIFY(file.open(QIODevice::ReadOnly));
        const auto gaze = SpaceOrbGaze::create(file.readAll(), 504);
        QVERIFY2(gaze.has_value(), gaze ? "" : qPrintable(gaze.error()));
        const auto neutral = gaze->imageAt({0, 0}); QVERIFY(neutral.has_value());
        const auto moved = gaze->imageAt({24, 0}); QVERIFY(moved.has_value());
        auto pixel = [](const auto &image, int x, int y, int channel) {
            return image->rgba()[(std::size_t(y) * image->width() + x) * 4 + channel];
        };
        QVERIFY(pixel(*neutral, 140, 252, 0) > 180);
        QVERIFY(pixel(*moved, 140, 252, 0) < 40);
        QVERIFY(pixel(*neutral, 195, 252, 0) < 40);
        QVERIFY(pixel(*moved, 195, 252, 0) > 180);
        QVERIFY(pixel(*neutral, 325, 252, 0) > 180);
        QVERIFY(pixel(*moved, 325, 252, 0) < 40);
        QVERIFY(pixel(*neutral, 380, 252, 0) < 40);
        QVERIFY(pixel(*moved, 380, 252, 0) > 180);
        for (const auto point : {QPoint(252, 252), QPoint(252, 4), QPoint(4, 252), QPoint(499, 252)})
            for (int channel = 0; channel < 4; ++channel)
                QCOMPARE(pixel(*neutral, point.x(), point.y(), channel), pixel(*moved, point.x(), point.y(), channel));
        QVERIFY(!SpaceOrbGaze::create(QByteArrayLiteral("<svg/>"), 504).has_value());
    }

    /// Verify that a host pointer event changes the painted view and can restore the original.
    void hoverMovesBothEyesAndReturnsToNeutral() {
        NativeSpaceSurface surface; surface.setSize({1100, 700});
        QVERIFY2(surface.ready(), qPrintable(surface.errorString()));
        const auto orb = surface.controlBounds(QStringLiteral("space-orb")); QVERIFY(!orb.isEmpty());
        const auto original = surface.model()->orbImage.get(); QVERIFY(original);
        QImage before(1100, 700, QImage::Format_ARGB32_Premultiplied); before.fill(Qt::transparent);
        { QPainter painter(&before); surface.paint(&painter); }
        const auto point = orb.center() + QPointF(500, 200);
        QHoverEvent move(QEvent::HoverMove, point, point, orb.center());
        QCoreApplication::sendEvent(&surface, &move);
        const auto changed = surface.model()->orbImage.get(); QVERIFY(changed);
        QVERIFY2(changed != original, "Pointer motion must update the actual LVUI image.");
        QVERIFY(!std::equal(changed->rgba().begin(), changed->rgba().end(), original->rgba().begin()));
        QImage after(1100, 700, QImage::Format_ARGB32_Premultiplied); after.fill(Qt::transparent);
        { QPainter painter(&after); surface.paint(&painter); }
        bool repainted = false;
        const auto area = orb.toAlignedRect().intersected(before.rect());
        for (int y = area.top(); y <= area.bottom() && !repainted; ++y)
            for (int x = area.left(); x <= area.right(); ++x)
                if (before.pixel(x, y) != after.pixel(x, y)) { repainted = true; break; }
        QVERIFY2(repainted, "Pointer motion must repaint the eyes in the visible LVUI frame.");
        QHoverEvent center(QEvent::HoverMove, orb.center(), orb.center(), point);
        QCoreApplication::sendEvent(&surface, &center);
        QCOMPARE(surface.model()->orbImage.get(), original);
    }

    /// Verify global sampling, unavailable pointers, hidden views and minimized windows.
    void gazeSamplingStopsWhenSpaceOrWindowIsHidden() {
        QQuickWindow window; window.resize(640, 480);
        NativeSpaceSurface surface(window.contentItem()); surface.setSize({640, 480});
        const auto orb = surface.controlBounds(QStringLiteral("space-orb")); QVERIFY(!orb.isEmpty());
        QPoint cursor = window.mapToGlobal((orb.center() + QPointF(800, 0)).toPoint());
        int samples = 0;
        bool pointerAvailable = true;
        surface.setGlobalPointerProvider([&]() -> std::optional<QPoint> {
            ++samples; return pointerAvailable ? std::optional<QPoint>(cursor) : std::nullopt;
        });
        QCOMPARE(samples, 0);
        window.show(); QCoreApplication::processEvents();
        QTRY_VERIFY_WITH_TIMEOUT(samples > 0, 1000);
        const auto rightGaze = surface.model()->orbImage.get(); QVERIFY(rightGaze);
        cursor = window.mapToGlobal((orb.center() + QPointF(-800, 0)).toPoint());
        pointerAvailable = false; QTest::qWait(120);
        QCOMPARE(surface.model()->orbImage.get(), rightGaze);
        pointerAvailable = true;
        QTRY_VERIFY_WITH_TIMEOUT(surface.model()->orbImage.get() != rightGaze, 1000);
        surface.setVisible(false);
        const auto hiddenSamples = samples; QTest::qWait(120);
        QCOMPARE(samples, hiddenSamples);
        surface.setVisible(true);
        QTRY_VERIFY_WITH_TIMEOUT(samples > hiddenSamples, 1000);
        window.setVisibility(QWindow::Minimized);
        const auto minimizedSamples = samples; QTest::qWait(120);
        QCOMPARE(samples, minimizedSamples);
        window.setVisibility(QWindow::Windowed);
        QTRY_VERIFY_WITH_TIMEOUT(samples > minimizedSamples, 1000);
        window.hide();
        const auto closedSamples = samples; QTest::qWait(120);
        QCOMPARE(samples, closedSamples);
    }

    void persistedGraphAndSelection() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid());
        const auto root = container(fixture, "first"); QVERIFY(!root.isEmpty());
        auto store = iiSocietyContainer::SpaceStore::open(root.toStdString()); QVERIFY(store);
        QVERIFY(store->createSession({"s1", "Research", "Find connections", "active"}));
        QVERIFY(store->createSession({"s2", "Other", "", "paused"}));
        QVERIFY(store->createIdea({"a", "s1", "First idea", "Saved content", "Source", "Unverified", 1}));
        QVERIFY(store->createIdea({"b", "s1", "Second idea", "Another idea", "", "", 1}));
        QVERIFY(store->createConnection({"edge", "s1", "a", "b", "derived", "Evidence"}));
        QVERIFY(store->createAgent({"agent", "s1", "Researcher", "idle"}));
        QVERIFY(store->recordActivity({"event", "s1", "agent", "a", "review", "Saved action", "2026-11-21"}));

        NativeSpaceSurface surface; surface.setSize({1440, 810});
        QVERIFY2(surface.ready(), qPrintable(surface.errorString()));
        QVERIFY2(surface.openContainer(root), qPrintable(surface.errorString()));
        surface.refresh();
        auto *vm = surface.model(); QVERIFY(vm);
        QImage frame(1440, 810, QImage::Format_ARGB32_Premultiplied); frame.fill(Qt::transparent);
        { QPainter painter(&frame); surface.paint(&painter); }
        QVERIFY2(frame.pixelColor(4, 4).alpha() == 255, "Space must cover the previously selected page.");
        const auto orb = vm->orbImage.get(); QVERIFY(orb);
        QCOMPARE(orb->width(), std::uint32_t(504)); QCOMPARE(orb->height(), std::uint32_t(504));
        const auto pixels = orb->rgba();
        const auto center = (std::size_t(252) * orb->width() + 252) * 4;
        const auto eye = (std::size_t(252) * orb->width() + 160) * 4;
        QVERIFY2(pixels[center] < 40 && pixels[center + 2] > 90, "The SVG body must retain its blue fill.");
        QVERIFY2(pixels[eye] > 180 && pixels[eye + 1] > 220, "The SVG eye must retain its cyan fill.");
        QCOMPARE(vm->sessions.get().size(), std::size_t(2));
        QCOMPARE(vm->ideaCount.get(), 2);
        QCOMPARE(vm->connectionCount.get(), 1);
        const auto links = vm->connectionsImage.get(); QVERIFY2(links, qPrintable(surface.errorString()));
        const auto linePixels = links->rgba();
        bool hasInk = false;
        for (std::size_t i = 3; i < linePixels.size(); i += 4) if (linePixels[i]) { hasInk = true; break; }
        QVERIFY2(hasInk, "A saved relationship must produce visible connection ink.");
        QCOMPARE(vm->activities.get().size(), std::size_t(1));
        QVERIFY(!surface.controlBounds(QStringLiteral("space-orb")).isEmpty());
        QVERIFY(surface.activateControl(QStringLiteral("space-sessions-toggle")));
        const auto sessionBounds = surface.controlBounds(QStringLiteral("space-session"));
        QVERIFY(!sessionBounds.isEmpty());
        QVERIFY2(sessionBounds.bottom() <= surface.height() + 1, "Session controls must fit in the view.");
        QVERIFY2(!surface.controlBounds(QStringLiteral("space-agent-panel")).isEmpty(), qPrintable(surface.errorString()));
        QVERIFY(surface.controlBounds(QStringLiteral("space-agent-send")).isEmpty());
        const auto idea = surface.keyedControlBounds(QStringLiteral("space-left-nodes"), QStringLiteral("a"), QStringLiteral("space-idea-button"));
        QVERIFY2(!idea.isEmpty(), qPrintable(surface.errorString()));
        QMouseEvent press(QEvent::MouseButtonPress, idea.center(), idea.center(), idea.center(), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QMouseEvent release(QEvent::MouseButtonRelease, idea.center(), idea.center(), idea.center(), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QCoreApplication::sendEvent(&surface, &press); QCoreApplication::sendEvent(&surface, &release);
        QVERIFY2(vm->hasSelection.get(), "Selecting a visible idea must open its inspector.");
        QVERIFY2(!surface.controlBounds(QStringLiteral("space-selected-title")).isEmpty(), qPrintable(surface.errorString()));
        QVERIFY(!surface.controlEnabled(QStringLiteral("space-idea-merge")));
        QCOMPARE(vm->selectedTitle.get(), std::string("First idea"));
        QCOMPARE(vm->selectedContent.get(), std::string("Saved content"));
        QCOMPARE(vm->selectedProvenance.get(), std::string("Source"));
        QCOMPARE(vm->selectedEvaluation.get(), std::string("Unverified"));
        QCOMPARE(vm->relationships.get().size(), std::size_t(1));
        vm->promptEdited("session one draft");
        vm->selectSession("s2"); surface.refresh();
        QCOMPARE(vm->currentSessionId.get(), std::string("s2"));
        QVERIFY(vm->promptDraft.get().empty());
        QCOMPARE(vm->ideaCount.get(), 0);
        QVERIFY(!vm->hasSelection.get());
        vm->promptEdited("session two draft");
        vm->selectSession("s1"); surface.refresh();
        QCOMPARE(vm->promptDraft.get(), std::string("session one draft"));
    }

    void navigationShowsSpaceAndReturnsToWorkspace() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid());
        const auto first = container(fixture, "navigation"); QVERIFY(!first.isEmpty());
        auto db = iiSocietyContainer::SpaceStore::open(first.toStdString()); QVERIFY(db);
        QVERIFY(db->createSession({"session", "Nav session", "", "active"}));
        QQuickWindow window;
        SpaceTestDrive drive; drive.rootPath = first; drive.contentsAvailable = true;
        SpaceTestShell shell(window.contentItem()); shell.setObjectName(QStringLiteral("societyContent"));
        shell.setSize({1440, 810}); shell.drive = &drive;
        QQuickItem viewport(&shell); viewport.setObjectName(QStringLiteral("dashboardTabViewport"));
        QQuickItem legacy(&viewport); legacy.setObjectName(QStringLiteral("dashboardTabs"));
        QQuickItem content(&shell); content.setSize({1440, 760});
        QQuickItem dashboard(&content); dashboard.setObjectName(QStringLiteral("dashboardView")); dashboard.setSize(content.size());
        auto *navigation = LegacyNavigationBridge::install(&window); QVERIFY(navigation);
        QSignalSpy requested(&shell, &SpaceTestShell::tabRequested);
        navigation->requested(QStringLiteral("Space"));
        auto *space = window.findChild<NativeSpaceSurface *>(QStringLiteral("nativeSpaceSurface"));
        QVERIFY(space); QVERIFY(space->isVisible());
        QCOMPARE(navigation->currentPage(), QStringLiteral("Space"));
        QCOMPARE(space->model()->sessionTitle.get(), std::string("Nav session"));
        drive.contentsAvailable = false; emit drive.contentsChanged();
        QVERIFY(!space->model()->hasSession.get());
        emit shell.tabRequested(QStringLiteral("Dashboard"));
        QVERIFY(!space->isVisible());
        QCOMPARE(navigation->currentPage(), QStringLiteral("Dashboard"));
        requested.clear();
        navigation->requested(QStringLiteral("Space"));
        QVERIFY(space->isVisible());
        navigation->requested(QStringLiteral("Environment"));
        QVERIFY(!space->isVisible());
        QCOMPARE(navigation->currentPage(), QStringLiteral("Dashboard"));
        QCOMPARE(requested.size(), 1);
        QCOMPARE(requested.takeFirst().at(0).toString(), QStringLiteral("Dashboard"));
    }

    void explicitCreationAndContainerSwitch() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid());
        const auto first = container(fixture, "one"), second = container(fixture, "two");
        QVERIFY(!first.isEmpty() && !second.isEmpty());
        NativeSpaceSurface surface; surface.setSize({1100, 700});
        QVERIFY(surface.openContainer(first));
        auto *vm = surface.model(); QVERIFY(vm); QVERIFY(!vm->hasSession.get());
        QVERIFY(surface.activateControl(QStringLiteral("space-sessions-toggle")));
        QVERIFY(surface.activateControl(QStringLiteral("space-new-session")));
        QVERIFY2(surface.controlBounds(QStringLiteral("space-session")).bottom() <= surface.height() + 1,
                 "The new-session form must remain visible.");
        vm->sessionEdited("My session"); surface.refresh();
        QVERIFY(surface.activateControl(QStringLiteral("space-create-session")));
        QVERIFY(vm->hasSession.get());
        vm->ideaEdited("An idea"); surface.refresh();
        QVERIFY(surface.activateControl(QStringLiteral("space-add-idea")));
        QCOMPARE(vm->ideaCount.get(), 1);
        auto store = iiSocietyContainer::SpaceStore::open(first.toStdString()); QVERIFY(store);
        auto sessions = store->sessions(); QVERIFY(sessions); QCOMPARE(sessions->size(), std::size_t(1));
        auto graph = store->graph(sessions->front().id); QVERIFY(graph);
        QCOMPARE(graph->ideas.size(), std::size_t(1));
        QCOMPARE(graph->ideas.front().title, std::string("An idea"));
        QVERIFY(surface.openContainer(second));
        QVERIFY(!vm->hasSession.get()); QCOMPARE(vm->ideaCount.get(), 0);
        QVERIFY(!surface.openContainer(fixture.filePath("missing.societycontainer")));
        QVERIFY(!vm->hasSession.get()); QCOMPARE(vm->ideaCount.get(), 0);
        QVERIFY(!vm->status.get().empty());
    }
};

QTEST_MAIN(NativeSpaceTest)
#include "tst_native_space.moc"
