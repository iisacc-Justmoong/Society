#pragma once

#include <QString>
#include <functional>

namespace society::semantics {
struct DestinationObservation {
    QString physicalIdentity;
    QString stamp;
};

// A metadata-only change can alter a stamp without changing the file. Retry
// only while the same independently verified native identity remains present.
[[nodiscard]] DestinationObservation stabilizeDestination(
    DestinationObservation previous, const std::function<DestinationObservation()> &observe);
}
