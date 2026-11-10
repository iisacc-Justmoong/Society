#pragma once

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QTimer>
#include <functional>

// The native adapter remains the owner of filesystem mounting. This service
// coordinates requests and validates the OS result without depending on a GUI.
class SystemDriveCoordinator final : public QObject {
    Q_OBJECT
public:
    using Completion = std::function<void(const QJsonObject &, const QString &)>;
    using Request = std::function<void(const QString &, Completion)>;
    using VerifyMount = std::function<bool(const QString &)>;

    explicit SystemDriveCoordinator(Request request, VerifyMount verify, QObject *parent = nullptr);
    void setSelection(const QString &path, const QString &identifier, bool ready = true);
    void check();
    bool connected() const { return !m_systemPath.isEmpty(); }
    QString systemPath() const { return m_systemPath; }
    QString errorString() const { return m_error; }
    QJsonObject state() const;
signals:
    void changed();
private:
    void complete(quint64 epoch, const QJsonObject &result, const QString &error);
    Request m_request;
    VerifyMount m_verify;
    QTimer m_timer;
    QString m_source, m_identifier, m_systemPath, m_error;
    quint64 m_epoch = 0;
    bool m_inFlight = false;
    bool m_ready = true;
};

SystemDriveCoordinator::Request macSystemDriveRequest(const QString &appBundle, QObject *owner);
SystemDriveCoordinator::Request desktopSystemDriveRequest(const QString &installedExecutable, QObject *owner);
bool isMountedSocietyFilesystem(const QString &path);
QString societySystemFilesystemType();
