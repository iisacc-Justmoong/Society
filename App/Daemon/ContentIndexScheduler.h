#pragma once

#include "Society/Content/ContentIndex.h"
#include <QObject>
#include <QFileSystemWatcher>
#include <QJsonObject>
#include <QTimer>
#include <functional>
#include <stop_token>
#include <vector>

namespace society::content { class FileDiscovery; }

// Owns bounded automatic content work for the selected drive. It does not
// select a model or mutate source files; the persisted policy supplies both
// the model choice and its document/query prefixes.
class ContentIndexScheduler final : public QObject {
public:
    using Provider=std::function<society::content::EmbedderAccess()>;
    using ForegroundBusy=std::function<bool()>;
    enum class Admission { ready, deferred, full };

    // The interval is injectable for hosts and tests; zero uses the validated
    // environment setting or a conservative default.
    explicit ContentIndexScheduler(Provider provider,ForegroundBusy foregroundBusy,
                                   int intervalMs=0,QObject *parent=nullptr);
    ~ContentIndexScheduler() override;

    // Revoke the old selection immediately; a finishing worker cannot publish
    // its status into the new selection.
    void selectContainer(const QString &container);
    [[nodiscard]] std::stop_token selectionToken() const;

    // Policy writes pause and cancel background work until the caller reports
    // completion, including failed or cancelled writes.
    void beginPolicyChange();
    void endPolicyChange();

    // A foreground request can wait for background cancellation without
    // blocking the event loop. The caller owns validation and reply handling.
    [[nodiscard]] Admission yieldTo(std::function<void()> continuation);
    // Return the last worker observation; it may lag a policy change or poll.
    [[nodiscard]] QJsonObject status() const;

private:
    // Start at most one bounded worker when the selected drive and foreground
    // state permit it; polling a disabled policy never starts inference.
    void tick();
    // Drop event hints when the selected container or its policy changes.
    void clearWatches();
    // Watch a bounded subset; periodic intake remains authoritative when the
    // platform cannot watch a path or its watch budget is exhausted.
    void addWatches(const QStringList &directories,const QStringList &files);
    Provider m_provider;
    ForegroundBusy m_foregroundBusy;
    QTimer m_timer;
    QFileSystemWatcher m_fileWatcher;
    std::shared_ptr<society::content::FileDiscovery> m_discovery;
    QString m_container;
    QJsonObject m_status{{"state","disabled"}};
    std::stop_source m_selection;
    std::stop_source m_stop;
    std::vector<std::function<void()>> m_waiters;
    quint64 m_generation=0;
    int m_policyChanges=0;
    bool m_active=false;
    bool m_disabled=false;
    bool m_discoveryHint=false;
    bool m_watchIncomplete=false;
};
