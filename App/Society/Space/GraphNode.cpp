#include "GraphNode.h"
#include "GraphNodeViewModel.h"
#include "NativeGraphNode.generated.h"
#include <iiGUI/CompiledViewSource.h>
#include <algorithm>
#include <array>
#include <stdexcept>
#include <type_traits>
#include <unordered_set>
#include <utility>

namespace society::space {
/// Shared owner-thread state allows graph updates to reach every mounted view.
struct GraphNode::State final {
    explicit State(std::vector<GraphNodeInput> initial) : inputs(std::move(initial)) {}
    ii::core::Observable<std::vector<GraphNodeInput>> inputs;
    ii::core::Observable<std::optional<GraphValue>> output;
};

GraphValueFormat graphValueFormat(const GraphValue &value) noexcept {
    switch (value.index()) {
    case 0: return GraphValueFormat::Label;
    case 1: return GraphValueFormat::TextInput;
    case 2: return GraphValueFormat::Button;
    case 3: return GraphValueFormat::Image;
    case 4: return GraphValueFormat::File;
    default: return GraphValueFormat::Unknown;
    }
}

namespace {
/// Supply display text without conflating a resource's ID with its name.
std::string valueText(const GraphValue &value) {
    return std::visit([](const auto &item) -> std::string {
        using T = std::decay_t<decltype(item)>;
        if constexpr (std::is_same_v<T, GraphLabelValue> || std::is_same_v<T, GraphTextInputValue>) return item.text;
        else if constexpr (std::is_same_v<T, GraphButtonValue>) return item.label;
        else if constexpr (std::is_same_v<T, GraphImageValue>) return item.altText;
        else return item.displayName;
    }, value);
}

/// Produce the human-readable name for a typed output format.
const char *formatName(GraphValueFormat format) noexcept {
    switch (format) {
    case GraphValueFormat::Label: return "Label";
    case GraphValueFormat::TextInput: return "Text input";
    case GraphValueFormat::Button: return "Button";
    case GraphValueFormat::Image: return "Image";
    case GraphValueFormat::File: return "File";
    case GraphValueFormat::Unknown: return "";
    }
    return "";
}

/// Project a typed value into mutually exclusive native LVUI presentation flags.
GraphValueRow projectValue(std::string id, std::string label, const GraphValue &value, bool output, bool actionEnabled) {
    GraphValueRow row;
    row.id=std::move(id); row.label=std::move(label); row.text=valueText(value);
    row.accessibilityName=row.label.empty() ? (output ? "Output" : "Input") : row.label;
    row.isOutput=output; row.actionEnabled=actionEnabled;
    switch (graphValueFormat(value)) {
    case GraphValueFormat::Label: row.isLabel=true; break;
    case GraphValueFormat::TextInput: row.isTextInput=true; break;
    case GraphValueFormat::Button: row.isButton=true; break;
    case GraphValueFormat::Image: {
        row.isImage=true;
        const auto &image=std::get<GraphImageValue>(value);
        row.image=image.preview;
        if (!image.altText.empty()) row.accessibilityName=image.altText;
        break;
    }
    case GraphValueFormat::File: row.isFile=true; if(row.text.empty()) row.text="Choose file"; break;
    case GraphValueFormat::Unknown: break;
    }
    return row;
}

/// Project all inputs while retaining their stable input identifiers.
std::vector<GraphValueRow> projectInputs(const std::vector<GraphNodeInput> &inputs, bool actionEnabled) {
    std::vector<GraphValueRow> rows;
    rows.reserve(inputs.size());
    for (const auto &input : inputs)
        if (input.content) rows.push_back(projectValue(input.id, input.label, *input.content, false, actionEnabled));
    return rows;
}

/// Project an optional output as a zero-or-one keyed collection.
std::vector<GraphValueRow> projectOutput(const std::optional<GraphValue> &output, bool actionEnabled) {
    if (!output) return {};
    return {projectValue("output", "Output", *output, true, actionEnabled)};
}

/// Provide the accessible role label for the edge's fixed semantic role.
const char *roleName(GraphPortRole role) noexcept {
    switch (role) {
    case GraphPortRole::Input: return "Input";
    case GraphPortRole::Output: return "Output";
    case GraphPortRole::Inheritance: return "Inheritance";
    case GraphPortRole::Reference: return "Reference";
    case GraphPortRole::Unknown: return "Unknown";
    }
    return "Unknown";
}

/// Construct unambiguous, edge-scoped row keys and complete graph addresses.
std::vector<GraphPort> makePorts(const std::string &nodeId, GraphPortSide side,
                                 std::size_t count, const char *label) {
    std::vector<GraphPort> result;
    result.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        const auto suffix = std::to_string(i);
        GraphPortEndpoint endpoint{nodeId, side, i};
        result.push_back({std::string(label) + ":" + suffix,
                          std::string(label) + " " + roleName(endpoint.role()) + " port " + std::to_string(i + 1),
                          std::move(endpoint)});
    }
    return result;
}
}

