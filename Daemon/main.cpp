#include "DaemonService.h"
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QTextStream>
#include <QTimer>
#include <csignal>

namespace { volatile std::sig_atomic_t stopRequested = 0; }

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
#ifdef Q_OS_MACOS
    const auto plugins = QDir(QCoreApplication::applicationDirPath()).absoluteFilePath("../PlugIns");
    if (QDir(plugins).exists()) QCoreApplication::setLibraryPaths({plugins});
#endif
    QCoreApplication::setApplicationName("SocietyDaemon");
    QCoreApplication::setApplicationVersion(SOCIETY_APP_VERSION);
    QCommandLineParser parser;
    parser.setApplicationDescription("Receives iisacc Helper data independently of the Society window.");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({{"d", "directory"}, "Shared Helper observation directory.", "path"});
    parser.addOption({"exit-after-ms", "Exit cleanly after a diagnostic run (zero keeps running).", "ms", "0"});
    parser.process(app);
    bool durationOk = false;
    const auto duration = parser.value("exit-after-ms").toInt(&durationOk);
    if (!durationOk || duration < 0) return 2;
    SocietyDaemonService service;
    if (!service.start(parser.value("directory"))) {
        QTextStream(stderr) << service.errorString() << Qt::endl;
        return 1;
    }
#ifdef Q_OS_UNIX
    // Signal handlers only set a flag; Qt cleanup runs safely on the event loop.
    std::signal(SIGTERM, [](int) { stopRequested = 1; });
    std::signal(SIGINT, [](int) { stopRequested = 1; });
    QTimer terminationPoll;
    QObject::connect(&terminationPoll, &QTimer::timeout, &app, [&app] { if (stopRequested) app.quit(); });
    terminationPoll.start(200);
#endif
    if (duration > 0) QTimer::singleShot(duration, &app, &QCoreApplication::quit);
    return app.exec();
}
