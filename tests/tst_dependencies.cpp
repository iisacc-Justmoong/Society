#include <iiSocietyHelper>
#include <iiSocietyContainer>
#include <iiWebServer>
#include <QFile>
#include <QJsonArray>
#include "CTestSupport.h"
#include "QtEventWait.h"
#include <QCoreApplication>
#include <QEvent>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>

class DependencyTest final
{
public:
    void fileProjectionHidesProviderControlFiles()
    {
        QTemporaryDir directory(SOCIETY_TEST_DIRECTORY "/share-boundary-XXXXXX");
        CTEST_CHECK(directory.isValid());
        for (const auto *name : {"original.txt", ".original.txt.iisacc-preview", ".iisacc-temporary.lock"}) {
            QFile file(directory.filePath(QString::fromLatin1(name)));
            CTEST_CHECK(file.open(QIODevice::WriteOnly)); CTEST_CHECK(file.write("fixture") > 0);
        }
        iiWebServer::FileShare share(directory.path());
        const auto listed = share.handle({{"op", "list"}, {"path", ""}});
        CTEST_CHECK(listed.value("ok").toBool());
        QStringList names;
        for (const auto &entry : listed.value("entries").toArray()) names.append(entry.toObject().value("name").toString());
        CTEST_COMPARE(names, QStringList{"original.txt"});
        for (const auto *name : {".original.txt.iisacc-preview", ".iisacc-temporary.lock"})
            CTEST_CHECK(!share.handle({{"op", "stat"}, {"path", QString::fromLatin1(name)}}).value("ok").toBool());
    }

    void packagedApplicationObservesPeers()
    {
        QTemporaryDir presence(SOCIETY_TEST_DIRECTORY "/helper-XXXXXX.societycontainer");
        CTEST_CHECK(iiSocietyContainer::SocietyDrive::create(presence.path()));
        const auto directory = presence.filePath(".society-runtime/helpers/v1");
        iiSocietyHelper::Helper observer;
        CTEST_CHECK(observer.start({"com.iisacc.society.test", "Society test", "1"},
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
        CTEST_CHECK(app.waitForStarted());
        QByteArray output;
        QElapsedTimer elapsed;
        elapsed.start();
        while (elapsed.elapsed() < 90000 && (observer.peers().isEmpty()
               || !output.contains("com.iisacc.society observed com.iisacc.society.test"))) {
            society_ctest::processFor(50);
            output += app.readAll();
        }
        const auto peers = observer.peers();
        const auto beforeStop = app.state(); const auto beforeExit = app.exitCode();
        app.terminate();
        if (!app.waitForFinished(3000)) { app.kill(); app.waitForFinished(3000); }
        output += app.readAll();
        CTEST_CHECK_MESSAGE(peers.size() == 1, qPrintable(QString("state=%1 exit=%2: %3").arg(beforeStop).arg(beforeExit).arg(QString::fromUtf8(output))));
        CTEST_COMPARE(peers.first().application.id, "com.iisacc.society");
        CTEST_CHECK_MESSAGE(output.contains("com.iisacc.society observed com.iisacc.society.test"), output.constData());
    }

    void directDependenciesMatchProductFeatures()
    {
        QFile file(QString::fromUtf8(SOCIETY_DIRECT_LINKS_FILE));
        CTEST_CHECK(file.open(QIODevice::ReadOnly));
        const auto links = QString::fromUtf8(file.readAll()).split('\n', Qt::SkipEmptyParts);
        CTEST_COMPARE(links.count(QStringLiteral("iiPhotoLibrary::iiPhotoLibrary")), 1);
#ifdef SOCIETY_TEST_WITH_LOCAL_MCP
        CTEST_CHECK(links.contains(QStringLiteral("iiMultimodalAgent::iiMultimodalAgent")));
        CTEST_CHECK(!links.contains(QStringLiteral("iiLocalLLM::iiLocalLLM")));
#endif
        for (const auto *target : {"iiAccountManager::Quick", "iiCalendar::iiCalendar",
                "iiPhotoLibrary::iiPhotoLibrary", "iiSocietyContainer::Gui",
                "iiSocietyHelper::iiSocietyHelper",
                "iiFileProvider::Native", "SocietyModelPackaging"})
            CTEST_CHECK_MESSAGE(links.contains(QString::fromLatin1(target)), target);
        for (const auto *target : {"iiGeneralDocument::iiGeneralDocument",
                "iiLicenseManager::iiLicenseManager", "iiUpdateManager::iiUpdateManager",
                "iiVoiceOver::iiVoiceOver", "iiWhatsNew::iiWhatsNew",
                "iiSharedCanvas::Core", "iiSharedCanvas::iiSharedCanvas",
                "iiLocalDiffusion::iiLocalDiffusion"})
            CTEST_CHECK_MESSAGE(!links.contains(QString::fromLatin1(target)), target);
        for (const auto *target : {"iiSocietySync::iiSocietySync", "iiSocietyMonitor::iiSocietyMonitor",
                "iiSocietyGeneration::Remote", "iiSocietyGeneration::Host"})
            CTEST_CHECK_MESSAGE(!links.contains(QString::fromLatin1(target)), target);
    }

};

int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    society_ctest::Runner runner(argc == 2 ? argv[1] : "");
    DependencyTest tests;
    struct FlushDeferredDeletes {
        ~FlushDeferredDeletes() { QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); }
    };
    runner.run("fileProjectionHidesProviderControlFiles", [&] { FlushDeferredDeletes cleanup; tests.fileProjectionHidesProviderControlFiles(); });
    runner.run("packagedApplicationObservesPeers", [&] { FlushDeferredDeletes cleanup; tests.packagedApplicationObservesPeers(); });
    runner.run("directDependenciesMatchProductFeatures", [&] { FlushDeferredDeletes cleanup; tests.directDependenciesMatchProductFeatures(); });
    return runner.result();
}
