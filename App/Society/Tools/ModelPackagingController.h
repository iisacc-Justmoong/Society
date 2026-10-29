#pragma once

#include <QObject>
#include <QElapsedTimer>
#include <QFutureWatcher>
#include <QTimer>
#include <QUrl>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>
#include <memory>
#include <stop_token>

// GUI state and asynchronous ownership only. Format validation and file writes
// belong to the Qt-free iiLocalDiffusion::ModelPackaging SDK module.
class ModelPackagingController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool supported READ supported CONSTANT)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool packaging READ packaging NOTIFY changed)
    Q_PROPERTY(bool cancelling READ cancelling NOTIFY changed)
    Q_PROPERTY(QString inputDirectory READ inputDirectory NOTIFY changed)
    Q_PROPERTY(QVariantMap report READ report NOTIFY changed)
    Q_PROPERTY(QString phase READ phase NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString detail READ detail NOTIFY changed)
    Q_PROPERTY(QString errorString READ errorString NOTIFY changed)
    Q_PROPERTY(QString completedOutput READ completedOutput NOTIFY changed)
    Q_PROPERTY(double progress READ progress NOTIFY changed)
    Q_PROPERTY(bool indeterminate READ indeterminate NOTIFY changed)
    Q_PROPERTY(int elapsedSeconds READ elapsedSeconds NOTIFY elapsedChanged)
public:
    explicit ModelPackagingController(QObject *parent = nullptr);
    ~ModelPackagingController() override;
    bool supported() const;
    bool busy() const { return m_busy; }
    bool packaging() const { return m_packaging; }
    bool cancelling() const { return m_stop && m_stop->stop_requested(); }
    QString inputDirectory() const { return m_input; }
    QVariantMap report() const { return m_report; }
    QString phase() const { return m_phase; }
    QString status() const;
    QString detail() const { return m_detail; }
    QString errorString() const { return m_error; }
    QString completedOutput() const { return m_output; }
    double progress() const { return m_progress; }
    bool indeterminate() const { return m_phase == "scan" || m_phase == "deduplicate" || m_phase == "save"; }
    int elapsedSeconds() const { return m_seconds; }
    Q_INVOKABLE bool scanFolder(const QString &directory);
    Q_INVOKABLE bool setComponentIncluded(const QString &id, bool included);
    Q_INVOKABLE bool createPackage(const QString &output);
    Q_INVOKABLE void cancel();
    Q_INVOKABLE QString localPath(const QUrl &url) const;
    Q_INVOKABLE QString outputPathForName(QString name, const QString &directory) const;
    Q_INVOKABLE QString formatSize(double bytes) const;
    Q_INVOKABLE void copyDetails() const;
    Q_INVOKABLE void openOutputFolder() const;
signals:
    void changed();
    void elapsedChanged();
    void finished(bool success, bool packaging);
private:
    bool start(bool packaging, const QString &output = {});
    bool fail(const QString &error);
    void rebuildSelection();
    void finish();
    QFutureWatcher<QVariantMap> m_watcher;
    QElapsedTimer m_elapsed;
    QTimer m_timer;
    std::shared_ptr<std::stop_source> m_stop;
    bool m_busy = false, m_packaging = false;
    QString m_input, m_phase, m_detail, m_error, m_output, m_requestedOutput;
    QVariantMap m_report, m_scanned;
    QStringList m_excluded;
    double m_progress = 0;
    int m_seconds = 0;
    quint64 m_revision = 0;
};
