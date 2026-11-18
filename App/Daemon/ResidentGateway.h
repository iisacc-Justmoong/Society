#pragma once

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <functional>
#include <memory>

// The executor owns the actual runtime. The gateway owns only request routing.
class ResidentGateway final : public QObject {
    Q_OBJECT
public:
    using Callback = std::function<void(QJsonObject)>;
    using Cancel = std::function<void()>;
    using Executor = std::function<Cancel(const QJsonArray &, const QJsonObject &, Callback)>;

    explicit ResidentGateway(QObject *parent = nullptr);
    ~ResidentGateway() override;
    QJsonObject status() const { return m_status; }
    void setLoading(const QString &model);
    void setReady(const QString &model, Executor executor);
    void setError(const QString &model, const QString &error);
    void clear(const QString &reason);
    QString start(const QJsonArray &messages, const QJsonObject &options, QString *error = nullptr);
    bool cancel(const QString &ticket);

signals:
    void changed(const QJsonObject &status);
    void event(const QString &ticket, const QJsonObject &event);

private:
    struct Bridge;
    struct Pending { Cancel cancel; std::shared_ptr<Executor> executor; bool invalidated = false; };
    void invalidate(const QString &reason);
    void deliver(const QString &ticket, const QJsonObject &event);
    void drain();
    std::shared_ptr<Bridge> m_bridge;
    std::shared_ptr<Executor> m_executor;
    QHash<QString, Pending> m_pending;
    QJsonObject m_status{{"state", "unavailable"}};
};
