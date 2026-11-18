#include "NativeNavigation.generated.h"
#include <iiGUI>
#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <stop_token>
#include <string_view>
#include <utility>

namespace {
using namespace ii::gui;
using Action = NativeNavigationViewModel::Action;

void require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}

template<class T>
T take(ii::core::Result<T> result) {
    if (!result) throw std::runtime_error(result.error().message);
    return std::move(*result);
}

void check(ii::core::Result<void> result) {
    if (!result) throw std::runtime_error(result.error().message);
}

class NavigationIcons final : public lvui::IconSource {
public:
    lvui::IconResult<std::shared_ptr<const lvui::IconAsset>> resolve(std::string_view) const override {
        return icon;
    }
    std::uint64_t revision() const noexcept override { return 1; }

private:
    const std::shared_ptr<const lvui::IconAsset> icon = std::make_shared<const lvui::IconAsset>(
        lvui::IconAsset{R"svg(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 1 1"><rect width="1" height="1"/></svg>)svg"});
};

class NavigationPaintProbe final : public FramePaintDevice {
public:
    bool hasFocusRing = false;

    PaintCapabilities capabilities() const noexcept override { return {}; }

    ii::core::Result<void> paint_frame(const CanvasFrame &frame, const PaintFrameInfo &, std::stop_token) override {
        const auto hasStroke = [](const auto &panels) {
            return std::ranges::any_of(panels, [](const auto &panel) {
                return panel.stroke_width > 0 && panel.top.alpha > 0;
            });
        };
        hasFocusRing = hasStroke(frame.panels) || std::ranges::any_of(frame.overlays,
            [&](const auto &layer) { return hasStroke(layer.panels); });
        return {};
    }
};

struct Segment {
    const char *id;
    const char *page;
    Action action;
};

constexpr std::array segments{
    Segment{"environment", "Dashboard", Action::Environment},
    Segment{"tasks", "Tools", Action::Tasks},
    Segment{"library", "Storage", Action::Library},
    Segment{"space", "Space", Action::Space},
    Segment{"browse", "Browse", Action::Browse},
    Segment{"people", "People", Action::People},
    Segment{"hosting", "Hosting", Action::Hosting},
};
}

int main() try {
    using View = lvui_generated::NativeNavigation;
    auto probe = std::make_shared<NavigationPaintProbe>();
    std::shared_ptr<View::Scope> scope;
    std::optional<Action> requested;
    auto view = take(PaintView::create(probe, [&](WidgetBuilder &builder) {
        return View::build(builder, {}, [&](View::Scope &candidate) {
            candidate.backend_0.iconSource = std::make_shared<NavigationIcons>();
            candidate.backend_0.onNavigate = [&](Action action) { requested = action; };
        }, &scope);
    }, View::appearance()));
    const auto ringVisible = [&] {
        check(view->paint({240, 48}));
        return probe->hasFocusRing;
    };
    require(!ringVisible(), "Navigation drew a ring before receiving focus");

    const auto click = [&](const Segment &segment) {
        const auto button = take(view->find(segment.id));
        const auto bounds = take(button.snapshot()).bounds;
        const auto x = bounds.x + bounds.width / 2;
        const auto y = bounds.y + bounds.height / 2;
        requested.reset();
        check(view->input({.kind = WidgetInputKind::pointer_down, .x = x, .y = y, .buttons = 1}));
        require(!ringVisible(), "A segment drew a focus ring during a mouse press");
        check(view->input({.kind = WidgetInputKind::pointer_up, .x = x, .y = y}));
        require(requested == segment.action, "A segment click did not request its page");
        require(scope->backend_0.setCurrent(segment.page), "The navigation model rejected its page");
        const auto selected = take(button.snapshot());
        require(selected.checked && selected.focused, "A segment click lost selection or logical focus");
        require(!ringVisible(), "A mouse-selected segment drew a focus ring");
    };
    for (const auto &segment : segments) click(segment);

    check(view->input({.kind = WidgetInputKind::focus_lost}));
    check(view->input({.kind = WidgetInputKind::key_down, .key = WidgetKey::tab}));
    const auto first = take(view->find(segments.front().id));
    require(take(first.snapshot()).focused && ringVisible(), "Tab focus did not draw a segment focus ring");
    const auto last = take(view->find(segments.back().id));
    require(take(last.snapshot()).checked, "Tab changed the active page");
    click(segments.front());
    requested.reset();
    check(view->input({.kind = WidgetInputKind::key_down, .key = WidgetKey::space}));
    require(ringVisible(), "Keyboard interaction did not restore the focus ring after a mouse click");
    check(view->input({.kind = WidgetInputKind::key_up, .key = WidgetKey::space}));
    require(requested == segments.front().action, "Space did not activate the focused segment");
    check(view->input({.kind = WidgetInputKind::focus_lost}));
    require(!ringVisible(), "The segment focus ring remained after focus was lost");
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
}
