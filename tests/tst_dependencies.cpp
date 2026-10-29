#include <iiSocietyHelper.h>
#include <iiSocietyContainer.h>
#include <iiWebServer.h>
#include <QFile>
#include <QJsonArray>
#include <QTest>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <SharedStorage.h>

class DependencyTest final : public QObject
{
    Q_OBJECT

private slots:
    void fileProjectionHidesProviderControlFiles()
    {
        QTemporaryDir directory(SOCIETY_TEST_DIRECTORY "/share-boundary-XXXXXX");
        QVERIFY(directory.isValid());
        for (const auto *name : {"original.txt", ".original.txt.iisacc-preview", ".iisacc-temporary.lock"}) {
            QFile file(directory.filePath(QString::fromLatin1(name)));
            QVERIFY(file.open(QIODevice::WriteOnly)); QVERIFY(file.write("fixture") > 0);
        }
        iiWebServer::FileShare share(directory.path());
        const auto listed = share.handle({{"op", "list"}, {"path", ""}});
        QVERIFY(listed.value("ok").toBool());
        QStringList names;
        for (const auto &entry : listed.value("entries").toArray()) names.append(entry.toObject().value("name").toString());
        QCOMPARE(names, QStringList{"original.txt"});
        for (const auto *name : {".original.txt.iisacc-preview", ".iisacc-temporary.lock"})
            QVERIFY(!share.handle({{"op", "stat"}, {"path", QString::fromLatin1(name)}}).value("ok").toBool());
    }

    void packagedApplicationObservesPeers()
    {
        QTemporaryDir presence(SOCIETY_TEST_DIRECTORY "/helper-XXXXXX.societycontainer");
        QVERIFY(iiSocietyContainer::SocietyDrive::create(presence.path()));
        const auto directory = presence.filePath(".society-runtime/helpers/v1");
        iiSocietyHelper::Helper observer;
        QVERIFY(observer.start({"com.iisacc.society.test", "Society test", "1"},
                                {directory, 1000, 15000}));
        auto environment = QProcessEnvironment::systemEnvironment();
        for (const auto *name : {"DYLD_LIBRARY_PATH", "DYLD_FRAMEWORK_PATH", "DYLD_FALLBACK_LIBRARY_PATH",
                "QML_IMPORT_PATH", "QML2_IMPORT_PATH"})
            environment.remove(QString::fromLatin1(name));
        environment.insert("SOCIETY_HELPER_DIRECTORY", directory);
        environment.insert("SOCIETY_CONTAINER_PATH", presence.path());
        environment.insert("SOCIETY_STORAGE_SETTINGS_PATH", presence.filePath("storage.json"));
        environment.insert("SOCIETY_DISABLE_SESSION_RESTORE", "1");
        environment.insert("SOCIETY_DISABLE_NATIVE_DRIVE", "1");
        environment.insert("SOCIETY_DISABLE_PHOTOS", "1");
        environment.insert("IISOCIETYSYNC_DISABLE_BLE", "1");
        environment.insert("IILOCALLLM_DISABLE_APP_MCP", "1");
        environment.insert("QT_QPA_PLATFORM", "offscreen");
        environment.insert("QT_QUICK_BACKEND", "software");
        environment.insert("QML_DISABLE_DISK_CACHE", "1");
        QProcess app;
        app.setProcessEnvironment(environment);
        app.setProcessChannelMode(QProcess::MergedChannels);
        app.start(QStringLiteral(SOCIETY_EXECUTABLE_PATH), {"--container", presence.path()});
        QVERIFY(app.waitForStarted());
        QByteArray output;
        QElapsedTimer elapsed;
        elapsed.start();
        while (elapsed.elapsed() < 90000 && (observer.peers().isEmpty()
               || !output.contains("com.iisacc.society observed com.iisacc.society.test"))) {
            QTest::qWait(50);
            output += app.readAll();
        }
        const auto peers = observer.peers();
        const auto beforeStop = app.state(); const auto beforeExit = app.exitCode();
        app.terminate();
        if (!app.waitForFinished(3000)) { app.kill(); app.waitForFinished(3000); }
        output += app.readAll();
        QVERIFY2(peers.size() == 1, qPrintable(QString("state=%1 exit=%2: %3").arg(beforeStop).arg(beforeExit).arg(QString::fromUtf8(output))));
        QCOMPARE(peers.first().application.id, "com.iisacc.society");
        QVERIFY2(output.contains("com.iisacc.society observed com.iisacc.society.test"), output.constData());
    }

    void directDependenciesMatchProductFeatures()
    {
        QFile file(QString::fromUtf8(SOCIETY_DIRECT_LINKS_FILE));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto links = QString::fromUtf8(file.readAll()).split('\n', Qt::SkipEmptyParts);
        QCOMPARE(links.count(QStringLiteral("iiPhotoLibrary::iiPhotoLibrary")), 1);
#ifdef SOCIETY_TEST_WITH_LOCAL_MCP
        QVERIFY(links.contains(QStringLiteral("iiMultimodalAgent::iiMultimodalAgent")));
        QVERIFY(!links.contains(QStringLiteral("iiLocalLLM::iiLocalLLM")));
#endif
        for (const auto *target : {"iiAccountManager::Quick", "iiCalendar::iiCalendar",
                "iiPhotoLibrary::iiPhotoLibrary", "iiSocietyContainer::Gui",
                "iiSocietyHelper::iiSocietyHelper",
                "iiFileProvider::Native", "SocietyModelPackaging"})
            QVERIFY2(links.contains(QString::fromLatin1(target)), target);
        for (const auto *target : {"iiGeneralDocument::iiGeneralDocument",
                "iiLicenseManager::iiLicenseManager", "iiUpdateManager::iiUpdateManager",
                "iiVoiceOver::iiVoiceOver", "iiWhatsNew::iiWhatsNew",
                "iiSharedCanvas::Core", "iiSharedCanvas::iiSharedCanvas",
                "iiLocalDiffusion::iiLocalDiffusion"})
            QVERIFY2(!links.contains(QString::fromLatin1(target)), target);
        for (const auto *target : {"iiSocietySync::iiSocietySync", "iiSocietyMonitor::iiSocietyMonitor",
                "iiSocietyGeneration::Remote", "iiSocietyGeneration::Host"})
            QVERIFY2(!links.contains(QString::fromLatin1(target)), target);
    }

};

QTEST_GUILESS_MAIN(DependencyTest)

#include "tst_dependencies.moc"
