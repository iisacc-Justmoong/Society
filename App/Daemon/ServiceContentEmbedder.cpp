#include "ServiceContentEmbedder.h"

#include <iiLocalLLM>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <stdexcept>

ServiceContentEmbedder::ServiceContentEmbedder(ServiceProvider provider) : m_provider(std::move(provider)) {}

// The fingerprint identifies declared model files and actual requested load
// options; it is not evidence that the embedding model is accurate.
QString ServiceContentEmbedder::revision(const QString &model,std::stop_token cancellation) {
    const auto [service,options]=access(model,cancellation);
    const auto record=service->resolveModel(model).get();
    if (cancellation.stop_requested()) throw std::runtime_error("Content embedding was cancelled.");
    if (!record.manifest.capabilities.contains("embedding"))
        throw std::runtime_error("The selected local model does not declare embedding support.");
    const QJsonObject identity{{"manifest",iiLocalLLM::manifestObject(record.manifest,true)},{"loadOptions",options}};
    return QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(identity).toJson(QJsonDocument::Compact),
                                                        QCryptographicHash::Sha256).toHex());
}

// Explicit options prevent reuse of a previously loaded model with different
// pooling. iiLocalLLM serializes this with the host's other model requests.
QList<float> ServiceContentEmbedder::embed(const QString &model,const QString &text,std::stop_token cancellation) {
    const auto [service,options]=access(model,cancellation);
    iiLocalLLM::ModelLoadRequest request; request.model=model; request.options=options;
    (void)service->loadModel(request).get();
    if (cancellation.stop_requested()) throw std::runtime_error("Content embedding was cancelled.");
    iiLocalLLM::CancellationToken token;
    std::stop_callback onStop(cancellation,[token] { token.cancel(); });
    return service->embedText(model,text,token).get();
}

ServiceContentEmbedder::ServiceAccess ServiceContentEmbedder::access(const QString &model,std::stop_token cancellation) const {
    if (!m_provider || cancellation.stop_requested()) throw std::runtime_error("The local embedding service is unavailable.");
    auto [service,options]=m_provider(model,cancellation);
    if (!service || cancellation.stop_requested()) throw std::runtime_error("The local embedding service is unavailable.");
    options.insert("embedding_pooling","model");
    return {std::move(service),std::move(options)};
}
