#include "App/Files/AppleModelSource.h"
#include "App/Files/ModelImporter.h"
#include <SocietyDrive.h>
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#import <Foundation/Foundation.h>

class AppleModelSourceTest : public QObject
{
    Q_OBJECT
private slots:
    void pickerUrlRemainsReadableUntilImportFinishes()
    {
        QTemporaryDir source(SOCIETY_TEST_DIRECTORY "/picker-source-XXXXXX");
        QTemporaryDir target(SOCIETY_TEST_DIRECTORY "/picker-target-XXXXXX");
        QVERIFY(iiSocietyContainer::SocietyDrive::create(target.path()));
        QFile file(source.filePath("선택한 모델.safetensors"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("picked model bytes");
        file.close();
        ModelImportSource picked;
        @autoreleasepool {
            picked = appleModelSource([NSURL fileURLWithPath:file.fileName().toNSString()]);
        }
        ModelImporter importer;
        importer.setContainerPath(target.path());
        QSignalSpy done(&importer, &ModelImporter::finished);
        QVERIFY(importer.importSources({picked}));
        QTRY_COMPARE(done.size(), 1);
        QVERIFY2(importer.errorString().isEmpty(), qPrintable(importer.errorString()));
        QFile imported(target.filePath("Models/선택한 모델.safetensors"));
        QVERIFY(imported.open(QIODevice::ReadOnly));
        QCOMPARE(imported.readAll(), QByteArray("picked model bytes"));
        QVERIFY(file.exists());
        QVERIFY(QDir(target.filePath("Files")).isEmpty());
    }

    void importsWhileTheProviderGrantsAccess()
    {
        QTemporaryDir source(SOCIETY_TEST_DIRECTORY "/apple-source-XXXXXX");
        QTemporaryDir target(SOCIETY_TEST_DIRECTORY "/apple-drive-XXXXXX");
        QVERIFY(iiSocietyContainer::SocietyDrive::create(target.path()));
        const auto path = source.filePath("provider-file.tmp");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("provided model bytes");
        file.close();
        NSItemProvider *provider = [NSItemProvider new];
        provider.suggestedName = @"mobile weights.safetensor";
        [provider registerFileRepresentationForTypeIdentifier:@"public.data"
            fileOptions:NSItemProviderFileOptionOpenInPlace visibility:NSItemProviderRepresentationVisibilityAll
            loadHandler:^NSProgress *(void (^completion)(NSURL *, BOOL, NSError *)) {
            completion([NSURL fileURLWithPath:path.toNSString()], YES, nil);
            return nil;
        }];
        ModelImporter importer;
        importer.setContainerPath(target.path());
        QSignalSpy done(&importer, &ModelImporter::finished);
        QVERIFY(importer.importSources({appleModelSource(provider)}));
        QTRY_COMPARE(done.size(), 1);
        QVERIFY2(importer.errorString().isEmpty(), qPrintable(importer.errorString()));
        QFile imported(target.filePath("Models/mobile weights.safetensor"));
        QVERIFY(imported.open(QIODevice::ReadOnly));
        QCOMPARE(imported.readAll(), QByteArray("provided model bytes"));
        QVERIFY(file.exists());
        QVERIFY(QDir(target.filePath("Files")).isEmpty());
    }

    void cancellationIgnoresALateProviderCallback()
    {
        QTemporaryDir target(SOCIETY_TEST_DIRECTORY "/apple-drive-XXXXXX");
        QVERIFY(iiSocietyContainer::SocietyDrive::create(target.path()));
        auto requested = std::make_shared<std::atomic_bool>(false);
        __block void (^reply)(NSURL *, BOOL, NSError *) = nil;
        NSItemProvider *provider = [NSItemProvider new];
        provider.suggestedName = @"delayed.safetensors";
        [provider registerFileRepresentationForTypeIdentifier:@"public.data"
            fileOptions:NSItemProviderFileOptionOpenInPlace visibility:NSItemProviderRepresentationVisibilityAll
            loadHandler:^NSProgress *(void (^completion)(NSURL *, BOOL, NSError *)) {
            reply = [completion copy];
            requested->store(true);
            return [NSProgress progressWithTotalUnitCount:1];
        }];
        ModelImporter importer;
        importer.setContainerPath(target.path());
        QSignalSpy done(&importer, &ModelImporter::finished);
        QVERIFY(importer.importSources({appleModelSource(provider)}));
        QTRY_VERIFY(requested->load());
        importer.cancel();
        QTRY_COMPARE(done.size(), 1);
        QVERIFY(!importer.busy());
        QVERIFY(QDir(target.filePath("Models")).isEmpty());
        QVERIFY(reply);
        reply(nil, NO, [NSError errorWithDomain:NSCocoaErrorDomain code:NSUserCancelledError userInfo:nil]);
        QTest::qWait(150);
        QCOMPARE(done.size(), 1);
        QVERIFY(QDir(target.filePath("Models")).isEmpty());
    }
};

QTEST_GUILESS_MAIN(AppleModelSourceTest)
#include "tst_applemodelsource.moc"
