#include "CTestSupport.h"
#include "QtEventWait.h"
#include <QCoreApplication>
#include <QEvent>
#include <QScopeGuard>
#include "Society/Notifications/NotificationCenter.h"

#include <QQmlComponent>
#include <QQmlEngine>
#include <memory>

class NotificationCenterTest final : public QObject {
public:
    void preservesVisibleAndActiveState() {
        int counts = 0, inserted = 0, removed = 0;
        NotificationCenter center;
        const auto countConnection = QObject::connect(&center, &NotificationCenter::countChanged, &center, [&] { ++counts; });
        const auto insertConnection = QObject::connect(&center, &QAbstractItemModel::rowsInserted, &center, [&] { ++inserted; });
        const auto removeConnection = QObject::connect(&center, &QAbstractItemModel::rowsRemoved, &center, [&] { ++removed; });
        CTEST_CHECK(countConnection && insertConnection && removeConnection);
        center.record(QStringLiteral("Models"), QStringLiteral(" first \r\nsecond\nfirst\n  "));
        CTEST_COMPARE(center.count(), 2);
        CTEST_COMPARE(inserted, 1);
        CTEST_COMPARE(center.data(center.index(0), Qt::UserRole + 1).toString(), QStringLiteral("Models"));
        CTEST_COMPARE(center.data(center.index(0), Qt::UserRole + 2).toString(), QStringLiteral("first"));
        center.dismiss(0);
        CTEST_COMPARE(removed, 1);
        center.record(QStringLiteral("Models"), QStringLiteral("first\nsecond"));
        CTEST_COMPARE(center.count(), 1);
        center.record(QStringLiteral("Models"), QString());
        center.record(QStringLiteral("Models"), QStringLiteral("first\nsecond"));
        CTEST_COMPARE(center.count(), 2);
        center.clear();
        center.record(QStringLiteral("Models"), QStringLiteral("first\nsecond"));
        CTEST_COMPARE(center.count(), 0);
        CTEST_COMPARE(counts, 4);
    }

    void existingShellCanBindToTheAdapter() {
        qmlRegisterType<NotificationCenter>("Society.Notifications.Test", 1, 0, "NotificationCenter");
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData("import Society.Notifications.Test 1.0\nNotificationCenter { property int visibleCount: count }", QUrl());
        std::unique_ptr<QObject> object(component.create());
        CTEST_CHECK_MESSAGE(object, qPrintable(component.errorString()));
        auto *center = qobject_cast<NotificationCenter *>(object.get());
        CTEST_CHECK(center);
        CTEST_COMPARE(object->property("visibleCount").toInt(), 0);
        CTEST_CHECK(QMetaObject::invokeMethod(center, "record", Q_ARG(QVariant, QStringLiteral("Storage")),
                                          Q_ARG(QVariant, QStringLiteral("Unavailable"))));
        CTEST_COMPARE(object->property("visibleCount").toInt(), 1);
        CTEST_COMPARE(center->entries()->rowCount(), 1);
        center->dismiss(0);
        CTEST_COMPARE(object->property("visibleCount").toInt(), 0);
    }
};

int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    society_ctest::Runner runner(argc == 2 ? argv[1] : "");
    NotificationCenterTest test;
    runner.run("preservesVisibleAndActiveState", [&] { const auto cleanup = qScopeGuard([] { QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); }); test.preservesVisibleAndActiveState(); });
    runner.run("existingShellCanBindToTheAdapter", [&] { const auto cleanup = qScopeGuard([] { QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); }); test.existingShellCanBindToTheAdapter(); });
    return runner.result();
}
