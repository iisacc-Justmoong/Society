#include "RecentBackend.h"
#include <QDateTime>
#include <QDir>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QtConcurrent/QtConcurrentRun>

namespace r=society::recent;
RecentBackend::RecentBackend(QObject *parent,Scan scan) : QObject(parent),m_scan(std::move(scan)) {
    if (!m_scan) m_scan=[](const QString &root,const QVector<r::Activity> &events,int limit) {
        return r::RecentCatalog::update(root,events,limit);
    };
    m_timer.setInterval(120000);
    connect(&m_timer,&QTimer::timeout,this,[this] { m_blocked.clear(); refresh(); if (!m_running) launch(); });
    m_snapshot={{"items",QJsonArray{}},{"status","Open Recent to view files."},{"loading",false}};
}
void RecentBackend::selectContainer(const QString &container) {
    const auto root=container.isEmpty()?QString():QDir::cleanPath(container);
    if (m_container==root) return;
    m_container=root; ++m_generation; m_dirty=false;
    m_snapshot={{"items",QJsonArray{}},{"status",root.isEmpty()?"Society drive is not available.":"Reading recent files…"},{"loading",!root.isEmpty()}};
    emit changed(m_snapshot);
    if (m_active && !root.isEmpty()) refresh();
}
void RecentBackend::activate() {
    if (m_active) return;
    m_active=true; m_timer.start(); refresh();
}
void RecentBackend::refresh() {
    if (m_container.isEmpty()) return;
    m_blocked.remove(m_container); m_dirty=true; if (!m_running) launch();
}
void RecentBackend::record(const QString &container,const QString &path,qint64 atMs) {
    const auto root=QDir::cleanPath(container), clean=QDir::cleanPath(path);
    if (container.isEmpty() || !QDir::isAbsolutePath(root) || !QDir::isAbsolutePath(clean) || clean!=path) return;
    if (!m_timer.isActive()) m_timer.start();
    auto &events=m_pending[root];
    if (events.size()>=4096 && !events.contains(clean)) {
        if (root==m_container) { m_snapshot.insert("status","Recent activity queue is full; retry after the background service catches up."); emit changed(m_snapshot); }
        return;
    }
    events[clean]=qMax(events.value(clean),atMs>0?atMs:QDateTime::currentMSecsSinceEpoch());
    m_blocked.remove(root);
    if (root==m_container) m_dirty=true;
    if (!m_running) launch();
}
void RecentBackend::launch() {
    if (m_running) return;
    QString root;
    if (m_dirty && !m_container.isEmpty() && !m_blocked.contains(m_container)) {
        root=m_container; m_dirty=false;
    } else {
        for (auto it=m_pending.cbegin();it!=m_pending.cend();++it)
            if (!it.value().isEmpty() && !m_blocked.contains(it.key())) { root=it.key(); break; }
    }
    if (root.isEmpty()) return;
    m_running=true;
    const auto generation=m_generation; const auto scan=m_scan;
    const auto queued=m_pending.take(root);
    QVector<r::Activity> events; events.reserve(queued.size());
    for (auto it=queued.cbegin();it!=queued.cend();++it) events.append({it.key(),it.value()});
    if (generation==m_generation && root==m_container) {
        m_snapshot.insert("loading",true); emit changed(m_snapshot);
    }
    auto *watcher=new QFutureWatcher<r::Snapshot>(this);
    connect(watcher,&QFutureWatcher<r::Snapshot>::finished,this,[this,watcher,root,generation,queued] {
        const auto result=watcher->result(); watcher->deleteLater(); m_running=false;
        if (!result.error.isEmpty()) {
            m_blocked.insert(root);
            auto &pending=m_pending[root];
            for (auto it=queued.cbegin();it!=queued.cend();++it) pending[it.key()]=qMax(pending.value(it.key()),it.value());
        }
        if (generation==m_generation && root==m_container) publish(result);
        // Retry failed writes on the timer, not in a tight loop.
        if (m_dirty || !m_pending.isEmpty()) launch();
    });
    watcher->setFuture(QtConcurrent::run([scan,root,events] {
        try { return scan(root,events,500); }
        catch (const std::exception &e) { r::Snapshot result; result.error=QString::fromUtf8(e.what()); return result; }
        catch (...) { r::Snapshot result; result.error="Could not read recent files."; return result; }
    }));
}
void RecentBackend::publish(const r::Snapshot &result) {
    QJsonArray rows;
    for (const auto &item:result.items) rows.append(QJsonObject{{"path",item.path},{"name",item.name},
        {"activityMs",QString::number(item.activityMs)},{"size",QString::number(item.size)}});
    auto status=!result.error.isEmpty()?result.error:rows.isEmpty()?QStringLiteral("No recent files")
        :QStringLiteral("%1 recent files").arg(rows.size());
    if (result.partial) status+=QStringLiteral(" · some activity could not be verified");
    m_snapshot={{"items",rows},{"status",status},{"loading",false},{"partial",result.partial}};
    emit changed(m_snapshot);
}
