#pragma once

#include "RecentCatalog.h"
#include <QHash>
#include <QJsonObject>
#include <QSet>
#include <QObject>
#include <QTimer>
#include <functional>

// Owns the Recent snapshot and persists user activity on a background worker.
class RecentBackend final : public QObject {
    Q_OBJECT
public:
    using Scan=std::function<society::recent::Snapshot(const QString &,const QVector<society::recent::Activity> &,int)>;
    explicit RecentBackend(QObject *parent=nullptr,Scan scan={});
    void selectContainer(const QString &container);
    void activate();
    void refresh();
    void record(const QString &container,const QString &path,qint64 atMs=0);
    QJsonObject snapshot() const { return m_snapshot; }
signals:
    void changed(const QJsonObject &snapshot);
private:
    void launch();
    void publish(const society::recent::Snapshot &result);
    Scan m_scan;
    QString m_container;
    QHash<QString,QHash<QString,qint64>> m_pending;
    QSet<QString> m_blocked;
    QJsonObject m_snapshot;
    QTimer m_timer;
    quint64 m_generation=0;
    bool m_active=false,m_running=false,m_dirty=false;
};
