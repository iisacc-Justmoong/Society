#include "App/Photos/PhotoStore.h"
#include "App/Photos/PhotoController.h"
#include <QFile>
#include <QDir>
#include <QImage>
#include <QPointingDevice>
#include <QPainter>
#include <QLinearGradient>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QTest>
#include <QSignalSpy>
#include <QSemaphore>
#include <QScopeGuard>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QQuickItem>
#include "backend/runtime/appbootstrap.h"
#include <iiSocietySync.h>

using namespace society::photos;
namespace {
QQuickItem *visualItem(QQuickItem *parent, const QString &name) {
    if (parent->objectName() == name) return parent;
    for (auto *child : parent->childItems()) if (auto *found = visualItem(child, name)) return found;
    return nullptr;
}
void write(const QString &path, const QByteArray &bytes) {
    QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write(bytes), bytes.size());
}
class Library final : public PhotoLibrary {
public:
    QString directory;
    Snapshot snapshot;
    std::atomic_int imports{0}, exports{0}, removals{0};
    Access permission = Access::Full;
    int authorizations = 0;
    std::function<void()> authorizationFinished;
    std::function<void(const QString &)> beforeExport;
    explicit Library(const QString &path) : directory(path) { snapshot.complete = true; }
    QString name() const override { return "Test Photos"; }
    Access access() const override { return permission; }
    void authorize(std::function<void()> done) override { ++authorizations; authorizationFinished = std::move(done); }
    Snapshot scan(QString *) override { auto result = snapshot; result.complete = permission == Access::Full; return result; }
    bool preview(const QString &id, const QString &path, QString *) override {
        // Non-square fixtures make unintended letterboxing visible in gallery QA.
        QByteArray contents;
        for (const auto &asset : snapshot.assets) if (asset.identifier == id && !asset.resources.isEmpty()) {
            QFile original(directory + '/' + asset.resources.first().token);
            if (original.open(QIODevice::ReadOnly)) contents = original.readAll();
        }
        const int hue = int(qChecksum(contents) % 360);
        QImage image(hue % 2 ? 180 : 320, hue % 2 ? 320 : 180, QImage::Format_RGB32);
        QPainter painter(&image);
        QLinearGradient gradient(0, 0, image.width(), image.height());
        gradient.setColorAt(0, QColor::fromHsv(hue, 180, 220));
        gradient.setColorAt(1, QColor::fromHsv((hue + 70) % 360, 120, 90));
        painter.fillRect(image.rect(), gradient);
        painter.setPen(Qt::NoPen); painter.setBrush(QColor(255, 255, 255, 70));
        painter.drawEllipse(QPointF(image.width()*0.55, image.height()*0.45), 50, 50);
        painter.end(); return image.save(path, "JPEG");
    }
    bool exportResource(const QString &, const QString &token, const QString &path, QString *) override {
        if (beforeExport) beforeExport(token);
        ++exports; return QFile::copy(directory + '/' + token, path);
    }
    QString import(const QJsonObject &record, const QStringList &paths, QString *) override {
        const auto id = record.value("id").toString();
        for (const auto &asset : snapshot.assets) if (asset.identifier == id) return id;
        Asset asset; asset.identifier = id; asset.name = record.value("name").toString();
        asset.media = record.value("media").toString(); asset.stamp = "imported";
        const auto resources = record.value("resources").toArray();
        for (int i = 0; i < paths.size(); ++i) {
            const auto name = importedResourceName(id, i, resources[i].toObject().value("name").toString());
            if (!QFile::copy(paths[i], directory + '/' + name)) return {};
            asset.resources.append({name, name, resources[i].toObject().value("role").toString()});
        }
        snapshot.assets.append(asset); ++imports; return id;
    }
    bool trash(const QString &id, QString *) override {
        for (qsizetype i = 0; i < snapshot.assets.size(); ++i) if (snapshot.assets[i].identifier == id) {
            snapshot.assets.removeAt(i); ++removals; break;
        }
        return true;
    }
    void add(const QString &name, const QByteArray &bytes, const QString &media = "photo") {
        write(directory + '/' + name, bytes);
        Asset asset; asset.identifier = name; asset.name = name; asset.stamp = "1";
        asset.created = QDateTime::fromString("2026-09-14T09:00:00Z", Qt::ISODate);
        asset.media = media; asset.resources = {{name, name, media}}; snapshot.assets.append(asset);
    }
};
struct Fixture {
    QTemporaryDir temporary{QStringLiteral(SOCIETY_TEST_DIRECTORY "/photos-XXXXXX")};
    std::shared_ptr<Library> library;
    std::unique_ptr<PhotoStore> store;
    Fixture() {
        QDir().mkpath(temporary.path() + "/native");
        QDir().mkpath(temporary.path() + "/drive");
        library = std::make_shared<Library>(temporary.path() + "/native");
        const auto drive = iiSocietyContainer::SocietyDrive::create(temporary.path() + "/drive");
        if (!drive) qFatal("Could not create the photo test container");
        store = std::make_unique<PhotoStore>(drive->rootPath(), library);
        if (!store->open()) qFatal("Could not open the photo test store: %s", qPrintable(store->errorString()));
    }
    QString photos() const { return temporary.path() + "/drive/Photos"; }
};
}
class PhotosTest : public QObject {
    Q_OBJECT
private slots:
    void foregroundContainerRequestsPhotoAccessOnceAndPublishesAfterGrant() {
        Fixture f; f.library->permission = Access::NotDetermined; f.library->add("first.jpg", "first photo");
        PhotoController controller({}, nullptr, f.library);
        controller.setApplicationActive(false);
        controller.configure(f.temporary.path() + "/drive", true);
        QTRY_VERIFY(!controller.busy()); QCOMPARE(f.library->authorizations, 0);
        controller.setApplicationActive(true);
        QTRY_COMPARE(f.library->authorizations, 1); QVERIFY(controller.busy());
        controller.refresh(); controller.connectLibrary(); controller.setApplicationActive(true);
        QCOMPARE(f.library->authorizations, 1);
        f.library->permission = Access::Full;
        std::exchange(f.library->authorizationFinished, {})();
        QTRY_COMPARE(controller.entries().size(), 1); QTRY_VERIFY(!controller.busy());
        QVERIFY(QFile::exists(f.photos() + '/' + controller.entries()[0].toMap()["id"].toString() + ".societyphoto"));
        controller.setApplicationActive(false); controller.setApplicationActive(true);
        QTRY_VERIFY(!controller.busy()); QCOMPARE(f.library->authorizations, 1);
    }
    void automaticPhotoAccessWaitsForAContainerAndNeverOpensSettings() {
        Fixture f; f.library->permission = Access::NotDetermined;
        PhotoController controller({}, nullptr, f.library);
        controller.setApplicationActive(true); controller.refresh(); QCOMPARE(f.library->authorizations, 0);
        f.library->permission = Access::Denied;
        controller.configure(f.temporary.path() + "/drive", true);
        QTRY_VERIFY(!controller.busy()); QCOMPARE(f.library->authorizations, 0);
        f.library->permission = Access::Limited; f.library->add("allowed.jpg", "selected photo");
        controller.setApplicationActive(false); controller.setApplicationActive(true);
        QTRY_COMPARE(controller.entries().size(), 1); QCOMPARE(f.library->authorizations, 0);
        controller.setApplicationActive(false); controller.setApplicationActive(true);
        QTRY_VERIFY(!controller.busy()); QCOMPARE(f.library->authorizations, 0);
    }
    void indexedPhotosAppearBeforeASlowOriginalFinishesAndReportRealProgress() {
        Fixture f; f.library->add("first.jpg", "first photo"); f.library->add("slow.mov", "second video", "video");
        QSemaphore gate;
        f.library->beforeExport = [&](const QString &token) { if (token == "slow.mov") gate.acquire(); };
        PhotoController controller({}, nullptr, f.library);
        // Release before the controller destructor joins its worker, even on a failed assertion.
        const auto release = qScopeGuard([&] { gate.release(); });
        QSignalSpy progress(&controller, &PhotoController::progress);
        controller.configure(f.temporary.path() + "/drive", true);
        QTRY_COMPARE(controller.entries().size(), 1); QVERIFY(controller.busy());
        QTRY_COMPARE(progress.size(), 1); QCOMPARE(progress[0][1].toLongLong(), qint64(11));
        QCOMPARE(progress[0][2].toLongLong(), qint64(11));
        gate.release(); QTRY_COMPARE(controller.entries().size(), 2); QTRY_VERIFY(!controller.busy());
        QCOMPARE(progress.size(), 2); QCOMPARE(progress[1][1].toLongLong(), qint64(12));
    }
    void portableNamesKeepTheirExtensionAndPlatformLimits() {
        const auto id = QString(64, 'a');
        const auto name = importedResourceName(id, 0, QString(100, QChar(0xac00)) + ":test?.png");
        QCOMPARE(importedPhotoId(name), id); QVERIFY(name.endsWith(".png")); QVERIFY(name.toUtf8().size() < 255);
        QVERIFY(!name.contains(':')); QVERIFY(!name.contains('?'));
        QVERIFY(importedPhotoId("Society-" + id + "-x-original.png").isEmpty());
    }
    void nativeOriginalsRemainAliasesAndPreviewsPersist() {
        Fixture f; f.library->add("still.jpg", "original-photo"); f.library->add("movie.mp4", "original-video", "video");
        QVERIFY2(f.store->refresh(), qPrintable(f.store->errorString()));
        const auto records = f.store->catalog(); QCOMPARE(records.size(), 2);
        QCOMPARE(f.library->imports, 0);
        for (const auto &value : records) {
            const auto record = value.toObject(); const auto id = record.value("id").toString();
            QVERIFY(QFile::exists(f.photos() + '/' + id + ".societyphoto"));
            QVERIFY(QFile::exists(f.store->previewPath(id)));
            QVERIFY(!QFile::exists(f.photos() + '/' + record.value("name").toString()));
            QVERIFY(!QJsonDocument(record).toJson().contains(f.library->directory.toUtf8()));
        }
        QFile original(f.library->directory + "/still.jpg"); QVERIFY(original.open(QIODevice::ReadOnly)); QCOMPARE(original.readAll(), QByteArray("original-photo"));
        const int exported = f.library->exports.load();
        QVERIFY(f.store->refresh()); QCOMPARE(f.library->exports, exported); QCOMPARE(f.store->catalog(), records);
        f.store->releaseExports();
        for (const auto &value : records) QVERIFY(!QFile::exists(f.store->resourcePath(value.toObject().value("id").toString(), 0)));
    }
    void localAdditionPublishesToNativeOnce() {
        Fixture f; QImage image(4, 4, QImage::Format_RGB32); image.fill(Qt::red);
        QVERIFY(image.save(f.photos() + "/new.png"));
        QVERIFY2(f.store->refresh(), qPrintable(f.store->errorString()));
        QCOMPARE(f.library->imports, 1); QCOMPARE(f.store->catalog().size(), 1);
        QVERIFY(!QFile::exists(f.photos() + "/new.png"));
        QVERIFY(f.store->refresh()); QCOMPARE(f.library->imports, 1); QCOMPARE(f.store->catalog().size(), 1);
    }
    void transferChecksHashesAndReusesNativeImport() {
        Fixture source, target;
        source.library->add("clip.mp4", QByteArray(700000, 'v'), "video"); QVERIFY(source.store->refresh());
        const auto record = source.store->catalog().first().toObject(); const auto id = record.value("id").toString();
        write(target.photos() + '/' + id + ".societyphoto", QJsonDocument(record).toJson());
        QVERIFY(target.store->refresh()); QVERIFY(!target.store->hasOriginal(id)); QVERIFY(source.store->prepare(id));
        QJsonObject base{{"id", id}, {"resource", 0}};
        auto request = base; request["action"] = "begin";
        QVERIFY(target.store->command(request).value("ok").toBool());
        for (qint64 offset = 0; offset < 700000; offset += PhotoStore::ChunkBytes) {
            request = base; request["action"] = "read"; request["offset"] = QString::number(offset);
            const auto chunk = source.store->command(request); QVERIFY(chunk.value("ok").toBool());
            request["action"] = "chunk"; request["data"] = chunk.value("data");
            QVERIFY(target.store->command(request).value("ok").toBool());
            QVERIFY(target.store->command(request).value("ok").toBool()); // Replayed acknowledgement.
        }
        request = base; request["action"] = "commit"; QVERIFY(target.store->command(request).value("ok").toBool());
        QVERIFY(target.store->consume(id)); QCOMPARE(target.library->imports, 1);
        QVERIFY(target.store->consume(id)); QVERIFY(target.store->refresh()); QCOMPARE(target.library->imports, 1);
        QCOMPARE(target.store->catalog().size(), 1);
        request["id"] = "../../outside"; QVERIFY(!target.store->command(request).value("ok").toBool());
    }
    void legacyLayoutKeepsNativeReferencesAndOriginals()
    {
        Fixture f; f.library->add("original.jpg", "unchanged original"); QVERIFY(f.store->refresh());
        const auto before = f.store->catalog(); const auto id = before.first().toObject().value("id").toString();
        const auto identity = f.store->containerId();
        const auto root = f.temporary.path() + "/drive";
        QFile manifest(root + "/.society-drive.json"); QVERIFY(manifest.open(QIODevice::ReadOnly));
        auto data = QJsonDocument::fromJson(manifest.readAll()).object(); manifest.close();
        QJsonArray legacy;
        for (const auto &entry : data.value("sections").toArray())
            if (entry.toObject().value("id") != "photos") legacy.append(entry);
        data["sections"] = legacy;
        QVERIFY(QDir().rename(root + "/Photos", root + "/Files/Photos"));
        write(manifest.fileName(), QJsonDocument(data).toJson());
        f.store = std::make_unique<PhotoStore>(root, f.library);
        QVERIFY2(f.store->open(), qPrintable(f.store->errorString()));
        QCOMPARE(f.store->containerId(), identity); QCOMPARE(f.store->catalog(), before);
        QVERIFY(f.store->hasOriginal(id)); QVERIFY(QFileInfo::exists(f.store->previewPath(id)));
        QCOMPARE(f.store->previewPath(id), root + "/Photos/.previews/" + id + ".jpg");
        const auto exports = f.library->exports.load();
        QVERIFY(f.store->refresh()); QCOMPARE(f.library->exports.load(), exports);
        QCOMPARE(f.library->imports.load(), 0);
        QVERIFY(!QFileInfo::exists(root + "/Files/Photos"));
    }

