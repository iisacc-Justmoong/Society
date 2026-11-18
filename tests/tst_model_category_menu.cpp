#include <iiSocietyContainer>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTest>

static QQuickItem *visualItem(QQuickItem *parent, const QString &name)
{
    if (!parent) return nullptr;
    if (parent->objectName() == name) return parent;
    for (auto *child : parent->childItems())
        if (auto *found = visualItem(child, name)) return found;
    return nullptr;
}

class ModelCategoryMenuTests : public QObject {
    Q_OBJECT
private slots:
    void categoriesOpenAsScrollableSubmenuAndSelect()
    {
        qmlRegisterType<iiSocietyContainer::FileActions>("Society", 1, 0, "FileActions");
        QQmlApplicationEngine engine;
        QStringList errors;
        connect(&engine, &QQmlApplicationEngine::warnings, this, [&](const QList<QQmlError> &warnings) {
            for (const auto &warning : warnings) errors.append(warning.toString());
        });
        engine.addImportPath(QString::fromUtf8(SOCIETY_LVRS_QML_IMPORT_PATH));
        engine.load(QUrl::fromLocalFile(QString::fromUtf8(SOCIETY_CATEGORY_MENU_HARNESS)));
        QVERIFY2(engine.rootObjects().size() == 1, qPrintable(errors.join('\n')));
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
        auto *menu = window->findChild<QObject *>("menuUnderTest"); QVERIFY(menu);
        iiSocietyContainer::StorageModelCatalog catalog;
        QVERIFY(menu->setProperty("categoryOptions", catalog.categories()));
        QSignalSpy selected(menu, SIGNAL(categoryRequested(QString,QString))); QVERIFY(selected.isValid());
        QVERIFY(QMetaObject::invokeMethod(menu, "openAt", Q_ARG(QVariant, 30), Q_ARG(QVariant, 30)));
        QTRY_VERIFY_WITH_TIMEOUT(menu->property("opened").toBool(), 10000);
        QVERIFY(QMetaObject::invokeMethod(menu, "triggerEntry", Q_ARG(QVariant, 9)));
        auto *submenu = window->findChild<QObject *>("modelCategoryMenu"); QVERIFY(submenu);
        QTRY_VERIFY_WITH_TIMEOUT(submenu->property("opened").toBool(), 10000);
        auto *list = window->findChild<QQuickItem *>("modelCategoryList"); QVERIFY(list);
        QTRY_COMPARE(list->property("count").toInt(), 23);
        QTRY_VERIFY(list->height() > 0);
        QTRY_VERIFY(list->property("contentHeight").toReal() > list->height());
        QQuickItem *option = nullptr;
        QTRY_VERIFY(option = visualItem(list, "modelCategoryOption0"));
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
            option->mapToScene(QPointF(option->width() / 2, option->height() / 2)).toPoint());
        QTRY_COMPARE_WITH_TIMEOUT(selected.size(), 1, 10000);
        QCOMPARE(selected.first().at(0).toString(), QString("/example/model.safetensors"));
        QCOMPARE(selected.first().at(1).toString(), QString("Checkpoint"));
        QTRY_VERIFY(!submenu->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(menu, "openAt", Q_ARG(QVariant, 30), Q_ARG(QVariant, 30)));
        QTRY_VERIFY_WITH_TIMEOUT(menu->property("opened").toBool(), 10000);
        QVERIFY(QMetaObject::invokeMethod(menu, "triggerEntry", Q_ARG(QVariant, 9)));
        QTRY_VERIFY_WITH_TIMEOUT(submenu->property("opened").toBool(), 10000);
        QVERIFY(QMetaObject::invokeMethod(list, "positionViewAtEnd"));
        QQuickItem *last = nullptr;
        QTRY_VERIFY(last = visualItem(list, "modelCategoryOption22"));
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
            last->mapToScene(QPointF(last->width() / 2, last->height() / 2)).toPoint());
        QTRY_COMPARE_WITH_TIMEOUT(selected.size(), 2, 10000);
        QCOMPARE(selected.last().at(1).toString(), QString("Other"));
        QVERIFY2(errors.isEmpty(), qPrintable(errors.join('\n')));
        window->close();
    }
};
QTEST_MAIN(ModelCategoryMenuTests)
#include "tst_model_category_menu.moc"
