#include "NativeNavigation.generated.h"
#include <iiGUI>
#include <algorithm>
#include <array>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {
void require(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
template<class T> T take(ii::core::Result<T> value) {
    if (!value) throw std::runtime_error(value.error().message);
    return std::move(*value);
}
void good(ii::core::Result<void> value) { if (!value) throw std::runtime_error(value.error().message); }
struct Device final : ii::gui::PaintDevice {
    std::size_t images = 0;
    ii::gui::PaintCapabilities capabilities() const noexcept override { return {}; }
    ii::core::Result<void> begin_frame(const ii::gui::PaintFrameInfo &) override { images = 0; return {}; }
    ii::core::Result<void> begin_scene(const ii::gui::PaintSceneInfo &) override { return {}; }
    ii::core::Result<void> end_scene() override { return {}; }
    ii::core::Result<void> begin_layer(const ii::gui::PaintLayerInfo &) override { return {}; }
    ii::core::Result<void> draw_panels(std::span<const ii::gui::Panel>) override { return {}; }
    ii::core::Result<void> draw_text(std::span<const ii::gui::TextRun>) override { return {}; }
    ii::core::Result<void> draw_images(std::span<const ii::gui::ImageRun> runs) override { images += runs.size(); return {}; }
    ii::core::Result<void> end_layer() override { return {}; }
    ii::core::Result<void> present() override { return {}; }
    void abort_frame() noexcept override {}
};
}

int main() {
    using View = lvui_generated::NativeNavigation;
    using Action = NativeNavigationViewModel::Action;
    auto device = std::make_shared<Device>();
    std::shared_ptr<View::Scope> scope;
    std::vector<Action> requests;
    auto view = take(ii::gui::PaintView::create(device, [&](auto &builder) {
        return View::build(builder, {}, [&](auto &value) {
            value.backend_0.onNavigate = [&](Action action) { requests.push_back(action); };
        }, &scope);
    }, View::appearance()));
    good(view->paint({166, 22}));
    require(device->images == 7, "all seven LVUI icons render");
    const auto group = take(take(view->find("society-navigation")).snapshot());
    require(group.bounds.width == 166 && group.bounds.height == 22, "seven buttons use LVUI Figma geometry");
    const std::array<std::pair<std::string_view, Action>, 7> items{{
        {"environment", Action::Environment}, {"tasks", Action::Tasks}, {"library", Action::Library},
        {"space", Action::Space}, {"browse", Action::Browse}, {"people", Action::People}, {"hosting", Action::Hosting}}};
    double nextX = group.bounds.x;
    for (const auto &[name, action] : items) {
        auto widget = take(view->find(name));
        auto snapshot = take(widget.snapshot());
        require(snapshot.bounds.x == nextX && snapshot.bounds.width == 22 && snapshot.bounds.height == 22,
                "Figma order, size and spacing");
        require(snapshot.accessibility_name.size() > 0, "segment has an accessibility name");
        require(snapshot.checked == (action == Action::Environment), "only current destination is selected");
        good(widget.activate());
        require(requests.back() == action, "activation routes its own action");
        nextX += 24;
    }
    require(scope->backend_0.setCurrent("Tools"), "update current page");
    require(take(take(view->find("tasks")).snapshot()).checked, "selection follows external navigation");
    require(!take(take(view->find("environment")).snapshot()).checked, "previous selection cleared");
    good(view->close());
    scope.reset();
}
