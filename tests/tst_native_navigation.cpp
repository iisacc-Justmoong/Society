#include "Society/Native/NativeNavigationSurface.h"
#include "Society/Native/QtPaintDevice.h"
#include <QAccessible>
#include <QCryptographicHash>
#include <QFile>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QtTest>

class NativeNavigationTests final : public QObject {
    Q_OBJECT
private slots:
    void rendersAndRoutesAllSegments();
    void paintAdapterPreservesFrames();
    void windowAttachmentMaintainsConnections();
    void keyboardMovesBetweenSegments();
    void keyboardLeavesAndReentersTheSegment();
};

void NativeNavigationTests::rendersAndRoutesAllSegments() {
    QQuickWindow window;
    window.resize(270, 60);
    NativeNavigationSurface surface(window.contentItem());
    surface.setPosition({10, 10});
    surface.setSize({214, 22});
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QTRY_VERIFY2(surface.ready(), qPrintable(surface.errorString()));
    QSignalSpy requested(&surface, &NativeNavigationSurface::requested);
    QCOMPARE(QSizeF(surface.implicitWidth(), surface.implicitHeight()), QSizeF(214, 22));
    const QStringList targets{"Environment", "Tools", "Storage", "Space", "Apps", "Service", "Community", "Learning", "Hosting"};
    const QStringList labels{"Environment", "Tasks", "Library", "Space", "Apps", "Service", "Community", "Learning", "Hosting"};
    auto *accessible = QAccessible::queryAccessibleInterface(&surface);
    QVERIFY(accessible);
    QCOMPARE(accessible->childCount(), 9);
    for (int index = 0; index < targets.size(); ++index) {
        auto *child = accessible->child(index);
        QVERIFY(child);
        QCOMPARE(child->text(QAccessible::Name), labels[index]);
        QVERIFY(child->actionInterface());
        QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, QPoint(10 + 11 + 24 * index, 21));
        QTRY_COMPARE(requested.size(), index + 1);
        QCOMPARE(requested.at(index).at(0).toString(), targets[index]);
        QVERIFY2(surface.ready(), qPrintable(surface.errorString()));
    }
    accessible->child(4)->actionInterface()->doAction(QAccessibleActionInterface::pressAction());
    QTRY_COMPARE(requested.size(), 10);
    QCOMPARE(requested.last().at(0).toString(), QStringLiteral("Apps"));
    surface.focusAccessibleItem(1);
    QTest::keyClick(&window, Qt::Key_Space);
    QTRY_COMPARE(requested.size(), 11);
    QCOMPARE(requested.last().at(0).toString(), QStringLiteral("Tools"));
    surface.setCurrentPage("Storage");
    QVERIFY(accessible->child(2)->state().selected);
    QVERIFY(!accessible->child(0)->state().selected);
    auto image = window.grabWindow();
    QVERIFY(!image.isNull());
    const auto capture = qEnvironmentVariable("SOCIETY_NATIVE_NAVIGATION_CAPTURE");
    if (!capture.isEmpty()) QVERIFY(image.save(capture));
    for (int index = 0; index < 9; ++index) {
        bool hasIcon = false;
        for (int y = 13; y < 29 && !hasIcon; ++y)
            for (int x = 13 + 24 * index; x < 29 + 24 * index; ++x)
                if (image.pixelColor(x, y).value() > 110) { hasIcon = true; break; }
        QVERIFY2(hasIcon, qPrintable(QString("segment %1 is missing its icon").arg(index)));
    }
    const QList<QPair<QByteArray, QByteArray>> figmaExports{
        {"toolwindowrun", "a475f104280b2f0e4af9577e14b22ddf17f653ab258d5847376202eeaf6e6fcd"},
        {"asset-library", "01087fac58014692374e419da90bcd85cfe2076a867899a7d7d45d82e63921cf"},
        {"toolwindownewUI", "543f3b5bb9294a18737ed51cf1829dde5e9c250a00d28e433388ac449ce57cd6"},
        {"application-alt", "a6e460d6a9bc90357ba9517befa6065a0978a597e160767d1bccf5f3c9fb3485"},
        {"usd", "e27fee1356f7de9e2c497ee14aa57c6bfd35d4e18ec8b905b8cbaf3efd17082c"},
        {"ppWeb", "f44e7dbd3355b11064d72cd7e5fe5a21b4664fd0fba53ab0b4a8b358ab63d3be"},
        {"cwmUsers", "e23b90c4c078a8c4a41e690d3bf00fed43e9b42c3a6aa12277914164a5549ce6"},
        {"kernel", "5444f9a5021c4294c4435d456b65e39fd84c22e435e93fba82cb82e4358b4bfe"}};
    for (const auto &[name, sha256] : figmaExports) {
        QFile asset(QStringLiteral(":/society/navigation/") + QString::fromLatin1(name) + QStringLiteral(".svg"));
        QVERIFY2(asset.open(QIODevice::ReadOnly), name.constData());
        QCOMPARE(QCryptographicHash::hash(asset.readAll(), QCryptographicHash::Sha256).toHex(), sha256);
    }
}

