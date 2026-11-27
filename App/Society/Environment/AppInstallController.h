#pragma once

#include <QObject>
#include <QThreadPool>
#include <QVariantMap>
class QFutureWatcherBase;

/// Coordinates one explicitly requested first installation on a private worker.
/// The caller supplies validated release values; progress is delivered on the
/// owning UI thread. The operating-system installer owns authorization and the
/// installation itself, so a successful handoff never asserts completion.
class AppInstallController final : public QObject {
    Q_OBJECT
public:
    explicit AppInstallController(QObject *parent = nullptr);
    /// Cancel pending work before waiting for its bounded I/O/verification steps.
    ~AppInstallController() override;
    /// Start only when no other request is active. Returns false on contention.
    bool start(const QVariantMap &release);
signals:
    void activityChanged(const QString &description, bool busy);
    void handoffFinished();
private:
    QThreadPool pool_;
    QFutureWatcherBase *watcher_ = nullptr;
    bool busy_ = false;
};
