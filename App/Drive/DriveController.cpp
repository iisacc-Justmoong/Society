#include "DriveController.h"
#include <SharedStorage.h>

#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QCoreApplication>
#if defined(Q_OS_MACOS) || defined(SOCIETY_DESKTOP_MOUNT)
#include <QProcessEnvironment>
#endif
#ifdef Q_OS_ANDROID
#include <AndroidStorage.h>
#endif
#if defined(Q_OS_IOS) || defined(Q_OS_ANDROID)
#include <QGuiApplication>
#endif
#ifdef Q_OS_IOS
#include <IosDriveBridge.h>
#include "../Files/IosFileAccess.h"
#include <QGuiApplication>
#include <QPointer>
#include <memory>
#endif
#include <utility>

using namespace iiSocietyContainer;

DriveController::DriveController(QObject *parent) : QObject(parent)
{
    m_timeout.setSingleShot(true);
    m_timeout.setInterval(35000);
    connect(&m_timeout, &QTimer::timeout, this, [this] {
#if defined(Q_OS_MACOS) || defined(SOCIETY_DESKTOP_MOUNT)
        m_process.kill();
#endif
        ++m_requestGeneration;
        finishNative(false, QJsonDocument(QJsonObject{{"error", tr("%1 did not respond in time.").arg(systemName())}}).toJson());
    });
#if defined(Q_OS_MACOS) || defined(SOCIETY_DESKTOP_MOUNT)
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            finishNative(false, QJsonDocument(QJsonObject{{"error", m_process.errorString()}}).toJson());
    });
    connect(&m_process, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        if (m_action.isEmpty())
            return;
        const auto output = m_process.readAllStandardOutput();
        if (code != 0 || status != QProcess::NormalExit) {
            finishNative(false, output);
        } else if (!m_commands.isEmpty()) {
            runNextCommand();
        } else {
            finishNative(true, output);
        }
    });
#endif
#if defined(Q_OS_IOS) || defined(Q_OS_ANDROID)
    connect(qGuiApp, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState state) {
        if (state == Qt::ApplicationSuspended || state == Qt::ApplicationHidden) {
            // UIKit/File Provider completions may arrive after suspension.
            // Retire this request so it cannot overwrite a newer connection.
            m_timeout.stop();
            ++m_requestGeneration;
            m_action.clear();
            emit systemChanged();
        } else if (state == Qt::ApplicationActive && !busy()) {
            if (hasDrive()) refreshSystem();
            else openDefaultContainer();
        }
    });
#endif
}

bool DriveController::hasDrive() const { return m_drive.has_value(); }
QString DriveController::rootPath() const { return m_drive ? m_drive->rootPath() : QString(); }
QString DriveController::identifier() const { return m_drive ? m_drive->identifier() : QString(); }
QString DriveController::currentPath() const { return m_currentPath; }
bool DriveController::atRoot() const { return m_currentPath.isEmpty(); }
QString DriveController::errorString() const { return m_error; }
bool DriveController::busy() const { return !m_action.isEmpty(); }
QString DriveController::systemPath() const { return m_systemPath; }
QString DriveController::systemStatus() const { return m_systemStatus; }

bool DriveController::managedContainer() const
{
#if defined(Q_OS_IOS) || defined(Q_OS_ANDROID)
    return true;
#else
    return false;
#endif
}

QString DriveController::systemName() const
{
#if defined(Q_OS_IOS) || defined(Q_OS_ANDROID)
    return tr("Files");
#elif defined(Q_OS_WIN)
    return tr("File Explorer");
#elif defined(Q_OS_LINUX)
    return tr("File Manager");
#else
    return tr("Finder");
#endif
}

void DriveController::openDefaultContainer()
{
    if (managedContainer())
        startNative(QStringLiteral("default"));
    else {
        QString error;
        const auto storage = SharedStorage::open({}, &error);
        if (storage) openContainer(storage->drive().rootPath());
        else fail(error);
    }
}

QVariantList DriveController::sections() const
{
    QVariantList entries;
    if (m_drive) {
        for (const auto section : allStoreSections())
            entries.append(QVariantMap{{"key", storeSectionKey(section)}, {"name", storeSectionName(section)},
                                       {"path", QDir(rootPath()).filePath(storeSectionName(section))}});
    }
    return entries;
}

QString DriveController::currentSection() const
{
    return atRoot() ? QString() : QDir(rootPath()).relativeFilePath(m_currentPath).section('/', 0, 0);
}

QVariantList DriveController::breadcrumbs() const
{
    QVariantList entries;
    if (!m_drive)
        return entries;
    entries.append(QVariantMap{{"name", m_drive->displayName()}, {"path", rootPath()}});
    if (!atRoot()) {
        QString path = rootPath();
        for (const auto &component : QDir(rootPath()).relativeFilePath(m_currentPath).split('/')) {
            path = QDir(path).filePath(component);
            entries.append(QVariantMap{{"name", component}, {"path", path}});
        }
    }
    return entries;
}

