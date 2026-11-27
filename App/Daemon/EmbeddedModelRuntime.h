#pragma once

#include <iiLocalLLM>
#include <QHash>
#include <memory>
#include <stop_token>

/// One host-approved model component within a resident Society model file.
/// The digest identifies the bytes in [offset, offset + size), not the package.
struct EmbeddedModelSource final {
    QString path;
    QString sha256;
    quint64 offset = 0;
    quint64 size = 0;
};

/// Register a small reference-backed runtime that verifies and opens existing
/// Models files, then delegates inference to iiLocalLLM's GGUF runtime.
/// The supplied root confines every source and remains a host-owned setting.
[[nodiscard]] std::shared_ptr<iiLocalLLM::Runtime> createEmbeddedModelRuntime(
    QString modelsRoot, QHash<QString,EmbeddedModelSource> sources,
    std::shared_ptr<iiLocalLLM::Runtime> delegate);

/// After a successful direct load, remove only the obsolete generated cache
/// whose full bytes and manifest still match the verified resident source.
/// Unknown, changed, redirected or cancelled entries are retained.
[[nodiscard]] bool removeVerifiedLegacyModelCache(QString modelsRoot,QString catalogRoot,
    const EmbeddedModelSource &source,std::stop_token cancellation={});
