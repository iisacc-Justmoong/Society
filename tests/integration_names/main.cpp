#include "CivitaiIntegration.h"
#include "HuggingFaceIntegration.h"
#include "OllamaIntegration.h"

#include <type_traits>

static_assert(std::is_class_v<HuggingFaceIntegration>);
static_assert(std::is_class_v<CivitaiIntegration>);
static_assert(std::is_class_v<OllamaIntegration>);

int main() {
    [[maybe_unused]] HuggingFaceIntegration hugging_face;
    [[maybe_unused]] CivitaiIntegration civitai;
    [[maybe_unused]] OllamaIntegration ollama;
}
