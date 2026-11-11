#include "Society/Native/NativeNavigationSurface.h"
#include "Society/Native/QtPaintDevice.h"
#include <QAccessible>
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
    window.resize(220, 60);
    NativeNavigationSurface surface(window.contentItem());
    surface.setPosition({10, 10});
    surface.setSize({166, 22});
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QTRY_VERIFY2(surface.ready(), qPrintable(surface.errorString()));
    QSignalSpy requested(&surface, &NativeNavigationSurface::requested);
    const QStringList targets{"Environment", "Tools", "Storage", "Space", "Browse", "People", "Hosting"};
    auto *accessible = QAccessible::queryAccessibleInterface(&surface);
    QVERIFY(accessible);
    QCOMPARE(accessible->childCount(), 7);
    for (int index = 0; index < targets.size(); ++index) {
        auto *child = accessible->child(index);
        QVERIFY(child);
        QVERIFY(!child->text(QAccessible::Name).isEmpty());
        QVERIFY(child->actionInterface());
        QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, QPoint(10 + 11 + 24 * index, 21));
        QTRY_COMPARE(requested.size(), index + 1);
        QCOMPARE(requested.at(index).at(0).toString(), targets[index]);
        QVERIFY2(surface.ready(), qPrintable(surface.errorString()));
    }
    accessible->child(4)->actionInterface()->doAction(QAccessibleActionInterface::pressAction());
    QTRY_COMPARE(requested.size(), 8);
    QCOMPARE(requested.last().at(0).toString(), QStringLiteral("Browse"));
    surface.focusAccessibleItem(1);
    QTest::keyClick(&window, Qt::Key_Space);
    QTRY_COMPARE(requested.size(), 9);
    QCOMPARE(requested.last().at(0).toString(), QStringLiteral("Tools"));
    surface.setCurrentPage("Storage");
    QVERIFY(accessible->child(2)->state().selected);
    QVERIFY(!accessible->child(0)->state().selected);
    auto image = window.grabWindow();
    QVERIFY(!image.isNull());
    const auto capture = qEnvironmentVariable("SOCIETY_NATIVE_NAVIGATION_CAPTURE");
    if (!capture.isEmpty()) QVERIFY(image.save(capture));
    for (int index = 0; index < 7; ++index) {
        bool hasIcon = false;
        for (int y = 13; y < 29 && !hasIcon; ++y)
            for (int x = 13 + 24 * index; x < 29 + 24 * index; ++x)
                if (image.pixelColor(x, y).value() > 110) { hasIcon = true; break; }
        QVERIFY2(hasIcon, qPrintable(QString("segment %1 is missing its icon").arg(index)));
    }
    bool purpleHosting = false;
    for (int y = 13; y < 29 && !purpleHosting; ++y)
        for (int x = 13 + 24 * 6; x < 29 + 24 * 6; ++x) {
            const auto color = image.pixelColor(x, y);
            if (color.blue() > color.green() + 35 && color.red() > color.green() + 20) { purpleHosting = true; break; }
        }
    QVERIFY2(purpleHosting, "Hosting must use the purple Figma instance, not the grey component export");
}

void NativeNavigationTests::windowAttachmentMaintainsConnections() {
    QTest::failOnWarning(QRegularExpression(QStringLiteral(".*unique connections require.*")));
    QQuickWindow window;
    NativeNavigationSurface surface;
    surface.setSize({166, 22});
    surface.setParentItem(window.contentItem());
    surface.setParentItem(nullptr);
    surface.setParentItem(window.contentItem());
    QVERIFY2(surface.ready(), qPrintable(surface.errorString()));
}

void NativeNavigationTests::keyboardMovesBetweenSegments() {
    QQuickWindow window;
    window.resize(220, 60);
    NativeNavigationSurface surface(window.contentItem());
    surface.setPosition({10, 10});
    surface.setSize({166, 22});
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
    window.resize(260, 60);
    QQuickItem before(window.contentItem());
    before.setActiveFocusOnTab(true);
    NativeNavigationSurface surface(window.contentItem());
    surface.setPosition({30, 10}); surface.setSize({166, 22});
    QQuickItem after(window.contentItem());
    after.setActiveFocusOnTab(true);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    surface.focusAccessibleItem(6);
    QTRY_VERIFY(surface.accessibleItemFocused(6));
    QTest::keyClick(&window, Qt::Key_Tab);
    QTRY_VERIFY(after.hasActiveFocus());
    QTest::keyClick(&window, Qt::Key_Tab, Qt::ShiftModifier);
    QTRY_VERIFY(surface.accessibleItemFocused(6));
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
