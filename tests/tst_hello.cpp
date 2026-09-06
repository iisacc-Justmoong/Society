#include "backend/runtime/appbootstrap.h"

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlError>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTest>

class HelloWorldTest final : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        engine.addImportPath(QString::fromUtf8(SOCIETY_LVRS_QML_IMPORT_PATH));
        connect(&engine, &QQmlApplicationEngine::warnings, this,
                [this](const QList<QQmlError> &errors) {
                    for (const auto &error : errors)
                        qmlWarnings.append(error.toString());
                });
        engine.load(QUrl::fromLocalFile(QString::fromUtf8(SOCIETY_QML_FILE)));
        QCOMPARE(engine.rootObjects().size(), 1);
        window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(window);
        greeting = window->findChild<QQuickItem *>(QStringLiteral("greeting"));
        QVERIFY(greeting);
    }

    void showsHelloWorld()
    {
        QCOMPARE(window->title(), QStringLiteral("Society"));
        QTRY_VERIFY(window->isVisible());
        QTRY_VERIFY(greeting->isVisible());
        QCOMPARE(greeting->property("text").toString(), QStringLiteral("Hello world!"));
        QVERIFY(greeting->width() > 0);
        QVERIFY(greeting->height() > 0);
    }

    void staysCenteredAfterResize_data()
    {
        QTest::addColumn<QSize>("size");
        QTest::newRow("initial") << QSize(640, 400);
        QTest::newRow("minimum") << QSize(320, 240);
        QTest::newRow("large") << QSize(960, 640);
    }

    void staysCenteredAfterResize()
    {
        QFETCH(QSize, size);
        window->resize(size);
        QTRY_COMPARE(window->size(), size);
        const auto centered = [this] {
            const QPointF center = greeting->mapToScene(
                QPointF(greeting->width() / 2, greeting->height() / 2));
            return qAbs(center.x() - window->width() / 2.0) < 1.0
                && qAbs(center.y() - window->height() / 2.0) < 1.0;
        };
        QTRY_VERIFY(centered());
        QVERIFY(greeting->isVisible());
    }

    void cleanupTestCase()
    {
        QVERIFY2(qmlWarnings.isEmpty(), qPrintable(qmlWarnings.join('\n')));
        if (window)
            window->close();
    }

private:
    QQmlApplicationEngine engine;
    QQuickWindow *window = nullptr;
    QQuickItem *greeting = nullptr;
    QStringList qmlWarnings;
};

int main(int argc, char *argv[])
{
    lvrs::AppBootstrapOptions options;
    options.applicationName = QStringLiteral("SocietyGuiTests");
    options.quickStyleName = QStringLiteral("Basic");
    options.bootstrapGraphicsBackend = false;
    options.configureRenderQualityDefaults = false;
    options.logBootstrapDiagnostics = false;
    options.logGraphicsBackend = false;
    if (!lvrs::preApplicationBootstrap(options).ok)
        return 1;

    QGuiApplication app(argc, argv);
    lvrs::postApplicationBootstrap(app, options);
    HelloWorldTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_hello.moc"