void NativeNavigationTests::windowAttachmentMaintainsConnections() {
    QTest::failOnWarning(QRegularExpression(QStringLiteral(".*unique connections require.*")));
    QQuickWindow window;
    NativeNavigationSurface surface;
    surface.setSize({214, 22});
    surface.setParentItem(window.contentItem());
    surface.setParentItem(nullptr);
    surface.setParentItem(window.contentItem());
    QVERIFY2(surface.ready(), qPrintable(surface.errorString()));
}

void NativeNavigationTests::keyboardMovesBetweenSegments() {
    QQuickWindow window;
    window.resize(270, 60);
    NativeNavigationSurface surface(window.contentItem());
    surface.setPosition({10, 10});
    surface.setSize({214, 22});
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QSignalSpy requested(&surface, &NativeNavigationSurface::requested);
    surface.focusAccessibleItem(0);
    QTRY_VERIFY(surface.accessibleItemFocused(0));
    QTest::keyClick(&window, Qt::Key_Tab);
    QTRY_VERIFY(surface.accessibleItemFocused(1));
    QTest::keyClick(&window, Qt::Key_Space);
    QTRY_COMPARE(requested.size(), 1);
    QCOMPARE(requested.first().first().toString(), QStringLiteral("Tools"));
    QTest::keyClick(&window, Qt::Key_Tab, Qt::ShiftModifier);
    QTRY_VERIFY(surface.accessibleItemFocused(0));
}

void NativeNavigationTests::keyboardLeavesAndReentersTheSegment() {
    QQuickWindow window;
    window.resize(300, 60);
    QQuickItem before(window.contentItem());
    before.setActiveFocusOnTab(true);
    NativeNavigationSurface surface(window.contentItem());
    surface.setPosition({30, 10}); surface.setSize({214, 22});
    QQuickItem after(window.contentItem());
    after.setActiveFocusOnTab(true);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    surface.focusAccessibleItem(8);
    QTRY_VERIFY(surface.accessibleItemFocused(8));
    QTest::keyClick(&window, Qt::Key_Tab);
    QTRY_VERIFY(after.hasActiveFocus());
    QTest::keyClick(&window, Qt::Key_Tab, Qt::ShiftModifier);
    QTRY_VERIFY(surface.accessibleItemFocused(8));
    surface.focusAccessibleItem(0);
    QTest::keyClick(&window, Qt::Key_Tab, Qt::ShiftModifier);
    QTRY_VERIFY(before.hasActiveFocus());
}

void NativeNavigationTests::paintAdapterPreservesFrames() {
    QtPaintDevice device;
    const ii::gui::PaintFrameInfo frame{.viewport={0, 0, 20, 20}, .background={0, 0, 0, 0},
        .scale=1, .pixel_width=20, .pixel_height=20};
    QVERIFY(device.begin_frame(frame));
    QVERIFY(device.begin_layer({}));
    const ii::gui::Panel panel{.bounds={4, 4, 12, 12}, .top={0.25f, 0.5f, 0.75f, 1},
        .bottom={0.25f, 0.5f, 0.75f, 1}};
    QVERIFY(device.draw_panels(std::span(&panel, 1)));
    QVERIFY(device.end_layer());
    QVERIFY(device.present());
    const auto committed = device.image();
    QCOMPARE(committed.pixelColor(1, 1).alpha(), 0);
    const auto sample = committed.pixelColor(10, 10);
    QVERIFY(qAbs(sample.red() - 137) <= 1);
    QVERIFY(qAbs(sample.green() - 188) <= 1);
    QVERIFY(qAbs(sample.blue() - 225) <= 1);
    QCOMPARE(sample.alpha(), 255);

    QVERIFY(device.begin_frame(frame));
    QVERIFY(device.begin_layer({}));
    const ii::gui::ImageRun invalid{};
    const auto result = device.draw_images(std::span(&invalid, 1));
    QVERIFY(!result);
    device.abort_frame();
    QCOMPARE(device.image(), committed);
}

QTEST_MAIN(NativeNavigationTests)
#include "tst_native_navigation.moc"
