#include "Society/Space/GraphNode.h"
#include <iiGUI>
#include <algorithm>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace society::space;

void require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}

template <class T> T take(ii::core::Result<T> result) {
    if (!result) throw std::runtime_error(result.error().message);
    return std::move(*result);
}

void good(ii::core::Result<void> result) {
    if (!result) throw std::runtime_error(result.error().message);
}

class PaintProbe final : public ii::gui::PaintDevice {
public:
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
}

int main() try {
    using namespace society::space;
    const GraphNode node({{"prompt", "Prompt", "mist"}, {"seed", "Seed", "42"}},
                         "image/png", "node-a", "Generator", "Creates an image",
                         {.top=2, .bottom=1, .left=3, .right=1});
    require(node.id() == "node-a" && node.inputs().size() == 2 && node.outputFormat() == "image/png",
            "constructor must preserve logical identity, inputs and output format");
    require(node.ports(GraphPortSide::Top).size() == 2 && node.ports(GraphPortSide::Bottom).size() == 1 &&
            node.ports(GraphPortSide::Left).size() == 3 && node.ports(GraphPortSide::Right).size() == 1,
            "each edge must have exactly the requested number of ports");
    require(node.ports(GraphPortSide::Left)[1].endpoint == GraphPortEndpoint{"node-a", GraphPortSide::Left, 1},
            "port addresses retain the node ID, edge and index");
    require(node.ports(GraphPortSide::Left)[0].endpoint.role() == GraphPortRole::Input &&
            node.ports(GraphPortSide::Right)[0].endpoint.role() == GraphPortRole::Output &&
            node.ports(GraphPortSide::Top)[0].endpoint.role() == GraphPortRole::Inheritance &&
            node.ports(GraphPortSide::Bottom)[0].endpoint.role() == GraphPortRole::Reference,
            "each edge exposes its specified port role");
    const GraphNode zero({}, "", "node-b", "Empty", "", {});
    require(zero.ports(GraphPortSide::Top).empty() && zero.ports(GraphPortSide::Left).empty(),
            "zero ports and empty optional content are supported");
    const GraphNode maximum({}, "", "node-max", "Max", "", {.top=64});
    require(maximum.ports(GraphPortSide::Top).size() == 64 &&
            maximum.ports(GraphPortSide::Top).back().endpoint.index == 63,
            "the documented maximum preserves all port identities");
    bool rejected = false;
    try { GraphNode invalid({{"dup", "A", ""}, {"dup", "B", ""}}, "", "n", "Name", "", {}); }
    catch (const std::invalid_argument &) { rejected = true; }
    require(rejected, "duplicate input identities must be rejected");
    rejected = false;
    try { GraphNode invalid({{std::string("a\0b", 3), "A", ""}}, "", "n", "Name", "", {}); }
    catch (const std::invalid_argument &) { rejected = true; }
    require(rejected, "input IDs rejected by the keyed collection cannot reach the view");
    rejected = false;
    try { GraphNode invalid({}, "", "", "Name", "", {}); }
    catch (const std::invalid_argument &) { rejected = true; }
    require(rejected, "a node needs a stable identity");
    rejected = false;
    try { GraphNode invalid({}, "", "n", "Name", "", {.top=65}); }
    catch (const std::invalid_argument &) { rejected = true; }
    require(rejected, "oversized port sets must fail before mounting");

    std::vector<GraphPortEndpoint> activations;
    auto source = node.viewSource([&](const GraphPortEndpoint &endpoint) { activations.push_back(endpoint); });
    auto device = std::make_shared<PaintProbe>();
    auto view = take(ii::gui::PaintView::create(device, [&](ii::gui::WidgetBuilder &builder) {
        return source->build(builder);
    }));
    good(view->paint({640, 500}));
    require(take(take(view->find("graph-node-title")).snapshot()).text == "Generator", "name reaches LVUI");
    require(take(take(view->find("graph-node-description")).snapshot()).text == "Creates an image", "description reaches LVUI");
    require(take(take(view->find("graph-node-output-format")).snapshot()).text == "image/png", "format reaches LVUI");
    const auto inputs = take(view->find("graph-node-inputs"));
    require(take(take(inputs.keyed_content_item("prompt", "graph-input-label")).snapshot()).text == "Prompt" &&
            take(take(inputs.keyed_content_item("prompt", "graph-input-value")).snapshot()).text == "mist" &&
            take(take(inputs.keyed_content_item("seed", "graph-input-value")).snapshot()).text == "42",
            "each input label and value reaches the correct keyed row");
    const auto body = take(take(view->find("graph-node-body")).snapshot()).bounds;
    const auto checkSide = [&](GraphPortSide side, GraphPortRole role, const char *roleName, const char *id) {
        auto list = take(view->find(id));
        for (const auto &port : node.ports(side)) {
            auto button = take(list.keyed_content_item(port.key, "graph-port-button"));
            const auto snapshot = take(button.snapshot());
            require(snapshot.accessibility_name.find(roleName) != std::string::npos,
                    "each port's accessible name identifies its semantic role");
            const auto &b = snapshot.bounds;
            switch (side) {
            case GraphPortSide::Top: require(b.y + b.height <= body.y + 0.01, "top ports must appear above the body"); break;
            case GraphPortSide::Bottom: require(b.y >= body.y + body.height - 0.01, "bottom ports must appear below the body"); break;
            case GraphPortSide::Left: require(b.x + b.width <= body.x + 0.01, "left ports must appear left of the body"); break;
            case GraphPortSide::Right: require(b.x >= body.x + body.width - 0.01, "right ports must appear right of the body"); break;
            }
            good(button.activate());
            require(!activations.empty() && activations.back() == port.endpoint && activations.back().role() == role,
                    "activation preserves the complete port address and semantic role");
        }
    };
    checkSide(GraphPortSide::Top, GraphPortRole::Inheritance, "Inheritance", "graph-ports-top");
    checkSide(GraphPortSide::Bottom, GraphPortRole::Reference, "Reference", "graph-ports-bottom");
    checkSide(GraphPortSide::Left, GraphPortRole::Input, "Input", "graph-ports-left");
    checkSide(GraphPortSide::Right, GraphPortRole::Output, "Output", "graph-ports-right");
    require(activations.size() == 7, "each control activates exactly once");
    const auto topHost = take(view->find("graph-ports-top"));
    auto keyboardPort = take(topHost.keyed_content_item(node.ports(GraphPortSide::Top)[0].key, "graph-port-button"));
    good(keyboardPort.focus());
    good(view->input({.kind=ii::gui::WidgetInputKind::key_down, .key=ii::gui::WidgetKey::space}));
    good(view->input({.kind=ii::gui::WidgetInputKind::key_up, .key=ii::gui::WidgetKey::space}));
    require(activations.size() == 8 && activations.back() == node.ports(GraphPortSide::Top)[0].endpoint,
            "keyboard activation reports the same endpoint");
    good(view->close());

    auto zeroSource = zero.viewSource();
    auto zeroView = take(ii::gui::PaintView::create(device, [&](ii::gui::WidgetBuilder &builder) {
        return zeroSource->build(builder);
    }));
    good(zeroView->paint({640, 500}));
    const auto emptyTop = take(zeroView->find("graph-ports-top"));
    require(!emptyTop.keyed_content_item("Top:0", "graph-port-button"),
            "a zero-count edge does not create a phantom control");
    good(zeroView->close());

    const GraphNode many({}, "text/plain", "node-many", "Many ports", "",
                         {.top=20, .bottom=20, .left=20, .right=20});
    const auto manySource = many.viewSource();
    auto manyView = take(ii::gui::PaintView::create(device, [&](ii::gui::WidgetBuilder &builder) {
        return manySource->build(builder);
    }));
    good(manyView->paint({1500, 1500}));
    const auto manyBody = take(take(manyView->find("graph-node-body")).snapshot()).bounds;
    for (const auto &[side, hostId] : std::vector<std::pair<GraphPortSide, const char *>>{
             {GraphPortSide::Top, "graph-ports-top"}, {GraphPortSide::Bottom, "graph-ports-bottom"},
             {GraphPortSide::Left, "graph-ports-left"}, {GraphPortSide::Right, "graph-ports-right"}}) {
        const auto host = take(manyView->find(hostId));
        for (const auto &port : many.ports(side)) {
            const auto b = take(take(host.keyed_content_item(port.key, "graph-port-button")).snapshot()).bounds;
            if (side == GraphPortSide::Top || side == GraphPortSide::Bottom)
                require(b.x >= manyBody.x - 0.01 && b.x + b.width <= manyBody.x + manyBody.width + 0.01,
                        "horizontal ports must remain alongside the node body");
            else
                require(b.y >= manyBody.y - 0.01 && b.y + b.height <= manyBody.y + manyBody.height + 0.01,
                        "vertical ports must remain alongside the node body");
        }
    }
    good(manyView->close());
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
}
