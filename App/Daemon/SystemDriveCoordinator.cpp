#include "SystemDriveCoordinator.h"
#include "Shared/LocalBackendAddress.h"

#include <QDir>
#include <QCoreApplication>
#include <QFileInfo>
#include <QJsonDocument>
#include <QPointer>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStorageInfo>
#include <QUuid>
#include <utility>

SystemDriveCoordinator::SystemDriveCoordinator(Request request, VerifyMount verify, QObject *parent)
    : QObject(parent), m_request(std::move(request)), m_verify(std::move(verify))
{
    m_timer.setInterval(30000);
    connect(&m_timer, &QTimer::timeout, this, &SystemDriveCoordinator::check);
}

void SystemDriveCoordinator::setSelection(const QString &path, const QString &identifier, bool ready)
{
    const auto source = path.isEmpty() ? QString() : QDir::cleanPath(path);
    if (source == m_source && identifier == m_identifier && ready == m_ready) return;
    ++m_epoch;
    m_source = source;
    m_identifier = identifier;
    m_ready = ready;
    m_systemPath.clear();
    m_error = !m_source.isEmpty() && !m_ready ? tr("The initial Society mirror is not ready.") : QString();
    if (m_source.isEmpty() || QUuid(m_identifier).isNull() || !m_ready) m_timer.stop();
    else m_timer.start();
    emit changed();
    check();
}

void SystemDriveCoordinator::check()
{
    if (m_source.isEmpty() || QUuid(m_identifier).isNull() || !m_ready || m_inFlight) return;
    if (!m_systemPath.isEmpty() && m_verify(m_systemPath)) return;
    if (!m_systemPath.isEmpty()) { m_systemPath.clear(); emit changed(); }
    m_inFlight = true;
    m_error.clear();
    emit changed();
    const auto epoch = m_epoch;
    const QPointer<SystemDriveCoordinator> self(this);
    m_request(m_source, [self, epoch](const QJsonObject &result, const QString &error) {
        if (self) self->complete(epoch, result, error);
    });
}

void SystemDriveCoordinator::complete(quint64 epoch, const QJsonObject &result, const QString &error)
{
    m_inFlight = false;
    if (epoch != m_epoch) { check(); return; }
    const auto path = result.value("systemPath").toString();
    const bool verified = error.isEmpty() && result.value("isMounted").toBool()
        && result.value("filesystemType").toString() == societySystemFilesystemType()
        && societySameBackendPath(result.value("sourcePath").toString(), m_source)
        && !QUuid(result.value("identifier").toString()).isNull()
        && QUuid(result.value("identifier").toString()) == QUuid(m_identifier)
        && QDir::isAbsolutePath(path) && m_verify(path);
    m_systemPath = verified ? path : QString();
    m_error = verified ? QString() : error.isEmpty()
        ? tr("The Society filesystem was not verified for this container.") : error;
    emit changed();
}

QJsonObject SystemDriveCoordinator::state() const
{
    return {{"connected", connected()}, {"systemPath", m_systemPath}, {"error", m_error},
            {"identifier", m_identifier}, {"sourcePath", m_source}, {"connecting", m_inFlight}, {"ready", m_ready}};
}

bool isMountedSocietyFilesystem(const QString &path)
{
    if (!QDir::isAbsolutePath(path)) return false;
    const QStorageInfo volume(path);
    return volume.isValid() && volume.isReady() && volume.fileSystemType() == societySystemFilesystemType().toUtf8()
        && QDir::cleanPath(volume.rootPath()) == QDir::cleanPath(path);
}

QString societySystemFilesystemType()
{
#ifdef Q_OS_WIN
    return QStringLiteral("Society");
#elif defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID)
    return QStringLiteral("fuse.society");
#else
    return QStringLiteral("societyfs");
#endif
}

