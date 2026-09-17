#include "App/Files/DirectoryLocation.h"
#include "backend/runtime/appbootstrap.h"

#include <QAbstractItemModel>
#include <QDir>
#include <QDateTime>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QJSValue>
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
        qmlRegisterType<StorageDirectoryModel>("Society", 1, 0, "StorageDirectoryModel");
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

    void historyGalleryCropsSquaresZoomsAndShowsInformationOnlyOnClick()
    {
        QTemporaryDir history(SOCIETY_TEST_DIRECTORY "/history-gallery-XXXXXX"); QVERIFY(history.isValid());
        QImage portrait(60, 180, QImage::Format_RGB32); portrait.fill(QColor("#36a67f"));
        for (int i = 0; i < 80; ++i) QVERIFY(portrait.save(history.filePath(QString("result-%1.png").arg(i, 2, 10, QChar('0')))));
        window->resize(960, 720);
        view->setProperty("heading", "Generation History");
        view->setProperty("imagesOnly", true); view->setProperty("path", history.path());
        QTRY_COMPARE(view->property("count").toInt(), 80);
        QTRY_VERIFY(!view->property("loading").toBool() && !view->property("initialPositionPending").toBool());
        auto *info = view->findChild<QObject *>("galleryInfo"); QVERIFY(info);
        QVERIFY(!info->property("visible").toBool());
        QTRY_VERIFY(tileNamed("result-00.png"));
        auto *entry = tileNamed("result-00.png");
        auto *tile = entry->findChild<QQuickItem *>("galleryTile"); QVERIFY(tile);
        QCOMPARE(tile->width(), tile->height());
        QCOMPARE(grid->property("cellWidth").toReal() - tile->width(), 2.0);
        QCOMPARE(tile->property("text").toString(), QString());
        QVERIFY(!entry->findChild<QQuickItem *>("fileName")->isVisible());
        auto *preview = tile->findChild<QQuickItem *>("galleryThumbnail"); QVERIFY(preview);
        QTRY_COMPARE(preview->property("status").toInt(), 1);
        QCOMPARE(preview->property("fillMode").toInt(), 2); // PreserveAspectCrop
        QCOMPARE(preview->width(), preview->height());
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QSignalSpy clicked(tile, SIGNAL(clicked()));
        QSignalSpy activated(view, SIGNAL(activated(QString,bool)));
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, tile->mapToScene(QPointF(tile->width()/2, tile->height()/2)).toPoint());
        QTRY_COMPARE(clicked.count(), 1);
        QTRY_VERIFY(info->property("visible").toBool());
        QCOMPARE(info->property("fileName").toString(), QString("result-00.png"));
        QCOMPARE(activated.count(), 0);
        const auto selected = view->property("selectedPath").toString();
        QVERIFY(QMetaObject::invokeMethod(info, "close")); QTRY_VERIFY(!info->property("visible").toBool()); QTest::qWait(250);
        const auto drag = [&](const QPoint &delta) {
            const auto start = grid->mapToScene(QPointF(grid->width()*0.4, grid->height()*0.6)).toPoint();
            QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, start);
            for (int i = 1; i <= 12; ++i) QTest::mouseMove(window, start + delta*i/12, 16);
            QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, start + delta);
        };
        const auto initialWidth = grid->property("cellWidth").toReal();
        drag(QPoint(150, 0)); QTRY_VERIFY(grid->property("cellWidth").toReal() > initialWidth);
        QCOMPARE(grid->property("cellWidth"), grid->property("cellHeight"));
        QCOMPARE(view->property("selectedPath").toString(), selected);
        QVERIFY(!info->property("visible").toBool()); QCOMPARE(activated.count(), 0);
        const auto enlarged = grid->property("cellWidth").toReal();
        drag(QPoint(-150, 0)); QTRY_VERIFY(grid->property("cellWidth").toReal() < enlarged);
        grid->setProperty("contentY", grid->property("originY"));
        const auto sizeBeforeScroll = grid->property("cellWidth").toReal();
        drag(QPoint(0, -160)); QTRY_VERIFY(grid->property("contentY").toReal() > grid->property("originY").toReal());
        QCOMPARE(grid->property("cellWidth").toReal(), sizeBeforeScroll);
        QVERIFY(!info->property("visible").toBool());
        QVERIFY(window->grabWindow().save(QStringLiteral(SOCIETY_TEST_DIRECTORY "/history-gallery-desktop.png")));
        window->resize(390, 844); QTest::qWait(100);
        QCOMPARE(grid->property("cellWidth"), grid->property("cellHeight"));
        QVERIFY(grid->width() <= window->width());
        QVERIFY(window->grabWindow().save(QStringLiteral(SOCIETY_TEST_DIRECTORY "/history-gallery-mobile.png")));
        view->setProperty("imagesOnly", false); view->setProperty("path", fixtures.path());
        QTRY_COMPARE(view->property("count").toInt(), 5);
    }

    void chronologicalFilesOpenAtTheNewestAndPreserveBrowsing()
    {
        QTemporaryDir timeline(SOCIETY_TEST_DIRECTORY "/file-timeline-XXXXXX");
        QVERIFY(timeline.isValid());
        const auto epoch = QDateTime::fromString("2026-01-01T00:00:00Z", Qt::ISODate);
        const auto write = [&](const QString &name, int seconds) {
            QFile file(timeline.filePath(name));
            return file.open(QIODevice::WriteOnly) && file.write("fixture") > 0 && file.flush()
                && file.setFileTime(epoch.addSecs(seconds), QFileDevice::FileModificationTime);
        };
        for (int i = 0; i < 40; ++i)
            QVERIFY(write(QString("photo-%1.txt").arg(39 - i, 2, 10, QChar('0')), i));
        const auto valueAt = [&](int index, const QByteArray &role) {
            auto *model = qvariant_cast<QAbstractItemModel *>(grid->property("model"));
            return model ? model->data(model->index(index, 0), model->roleNames().key(role, -1)) : QVariant();
        };
        window->resize(640, 480);
        QVERIFY(view->setProperty("chronological", true));
        QVERIFY(view->setProperty("path", timeline.path()));
        QTRY_COMPARE(view->property("count").toInt(), 40);
        QTRY_VERIFY(!view->property("loading").toBool());
        QCOMPARE(valueAt(0, "fileName").toString(), QString("photo-39.txt"));
        QCOMPARE(valueAt(39, "fileName").toString(), QString("photo-00.txt"));
        QTRY_VERIFY(grid->property("atYEnd").toBool());
        QVERIFY(grid->property("contentY").toReal() > 0);
        QCOMPARE(grid->property("currentIndex").toInt(), -1);

        // Newest arrivals remain visible when already following the end.
        QVERIFY(write("AAA-newest.txt", 50));
        QTRY_COMPARE(view->property("count").toInt(), 41);
        QCOMPARE(valueAt(40, "fileName").toString(), QString("AAA-newest.txt"));
        QTRY_VERIFY(grid->property("atYEnd").toBool());

        // atYEnd can still describe the previous layout while the queued restore
        // is pending. Finish that arrival before simulating a new browsing action.
        const auto viewSettled = [&] {
            const auto pending = view->property("pendingViewState");
            return !view->property("loading").toBool() && !view->property("initialPositionPending").toBool()
                && (pending.isNull() || pending.value<QJSValue>().isNull());
        };
        QTRY_VERIFY(viewSettled());

        // Browsing older files must not jump back to the newest on a directory update.
        QVERIFY(grid->setProperty("currentIndex", 8));
        QVERIFY(grid->setProperty("contentY", 352.0));
        QTest::qWait(100);
        const auto selected = view->property("selectedPath").toString();
        const auto scroll = grid->property("contentY").toReal();
        QVERIFY(scroll > 0 && !grid->property("atYEnd").toBool());
        QVERIFY(write("AA-another-newest.txt", 60));
        QTRY_COMPARE(view->property("count").toInt(), 42);
        QTRY_COMPARE(view->property("selectedPath").toString(), selected);
        QTRY_COMPARE(grid->property("contentY").toReal(), scroll);
        QVERIFY(QFile::remove(timeline.filePath("AA-another-newest.txt")));
        QTRY_COMPARE(view->property("count").toInt(), 41);
        QTRY_COMPARE(view->property("selectedPath").toString(), selected);
        QTRY_COMPARE(grid->property("contentY").toReal(), scroll);

        // A modification can reorder rows without changing their count.
        QVERIFY(write("photo-39.txt", 100));
        QTRY_COMPARE(valueAt(40, "fileName").toString(), QString("photo-39.txt"));
        QTRY_COMPARE(valueAt(grid->property("currentIndex").toInt(), "filePath").toString(), selected);
        QTRY_COMPARE(view->property("selectedPath").toString(), selected);
        QTRY_COMPARE(grid->property("contentY").toReal(), scroll);

        // Empty directories, later asynchronous results and re-entry initialize separately.
        QVERIFY(view->setProperty("path", fixtures.filePath("Empty")));
        QTRY_COMPARE(view->property("count").toInt(), 0);
        QVERIFY(view->setProperty("path", timeline.path()));
        QTRY_COMPARE(view->property("count").toInt(), 41);
        QTRY_VERIFY(grid->property("atYEnd").toBool());
        QCOMPARE(grid->property("currentIndex").toInt(), -1);
        QVERIFY(view->setProperty("chronological", false));
        QTRY_COMPARE(valueAt(0, "fileName").toString(), QString("AAA-newest.txt"));
        QTRY_VERIFY(grid->property("atYBeginning").toBool());
        QVERIFY(view->setProperty("path", fixtures.path()));
        QTRY_COMPARE(view->property("count").toInt(), 5);
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

    void remoteFileShowsDownloadStateAndOpensAfterPublication()
    {
        QTemporaryDir remote(QStringLiteral(SOCIETY_TEST_DIRECTORY "/remote-grid-XXXXXX"));
        const auto drive = iiSocietyContainer::SocietyDrive::create(remote.path()); QVERIFY(drive);
        iiSocietyContainer::StorageMap map(*drive);
        QJsonObject entry{{"path", "files/Documents/remote.txt"}, {"kind", "file"}, {"size", "3"},
            {"version", QString(64, 'a')}, {"hash", QString(64, 'b')}, {"resident", false}};
        QVERIFY(map.publish({entry}));
        QVERIFY(view->setProperty("path", remote.filePath("Files/Documents")));
        QTRY_COMPARE(view->property("count").toInt(), 1);
        QVERIFY(grid->setProperty("currentIndex", 0));
        QSignalSpy activated(view, SIGNAL(activated(QString,bool)));
        QVERIFY(QMetaObject::invokeMethod(view, "activateCurrent"));
        QCOMPARE(activated.size(), 0);
        QTRY_COMPARE(map.pendingRequests().size(), 1);
        QVERIFY(view->property("downloadStatus").toString().contains("Downloading"));
        const auto request = map.pendingRequests().first().toObject().value("id").toString();
        QVERIFY(map.finishRequest(request, "host_version_changed"));
        QTRY_VERIFY(view->property("downloadStatus").toString().contains("retry"));
        QCOMPARE(activated.size(), 0);
        QVERIFY(QMetaObject::invokeMethod(view, "activateCurrent"));
        QTRY_COMPARE(map.pendingRequests().size(), 1);
        const auto retry = map.pendingRequests().first().toObject().value("id").toString();
        const auto path = remote.filePath("Files/Documents/remote.txt");
        QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write("new"), 3); file.close();
        entry["resident"] = true; QVERIFY(map.publish({entry})); QVERIFY(map.finishRequest(retry));
        QTRY_COMPARE(activated.size(), 1);
        QCOMPARE(activated.first().first().toString(), path);
        QVERIFY(view->property("downloadStatus").toString().isEmpty());
        QVERIFY(view->setProperty("path", ""));
        QTRY_COMPARE(view->property("count").toInt(), 0);
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
