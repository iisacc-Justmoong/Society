#include "RelocationVerification.h"

#include <stdexcept>
#include <utility>

namespace society::semantics {
DestinationObservation stabilizeDestination(DestinationObservation previous,
                                              const std::function<DestinationObservation()> &observe) {
    if (previous.stamp.isEmpty()) throw std::runtime_error("The relocation destination cannot be verified");
    constexpr int maximumObservations = 3;
    for (int attempt = 0; attempt < maximumObservations; ++attempt) {
        auto current = observe();
        if (current.stamp.isEmpty() || current.physicalIdentity != previous.physicalIdentity)
            throw std::runtime_error("The asserted relocation destination changed while being verified");
        if (current.stamp == previous.stamp) return current;
        if (previous.physicalIdentity.isEmpty())
            throw std::runtime_error("The asserted relocation destination changed while being verified");
        previous = std::move(current);
    }
    throw std::runtime_error("The asserted relocation destination did not become stable");
}
}
