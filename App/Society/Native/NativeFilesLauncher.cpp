#include "NativeFilesLauncher.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#if !defined(Q_OS_IOS) && !defined(Q_OS_ANDROID)
#include <QProcess>
#endif

namespace society::native {
bool launchFiles(const QString &program,const StartFiles &start,QString *error) {
    if (error) error->clear();
    const QFileInfo info(program);
    if (program.isEmpty() || !info.isFile() || !info.isExecutable() || !start || !start(program)) {
        if (error) *error=QCoreApplication::translate("NativeFilesLauncher","The native file view could not be started.");
        return false;
    }
    return true;
}
bool launchBundledFiles(QString *error) {
#if defined(Q_OS_IOS) || defined(Q_OS_ANDROID)
    if (error) *error=QCoreApplication::translate("NativeFilesLauncher","The native file view is unavailable on this device.");
    return false;
#else
#if defined(Q_OS_WIN)
    const auto name=QStringLiteral("SocietyNativeFiles.exe");
#else
    const auto name=QStringLiteral("SocietyNativeFiles");
#endif
    const auto program=QDir(QCoreApplication::applicationDirPath()).filePath(name);
    return launchFiles(program,[](const QString &path){return QProcess::startDetached(path,{});},error);
#endif
}
}