GraphNode::GraphNode(std::vector<GraphNodeInput> inputs, std::string outputFormat,
                     std::string nodeId, std::string name, std::string description,
                     GraphPortCounts counts)
    : outputFormat_(std::move(outputFormat)), id_(std::move(nodeId)),
      name_(std::move(name)), description_(std::move(description)) {
    if (id_.empty()) throw std::invalid_argument("Graph node ID must not be empty");
    constexpr std::size_t maximumPortsPerEdge = 64;
    for (const auto count : {counts.top, counts.bottom, counts.left, counts.right})
        if (count > maximumPortsPerEdge) throw std::invalid_argument("Too many graph ports on one edge");
    std::unordered_set<std::string> ids;
    for (auto &input : inputs) {
        if (input.id.empty() || input.id.size() > 1024 || input.id.find('\0') != std::string::npos ||
            !ids.insert(input.id).second)
            throw std::invalid_argument("Graph node input IDs must be nonempty, unique, NUL-free and at most 1024 bytes");
        if (!input.content) input.content=GraphLabelValue{input.value};
        if (input.content->valueless_by_exception()) throw std::invalid_argument("Graph input value is invalid");
        input.value=valueText(*input.content);
    }
    state_=std::make_shared<State>(std::move(inputs));
    top_ = makePorts(id_, GraphPortSide::Top, counts.top, "Top");
    bottom_ = makePorts(id_, GraphPortSide::Bottom, counts.bottom, "Bottom");
    left_ = makePorts(id_, GraphPortSide::Left, counts.left, "Left");
    right_ = makePorts(id_, GraphPortSide::Right, counts.right, "Right");
}

GraphNode::GraphNode(std::vector<GraphNodeInput> inputs, GraphValue output, std::string nodeId,
                     std::string name, std::string description, GraphPortCounts counts)
    : GraphNode(std::move(inputs), std::string(formatName(graphValueFormat(output))), std::move(nodeId),
                std::move(name), std::move(description), counts) {
    if (output.valueless_by_exception()) throw std::invalid_argument("Graph output value is invalid");
    state_->output.set(std::move(output));
}

const std::string &GraphNode::id() const noexcept { return id_; }
const std::vector<GraphNodeInput> &GraphNode::inputs() const { return state_->inputs.get(); }
const std::string &GraphNode::outputFormat() const noexcept { return outputFormat_; }
const std::string &GraphNode::name() const noexcept { return name_; }
const std::string &GraphNode::description() const noexcept { return description_; }
std::optional<GraphValue> GraphNode::outputValue() const { return state_->output.get(); }

std::expected<void, std::string> GraphNode::setInputValue(std::string_view id, GraphValue value) const {
    if (value.valueless_by_exception()) return std::unexpected("Invalid input value");
    auto inputs=state_->inputs.get();
    const auto it=std::find_if(inputs.begin(), inputs.end(), [&](const auto &item) { return item.id==id; });
    if (it==inputs.end()) return std::unexpected("Unknown input ID");
    if (!it->content || it->content->index()!=value.index()) return std::unexpected("Input format mismatch");
    it->value=valueText(value);
    it->content=std::move(value);
    state_->inputs.set(std::move(inputs));
    return {};
}

std::expected<void, std::string> GraphNode::setOutputValue(GraphValue value) const {
    if (value.valueless_by_exception()) return std::unexpected("Invalid output value");
    const auto &old=state_->output.get();
    if (!old) return std::unexpected("Node has no typed output");
    if (old->index()!=value.index()) return std::unexpected("Output format mismatch");
    state_->output.set(std::optional<GraphValue>(std::move(value)));
    return {};
}

