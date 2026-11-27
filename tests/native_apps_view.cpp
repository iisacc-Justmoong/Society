#include "NativeApps.generated.h"
#include <iiGUI>
#include <memory>
#include <source_location>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {
void check(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
template<class T> T take(ii::core::Result<T> result, const std::source_location location = std::source_location::current()) {
    if (!result) throw std::runtime_error(std::to_string(location.line()) + ": " + result.error().message);
    return std::move(*result);
}
void good(ii::core::Result<void> result) { if (!result) throw std::runtime_error(result.error().message); }
struct Device final : ii::gui::PaintDevice {
    ii::gui::PaintCapabilities capabilities() const noexcept override { return {}; }
    ii::core::Result<void> begin_frame(const ii::gui::PaintFrameInfo &) override { return {}; }
    ii::core::Result<void> begin_scene(const ii::gui::PaintSceneInfo &) override { return {}; }
    ii::core::Result<void> end_scene() override { return {}; }
    ii::core::Result<void> begin_layer(const ii::gui::PaintLayerInfo &) override { return {}; }
    ii::core::Result<void> draw_panels(std::span<const ii::gui::Panel>) override { return {}; }
    ii::core::Result<void> draw_text(std::span<const ii::gui::TextRun>) override { return {}; }
    ii::core::Result<void> draw_images(std::span<const ii::gui::ImageRun>) override { return {}; }
    ii::core::Result<void> end_layer() override { return {}; }
    ii::core::Result<void> present() override { return {}; }
    void abort_frame() noexcept override {}
};
struct Icons final : lvui::IconSource {
    lvui::IconResult<std::shared_ptr<const lvui::IconAsset>> resolve(std::string_view) const override { return image; }
    std::uint64_t revision() const noexcept override { return 1; }
    std::shared_ptr<const lvui::IconAsset> image = std::make_shared<const lvui::IconAsset>(lvui::IconAsset{R"svg(<svg xmlns="http://www.w3.org/2000/svg" width="18" height="18"/>)svg"});
};
}

int main() {
    using View = lvui_generated::NativeApps;
    std::shared_ptr<View::Scope> scope;
    std::vector<std::string> actions;
    auto view = take(ii::gui::PaintView::create(std::make_shared<Device>(), [&](auto &builder) {
        return View::build(builder, {}, [&](auto &value) {
            value.backend_0.iconSource = std::make_shared<Icons>();
            value.backend_0.setApps({{"congregation", "Congregation", "Canvas", "Not installed", "Install", "iisacc", false, true},
                                     {"dreamscapes", "Dreamscapes", "Images", "Installed", "Open", "iisacc", true, true}});
            value.backend_0.onAction = [&](const std::string &id) { actions.push_back(id); };
        }, &scope);
    }, View::appearance()));
    scope->backend_0.resize(1440, 844);
    good(view->paint({1440, 844}));
    const auto sidebar = take(take(view->find("apps-sidebar")).snapshot());
    check(sidebar.bounds.width == 220, "Apps uses the Figma sidebar width");
    const auto detailIcon = take(take(view->find("apps-detail-icon")).snapshot());
    check(detailIcon.bounds.width == 48 && detailIcon.bounds.height == 48, "the selected-app icon uses the Figma slot");
    const auto rows = take(view->find("apps-rows"));
    const auto cards = take(rows.keyed_content_item("congregation", "apps-cards"));
    const auto firstCard = take(cards.keyed_content_item("congregation"));
    const auto cardIcon = take(take(firstCard.content_item("apps-card-icon")).snapshot());
    check(cardIcon.bounds.width == 64 && cardIcon.bounds.height == 64, "app cards use the Figma icon slot");
    check(take(view->find("apps-detail-action")).snapshot().has_value(), "the selected app has an action in the detail panel");
    good(take(view->find("apps-detail-action")).activate());
    check(actions == std::vector<std::string>{"congregation"}, "detail action uses the selected app");
    const auto secondCardContent = take(cards.keyed_content_item("dreamscapes"));
    const auto secondCard = take(take(secondCardContent.content_item("apps-card-hit")).snapshot());
    const double x = secondCard.bounds.x + 8, y = secondCard.bounds.y + 8;
    good(view->input({.kind=ii::gui::WidgetInputKind::pointer_down,.x=x,.y=y,.buttons=1}));
    good(view->input({.kind=ii::gui::WidgetInputKind::pointer_up,.x=x,.y=y}));
    check(scope->backend_0.selectedId.get() == "dreamscapes", "clicking the card background selects its app");
    good(take(view->find("apps-mine")).activate());
    check(scope->backend_0.visibleApps.get().size() == 1 && scope->backend_0.selectedId.get() == "dreamscapes", "the rendered My apps navigation filters by installation");
    good(view->paint({1440, 844}));
    good(take(view->find("apps-detail-action")).activate());
    check(actions.back() == "dreamscapes", "detail action follows the current selection");
    good(view->close());
}
