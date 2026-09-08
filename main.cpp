#include "backend/runtime/appentry.h"
#include <iiSocietyHelper.h>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include "App/Services/SocietyRuntime.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
#ifdef SOCIETY_IOS_FILES_INTEGRATION_TEST
#include <QTimer>
extern "C" void society_ios_files_integration_test();
#endif
#ifdef Q_OS_MACOS
#include "Daemon/platform/macos/ServiceRegistration.h"
#endif

int main(int argc, char *argv[])
{
#ifdef Q_OS_MACOS
    if (argc >= 2 && QString::fromLocal8Bit(argv[1]) == "--daemon-service") {
        QCoreApplication app(argc, argv);
        const auto result = societyDaemonRegistration(argc > 2 ? QString::fromLocal8Bit(argv[2]) : "status");
        QTextStream(stdout) << QJsonDocument(QJsonObject::fromVariantMap(result)).toJson(QJsonDocument::Compact) << Qt::endl;
        return result.value("ok").toBool() ? 0 : 1;
    }
#endif
    lvrs::QmlAppLaunchSpec launchSpec;
    launchSpec.bootstrap.applicationName = QStringLiteral("Society");
    launchSpec.bootstrap.quickStyleName = QStringLiteral("Basic");
    launchSpec.moduleUri = QStringLiteral("Society");
    launchSpec.rootObject = QStringLiteral("Main");
    launchSpec.qmlImportPaths.append(QString::fromUtf8(SOCIETY_LVRS_QML_IMPORT_PATH));
    launchSpec.configureEngine = [](QQmlApplicationEngine &engine) {
#ifdef SOCIETY_IOS_FILES_INTEGRATION_TEST
        QTimer::singleShot(2000, &engine, society_ios_files_integration_test);
#endif
        auto *runtime = new SocietyRuntime(
#if defined(Q_OS_IOS) || defined(Q_OS_ANDROID)
            true,
#else
            false,
#endif
            {}, &engine);
        auto *helper = runtime->helper();
        helper->setObjectName(QStringLiteral("societyHelper"));
        engine.rootContext()->setContextProperty(QStringLiteral("societyHelper"), helper);
        auto *inbox = runtime->inbox();
        inbox->setObjectName(QStringLiteral("societyInbox"));
        engine.rootContext()->setContextProperty(QStringLiteral("societyInbox"), inbox);
        QObject::connect(qGuiApp, &QGuiApplication::applicationStateChanged, runtime, &SocietyRuntime::setApplicationState);
        runtime->setApplicationState(QGuiApplication::applicationState());
#ifdef Q_OS_MACOS
        // Isolated SDK/app tests must not change the user's login services.
        if (!qEnvironmentVariableIsSet("SOCIETY_HELPER_DIRECTORY")) {
            const auto result = societyDaemonRegistration("register");
            if (!result.value("ok").toBool()) qWarning() << "Society daemon service" << result;
        }
#endif
    };
    for (int index = 1; index < argc; ++index) {
        if (QString::fromLocal8Bit(argv[index]) == QStringLiteral("--container") && index + 1 < argc) {
            launchSpec.initialProperties.insert(QStringLiteral("initialContainerPath"), QString::fromLocal8Bit(argv[++index]));
        }
    }

    return lvrs::runBootstrappedQmlApp(argc, argv, launchSpec);
}
