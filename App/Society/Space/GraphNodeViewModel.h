#pragma once

#include "GraphNode.h"
#include <iiSDKCore>
#include <functional>
#include <string>
#include <vector>

namespace society::space {

/// A render-ready projection of one typed input or output slot. The flags are
/// mutually exclusive and allow LVUI to use its native control for each format.
struct GraphValueRow final {
    std::string id, label, text, accessibilityName;
    std::shared_ptr<const ii::gui::RasterImage> image;
    bool isLabel=false, isTextInput=false, isButton=false, isImage=false, isFile=false, isOutput=false, actionEnabled=false;
    friend bool operator==(const GraphValueRow &, const GraphValueRow &) = default;
};

/// Per-mount presentation state consumed by the generated LVUI node view.
/// Typed values are observed from the graph-owned node state on its owner thread.
class GraphNodeViewModel final {
public:
    ii::core::Observable<std::string> title, description, outputFormat;
    ii::core::Observable<std::vector<GraphValueRow>> inputs, outputs;
    ii::core::Observable<std::vector<GraphPort>> top, bottom, left, right;
    ii::core::Observable<int> horizontalPortCount{0}, verticalPortCount{0};

    /// Initialize one LVUI activation before its widgets and bindings are built.
    void configure(const GraphNode &node, GraphNode::PortActivated callback, GraphNode::ValueAction valueAction);
    /// Produce a callback for an existing port key without creating a connection.
    [[nodiscard]] std::function<void()> activatePort(const std::string &key) const;
    /// Produce a text-edit handler that updates the identified typed slot.
    [[nodiscard]] std::function<void(std::string)> editValue(const std::string &id, bool output) const;
    /// Produce a button or file action handler without performing external I/O.
    [[nodiscard]] std::function<void()> activateValue(const std::string &id, bool output) const;

private:
    GraphNode::PortActivated callback_;
    GraphNode::ValueAction valueAction_;
    std::shared_ptr<GraphNode> node_;
    ii::core::ObservableSubscription inputSubscription_, outputSubscription_;
};

} // namespace society::space
