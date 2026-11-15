#pragma once
#include "SuggestedCatalog.h"
#include <QJsonArray>
#include <QJsonObject>
#include <QHash>
#include <QObject>
#include <QTimer>
#include <atomic>
#include <functional>
#include <memory>
#include <vector>

class WorkspaceAgentBackend;
// Daemon-owned, bounded asynchronous recommendation state. A window only
// subscribes to snapshots; it does not scan or open metadata itself.
class SuggestedBackend final : public QObject {
    Q_OBJECT
public:
    using Scan=std::function<society::suggested::CatalogSnapshot(const std::string &,std::size_t,const std::atomic_bool *)>;
    using Validate=std::function<bool(const std::string &,const society::suggested::Candidate &)>;
    explicit SuggestedBackend(WorkspaceAgentBackend *,QObject *parent=nullptr,Scan scan={},Validate validate={});
    ~SuggestedBackend() override;
    void selectContainer(const QString &container);
    void activate();
    void refresh();
    QJsonObject snapshot() const { return m_snapshot; }
signals:
    void changed(const QJsonObject &snapshot);
private:
    struct Item { society::suggested::Candidate candidate; society::suggested::Recommendation recommendation; };
    struct Assessment { society::suggested::Candidate candidate; std::optional<society::suggested::Recommendation> recommendation; };
    void launchScan(quint64 generation);
    void acceptScan(quint64 generation,const society::suggested::CatalogSnapshot &);
    void startNext();
    void assessed(const QString &ticket,const QJsonArray &,const QString &error);
    void publish();
    WorkspaceAgentBackend *m_agent;
    Scan m_scan; Validate m_validate;
    QTimer m_timer;
    QString m_container,m_ticket,m_error;
    QJsonObject m_snapshot;
    quint64 m_generation=0;
    qsizetype m_unassessable=0;
    std::shared_ptr<std::atomic_bool> m_cancel;
    std::vector<Item> m_items;
    std::vector<society::suggested::Candidate> m_pending,m_batch;
    QHash<QString,std::optional<society::suggested::Recommendation>> m_cache;
    bool m_active=false,m_scanning=false,m_rescan=false,m_validating=false,m_partial=false,m_pendingIndex=false,m_waiting=false;
};
