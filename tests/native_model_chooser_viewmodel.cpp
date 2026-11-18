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
    const auto controller = model.controller.get();
    const auto before = model.source->index().value();
    std::vector<std::int64_t> published;
    auto changes = model.revision.observe([&](std::int64_t value, std::uint64_t) { published.push_back(value); });
    auto snapshot = model.source->snapshot();
    require(bool(snapshot) && (*snapshot)->entries.size() == 2);
    model.focus(0, (*snapshot)->entries[0]);
    require(model.canToggle.get() && published.empty());
    // Moving focus must not add or remove a model.
    std::vector<std::string> result;
    model.onConfirm = [&](const auto &paths) { result = paths; };
    model.confirm();
    require(result == std::vector<std::string>{choices[1].path});
    model.toggleFocused();
    const auto after = model.source->index().value();
    require(model.controller.get() == controller && published == std::vector<std::int64_t>{1});
    require(before.entry(0).description == "Models / Checkpoint"
            && after.entry(0).description == "Selected · Models / Checkpoint"
            && &after.entry(1) == &before.entry(1));
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
