#pragma once

#include <QString>
#include <optional>

// A best-effort native file identity that does not read file contents. An
// unavailable creation time is deliberately not replaced by a weaker guess.
namespace society::semantics {
[[nodiscard]] std::optional<QString> physicalIdentity(const QString &absolutePath);
}
