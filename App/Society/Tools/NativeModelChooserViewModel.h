#pragma once

#include "NativeModelChooserState.h"

#include <LVUI>
#include <iiSDKCore>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// Constructed by LVUI for one chooser activation; storage stays in Society.
struct NativeModelChooserViewModel final {
    std::shared_ptr<lvui::ListMemory> source = lvui::ListMemory::create().value();
    ii::core::Observable<std::shared_ptr<lvui::ListController>> controller{
        std::make_shared<lvui::ListController>(lvui::ListController::create(source).value())};
    ii::core::Observable<std::int64_t> revision{0};
    ii::core::Observable<std::string> status{"No models selected"};
    ii::core::Observable<bool> canConfirm{false};
    ii::core::Observable<bool> canToggle{false};
    ii::core::Observable<double> listWidth{822};
    ii::core::Observable<double> listHeight{400};
    std::function<void(const std::vector<std::string> &)> onConfirm;
    std::function<void()> onCancel;
    std::function<void()> onBrowse;

    void initialize(std::vector<society::ModelChoice> choices, const std::vector<std::string> &selected);
    void focus(std::size_t index, const lvui::ListEntry &entry);
    void toggleFocused();
    void confirm();
    void cancel();
    void browse();
    void resize(double width, double height);
private:
    void refreshStatus();
    society::ModelChooserState state;
    std::optional<std::size_t> focused;
    std::string focusedKey;
};