bool DriveController::fail(const QString &message)
{
    m_error = message;
    emit errorChanged();
    return false;
}

bool DriveController::openContainerUrl(const QUrl &url)
{
    return url.isLocalFile() ? openContainer(url.toLocalFile()) : fail(tr("Select a local folder for Society."));
}

bool DriveController::openContainer(const QString &path)
{
    if (busy())
        return fail(tr("Wait for the %1 connection to finish.").arg(systemName()));
    if (!QDir::isAbsolutePath(path))
        return fail(tr("The container requires an absolute folder path."));
    QString error;
    auto drive = SocietyDrive::create(path, &error);
    if (!drive)
        return fail(error);
    if (!SharedStorage::setDefaultContainer(drive->rootPath(), &error))
        return fail(error);
    m_drive = std::move(drive);
    m_currentPath.clear();
    m_systemPath.clear();
    m_systemStatus = systemSupported() ? tr("Connect this container to %1.").arg(systemName())
                                      : tr("Install the Society system drive adapter to connect this container.");
    fail({});
    emit locationChanged();
    emit systemChanged();
    return true;
}

bool DriveController::openSection(const QString &key)
{
    if (m_drive) {
        for (const auto section : allStoreSections())
            if (storeSectionKey(section) == key)
                return navigate(m_drive->sectionPath(section));
    }
    return fail(tr("Unknown drive section."));
}

bool DriveController::navigate(const QString &path)
{
    if (!m_drive || !m_drive->isValid())
        return fail(tr("The container is unavailable or its section layout has changed."));
    const QFileInfo info(path);
    if (!info.isAbsolute() || !info.isDir() || !info.isReadable())
        return fail(tr("This folder is unavailable."));
    const QString canonical = info.canonicalFilePath();
    if (canonical == rootPath()) {
        goHome();
        return true;
    }
    const auto section = m_drive->sectionForPath(canonical);
    if (!section)
        return fail(tr("This folder is outside the drive sections."));
    if (*section == StoreSection::GenerationHistory
        && canonical != m_drive->sectionPath(StoreSection::GenerationHistory))
        return fail(tr("Generation History contains generated images without folders."));
    m_currentPath = canonical;
    fail({});
    emit locationChanged();
    return true;
}

void DriveController::goHome()
{
    m_currentPath.clear();
    fail({});
    emit locationChanged();
}

void DriveController::goUp()
{
    if (!atRoot())
        navigate(QFileInfo(m_currentPath).dir().absolutePath());
}

bool DriveController::openFile(const QString &path)
{
    if (!m_drive || !m_drive->sectionForPath(path) || !QFileInfo(path).isFile())
        return fail(tr("This file is outside the drive sections or is unavailable."));
#ifdef Q_OS_IOS
    return previewIosFile(QFileInfo(path).canonicalFilePath())
        || fail(tr("A preview is not available for this file type."));
#else
    return QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).canonicalFilePath()))
        || fail(tr("No application could open this file."));
#endif
}

bool DriveController::systemSupported() const
{
#if defined(Q_OS_MACOS) || defined(SOCIETY_DESKTOP_MOUNT)
    return !nativeExecutable().isEmpty();
#elif defined(Q_OS_IOS) || defined(Q_OS_ANDROID)
    return true;
#else
    return false;
#endif
}

QString DriveController::nativeExecutable() const
{
#ifdef SOCIETY_NATIVE_DRIVE_APP
    return QString::fromUtf8(SOCIETY_NATIVE_DRIVE_APP) + QStringLiteral("/Contents/MacOS/SocietyContainerDrive");
#elif defined(SOCIETY_MOUNT_EXECUTABLE)
#ifdef Q_OS_WIN
    const auto local = QDir(QCoreApplication::applicationDirPath()).filePath("iiSocietyContainerMount.exe");
#else
    const auto local = QDir(QCoreApplication::applicationDirPath()).filePath("iiSocietyContainerMount");
#endif
    if (QFileInfo(local).isExecutable()) return local;
    const auto installed = QString::fromUtf8(SOCIETY_MOUNT_EXECUTABLE);
    return QFileInfo(installed).isExecutable() ? installed : QString();
#else
    return {};
#endif
}

void DriveController::connectToSystem() { startNative(QStringLiteral("register"), true); }
void DriveController::refreshSystem() { startNative(QStringLiteral("refresh")); }

void DriveController::revealInSystem()
{
#if defined(Q_OS_IOS) || defined(Q_OS_ANDROID)
    startNative(QStringLiteral("path"));
#else
    if (!m_systemPath.isEmpty())
        QDesktopServices::openUrl(QUrl::fromLocalFile(m_systemPath));
    else
        startNative(QStringLiteral("path"));
#endif
}

