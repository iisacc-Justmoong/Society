#pragma once
#include "PhotoClippedRegion.h"

namespace society::native {
// Process-lifetime stateless driver; native region creation is owner-thread only.
[[nodiscard]] PhotoWindowDriver& photoWindowsDriver() noexcept;
} // namespace society::native
