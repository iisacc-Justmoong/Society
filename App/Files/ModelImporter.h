#pragma once

#include "ModelImportSource.h"
#include <QObject>
#include <QThread>
#include <QUrl>
#include <QWindow>
#include <QPointer>
#include <QtQml/qqmlregistration.h>
#include <atomic>
#include <memory>

class ModelImporter : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString containerPath READ containerPath WRITE setContainerPath NOTIFY containerPathChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(double progress READ progress NOTIFY stateChanged)
    Q_PROPERTY(QString status READ status NOTIFY stateChanged)
    Q_PROPERTY(QString errorString READ errorString NOTIFY stateChanged)
    Q_PROPERTY(bool nativeDragActive READ nativeDragActive NOTIFY nativeDragChanged)
    Q_PROPERTY(bool choosingFiles READ choosingFiles NOTIFY stateChanged)

public:
    explicit ModelImporter(QObject *parent = nullptr);
    ~ModelImporter() override;
    QString containerPath() const;
    void setContainerPath(const QString &path);
    bool busy() const;
    double progress() const;
    QString status() const;
    QString errorString() const;
    bool nativeDragActive() const;
    void setNativeDragActive(bool active);
    bool choosingFiles() const { return m_choosingFiles; }
    void setChoosingFiles(bool choosing);
    bool importSources(const QList<ModelImportSource> &sources);

    Q_INVOKABLE bool accepts(const QList<QUrl> &urls) const;
    Q_INVOKABLE bool importFiles(const QList<QUrl> &urls);
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void attachWindow(QWindow *window);
    Q_INVOKABLE bool chooseFiles();

signals:
    void containerPathChanged();
    void stateChanged();
    void nativeDragChanged();
    void finished(const QString &containerPath, const QStringList &paths);

private:
    QString m_containerPath;
    QString m_status;
    QString m_error;
    double m_progress = 0;
    bool m_nativeDragActive = false;
    bool m_choosingFiles = false;
    QPointer<QWindow> m_window;
    QThread *m_worker = nullptr;
    std::shared_ptr<std::atomic_bool> m_cancelled;
};