void DriveController::startNative(const QString &action, bool registerBundle)
{
    if (busy() || (!m_drive && !(managedContainer() && action == QStringLiteral("default"))))
        return;
    if (!systemSupported()) {
        fail(tr("The native Society adapter is not installed."));
        return;
    }
    if (m_drive && !m_drive->isValid()) {
        fail(tr("The container is unavailable or its section layout has changed."));
        return;
    }
    m_action = action;
    m_systemStatus = tr("Connecting to %1…").arg(systemName());
    fail({});
#ifdef Q_OS_MACOS
    if (!QFileInfo::exists(nativeExecutable())) {
        finishNative(false);
        return;
    }
    if (registerBundle) {
        const auto bundle = QFileInfo(nativeExecutable()).dir().absoluteFilePath(QStringLiteral("../.."));
        m_commands.append({QStringLiteral("/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister"), {QStringLiteral("-f"), bundle}});
        m_commands.append({QStringLiteral("/usr/bin/pluginkit"), {QStringLiteral("-a"), bundle + QStringLiteral("/Contents/PlugIns/SocietyContainerProvider.appex")}});
        // Launch Services must acknowledge the containing app after an update
        // before File Provider treats its extension as launchable.
        m_commands.append({QStringLiteral("/usr/bin/open"), {QStringLiteral("-W"), QStringLiteral("-g"),
            QStringLiteral("-a"), bundle, QStringLiteral("--args"), QStringLiteral("list")}});
    }
    m_commands.append({nativeExecutable(), {action, action == QStringLiteral("register") ? rootPath() : identifier()}});
    emit systemChanged();
    m_timeout.start();
    runNextCommand();
#elif defined(SOCIETY_DESKTOP_MOUNT)
    Q_UNUSED(registerBundle);
    m_commands.append({nativeExecutable(), {action, action == QStringLiteral("register") ? rootPath() : identifier()}});
    emit systemChanged();
    m_timeout.start();
    runNextCommand();
#elif defined(Q_OS_ANDROID)
    Q_UNUSED(registerBundle);
    emit systemChanged();
    finishNative(true, androidDriveRequest(action, rootPath(), identifier()));
#elif defined(Q_OS_IOS)
    Q_UNUSED(registerBundle);
    struct Request { QPointer<DriveController> owner; quint64 generation; };
    auto *request = new Request{this, ++m_requestGeneration};
    emit systemChanged();
    m_timeout.start();
    society_ios_drive_request(action.toUtf8().constData(), rootPath().toUtf8().constData(),
        identifier().toUtf8().constData(), request, [](void *context, const char *output) {
            const std::unique_ptr<Request> request(static_cast<Request *>(context));
            if (request->owner && request->owner->m_requestGeneration == request->generation)
                request->owner->finishNative(true, QByteArray(output));
        });
#else
    Q_UNUSED(registerBundle);
#endif
}

#if defined(Q_OS_MACOS) || defined(SOCIETY_DESKTOP_MOUNT)
void DriveController::runNextCommand()
{
    const auto command = m_commands.takeFirst();
    auto environment = QProcessEnvironment::systemEnvironment();
    for (const auto &name : {"DYLD_LIBRARY_PATH", "DYLD_FRAMEWORK_PATH", "DYLD_FALLBACK_LIBRARY_PATH", "DYLD_FALLBACK_FRAMEWORK_PATH"})
        environment.remove(QString::fromLatin1(name));
    m_process.setProcessEnvironment(environment);
    m_process.start(command.program, command.arguments);
}
#endif

void DriveController::finishNative(bool success, const QByteArray &output)
{
    m_timeout.stop();
#if defined(Q_OS_MACOS) || defined(SOCIETY_DESKTOP_MOUNT)
    m_commands.clear();
#endif
    const auto action = std::exchange(m_action, {});
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(output, &parseError);
    const auto result = document.object();
    if (!success || parseError.error != QJsonParseError::NoError || result.contains("error")) {
        m_systemPath.clear();
        m_systemStatus = result.value("error").toString(tr("The %1 connection failed.").arg(systemName()));
        fail(m_systemStatus);
    } else if (action == QStringLiteral("default")) {
        if (openContainer(result.value("sourcePath").toString()))
            connectToSystem();
    } else {
        if (result.contains("systemPath"))
            m_systemPath = result.value("systemPath").toString();
        m_systemStatus = result.contains("enabled") && !result.value("enabled").toBool()
            ? tr("Open %1 and enable Society in Locations.").arg(systemName())
            : tr("Connected to %1").arg(systemName());
#if !defined(Q_OS_IOS) && !defined(Q_OS_ANDROID)
        if (action == QStringLiteral("path") && !m_systemPath.isEmpty())
            QDesktopServices::openUrl(QUrl::fromLocalFile(m_systemPath));
#endif
    }
    emit systemChanged();
}
