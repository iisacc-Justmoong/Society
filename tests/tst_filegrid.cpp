#include "App/Files/DirectoryLocation.h"
#include "backend/runtime/appbootstrap.h"

#include <QAbstractItemModel>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QQmlApplicationEngine>
#include <QQmlError>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

class FileGridTest final : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QVERIFY(fixtures.isValid());
        QVERIFY(QDir(fixtures.path()).mkdir("Z Folder"));
        QVERIFY(QDir(fixtures.path()).mkdir("Empty"));
        for (const auto &name : {"Alpha.txt", "beta.txt", ".hidden"}) {
            QFile file(fixtures.filePath(name));
            QVERIFY(file.open(QIODevice::WriteOnly));
            QVERIFY(file.write("Society file grid fixture\n") > 0);
        }
        QImage image(80, 60, QImage::Format_RGB32);
        image.fill(QColor("#0a84ff"));
        QVERIFY(image.save(fixtures.filePath("preview #한글.png")));

        qmlRegisterType<DirectoryLocation>("Society", 1, 0, "DirectoryLocation");
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
        view = window->findChild<QQuickItem *>("fileGridView");
        QVERIFY(view);
        grid = view->findChild<QQuickItem *>("fileGrid");
        QVERIFY(grid);
    }

    void startsWithNoFolder()
    {
        QCOMPARE(window->title(), QStringLiteral("Society"));
        QTRY_VERIFY(window->isVisible());
        QVERIFY(view->isVisible());
        QCOMPARE(view->property("path").toString(), QString());
        QCOMPARE(view->property("count").toInt(), 0);
        QCOMPARE(grid->property("currentIndex").toInt(), -1);
        QVERIFY(!view->findChild<QObject *>("fileModelLoader")->property("active").toBool());
        QCOMPARE(emptyTitle(), QStringLiteral("No folder selected"));
        QCOMPARE(view->findChild<QObject *>("fileCount")->property("text").toString(),
                 QStringLiteral("0 items"));
    }

    void validatesNativePaths()
    {
        DirectoryLocation location;
        QVERIFY(location.folderUrl().isEmpty());
        QVERIFY(location.errorString().isEmpty());
        for (const auto &path : {QStringLiteral("relative/path"),
                                fixtures.filePath("missing"), fixtures.filePath("Alpha.txt")}) {
            location.setPath(path);
            QVERIFY(location.folderUrl().isEmpty());
            QVERIFY(!location.errorString().isEmpty());
        }
        const QString unicodePath = fixtures.filePath("Folder #한글 % spaces");
        QVERIFY(QDir().mkdir(unicodePath));
        location.setPath(unicodePath);
        QCOMPARE(location.folderUrl().toLocalFile(), unicodePath);
        QVERIFY(location.errorString().isEmpty());
        location.setPath("");
        QVERIFY(location.folderUrl().isEmpty());
        QVERIFY(location.errorString().isEmpty());
        QVERIFY(QDir().rmdir(unicodePath));
    }

    void displaysFilesAndThumbnails()
    {
        QVERIFY(view->setProperty("path", fixtures.path()));
        QTRY_COMPARE(view->property("count").toInt(), 5);
        auto *model = qvariant_cast<QAbstractItemModel *>(grid->property("model"));
        QVERIFY(model);
        int nameRole = -1;
        const auto roles = model->roleNames();
        for (auto it = roles.begin(); it != roles.end(); ++it)
            if (it.value() == "fileName")
                nameRole = it.key();
        QVERIFY(nameRole >= 0);
        QStringList names;
        for (int i = 0; i < model->rowCount(); ++i)
            names.append(model->data(model->index(i, 0), nameRole).toString());
        QCOMPARE(names, QStringList({"Empty", "Z Folder", "Alpha.txt", "beta.txt",
                                     "preview #한글.png"}));
        QTRY_VERIFY(tileNamed("preview #한글.png"));
        auto *preview = tileNamed("preview #한글.png")->findChild<QQuickItem *>("fileThumbnail");
        QVERIFY(preview);
        QTRY_COMPARE(preview->property("status").toInt(), 1); // Image.Ready
        QCOMPARE(preview->property("source").toUrl().toLocalFile(),
                 fixtures.filePath("preview #한글.png"));
        QVERIFY(!view->findChild<QQuickItem *>("fileGridEmptyState")->isVisible());
    }

    void selectsAndActivatesFiles()
    {
        auto *tile = tileNamed("Alpha.txt");
        QVERIFY(tile);
        const auto center = tile->mapToScene(QPointF(tile->width() / 2, tile->height() / 2));
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, center.toPoint());
        QTRY_COMPARE(view->property("selectedPath").toString(), fixtures.filePath("Alpha.txt"));
        QSignalSpy activated(view, SIGNAL(activated(QString,bool)));
        QVERIFY(activated.isValid());
        QTest::keyClick(window, Qt::Key_Return);
        QCOMPARE(activated.size(), 1);
        QCOMPARE(activated.first().at(0).toString(), fixtures.filePath("Alpha.txt"));
        QCOMPARE(activated.first().at(1).toBool(), false);
        QTest::keyClick(window, Qt::Key_Right);
        QTRY_COMPARE(view->property("selectedPath").toString(), fixtures.filePath("beta.txt"));
        QTest::keyClick(window, Qt::Key_Escape);
        QTRY_COMPARE(grid->property("currentIndex").toInt(), -1);
        QTest::mouseDClick(window, Qt::LeftButton, Qt::NoModifier, center.toPoint());
        QTRY_COMPARE(activated.size(), 2);
        QCOMPARE(activated.last().at(0).toString(), fixtures.filePath("Alpha.txt"));
    }

    void adaptsToWindowSize_data()
    {
        QTest::addColumn<QSize>("size");
        QTest::newRow("minimum") << QSize(360, 320);
        QTest::newRow("initial") << QSize(960, 640);
        QTest::newRow("large") << QSize(1280, 800);
    }

    void touchModeOpensWithOneTap()
    {
        QVERIFY(view->setProperty("touchNavigation", true));
        QSignalSpy activated(view, SIGNAL(activated(QString,bool)));
        auto *tile = tileNamed("Z Folder");
        QVERIFY(tile);
        const auto center = tile->mapToScene(QPointF(tile->width() / 2, tile->height() / 2));
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, center.toPoint());
        QTRY_COMPARE(activated.size(), 1);
        QCOMPARE(activated.first().at(0).toString(), fixtures.filePath("Z Folder"));
        QVERIFY(activated.first().at(1).toBool());
        QVERIFY(view->setProperty("touchNavigation", false));
    }

    void adaptsToWindowSize()
    {
        QFETCH(QSize, size);
        window->resize(size);
        QTRY_COMPARE(window->size(), size);
        QTRY_COMPARE(qRound(view->width()), size.width());
        const int expectedColumns = qMax(1, int(grid->width() / 160));
        QTRY_COMPARE(grid->property("columns").toInt(), expectedColumns);
        const auto cellWidth = grid->property("cellWidth").toDouble();
        QVERIFY(qAbs(cellWidth * expectedColumns - grid->width()) < 1);
        QVERIFY(grid->height() > 0);
        if (size == QSize(360, 320)) {
            QVERIFY(grid->property("contentHeight").toDouble() > grid->height());
            grid->forceActiveFocus();
            grid->setProperty("currentIndex", 0);
            for (int step = 0; step < 4; ++step)
                QTest::keyClick(window, Qt::Key_Right);
            QTRY_COMPARE(grid->property("currentIndex").toInt(), 4);
            QTRY_VERIFY(grid->property("contentY").toDouble() > 0);
        }
    }

    void historyFilterChangesTogetherWithTheLocation()
    {
        const auto history = fixtures.filePath("Empty");
        QVERIFY(QDir().mkdir(history + "/Old App Folder"));
        QVERIFY(QFile::copy(fixtures.filePath("preview #한글.png"), history + "/result.PNG"));
        QVERIFY(QFile::copy(fixtures.filePath("Alpha.txt"), history + "/request.json"));
        for (int iteration = 0; iteration < 5; ++iteration) {
            QVERIFY(view->setProperty("imagesOnly", true));
            QVERIFY(view->setProperty("path", history));
            QTRY_COMPARE(view->property("count").toInt(), 1);
            QTRY_VERIFY(tileNamed("result.PNG"));
            QVERIFY(!view->findChild<QQuickItem *>("folderPath")->isVisible());
            QVERIFY(view->setProperty("imagesOnly", false));
            QVERIFY(view->setProperty("path", fixtures.path()));
            QTRY_COMPARE(view->property("count").toInt(), 5);
            QTRY_VERIFY(tileNamed("Z Folder"));
        }
        QVERIFY(QFile::remove(history + "/result.PNG"));
        QVERIFY(QFile::remove(history + "/request.json"));
        QVERIFY(QDir().rmdir(history + "/Old App Folder"));
    }

    void clearsOldFilesWhenPathChanges()
    {
        view->setProperty("path", fixtures.filePath("Empty"));
        QTRY_COMPARE(view->property("count").toInt(), 0);
        QTRY_COMPARE(emptyTitle(), QStringLiteral("This folder is empty"));
        view->setProperty("path", fixtures.filePath("missing"));
        QTRY_COMPARE(emptyTitle(), QStringLiteral("Folder unavailable"));
        QCOMPARE(view->property("count").toInt(), 0);
        QVERIFY(!view->findChild<QObject *>("fileModelLoader")->property("active").toBool());
        view->setProperty("path", "");
        QTRY_COMPARE(emptyTitle(), QStringLiteral("No folder selected"));
        QCOMPARE(view->property("count").toInt(), 0);
        QCOMPARE(view->property("selectedPath").toString(), QString());
    }

    void cleanupTestCase()
    {
        QVERIFY2(qmlWarnings.isEmpty(), qPrintable(qmlWarnings.join('\n')));
        if (window)
            window->close();
    }

private:
    QString emptyTitle() const
    {
        return view->findChild<QObject *>("emptyStateTitle")->property("text").toString();
    }

    QQuickItem *tileNamed(const QString &name) const
    {
        // GridView reparents delegates to its visual content item.
        const auto visit = [&](auto &&self, QQuickItem *item) -> QQuickItem * {
            if (item->objectName() == "fileTile" && item->property("fileName").toString() == name)
                return item;
            for (auto *child : item->childItems())
                if (auto *found = self(self, child))
                    return found;
            return nullptr;
        };
        return visit(visit, grid);
    }

    QTemporaryDir fixtures{QStringLiteral(SOCIETY_TEST_DIRECTORY "/file-grid-XXXXXX")};
    QQmlApplicationEngine engine;
    QQuickWindow *window = nullptr;
    QQuickItem *view = nullptr;
    QQuickItem *grid = nullptr;
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
    FileGridTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_filegrid.moc"
