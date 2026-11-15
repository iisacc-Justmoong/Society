#include "SuggestedBackend.h"
#include "WorkspaceAgentBackend.h"
#include <QDateTime>
#include <QDir>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QPointer>
#include <QUuid>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>
#include <utility>

namespace s=society::suggested;
namespace {
constexpr std::size_t scanLimit=4096, resultLimit=200, assessmentLimit=64, batchLimit=4;
QString text(const std::string &s) { return QString::fromUtf8(s.data(),qsizetype(s.size())); }
std::string bytes(const QString &s) { return s.toUtf8().toStdString(); }
QString cacheKey(const s::Candidate &candidate) {
    return text(candidate.evidence.resourceId)+QChar(0x1f)+text(candidate.evidence.version)+QChar(0x1f)
        +QString::number(candidate.metadataRevision)+QChar(0x1f)
        +text(candidate.resource.resource.observedRevision ? candidate.resource.resource.observedRevision->value : std::string{});
}
}
SuggestedBackend::SuggestedBackend(WorkspaceAgentBackend *agent,QObject *parent,Scan scan,Validate validate)
    : QObject(parent),m_agent(agent),m_scan(std::move(scan)),m_validate(std::move(validate)) {
    if (!m_scan) m_scan=[](const std::string &root,std::size_t limit,const std::atomic_bool *cancel) {
        return s::SuggestedCatalog::scan(root,limit,cancel);
    };
    if (!m_validate) m_validate=[](const std::string &root,const s::Candidate &item) {
        return s::SuggestedCatalog::validate(root,item);
    };
    m_timer.setInterval(120000);
    connect(&m_timer,&QTimer::timeout,this,[this] {
        if (!m_scanning && !m_validating && m_ticket.isEmpty() && (m_pending.empty() || m_waiting)) refresh();
    });
    if (m_agent) {
        connect(m_agent,&WorkspaceAgentBackend::suggestedAssessed,this,&SuggestedBackend::assessed);
        connect(m_agent,&WorkspaceAgentBackend::changed,this,[this](const QJsonObject &state) {
            if (!state.value("busy").toBool() && !state.value("model").toString().isEmpty() &&
                !m_pending.empty() && m_ticket.isEmpty() && !m_scanning && !m_validating)
                QTimer::singleShot(0,this,&SuggestedBackend::startNext);
        });
    }
    publish();
}
SuggestedBackend::~SuggestedBackend() {
    if (m_cancel) m_cancel->store(true);
    if (m_agent) m_agent->cancelSuggested();
}
void SuggestedBackend::selectContainer(const QString &container) {
    const auto root=container.isEmpty()?QString():QDir::cleanPath(container);
    if (root==m_container) return;
    m_container=root; m_timer.stop(); m_cache.clear();
    if (m_active) refresh(); else { ++m_generation; m_items.clear(); m_pending.clear(); publish(); }
}
void SuggestedBackend::activate() {
    if (!m_active) refresh();
}
void SuggestedBackend::refresh() {
    m_active=true;
    if (!m_container.isEmpty() && !m_timer.isActive()) m_timer.start();
    ++m_generation;
    if (m_cancel) m_cancel->store(true);
    if (m_agent) m_agent->cancelSuggested();
    m_ticket.clear(); m_pending.clear(); m_batch.clear(); m_items.clear(); m_error.clear();
    m_partial=false; m_pendingIndex=false; m_waiting=false; m_unassessable=0;
    if (m_container.isEmpty()) { m_rescan=false; publish(); return; }
    if (m_scanning) { m_rescan=true; publish(); return; }
    launchScan(m_generation);
}
void SuggestedBackend::launchScan(quint64 generation) {
    m_rescan=false; m_scanning=true; m_cancel=std::make_shared<std::atomic_bool>(false);
    const auto root=bytes(m_container); const auto scan=m_scan; const auto cancel=m_cancel;
    auto *watcher=new QFutureWatcher<s::CatalogSnapshot>(this);
    connect(watcher,&QFutureWatcher<s::CatalogSnapshot>::finished,this,[this,watcher,generation] {
        const auto value=watcher->result(); watcher->deleteLater(); m_scanning=false;
        if (generation!=m_generation) { if (m_rescan && !m_container.isEmpty()) launchScan(m_generation); return; }
        acceptScan(generation,value);
    });
    watcher->setFuture(QtConcurrent::run([root,scan,cancel] {
        try { return scan(root,scanLimit,cancel.get()); }
        catch (const std::exception &error) { s::CatalogSnapshot value; value.error=error.what(); return value; }
        catch (...) { s::CatalogSnapshot value; value.error="Could not read the Files index."; return value; }
    }));
    publish();
}
void SuggestedBackend::acceptScan(quint64 generation,const s::CatalogSnapshot &scan) {
    if (generation!=m_generation) return;
    m_error=text(scan.error); m_partial=scan.partial; m_pendingIndex=scan.pending;
    s::Policy policy;
    for (const auto &candidate:scan.items) {
        const auto rule=policy.classify(candidate.evidence);
        if (rule.kind==s::RuleKind::Recommend) {
            if (auto result=policy.evaluate(candidate.evidence,{})) m_items.push_back({candidate,std::move(*result)});
        } else if (rule.kind==s::RuleKind::Unknown &&
                   (!candidate.evidence.description.empty() || !candidate.evidence.text.empty())) {
            const auto key=cacheKey(candidate);
            if (m_cache.contains(key)) {
                auto rec=m_cache.value(key);
                if (rec) { rec->path=candidate.evidence.path; m_items.push_back({candidate,std::move(*rec)}); }
            } else if (m_pending.size()<assessmentLimit) m_pending.push_back(candidate);
            else m_partial=true;
        } else if (rule.kind==s::RuleKind::Unknown || rule.kind==s::RuleKind::Conflicting) ++m_unassessable;
    }
    publish(); startNext();
}
void SuggestedBackend::startNext() {
    if (m_scanning || m_validating || !m_ticket.isEmpty() || m_pending.empty() || m_container.isEmpty()) return;
    if (m_items.size()>=resultLimit) {
        // Rules are ordered first, so additional estimates cannot appear in
        // this bounded snapshot. Avoid loading models for invisible results.
        m_partial=true; m_pending.clear(); m_waiting=false; publish(); return;
    }
    if (!m_agent || m_agent->snapshot().value("busy").toBool() || m_agent->snapshot().value("model").toString().isEmpty()) {
        m_waiting=true; publish(); return;
    }
    const auto count=std::min(batchLimit,m_pending.size());
    m_batch.assign(m_pending.begin(),m_pending.begin()+count);
    QJsonArray evidence;
    for (const auto &item:m_batch) evidence.append(QJsonObject{{"name",text(item.evidence.name)},
        {"description",text(item.evidence.description)},{"text",text(item.evidence.text)}});
    const auto ticket=QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (!m_agent->assessSuggested(evidence,ticket)) { m_batch.clear(); m_waiting=true; publish(); return; }
    m_pending.erase(m_pending.begin(),m_pending.begin()+count);
    m_ticket=ticket; m_waiting=false; publish();
}
void SuggestedBackend::assessed(const QString &ticket,const QJsonArray &estimates,const QString &error) {
    if (ticket!=m_ticket || m_ticket.isEmpty()) return;
    m_ticket.clear();
    if (error=="Deferred for user conversation.") {
        m_pending.insert(m_pending.begin(),m_batch.begin(),m_batch.end()); m_batch.clear(); m_waiting=true; publish(); return;
    }
    if (!error.isEmpty()) { m_error=QStringLiteral("AI assessment unavailable: ")+error; m_pending.clear(); m_batch.clear(); publish(); return; }
    // The model may omit an item or return an invalid estimate. Do not silently
    // present these cases as if all uncertain files had been assessed.
    if (std::size_t(estimates.size())<m_batch.size()) m_unassessable+=m_batch.size()-std::size_t(estimates.size());
    const auto generation=m_generation; const auto root=bytes(m_container); const auto batch=m_batch; const auto validate=m_validate;
    m_batch.clear(); m_validating=true; publish();
    auto *watcher=new QFutureWatcher<std::vector<Assessment>>(this);
    connect(watcher,&QFutureWatcher<std::vector<Assessment>>::finished,this,[this,watcher,generation,expected=estimates.size()] {
        const auto accepted=watcher->result(); watcher->deleteLater(); m_validating=false;
        if (generation!=m_generation) {
            if (m_rescan && !m_scanning && !m_container.isEmpty()) launchScan(m_generation);
            else if (!m_scanning) startNext();
            return;
        }
        if (accepted.size()<std::size_t(expected)) {
            m_partial=true; m_unassessable+=std::size_t(expected)-accepted.size();
        }
        for (const auto &assessment:accepted) {
            if (m_cache.size()>=qsizetype(scanLimit)) m_cache.clear();
            m_cache.insert(cacheKey(assessment.candidate),assessment.recommendation);
            if (assessment.recommendation) m_items.push_back({assessment.candidate,*assessment.recommendation});
        }
        publish(); startNext();
    });
    watcher->setFuture(QtConcurrent::run([root,batch,validate,estimates] {
        std::vector<Assessment> accepted; s::Policy policy;
        for (const auto &value:estimates) {
            const auto object=value.toObject(); const int index=object.value("index").toInt(-1);
            if (index<0 || std::size_t(index)>=batch.size()) continue;
            const auto &candidate=batch[std::size_t(index)];
            try { if (!validate(root,candidate)) continue; } catch (...) { continue; }
            const s::Estimate e{object.value("probability").toDouble(-1),object.value("gain").toDouble(-1),
                object.value("loss").toDouble(-1),object.value("cost").toDouble(-1),bytes(object.value("explanation").toString())};
            auto rec=policy.evaluate(candidate.evidence,[&](const s::Evidence &) { return std::optional(e); });
            accepted.push_back({candidate,std::move(rec)});
        }
        return accepted;
    }));
}
void SuggestedBackend::publish() {
    std::stable_sort(m_items.begin(),m_items.end(),[](const Item &a,const Item &b) {
        if (a.recommendation.source!=b.recommendation.source) return a.recommendation.source==s::Source::Rule;
        return a.candidate.modifiedNs>b.candidate.modifiedNs;
    });
    QJsonArray rows;
    for (std::size_t i=0;i<std::min(resultLimit,m_items.size());++i) {
        const auto &item=m_items[i]; const auto &e=item.candidate.evidence; const auto &r=item.recommendation;
        rows.append(QJsonObject{{"path",text(e.path)},{"name",text(e.name)},{"resourceId",text(e.resourceId)},
            {"workingVersion",text(e.version)},{"contentRevision",text(item.candidate.resource.resource.observedRevision ? item.candidate.resource.resource.observedRevision->value : std::string{})},
            {"size",QString::number(item.candidate.size)},{"modifiedMs",QString::number(item.candidate.modifiedNs/1000000)},
            {"reason",text(r.reason)},{"explanation",text(r.explanation)},
            {"source",r.source==s::Source::Rule?"rule":"model_estimate"}});
    }
    QString status;
    if (m_container.isEmpty()) status="Society drive is not available.";
    else if (!m_active) status="Open Suggested to assess files.";
    else if (m_scanning || m_rescan) status="Finding unfinished work…";
    else if (!m_error.isEmpty()) status=m_error;
    else if (m_pendingIndex) status="Waiting for the Files index.";
    else if (m_waiting) status="Rule results ready · waiting for the local agent.";
    else if (!m_ticket.isEmpty() || m_validating || !m_pending.empty()) status="Assessing uncertain files · AI estimates are uncalibrated.";
    else status=rows.isEmpty()?QStringLiteral("No unfinished files suggested."):QStringLiteral("%1 suggested files").arg(rows.size());
    if (!m_scanning && !m_pendingIndex && m_unassessable>0)
        status+=QStringLiteral(" · %1 need more evidence").arg(m_unassessable);
    if (m_partial || m_items.size()>resultLimit) status+=QStringLiteral(" · partial results");
    m_snapshot={{"items",rows},{"status",status},{"partial",m_partial || m_items.size()>resultLimit},
        {"assessing",m_scanning || m_rescan || m_validating || !m_ticket.isEmpty() || (!m_pending.empty() && !m_waiting)},
        {"waiting",m_waiting},{"pendingIndex",m_pendingIndex},{"unassessable",int(m_unassessable)}};
    emit changed(m_snapshot);
}
