#include "NativeModelChooserViewModel.h"
#include <LVUI>

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace {
std::string description(const society::ModelChoice &choice, bool selected)
{
    return (selected ? "Selected · Models / " : "Models / ") + choice.category;
}
}

void NativeModelChooserViewModel::initialize(std::vector<society::ModelChoice> choices,
                                             const std::vector<std::string> &selected)
{
    society::ModelChooserState candidate(std::move(choices), selected);
    std::vector<lvui::ListEntry> entries;
    entries.reserve(candidate.choices().size());
    for (std::size_t i = 0; i < candidate.choices().size(); ++i) {
        const auto &choice = candidate.choices()[i];
        entries.push_back({.key = std::to_string(i), .label = choice.name,
                           .description = description(choice, candidate.isSelected(i)),
                           .editable = false, .draggable = false});
    }
    auto next = lvui::ListMemory::create(std::move(entries));
    if (!next) throw std::runtime_error(next.error().message);
    auto nextController = lvui::ListController::create(*next);
    if (!nextController) throw std::runtime_error(nextController.error().message);
    state = std::move(candidate); source = *next;
    focused.reset(); focusedKey.clear(); canToggle.set(false);
    controller.set(std::make_shared<lvui::ListController>(std::move(*nextController)));
    revision.set(0);
    refreshStatus();
}

void NativeModelChooserViewModel::focus(std::size_t index, const lvui::ListEntry &entry)
{
    const auto rows = source->index();
    if (!rows || rows->find(entry.key) != index || index >= state.choices().size()) return;
    focused = index;
    focusedKey = entry.key;
    canToggle.set(true);
}

void NativeModelChooserViewModel::toggleFocused()
{
    if (!focused) return;
    const auto rows = source->index();
    if (!rows) return;
    const auto index = rows->find(focusedKey);
    if (index != focused) {
        focused.reset();
        focusedKey.clear();
        canToggle.set(false);
        return;
    }
    auto replacement = rows->entry(*index);
    const bool selected = !state.isSelected(*index);
    replacement.description = description(state.choices()[*index], selected);
    const auto updated = source->apply({.revision = rows->revision(), .kind = lvui::ListEditKind::replace,
                                        .key = focusedKey, .item = std::move(replacement)});
    if (!updated || !state.toggle(*index)) return;
    revision.set(static_cast<std::int64_t>(*updated));
    refreshStatus();
}

void NativeModelChooserViewModel::refreshStatus()
{
    const auto count = state.selectedCount();
    status.set(count ? std::to_string(count) + (count == 1 ? " model selected" : " models selected")
                     : "No models selected");
    canConfirm.set(count != 0);
}

void NativeModelChooserViewModel::confirm() { if (canConfirm.get() && onConfirm) onConfirm(state.selectedPaths()); }
void NativeModelChooserViewModel::cancel() { if (onCancel) onCancel(); }
void NativeModelChooserViewModel::browse() { if (onBrowse) onBrowse(); }

void NativeModelChooserViewModel::resize(double width, double height)
{
    const auto &tokens = lvui::default_style_tokens();
    const auto padding = tokens.number("--lvrs-theme-metrics-space-18").value();
    const auto gap = tokens.number("--lvrs-theme-metrics-space-12").value();
    const auto row = tokens.number("--lvrs-theme-metrics-size-56").value();
    const auto minimum = tokens.number("--lvrs-theme-metrics-size-140").value();
    // Reserve token-derived space for the surrounding labels and buttons.
    listWidth.set(std::max(minimum, width - 2 * padding));
    listHeight.set(std::max(minimum, height - 2 * padding - 3 * gap - 2 * row));
}