ii::core::ObservableSubscription GraphNode::observeOutput(std::function<void(const GraphValue &)> callback) const {
    if (!callback || !state_->output.get()) throw std::invalid_argument("A typed output and callback are required");
    return state_->output.observe([callback=std::move(callback)](const auto &value, std::uint64_t) {
        if (value) callback(*value);
    });
}

const std::vector<GraphPort> &GraphNode::ports(GraphPortSide side) const noexcept {
    switch (side) {
    case GraphPortSide::Top: return top_;
    case GraphPortSide::Bottom: return bottom_;
    case GraphPortSide::Left: return left_;
    case GraphPortSide::Right: return right_;
    }
    return top_;
}

std::shared_ptr<ii::gui::NativeViewSource> GraphNode::viewSource(PortActivated callback, ValueAction valueAction) const {
    return ii::gui::compiled_view_source([node = *this, callback = std::move(callback), valueAction=std::move(valueAction)](ii::gui::WidgetBuilder &builder) {
        return lvui_generated::NativeGraphNode::build(builder, {}, [&](auto &scope) {
            scope.backend_0.configure(node, callback, valueAction);
        });
    });
}

void GraphNodeViewModel::configure(const GraphNode &node, GraphNode::PortActivated callback, GraphNode::ValueAction valueAction) {
    title.set(node.name());
    description.set(node.description());
    outputFormat.set(node.outputFormat());
    node_=std::make_shared<GraphNode>(node);
    valueAction_=std::move(valueAction);
    inputs.set(projectInputs(node.inputs(),bool(valueAction_)));
    outputs.set(projectOutput(node.outputValue(),bool(valueAction_)));
    inputSubscription_=node.state_->inputs.observe([this](const auto &value, std::uint64_t) { inputs.set(projectInputs(value,bool(valueAction_))); });
    outputSubscription_=node.state_->output.observe([this](const auto &value, std::uint64_t) { outputs.set(projectOutput(value,bool(valueAction_))); });
    top.set(node.ports(GraphPortSide::Top));
    bottom.set(node.ports(GraphPortSide::Bottom));
    left.set(node.ports(GraphPortSide::Left));
    right.set(node.ports(GraphPortSide::Right));
    horizontalPortCount.set(static_cast<int>(std::max(top.get().size(), bottom.get().size())));
    verticalPortCount.set(static_cast<int>(std::max(left.get().size(), right.get().size())));
    callback_ = std::move(callback);
}

std::function<void()> GraphNodeViewModel::activatePort(const std::string &key) const {
    for (const auto *side : {&top, &bottom, &left, &right}) {
        const auto &ports = side->get();
        const auto it = std::find_if(ports.begin(), ports.end(), [&](const auto &port) { return port.key == key; });
        if (it != ports.end()) {
            const auto endpoint = it->endpoint;
            const auto callback = callback_;
            return [endpoint, callback] { if (callback) callback(endpoint); };
        }
    }
    return [] {};
}

std::function<void(std::string)> GraphNodeViewModel::editValue(const std::string &id, bool output) const {
    const auto node=node_; const auto action=valueAction_;
    return [node,action,id,output](std::string text) {
        if (!node) return;
        GraphValue value=GraphTextInputValue{std::move(text)};
        auto result=output ? node->setOutputValue(value) : node->setInputValue(id,value);
        if (result && action) action({node->id(),id,output?GraphValueDirection::Output:GraphValueDirection::Input,
                                      GraphValueActionKind::TextChanged,std::move(value)});
    };
}

std::function<void()> GraphNodeViewModel::activateValue(const std::string &id, bool output) const {
    const auto node=node_; const auto action=valueAction_;
    return [node,action,id,output] {
        if (!node || !action) return;
        std::optional<GraphValue> value;
        if (output) value=node->outputValue();
        else {
            const auto &items=node->inputs();
            const auto it=std::find_if(items.begin(),items.end(),[&](const auto &item){return item.id==id;});
            if (it!=items.end()) value=it->content;
        }
        if (!value) return;
        GraphValueActionKind kind;
        if (std::holds_alternative<GraphButtonValue>(*value)) kind=GraphValueActionKind::ActivateButton;
        else if (const auto *file=std::get_if<GraphFileValue>(&*value))
            kind=file->resourceId.empty()?GraphValueActionKind::ChooseFile:GraphValueActionKind::OpenFile;
        else return;
        action({node->id(),id,output?GraphValueDirection::Output:GraphValueDirection::Input,kind,std::move(*value)});
    };
}
} // namespace society::space
