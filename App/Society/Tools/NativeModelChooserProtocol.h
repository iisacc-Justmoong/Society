#pragma once

#include "NativeModelChooserState.h"
#include <string>
#include <string_view>
#include <vector>

namespace society {
struct ModelChooserRequest final {
    std::vector<ModelChoice> models;
    std::vector<std::string> selected;
};
// Bounded, strict UTF-8 JSON. Throws invalid_argument for a malformed request.
ModelChooserRequest parseModelChooserRequest(std::string_view input);
std::string modelChooserSelected(const std::vector<std::string> &paths);
std::string modelChooserFolder(std::string_view path);
std::string modelChooserCancel();
}
