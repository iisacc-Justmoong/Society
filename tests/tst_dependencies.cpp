#include <iiFilePreview.h>
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

using GreetingFunction = QString (*)();
Q_DECLARE_METATYPE(GreetingFunction)

class DependencyTest final : public QObject
{
    Q_OBJECT

private slots:
    void bootstrapSdkSymbols_data()
    {
        QTest::addColumn<GreetingFunction>("greeting");
        QTest::newRow("iiFilePreview") << &iiFilePreview::helloWorld;
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
