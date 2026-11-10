#pragma once
#include <optional>
#include <string>

namespace society::native {
// Returns an error if the operating system rejects the open request.
std::optional<std::string> openNativeFile(const std::string &absolutePath);
}
