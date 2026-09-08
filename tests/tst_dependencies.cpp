#include <iiFileProvider.h>
#include <iiGeneralDocument/Model/Document.h>
#include <iiLicenseManager/LicenseClient.h>
#include <Compute/ComputeRuntime.hpp>
#include <iiLocalLLM.h>
#include <iiServerHost.h>
#include <iiSharedCanvas/Document/Document.h>
#include <iiSocietyContainer.h>
#include <iiSocietyHelper.h>
#include <iiSocietySync.h>
#include <iiUpdateManager/UpdateManager.h>
#include <iiVoiceOver.h>
#include <iiWhatsNew.h>

#include <QMetaEnum>
#include <QTest>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>

using GreetingFunction = QString (*)();
Q_DECLARE_METATYPE(GreetingFunction)

class DependencyTest final : public QObject
{
    Q_OBJECT

private slots:
    void packagedApplicationObservesPeers()
    {
        QTemporaryDir presence(SOCIETY_TEST_DIRECTORY "/helper-XXXXXX");
        iiSocietyHelper::Helper observer;
        QVERIFY(observer.start({"com.iisacc.society.test", "Society test", "1"},
                                {presence.path(), 100, 5000}));
        auto environment = QProcessEnvironment::systemEnvironment();
        environment.insert("SOCIETY_HELPER_DIRECTORY", presence.path());
        environment.insert("SOCIETY_STORAGE_SETTINGS_PATH", presence.filePath("storage.json"));
        environment.insert("QT_QPA_PLATFORM", "offscreen");
        environment.insert("QT_QUICK_BACKEND", "software");
        environment.insert("QML_DISABLE_DISK_CACHE", "1");
        QProcess app;
        app.setProcessEnvironment(environment);
        app.setProcessChannelMode(QProcess::MergedChannels);
        app.start(QStringLiteral(SOCIETY_EXECUTABLE_PATH), {});
        QVERIFY(app.waitForStarted());
        QByteArray output;
        QElapsedTimer elapsed;
        elapsed.start();
        while (elapsed.elapsed() < 6000 && (observer.peers().isEmpty()
               || !output.contains("com.iisacc.society observed com.iisacc.society.test"))) {
            QTest::qWait(50);
            output += app.readAll();
        }
        const auto peers = observer.peers();
        app.terminate();
        if (!app.waitForFinished(3000)) { app.kill(); app.waitForFinished(3000); }
        QCOMPARE(peers.size(), 1);
        QCOMPARE(peers.first().application.id, "com.iisacc.society");
        QVERIFY2(output.contains("com.iisacc.society observed com.iisacc.society.test"), output.constData());
    }

    void bootstrapSdkSymbols_data()
    {
        QTest::addColumn<GreetingFunction>("greeting");
        QTest::newRow("iiFileProvider") << &iiFileProvider::helloWorld;
        QTest::newRow("iiLocalLLM") << &iiLocalLLM::helloWorld;
        QTest::newRow("iiServerHost") << &iiServerHost::helloWorld;
        QTest::newRow("iiSocietyContainer") << &iiSocietyContainer::helloWorld;
        QTest::newRow("iiSocietyHelper") << &iiSocietyHelper::helloWorld;
        QTest::newRow("iiSocietySync") << &iiSocietySync::helloWorld;
        QTest::newRow("iiVoiceOver") << &iiVoiceOver::helloWorld;
        QTest::newRow("iiWhatsNew") << &iiWhatsNew::helloWorld;
    }

    void bootstrapSdkSymbols()
    {
        QFETCH(GreetingFunction, greeting);
        QCOMPARE(greeting(), QStringLiteral("Hello world!"));
    }

    void generalDocumentApi()
    {
        ii::document::Document document;
        QVERIFY(document.pages().empty());
        document.metadata()["title"] = "Society";
        QCOMPARE(document.metadata().at("title"), std::string("Society"));
    }

    void licenseManagerApi()
    {
        using State = iisacc::licensing::LicenseClient::State;
        const QMetaEnum states = QMetaEnum::fromType<State>();
        QVERIFY(states.isValid());
        QCOMPARE(states.keyToValue("Idle"), static_cast<int>(State::Idle));
    }

    void localDiffusionApi()
    {
        QCOMPARE(iild::computeDeviceName(iild::ComputeDevice::cpu), std::string_view("cpu"));
    }

    void sharedCanvasApi()
    {
        iiSharedCanvas::VectorAsset vectorAsset;
        vectorAsset.id = "society";
        const iiSharedCanvas::Asset asset = vectorAsset;
        QCOMPARE(iiSharedCanvas::assetId(asset), std::string("society"));
        QCOMPARE(iiSharedCanvas::contentKind(asset), iiSharedCanvas::ContentKind::Vector);
    }

    void updateManagerApi()
    {
        using State = iisacc::updates::UpdateManager::State;
        const QMetaEnum states = QMetaEnum::fromType<State>();
        QVERIFY(states.isValid());
        QCOMPARE(states.keyToValue("Idle"), static_cast<int>(State::Idle));
    }
};

QTEST_GUILESS_MAIN(DependencyTest)

#include "tst_dependencies.moc"
