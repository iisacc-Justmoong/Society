#pragma once

#include <SocietyDrive.h>
#include <QObject>
#include <QTimer>
#if defined(Q_OS_WIN) || (defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID))
#define SOCIETY_DESKTOP_MOUNT
#endif
#if defined(Q_OS_MACOS) || defined(SOCIETY_DESKTOP_MOUNT)
#include <QProcess>
#endif
#include <QUrl>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

class DriveController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool hasDrive READ hasDrive NOTIFY locationChanged)
    Q_PROPERTY(QString rootPath READ rootPath NOTIFY locationChanged)
    Q_PROPERTY(QString identifier READ identifier NOTIFY locationChanged)
    Q_PROPERTY(QVariantList sections READ sections NOTIFY locationChanged)
    Q_PROPERTY(QString currentPath READ currentPath NOTIFY locationChanged)
    Q_PROPERTY(QString currentSection READ currentSection NOTIFY locationChanged)
    Q_PROPERTY(bool atRoot READ atRoot NOTIFY locationChanged)
    Q_PROPERTY(QVariantList breadcrumbs READ breadcrumbs NOTIFY locationChanged)
    Q_PROPERTY(QString errorString READ errorString NOTIFY errorChanged)
    Q_PROPERTY(bool systemSupported READ systemSupported CONSTANT)
    Q_PROPERTY(QString systemName READ systemName CONSTANT)
    Q_PROPERTY(bool managedContainer READ managedContainer CONSTANT)
    Q_PROPERTY(bool busy READ busy NOTIFY systemChanged)
    Q_PROPERTY(QString systemPath READ systemPath NOTIFY systemChanged)
    Q_PROPERTY(QString systemStatus READ systemStatus NOTIFY systemChanged)
    Q_PROPERTY(bool mirrorPending READ mirrorPending WRITE setMirrorPending NOTIFY contentsChanged)
    Q_PROPERTY(bool contentsAvailable READ contentsAvailable NOTIFY contentsChanged)

public:
    explicit DriveController(QObject *parent = nullptr);
    bool hasDrive() const;
    QString rootPath() const;
    QString identifier() const;
    QVariantList sections() const;
    QString currentPath() const;
    QString currentSection() const;
    bool atRoot() const;
    QVariantList breadcrumbs() const;
    QString errorString() const;
    bool systemSupported() const;
    QString systemName() const;
    bool managedContainer() const;
    bool busy() const;
    QString systemPath() const;
    QString systemStatus() const;
    bool mirrorPending() const { return m_mirrorPending; }
    bool contentsAvailable() const { return hasDrive() && !m_mirrorPending; }
    void setMirrorPending(bool pending);
    Q_INVOKABLE bool reloadFromDisk();

    Q_INVOKABLE bool openContainer(const QString &path);
    Q_INVOKABLE bool openContainerUrl(const QUrl &url);
    Q_INVOKABLE void openDefaultContainer();
    Q_INVOKABLE bool openSection(const QString &key);
    Q_INVOKABLE bool navigate(const QString &path);
    Q_INVOKABLE void goHome();
    Q_INVOKABLE void goUp();
    Q_INVOKABLE bool openFile(const QString &path);
    Q_INVOKABLE void connectToSystem();
    Q_INVOKABLE void refreshSystem();
    Q_INVOKABLE void revealInSystem();

signals:
    void locationChanged();
    void errorChanged();
    void systemChanged();
    void contentsChanged();

private:
    bool fail(const QString &message);
#if defined(Q_OS_MACOS) || defined(SOCIETY_DESKTOP_MOUNT)
    struct Command { QString program; QStringList arguments; };
    void runNextCommand();
#endif
    void startNative(const QString &action, bool registerBundle = false);
    void finishNative(bool success, const QByteArray &output = {});
    QString nativeExecutable() const;

    std::optional<iiSocietyContainer::SocietyDrive> m_drive;
    QString m_currentPath;
    QString m_error;
    QString m_systemPath;
    QString m_systemStatus;
    QString m_action;
#if defined(Q_OS_MACOS) || defined(SOCIETY_DESKTOP_MOUNT)
    QList<Command> m_commands;
    QProcess m_process;
#endif
    quint64 m_requestGeneration = 0;
    QTimer m_timeout;
    bool m_mirrorPending = false, m_nativeRebind = false;
};
