#include "ModelCalibratorLauncher.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#if !defined(Q_OS_IOS) && !defined(Q_OS_ANDROID)
#include <QProcess>
#endif

bool ModelCalibratorLauncher::open(const QString &modelsDirectory) {
    error_.clear();
#if !defined(Q_OS_IOS) && !defined(Q_OS_ANDROID)
#ifdef Q_OS_WIN
    const auto program = QDir(QCoreApplication::applicationDirPath()).filePath("SocietyModelCalibrator.exe");
#else
    const auto program = QDir(QCoreApplication::applicationDirPath()).filePath("SocietyModelCalibrator");
#endif
    if (!QFileInfo(program).isExecutable() || !QProcess::startDetached(program, {modelsDirectory}))
        error_ = tr("The native model calibrator could not be started.");
#else
    Q_UNUSED(modelsDirectory)
    error_ = tr("The native model calibrator is not available on this device.");
#endif
    emit errorChanged();
    return error_.isEmpty();
}
