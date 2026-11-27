#pragma once

#include <QJsonObject>
#include <QList>
#include <QString>
#include <memory>
#include <optional>
#include <stop_token>

namespace society::content {

// The host chooses a model and any model-specific instruction prefixes.
// Neither prefix is inferred from the filename or from an agent response.
struct Profile final {
    QString model;
    QString documentPrefix;
    QString queryPrefix;
};

// Implementations own model resolution and inference. The revision must
// change when the model's declared files or inference configuration changes.
class Embedder {
public:
    virtual ~Embedder() = default;
    [[nodiscard]] virtual QString revision(const QString &model, std::stop_token cancellation) = 0;
    [[nodiscard]] virtual QList<float> embed(const QString &model, const QString &text,
                                             std::stop_token cancellation) = 0;
};

// The host invalidates the selection token when its container or model
// service changes. A retained provider then cannot publish to the old drive.
struct EmbedderAccess final {
    std::shared_ptr<Embedder> embedder;
    std::stop_token cancellation;
};

// A failed operation has a nonempty error; successful data is structured for
// the daemon, application API and language bindings to project consistently.
struct Result final {
    enum class Failure { none, unsupported, retryable };
    QJsonObject value;
    QString error;
    Failure failure = Failure::none;
};

// One invocation discovers and processes a bounded amount of observed work.
// Tests may inject a clock and temporary root; production uses the system clock.
struct BatchOptions final {
    int scanLimit = 32;
    int workLimit = 4;
    qint64 nowMs = -1;
    QString temporaryRoot;
};

// File indexing is restricted to file hashes and directory metadata. Legacy
// content-embedding entrypoints remain callable only to report this policy.
[[nodiscard]] bool fileContentEmbeddingEnabled() noexcept;

// A cheap intake filter; the index validates UTF-8 and content only when it
// reads the current file and rechecks the managed revision.
[[nodiscard]] bool isTextCandidate(const QString &path, quint64 size);

// Legacy content-embedding entrypoint. Under the hash-metadata-only policy it
// returns an explicit unsupported result before reading files or using a model.
// The former temporary-root parameter remains accepted for compatibility.
[[nodiscard]] Result indexText(const QString &container, const QString &path, const Profile &profile,
                               Embedder &embedder, const QString &temporaryRoot = {},
                               std::stop_token cancellation = {});

// Legacy content-vector search is disabled by the hash-metadata-only policy.
[[nodiscard]] Result searchText(const QString &container, const QString &query, const Profile &profile,
                                Embedder &embedder, int limit = 10,
                                std::stop_token cancellation = {});

// Legacy content-embedding queue processing is disabled by the same policy.
[[nodiscard]] Result indexTrackedTextBatch(const QString &container, const Profile &profile,
                                           Embedder &embedder, const BatchOptions &options = {},
                                           std::stop_token cancellation = {});

// Report automatic content embedding as disabled without creating a queue or
// loading a model. A previously configured profile is preserved and reported.
[[nodiscard]] Result trackedTextPolicy(const QString &container, std::stop_token cancellation = {});

// Reject content-embedding enablement. A disable request reports the effective
// policy without deleting an existing persisted profile or its old data.
[[nodiscard]] Result setTrackedTextPolicy(const QString &container, const std::optional<Profile> &profile,
                                          Embedder *embedder = nullptr, std::stop_token cancellation = {});

}