    void deniedAndLimitedAccessNeverErasePhotos() {
        Fixture f; f.library->add("a.jpg", "original"); QVERIFY(f.store->refresh());
        const auto records = f.store->catalog(); f.library->snapshot.assets.clear(); f.library->permission = Access::Limited;
        QVERIFY(f.store->refresh()); QCOMPARE(f.store->catalog(), records); QCOMPARE(f.library->removals, 0);
        f.library->permission = Access::Denied; QVERIFY(f.store->refresh()); QCOMPARE(f.store->catalog(), records);
    }
    void corruptTransferAndRedirectedCacheAreRejected() {
        Fixture source, target; source.library->add("a.jpg", "original"); QVERIFY(source.store->refresh());
        const auto record = source.store->catalog().first().toObject(); const auto id = record.value("id").toString();
        write(target.photos() + '/' + id + ".societyphoto", QJsonDocument(record).toJson()); QVERIFY(target.store->refresh());
        QJsonObject request{{"id", id}, {"resource", 0}, {"action", "begin"}};
        QVERIFY(target.store->command(request).value("ok").toBool());
        request["action"] = "chunk"; request["offset"] = "0"; request["data"] = QString::fromLatin1(QByteArray("tampered").toBase64());
        QVERIFY(target.store->command(request).value("ok").toBool());
        request["action"] = "commit"; QVERIFY(!target.store->command(request).value("ok").toBool());
        QCOMPARE(target.library->imports, 0);
        const auto outside = target.temporary.path() + "/untouched"; write(outside, "private");
        const auto cache = target.store->resourcePath(id, 0); QVERIFY(QFile::link(outside, cache));
        request["action"] = "read"; QVERIFY(!target.store->command(request).value("ok").toBool());
        QFile untouched(outside); QVERIFY(untouched.open(QIODevice::ReadOnly)); QCOMPARE(untouched.readAll(), QByteArray("private"));
    }
    void originalEditUpdatesOneObjectAndTrashIsExplicit() {
        Fixture f; f.library->add("a.jpg", "first"); QVERIFY(f.store->refresh());
        const auto id = f.store->catalog().first().toObject().value("id").toString();
        write(f.library->directory + "/a.jpg", "second"); f.library->snapshot.assets[0].stamp = "2";
        QVERIFY(f.store->refresh()); QCOMPARE(f.store->catalog().size(), 1);
        QCOMPARE(f.store->catalog().first().toObject().value("id").toString(), id);
        QVERIFY(f.store->trash(id)); QCOMPARE(f.library->removals, 1);
        QVERIFY(f.store->refresh()); QVERIFY(f.store->catalog().first().toObject().value("deleted").toBool());
        QVERIFY(!f.store->hasOriginal(id));
    }
    void unavailableAssetDoesNotBlockOtherPhotos() {
        Fixture f; f.library->add("offline.jpg", "offline"); f.library->add("ready.jpg", "ready");
        QVERIFY(QFile::remove(f.library->directory + "/offline.jpg"));
        QVERIFY(!f.store->refresh()); QCOMPARE(f.store->catalog().size(), 1);
        QCOMPARE(f.store->catalog().first().toObject().value("name").toString(), QString("ready.jpg"));
    }
    void mediaDatabaseReplacementDoesNotBecomeGlobalDeletion() {
        Fixture f; f.library->snapshot.revision = "first-volume";
        f.library->add("a.jpg", "original"); QVERIFY(f.store->refresh());
        const auto records = f.store->catalog(); f.library->snapshot.assets.clear();
        f.library->snapshot.revision = "replacement-volume";
        QVERIFY(f.store->refresh()); QVERIFY(f.store->refresh());
        QCOMPARE(f.store->catalog(), records); QCOMPARE(f.library->removals, 0);
    }
    void anotherTransferKeepsItsOriginalLease() {
        Fixture f; f.library->add("a.jpg", "original"); QVERIFY(f.store->refresh());
        const auto id = f.store->catalog().first().toObject().value("id").toString();
        const auto a = QUuid::createUuid().toString(), b = QUuid::createUuid().toString();
        auto request = QJsonObject{{"id", id}, {"action", "prepare"}, {"lease", a}};
        QVERIFY(f.store->command(request).value("ok").toBool());
        request["lease"] = b; QVERIFY(f.store->command(request).value("ok").toBool());
        request["action"] = "release"; request["lease"] = a;
        QVERIFY(f.store->command(request).value("ok").toBool());
        QVERIFY(QFile::exists(f.store->resourcePath(id, 0)));
        request["action"] = "read"; request["lease"] = b; request["resource"] = 0; request["offset"] = "0";
        QVERIFY(f.store->command(request).value("ok").toBool());
        request["action"] = "release"; QVERIFY(f.store->command(request).value("ok").toBool());
        QVERIFY(!QFile::exists(f.store->resourcePath(id, 0)));
    }
    void expiredViewingCopiesAreRemovedWithoutTouchingNativeOriginals() {
        Fixture f; f.library->add("a.jpg", "original"); QVERIFY(f.store->refresh());
        const auto id = f.store->catalog().first().toObject().value("id").toString();
        const auto path = f.store->originalForViewing(id); QVERIFY(!path.isEmpty());
        QFile copy(path); QVERIFY(copy.open(QIODevice::ReadWrite));
        QVERIFY(copy.setFileTime(QDateTime::currentDateTimeUtc().addDays(-2), QFileDevice::FileModificationTime)); copy.close();
        QVERIFY(f.store->refresh()); QVERIFY(!QFile::exists(path));
        QFile original(f.library->directory + "/a.jpg"); QVERIFY(original.open(QIODevice::ReadOnly)); QCOMPARE(original.readAll(), QByteArray("original"));
    }
    void unchangedRefreshDoesNotStartAnotherFileSync() {
        Fixture f; f.library->add("a.jpg", "original");
        PhotoController controller({}, nullptr, f.library);
        QSignalSpy contents(&controller, &PhotoController::contentsChanged);
        QSignalSpy entries(&controller, &PhotoController::entriesChanged);
        controller.configure(f.temporary.path() + "/drive", true);
        QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 5000);
        QCOMPARE(contents.count(), 1); QCOMPARE(entries.count(), 1);
        controller.refresh(); QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 5000);
        QCOMPARE(contents.count(), 1); QCOMPARE(entries.count(), 1);
    }
    void partialNativeImportCannotReplaceACompleteAlias() {
        Fixture f; f.library->add("still.jpg", "still"); f.library->add("motion.mov", "motion", "video");
        f.library->snapshot.assets[0].resources.append({"motion.mov", "motion.mov", "pairedVideo"});
        f.library->snapshot.assets.removeLast(); QVERIFY(f.store->refresh());
        const auto record = f.store->catalog().first().toObject(); const auto id = record.value("id").toString();
        auto &asset = f.library->snapshot.assets[0]; asset.resources.removeLast(); asset.stamp = "partial";
        asset.resources[0].name = importedResourceName(id, 0, "still.jpg");
        QVERIFY(!f.store->refresh()); QCOMPARE(f.store->catalog().first().toObject(), record);
        QVERIFY(!f.store->hasOriginal(id));
    }
    void remoteResourceVersionIsNotOverwrittenByAnUnchangedNativeCopy() {
        Fixture f; f.library->add("a.jpg", "first"); QVERIFY(f.store->refresh());
        auto record = f.store->catalog().first().toObject(); const auto id = record.value("id").toString();
        auto resource = record.value("resources").toArray().first().toObject();
        resource["hash"] = QString(64, 'a'); record["resources"] = QJsonArray{resource};
        write(f.photos() + '/' + id + ".societyphoto", QJsonDocument(record).toJson());
        QVERIFY(f.store->refresh()); QCOMPARE(f.store->catalog().first().toObject(), record);
        QVERIFY(!f.store->hasOriginal(id));
    }
    void completedRequestCacheDoesNotStopLongTransfers() {
        Fixture f; f.library->add("a.jpg", "original"); QVERIFY(f.store->refresh());
        const auto record = f.store->catalog().first().toObject();
        PhotoController controller({}, nullptr, f.library);
        controller.configure(f.temporary.path() + "/drive", true, {"peer"});
        QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 5000);
        for (int i = 0; i < 80; ++i) {
            const QJsonObject request{{"op", "society.photos"}, {"container", f.store->containerId()},
                {"ticket", QUuid::createUuid().toString()}, {"action", "available"}, {"id", record.value("id")}};
            QJsonObject reply;
            QTRY_VERIFY_WITH_TIMEOUT(!(reply = controller.handle("peer", request)).value("pending").toBool(), 5000);
            QVERIFY2(reply.value("ok").toBool(), qPrintable(reply.value("error").toString()));
        }
    }
    void galleryRendersAndLatePreviewKeepsBrowsingPosition() {
        Fixture f;
        for (int i = 0; i < 40; ++i) f.library->add(QString("Photo %1.jpg").arg(i), QByteArray("original-") + QByteArray::number(i));
        QVERIFY(f.store->refresh()); const auto records = f.store->catalog();
        const auto id = records.first().toObject().value("id").toString();
        QFile image(f.store->previewPath(id)); QVERIFY(image.open(QIODevice::ReadOnly)); const auto preview = image.readAll(); image.close();
        QVERIFY(image.remove()); f.library->permission = Access::Denied;
        PhotoController controller({}, nullptr, f.library);
        controller.configure(f.temporary.path() + "/drive", true);
        QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 5000);
        QQmlApplicationEngine engine; engine.addImportPath(QStringLiteral(SOCIETY_LVRS_QML_IMPORT_PATH));
        QStringList warnings;
        connect(&engine, &QQmlEngine::warnings, this, [&warnings](const QList<QQmlError> &errors) {
            for (const auto &error : errors) warnings.append(error.toString());
        });
        engine.setInitialProperties({{"controller", QVariant::fromValue(&controller)}});
        engine.load(QUrl::fromLocalFile(QStringLiteral(SOCIETY_PHOTOS_QML_FILE)));
        QVERIFY2(!engine.rootObjects().isEmpty(), qPrintable(warnings.join('\n')));
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
        auto *view = visualItem(window->contentItem(), "photosView"); QVERIFY(view);
        auto *grid = visualItem(view, "photoGrid"); QVERIFY(grid);
        QTRY_COMPARE(grid->property("count").toInt(), 40);
        view->setProperty("selectedId", id);
        QSignalSpy contentChanges(&controller, &PhotoController::contentsChanged);
        write(f.store->previewPath(id), preview);
        controller.refresh(); QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 5000);
        QQuickItem *loaded = nullptr;
        QTRY_VERIFY((loaded = visualItem(grid, "photoPreview-" + id)) != nullptr);
        QTRY_COMPARE(loaded->property("status").toInt(), 1); // Image.Ready
        QCOMPARE(contentChanges.count(), 0);
        grid->setProperty("contentY", 250.0); QTest::qWait(100);
        const auto scroll = grid->property("contentY").toReal();
        controller.refresh(); QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 5000);
        QCOMPARE(view->property("selectedId").toString(), id);
        QCOMPARE(grid->property("contentY").toReal(), scroll);
        auto *info = view->findChild<QObject *>("galleryInfo"); QVERIFY(info);
        QVERIFY(!info->property("visible").toBool());
        grid->setProperty("contentY", grid->property("originY")); QTest::qWait(100);
        auto *tile = visualItem(grid, "galleryTile"); QVERIFY(tile);
        QCOMPARE(tile->width(), tile->height());
        QCOMPARE(grid->property("cellWidth").toReal() - tile->width(), 2.0);
        QCOMPARE(tile->property("text").toString(), QString());
        auto *touchDevice = QTest::createTouchDevice(QInputDevice::DeviceType::TouchScreen);
        const auto center = tile->mapToScene(QPointF(tile->width()/2, tile->height()/2)).toPoint();
        QTest::touchEvent(window, touchDevice).press(0, center).commit();
        QTest::touchEvent(window, touchDevice).release(0, center).commit();
        QTRY_VERIFY(info->property("visible").toBool());
        QCOMPARE(info->property("fileName").toString(), tile->property("name").toString());
        QVERIFY(QDir().mkpath(QStringLiteral(SOCIETY_TEST_DIRECTORY "/photos")));
        QTest::qWait(250);
        QVERIFY(window->grabWindow().save(QStringLiteral(SOCIETY_TEST_DIRECTORY "/photos/information-desktop.png")));
        QVERIFY(QMetaObject::invokeMethod(info, "close")); QTRY_VERIFY(!info->property("visible").toBool()); QTest::qWait(250);
        const auto beforePinch = grid->property("cellWidth").toReal();
        const auto middle = grid->mapToScene(QPointF(grid->width()/2, grid->height()/2)).toPoint();
        QTest::touchEvent(window, touchDevice).press(0, middle-QPoint(40,0)).press(1, middle+QPoint(40,0)).commit();
        for (int distance = 50; distance <= 120; distance += 10) {
            QTest::touchEvent(window, touchDevice).move(0, middle-QPoint(distance,0)).move(1, middle+QPoint(distance,0)).commit(); QTest::qWait(20);
        }
        QTest::touchEvent(window, touchDevice).release(0, middle-QPoint(120,0)).release(1, middle+QPoint(120,0)).commit();
        QTRY_VERIFY(grid->property("cellWidth").toReal() > beforePinch);
        QCOMPARE(grid->property("cellWidth"), grid->property("cellHeight"));
        QVERIFY(!info->property("visible").toBool());
        const auto enlarged = grid->property("cellWidth").toReal();
        QTest::touchEvent(window, touchDevice).press(0, middle-QPoint(120,0)).press(1, middle+QPoint(120,0)).commit();
        for (int distance = 110; distance >= 40; distance -= 10) {
            QTest::touchEvent(window, touchDevice).move(0, middle-QPoint(distance,0)).move(1, middle+QPoint(distance,0)).commit(); QTest::qWait(20);
        }
        QTest::touchEvent(window, touchDevice).release(0, middle-QPoint(40,0)).release(1, middle+QPoint(40,0)).commit();
        QTRY_VERIFY(grid->property("cellWidth").toReal() < enlarged);
        QVERIFY(!info->property("visible").toBool());
        auto *zoom = view->findChild<QObject *>("galleryZoom"); QVERIFY(zoom);
        zoom->setProperty("thumbnailSize", 128.0);
        view->setProperty("selectedId", id);
        QVERIFY(QDir().mkpath(QStringLiteral(SOCIETY_TEST_DIRECTORY "/photos")));
        QTest::qWait(100); QVERIFY(window->grabWindow().save(QStringLiteral(SOCIETY_TEST_DIRECTORY "/photos/gallery-desktop.png")));
        window->resize(390, 844); QTest::qWait(100);
        const auto *add = visualItem(view, "addPhotos"); QVERIFY(add);
        QVERIFY(add->mapToItem(view, QPointF(add->width(), 0)).x() <= view->width());
        QVERIFY(window->grabWindow().save(QStringLiteral(SOCIETY_TEST_DIRECTORY "/photos/gallery-mobile.png")));
        auto deleted = records.first().toObject(); deleted["deleted"] = true;
        write(f.photos() + '/' + id + ".societyphoto", QJsonDocument(deleted).toJson());
        controller.refresh(); QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 5000);
        QTRY_COMPARE(grid->property("count").toInt(), 39);
        QCOMPARE(view->property("selectedId").toString(), QString());
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
        window->close();
    }
    void authenticatedExchangeUnifiesBothNativeLibraries() {
        Fixture host, client;
        const auto hostDrive = iiSocietyContainer::SocietyDrive::open(host.temporary.path() + "/drive");
        const auto clientDrive = iiSocietyContainer::SocietyDrive::open(client.temporary.path() + "/drive");
        QVERIFY(iiSocietyContainer::SocietyDrive::adoptReplicaIdentity(clientDrive->rootPath(), clientDrive->identifier(), hostDrive->identifier()));
        QVERIFY(iiSocietyContainer::SocietyDrive::completeReplica(clientDrive->rootPath(), hostDrive->identifier()));
        client.store = std::make_unique<PhotoStore>(clientDrive->rootPath(), client.library); QVERIFY(client.store->open());
        host.library->add("host.jpg", QByteArray(530000, 'h')); client.library->add("phone.mp4", QByteArray(720000, 'p'), "video");
        QVERIFY(host.store->refresh()); QVERIFY(client.store->refresh());
        const auto hostRecord = host.store->catalog().first().toObject(), clientRecord = client.store->catalog().first().toObject();
        // Portable aliases use the ordinary container sync path. Originals use
        // the authenticated, resumable photo channel tested below.
        write(client.photos() + '/' + hostRecord.value("id").toString() + ".societyphoto", QJsonDocument(hostRecord).toJson());
        write(host.photos() + '/' + clientRecord.value("id").toString() + ".societyphoto", QJsonDocument(clientRecord).toJson());
        host.store->releaseExports(); client.store->releaseExports();
        PhotoController receiver([](const auto &, const auto &) { return QString(); }, nullptr, host.library);
        PhotoController *clientController = nullptr;
        int requests = 0;
        PhotoController sender([&](const QString &peer, const QJsonObject &packet) {
            ++requests; const auto ticket = QString::number(requests);
            const auto reply = peer == "host" ? receiver.handle("client", packet) : QJsonObject{};
            QTimer::singleShot(0, &receiver, [&, ticket, reply] { clientController->receive(ticket, reply); });
            return ticket;
        }, nullptr, client.library);
        clientController = &sender;
        receiver.configure(hostDrive->rootPath(), true, {"client"});
        QTRY_VERIFY_WITH_TIMEOUT(!receiver.busy(), 10000);
        const QJsonObject packet{{"op", "society.photos"}, {"container", hostDrive->identifier()},
            {"ticket", QUuid::createUuid().toString()}, {"action", "available"}, {"id", hostRecord.value("id")}};
        QVERIFY(!receiver.handle("stranger", packet).value("ok").toBool());
        sender.configure(clientDrive->rootPath(), true, {"host"}, {"host"});
        QTRY_COMPARE_WITH_TIMEOUT(host.library->imports.load(), 1, 20000);
        QTRY_COMPARE_WITH_TIMEOUT(client.library->imports.load(), 1, 20000);
        QTRY_VERIFY_WITH_TIMEOUT(!sender.busy(), 10000);
        QVERIFY(requests > 10);
        receiver.configure({}, false); sender.configure({}, false);
    }
    void nativeLibrariesExchangeAliasesPreviewsAndOriginalsOverTls() {
        Fixture desktop, phone;
        desktop.library->add("desktop.jpg", QByteArray(600000, 'd'));
        phone.library->add("phone.mp4", QByteArray(800000, 'p'), "video");
        QVERIFY(desktop.store->refresh());
        const auto desktopRecord = desktop.store->catalog().first().toObject();
        iiServerHost::LanPeer host, client;
        iiSocietySync::Controller hosting({}), syncing([&](const auto &peer, const auto &packet) { return client.request(peer, packet); });
        PhotoController hostPhotos({}, nullptr, desktop.library);
        PhotoController phonePhotos([&](const auto &peer, const auto &packet) { return client.request(peer, packet); }, nullptr, phone.library);
        const auto files = iiSocietySync::filesHandler(desktop.temporary.path() + "/drive");
        connect(&client, &iiServerHost::LanPeer::completed, this, [&](const auto &id, const auto &result, const auto &transport) {
            QCOMPARE(transport, QString("local")); syncing.receive(id, result); phonePhotos.receive(id, result);
        });
        QVERIFY(host.startHost("desktop", "Desktop", [&](const auto &peer, const auto &packet) {
            if (packet.value("op") == "society.photos") return hostPhotos.handle(peer, packet);
            return packet.value("op") == "society.sync" ? hosting.handle(peer, packet) : files(peer, packet);
        }, {"127.0.0.1"}, QHostAddress::LocalHost));
        QVERIFY(client.join(host.createOffer(), "phone", "Phone")); QTRY_VERIFY2(client.connected(), qPrintable(client.errorString()));
        hosting.open(desktop.temporary.path() + "/drive", QString(64, 'a'));
        syncing.open(phone.temporary.path() + "/drive", QString(64, 'a'));
        QTRY_VERIFY(hosting.available() && syncing.available());
        hosting.setPeers({"phone"}, {});
        QSignalSpy mirrored(&syncing, &iiSocietySync::Controller::synchronized);
        syncing.setPeers({"desktop"}, {"desktop"});
        QTRY_VERIFY2_WITH_TIMEOUT(!mirrored.isEmpty(), qPrintable(syncing.errorString()), 15000);
        phone.store = std::make_unique<PhotoStore>(phone.temporary.path() + "/drive", phone.library); QVERIFY(phone.store->open());
        const auto desktopId = desktopRecord.value("id").toString();
        QVERIFY(QFile::exists(phone.photos() + '/' + desktopId + ".societyphoto"));
        QCOMPARE(PhotoStore::digestFile(phone.store->previewPath(desktopId)), PhotoStore::digestFile(desktop.store->previewPath(desktopId)));
        connect(&hostPhotos, &PhotoController::contentsChanged, &hosting, &iiSocietySync::Controller::synchronizeNow);
        connect(&phonePhotos, &PhotoController::contentsChanged, &syncing, &iiSocietySync::Controller::synchronizeNow);
        connect(&syncing, &iiSocietySync::Controller::synchronized, &phonePhotos, &PhotoController::refresh);
        hostPhotos.configure(desktop.temporary.path() + "/drive", true, {"phone"});
        QTRY_VERIFY_WITH_TIMEOUT(!hostPhotos.busy(), 5000);
        phonePhotos.configure(phone.temporary.path() + "/drive", true, {"desktop"}, {"desktop"});
        QTRY_COMPARE_WITH_TIMEOUT(desktop.library->imports.load(), 1, 20000);
        QTRY_COMPARE_WITH_TIMEOUT(phone.library->imports.load(), 1, 20000);
        QTRY_VERIFY_WITH_TIMEOUT(!phonePhotos.busy(), 10000);
        QCOMPARE(desktop.library->exports.load() > 0, true);
        QCOMPARE(phone.library->exports.load() > 0, true);
        phonePhotos.trashPhoto(desktopId);
        QTRY_COMPARE_WITH_TIMEOUT(phone.library->removals.load(), 1, 5000);
        const auto remotelyDeleted = [&] {
            QFile file(desktop.photos() + '/' + desktopId + ".societyphoto");
            return file.open(QIODevice::ReadOnly) && QJsonDocument::fromJson(file.readAll()).object().value("deleted").toBool();
        };
        QTRY_VERIFY_WITH_TIMEOUT(remotelyDeleted(), 10000);
        hostPhotos.refresh(); QTRY_COMPARE_WITH_TIMEOUT(desktop.library->removals.load(), 1, 5000);
        phonePhotos.configure({}, false); hostPhotos.configure({}, false);
        syncing.closeAndWait(); hosting.closeAndWait(); client.stop(); host.stop();
    }
};
int main(int argc, char **argv) {
    lvrs::AppBootstrapOptions options;
    options.applicationName = "SocietyPhotoTests"; options.quickStyleName = "Basic";
    options.bootstrapGraphicsBackend = false; options.configureRenderQualityDefaults = false;
    options.logBootstrapDiagnostics = false; options.logGraphicsBackend = false;
    if (!lvrs::preApplicationBootstrap(options).ok) return 1;
    QGuiApplication app(argc, argv); lvrs::postApplicationBootstrap(app, options);
    PhotosTest test; return QTest::qExec(&test, argc, argv);
}
#include "tst_photos.moc"