namespace {
class MountAttempt final : public QObject {
public:
    using Command = QPair<QString, QStringList>;
    MountAttempt(QString executable, QList<Command> commands, SystemDriveCoordinator::Completion completion, QObject *parent)
        : QObject(parent), m_executable(std::move(executable)), m_commands(std::move(commands)), m_completion(std::move(completion))
    {
        m_timeout.setSingleShot(true);
        m_timeout.setInterval(45000);
        connect(&m_timeout, &QTimer::timeout, this, [this] {
            m_process.kill();
            finish({}, tr("The Society filesystem host did not respond."));
        });
        connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
            if (error == QProcess::FailedToStart) finish({}, m_process.errorString());
        });
        connect(&m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this](int code, QProcess::ExitStatus status) {
                if (m_finished) return;
                const auto output = m_process.readAllStandardOutput();
                const auto errors = m_process.readAllStandardError();
                if (status != QProcess::NormalExit || code != 0) {
                    const auto message = QJsonDocument::fromJson(output).object().value("error").toString();
                    finish({}, message.isEmpty() ? QString::fromUtf8(errors).trimmed() : message);
                    return;
                }
                if (!m_commands.isEmpty()) { next(); return; }
                QJsonParseError parse;
                const auto document = QJsonDocument::fromJson(output, &parse);
                if (parse.error != QJsonParseError::NoError || !document.isObject())
                    finish({}, tr("The Society filesystem host returned an invalid response."));
                else finish(document.object(), {});
            });
    }
    void start() {
        if (!QFileInfo(m_executable).isExecutable()) {
            finish({}, tr("The Society filesystem host is unavailable.")); return;
        }
        auto environment = QProcessEnvironment::systemEnvironment();
        for (const auto *name : {"DYLD_LIBRARY_PATH", "DYLD_FRAMEWORK_PATH", "DYLD_FALLBACK_LIBRARY_PATH", "DYLD_FALLBACK_FRAMEWORK_PATH"})
            environment.remove(QString::fromLatin1(name));
        m_process.setProcessEnvironment(environment);
        m_timeout.start();
        next();
    }
private:
    void next() {
        const auto command = m_commands.takeFirst();
        m_process.start(command.first, command.second);
    }
    void finish(const QJsonObject &result, QString error) {
        if (m_finished) return;
        m_finished = true;
        m_timeout.stop();
        if (result.isEmpty() && error.isEmpty()) error = tr("The Society filesystem host could not mount this container.");
        m_completion(result, error);
        deleteLater();
    }
    QString m_executable;
    QList<Command> m_commands;
    SystemDriveCoordinator::Completion m_completion;
    QProcess m_process;
    QTimer m_timeout;
    bool m_finished = false;
};
}

SystemDriveCoordinator::Request macSystemDriveRequest(const QString &appBundle, QObject *owner)
{
    return [appBundle, owner](const QString &source, SystemDriveCoordinator::Completion completion) {
        const auto executable = appBundle + "/Contents/MacOS/SocietyContainerDrive";
        QList<MountAttempt::Command> commands{
            {QStringLiteral("/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister"), {"-f", appBundle}},
            {QStringLiteral("/usr/bin/open"), {"-W", "-g", "-a", appBundle, "--args", "ready"}},
            {executable, {"mount", source}}};
        (new MountAttempt(executable, std::move(commands), std::move(completion), owner))->start();
    };
}

SystemDriveCoordinator::Request desktopSystemDriveRequest(const QString &installedExecutable, QObject *owner)
{
    return [installedExecutable, owner](const QString &source, SystemDriveCoordinator::Completion completion) {
#ifdef Q_OS_WIN
        const auto local = QDir(QCoreApplication::applicationDirPath()).filePath("iiSocietyContainerMount.exe");
#else
        const auto local = QDir(QCoreApplication::applicationDirPath()).filePath("iiSocietyContainerMount");
#endif
        const auto executable = QFileInfo(local).isExecutable() ? local : installedExecutable;
        QList<MountAttempt::Command> commands{{executable, {"mount", source}}};
        (new MountAttempt(executable, std::move(commands), std::move(completion), owner))->start();
    };
}
