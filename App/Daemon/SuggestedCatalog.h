#pragma once
#include "SuggestedPolicy.h"
#include <iiResource/Resource.h>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace society::suggested {
struct Candidate {
    Evidence evidence;
    std::string catalogKey;
    ii::resource::Snapshot resource;
    std::uint64_t metadataRevision = 0, size = 0;
    std::int64_t modifiedNs = 0;
};
struct CatalogSnapshot {
    std::vector<Candidate> items;
    std::string error;
    bool pending = false, partial = false;
};
class SuggestedCatalog final {
public:
    // Blocking and read-only. Invoke on a background I/O worker.
    static CatalogSnapshot scan(const std::string &container, std::size_t limit,
                                const std::atomic_bool *cancel = nullptr);
    static bool validate(const std::string &container, const Candidate &candidate);
};
}
