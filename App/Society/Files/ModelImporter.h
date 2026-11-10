#pragma once

#include "ModelImportSource.h"
#include "ModelBackendClient.h"
#include <ModelImportService.h>
#include <QPointer>
#include <QWindow>
#include <QtQml/qqmlregistration.h>

// Platform picker and drag adapter; container operations belong to the SDK.
class ModelImporter : public iiSocietyContainer::ModelImportService
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool nativeDragActive READ nativeDragActive NOTIFY nativeDragChanged)
    Q_PROPERTY(bool backendManaged READ backendManaged WRITE setBackendManaged NOTIFY backendManagedChanged)
    Q_PROPERTY(bool backendConnected READ backendConnected NOTIFY backendConnectedChanged)
    Q_PROPERTY(QString containerPath READ containerPath WRITE setContainerPath NOTIFY containerPathChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(bool organizing READ organizing NOTIFY stateChanged)
    Q_PROPERTY(double progress READ progress NOTIFY stateChanged)
    Q_PROPERTY(QString status READ status NOTIFY stateChanged)
    Q_PROPERTY(QString errorString READ errorString NOTIFY stateChanged)
    Q_PROPERTY(bool choosingFiles READ choosingFiles NOTIFY stateChanged)
    Q_PROPERTY(QString currentJobId READ currentJobId NOTIFY jobsChanged)
    Q_PROPERTY(QString selectedJobId READ selectedJobId NOTIFY jobsChanged)
    Q_PROPERTY(QVariantList jobs READ jobs NOTIFY jobsChanged)
    Q_PROPERTY(QVariantList jobItems READ jobItems NOTIFY jobsChanged)
    Q_PROPERTY(bool hasMoreJobItems READ hasMoreJobItems NOTIFY jobsChanged)
    Q_PROPERTY(QString jobsError READ jobsError NOTIFY jobsChanged)
public:
    explicit ModelImporter(QObject *parent = nullptr);
    bool backendManaged() const { return m_backendManaged; }
    void setBackendManaged(bool managed);
    bool backendConnected() const { return m_backendManaged && m_backend.connected(); }
    QString containerPath() const;
    void setContainerPath(const QString &path);
    bool busy() const;
    bool organizing() const;
    double progress() const;
    QString status() const;
    QString errorString() const;
    bool choosingFiles() const;
    QString currentJobId() const;
    QString selectedJobId() const;
    QVariantList jobs() const;
    QVariantList jobItems() const;
    bool hasMoreJobItems() const;
    QString jobsError() const;
    bool nativeDragActive() const;
    void setNativeDragActive(bool active);
    Q_INVOKABLE bool accepts(const QList<QUrl> &urls) const;
    Q_INVOKABLE bool importFiles(const QList<QUrl> &urls);
    Q_INVOKABLE void attachWindow(QWindow *window);
    Q_INVOKABLE bool chooseFiles();
    Q_INVOKABLE void cancel();
    Q_INVOKABLE bool organizeModels();
    Q_INVOKABLE bool recategorize(const QString &path, const QString &category);
    Q_INVOKABLE void refreshJobs();
    Q_INVOKABLE void selectJob(const QString &jobId);
    Q_INVOKABLE void loadMoreJobItems();
    Q_INVOKABLE bool retryFailed(const QString &jobId);
signals:
    void nativeDragChanged();
    void backendManagedChanged();
    void backendConnectedChanged();
private:
    void inputError(const QString &error);
    bool backendRequest(const QString &operation, QJsonObject arguments = {}, bool mutation = false);
    bool m_backendManaged = false;
    bool m_connectionError = false;
    QString m_containerPath, m_backendError;
    QJsonObject m_backendState;
    ModelBackendClient m_backend;
    bool m_nativeDragActive = false;
    QPointer<QWindow> m_window;
};
