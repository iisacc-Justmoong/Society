#include "backend/runtime/appentry.h"

int main(int argc, char *argv[])
{
    lvrs::QmlAppLaunchSpec launchSpec;
    launchSpec.bootstrap.applicationName = QStringLiteral("Society");
    launchSpec.bootstrap.quickStyleName = QStringLiteral("Basic");
    launchSpec.moduleUri = QStringLiteral("Society");
    launchSpec.rootObject = QStringLiteral("Main");
    launchSpec.qmlImportPaths.append(QString::fromUtf8(SOCIETY_LVRS_QML_IMPORT_PATH));

    return lvrs::runBootstrappedQmlApp(argc, argv, launchSpec);
}
