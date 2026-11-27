#pragma once

#include "Society/Content/ContentIndex.h"
#include <QJsonObject>
#include <functional>
#include <memory>
#include <stop_token>
#include <utility>

namespace iiLocalLLM { class Service; }

// Adapts a host-owned iiLocalLLM service to the content index. The provider
// chooses the service and load options without making the index own a model.
class ServiceContentEmbedder final : public society::content::Embedder {
public:
    using ServiceAccess=std::pair<std::shared_ptr<iiLocalLLM::Service>,QJsonObject>;
    using ServiceProvider=std::function<ServiceAccess(const QString &,std::stop_token)>;
    explicit ServiceContentEmbedder(ServiceProvider provider);
    [[nodiscard]] QString revision(const QString &model,std::stop_token cancellation) override;
    [[nodiscard]] QList<float> embed(const QString &model,const QString &text,
                                     std::stop_token cancellation) override;
private:
    [[nodiscard]] ServiceAccess access(const QString &model,std::stop_token cancellation) const;
    ServiceProvider m_provider;
};
