#pragma once

#include <QObject>
#include <QElapsedTimer>
#include <QTimer>
#include <QUrl>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>
#if !defined(Q_OS_IOS) && !defined(Q_OS_ANDROID)
#include <QProcess>
#define SOCIETY_MODEL_MERGE_PROCESS
#endif

// Owns one asynchronous SDK invocation; tensor arithmetic stays in iiLocalDiffusion.
class ModelMergeController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool supported READ supported CONSTANT)
    Q_PROPERTY(QString defaultExecutable READ defaultExecutable CONSTANT)
    Q_PROPERTY(QString defaultPython READ defaultPython CONSTANT)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool cancelling READ cancelling NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString errorString READ errorString NOTIFY changed)
    Q_PROPERTY(QString details READ details NOTIFY changed)
    Q_PROPERTY(QString completedOutput READ completedOutput NOTIFY changed)
    Q_PROPERTY(int elapsedSeconds READ elapsedSeconds NOTIFY elapsedChanged)
public:
    explicit ModelMergeController(QObject *parent = nullptr);
    ~ModelMergeController() override;
    bool supported() const;
    QString defaultExecutable() const;
    QString defaultPython() const;
    bool busy() const { return m_busy; }
    bool cancelling() const { return m_cancelled; }
    QString status() const { return m_status; }
    QString errorString() const { return m_error; }
    QString details() const { return m_details; }
    QString completedOutput() const { return m_completedOutput; }
    int elapsedSeconds() const { return m_elapsedSeconds; }
    Q_INVOKABLE bool run(const QVariantMap &options, bool validateOnly = false);
    Q_INVOKABLE void cancel();
    Q_INVOKABLE QString localPath(const QUrl &url) const;
    Q_INVOKABLE QString outputPathForName(const QString &name, const QString &directory, const QString &mode = "weighted-sum") const;
    Q_INVOKABLE void copyDetails() const;
    Q_INVOKABLE void openOutputFolder() const;
signals:
    void changed();
    void elapsedChanged();
    void finished(bool success, bool validationOnly);
private:
    bool fail(const QString &message);
    void finish(int exitCode, bool crashed);
    void readOutput();
    bool m_busy = false, m_cancelled = false, m_validationOnly = false;
    QString m_status, m_error, m_details, m_completedOutput, m_requestedOutput;
    QByteArray m_stdout, m_stderr;
    QElapsedTimer m_elapsed;
    QTimer m_tick;
    int m_elapsedSeconds = 0;
#ifdef SOCIETY_MODEL_MERGE_PROCESS
    QProcess m_process;
#endif
};
