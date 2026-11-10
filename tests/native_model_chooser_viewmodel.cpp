#include "NativeModelChooserViewModel.h"
#include <stdexcept>
#include <vector>

static void require(bool condition)
{
    if (!condition) throw std::runtime_error("Native model chooser viewmodel contract failed");
}

int main()
{
    NativeModelChooserViewModel model;
    const std::vector<society::ModelChoice> choices{
        {"/Models/Checkpoint/base.safetensors", "base.safetensors", "Checkpoint"},
        {"/Models/LLM/encoder.gguf", "encoder.gguf", "LLM"}};
    model.initialize(choices, {choices[1].path});
    require(model.canConfirm.get());
    auto snapshot = model.source->snapshot();
    require(bool(snapshot) && (*snapshot)->entries.size() == 2);
    model.focus(0, (*snapshot)->entries[0]);
    require(model.canToggle.get());
    // Moving focus must not add or remove a model.
    std::vector<std::string> result;
    model.onConfirm = [&](const auto &paths) { result = paths; };
    model.confirm();
    require(result == std::vector<std::string>{choices[1].path});
    model.toggleFocused();
    model.confirm();
    require(result == std::vector<std::string>{choices[0].path, choices[1].path});
    snapshot = model.source->snapshot();
    model.focus(1, (*snapshot)->entries[1]);
    model.toggleFocused();
    snapshot = model.source->snapshot();
    model.focus(0, (*snapshot)->entries[0]);
    model.toggleFocused();
    require(!model.canConfirm.get());
    result.clear();
    model.confirm();
    require(result.empty());
    model.resize(860, 587);
    require(model.listWidth.get() > 0 && model.listHeight.get() > 0);
}
