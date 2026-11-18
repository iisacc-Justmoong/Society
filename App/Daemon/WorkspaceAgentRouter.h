#pragma once
#include <iiMultimodalAgent>
#include <QHash>
#include <QJsonObject>
#include <functional>
#include <memory>
#include <mutex>

// Produces explicitly uncalibrated estimates for text-model routing only.
// It does not approve or execute tools, grant permissions, or claim measured
// success probabilities. iiDecision still evaluates the returned candidates.
class WorkspaceAgentRouter final {
public:
    using Token = iiMultimodalAgent::CancellationToken;
    using Status = std::function<void(const QString &)>;
    WorkspaceAgentRouter(std::shared_ptr<iiMultimodalAgent::agent::Model> estimator,
        QString primaryModel, iiMultimodalAgent::agent::DecisionOptions policy,
        QHash<QString,QJsonObject> routeEvidence, std::function<Token()> token,
        std::function<QString()> context, Status status);
    iiMultimodalAgent::agent::DecisionInput assess(const iiMultimodalAgent::agent::DecisionRequest &request);
private:
    std::shared_ptr<iiMultimodalAgent::agent::Model> m_estimator;
    QString m_primary;
    iiMultimodalAgent::agent::DecisionOptions m_policy;
    QHash<QString,QJsonObject> m_routeEvidence;
    std::function<Token()> m_token;
    std::function<QString()> m_context;
    Status m_status;
    std::mutex m_mutex;
    std::string m_cachedRun, m_cachedGoal;
    std::vector<iiMultimodalAgent::agent::DecisionEstimate> m_estimates;
};
