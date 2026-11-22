#pragma once

#include <iiGUI/NativeView.h>
#include <iiSDKCore>
#include <cstddef>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace society::space {

/// The five distinct presentation and value categories supported on either side.
enum class GraphValueFormat { Label, TextInput, Button, Image, File, Unknown };

/// Static textual content; it is distinct from an editable text input.
struct GraphLabelValue final { std::string text; friend bool operator==(const GraphLabelValue &, const GraphLabelValue &) = default; };
/// The current contents of an editable text field.
struct GraphTextInputValue final { std::string text; friend bool operator==(const GraphTextInputValue &, const GraphTextInputValue &) = default; };
/// A button's display label. Activations are emitted as separate events.
struct GraphButtonValue final { std::string label; friend bool operator==(const GraphButtonValue &, const GraphButtonValue &) = default; };
/// A resource reference and optional already-decoded preview; neither implies original availability.
struct GraphImageValue final {
    std::string resourceId, revisionId, altText;
    std::shared_ptr<const ii::gui::RasterImage> preview;
    friend bool operator==(const GraphImageValue &, const GraphImageValue &) = default;
};
/// A logical file reference and display name; this component performs no file I/O.
struct GraphFileValue final {
    std::string resourceId, revisionId, displayName;
    friend bool operator==(const GraphFileValue &, const GraphFileValue &) = default;
};
/// A value preserves its alternative and its complete payload when forwarded.
using GraphValue = std::variant<GraphLabelValue, GraphTextInputValue, GraphButtonValue, GraphImageValue, GraphFileValue>;
/// Return the stable format corresponding to a value's alternative.
[[nodiscard]] GraphValueFormat graphValueFormat(const GraphValue &value) noexcept;

/// One caller-owned input value. The ID identifies the input within the node;
/// it is independent of its display label, value and any connection port. The
/// legacy string is used as a label when the optional typed content is omitted.
struct GraphNodeInput final {
    std::string id;
    std::string label;
    std::string value;
    std::optional<GraphValue> content;
    friend bool operator==(const GraphNodeInput &, const GraphNodeInput &) = default;
};

/// The geometric edge hosting a port; each edge has one fixed semantic role.
enum class GraphPortSide { Top, Bottom, Left, Right };

/// The semantic role implied by a port's edge. Unknown represents an invalid
/// enum value and is never produced for a port created by GraphNode.
enum class GraphPortRole { Input, Output, Inheritance, Reference, Unknown };

/// A stable address understood by the owning graph, not a display name or path.
struct GraphPortEndpoint final {
    std::string nodeId;
    GraphPortSide side;
    std::size_t index;
    /// Derive the port role from its edge without storing duplicate state.
    [[nodiscard]] constexpr GraphPortRole role() const noexcept {
        switch (side) {
        case GraphPortSide::Left: return GraphPortRole::Input;
        case GraphPortSide::Right: return GraphPortRole::Output;
        case GraphPortSide::Top: return GraphPortRole::Inheritance;
        case GraphPortSide::Bottom: return GraphPortRole::Reference;
        }
        return GraphPortRole::Unknown;
    }
    friend bool operator==(const GraphPortEndpoint &, const GraphPortEndpoint &) = default;
};

/// Counts of ports on each edge. Empty edges are represented by zero.
struct GraphPortCounts final {
    std::size_t top = 0;
    std::size_t bottom = 0;
    std::size_t left = 0;
    std::size_t right = 0;
};

/// Presentation and identity for one port in an edge's keyed LVUI collection.
struct GraphPort final {
    std::string key;
    std::string accessibilityName;
    GraphPortEndpoint endpoint;
    friend bool operator==(const GraphPort &, const GraphPort &) = default;
};

/// The side of the node whose value emitted an interaction.
enum class GraphValueDirection { Input, Output };
/// User actions requiring graph-owner handling; these never perform file I/O.
enum class GraphValueActionKind { TextChanged, ActivateButton, ChooseFile, OpenFile };
/// An actionable value event identifies the node, slot, direction and typed value.
struct GraphValueAction final {
    std::string nodeId, slotId;
    GraphValueDirection direction;
    GraphValueActionKind kind;
    GraphValue value;
};

/// Society-owned graph node configuration and observable, embeddable value state.
/// The surrounding graph owns connections, type compatibility,
/// stream execution and persistence. The component only reports port activation.
class GraphNode final {
public:
    using PortActivated = std::function<void(const GraphPortEndpoint &)>;
    using ValueAction = std::function<void(const GraphValueAction &)>;

    /// Capture the node's data and edge counts. Empty node IDs, empty, duplicate,
    /// NUL-containing or overlong input IDs, or more than 64 ports on an edge
    /// are rejected with std::invalid_argument.
    GraphNode(std::vector<GraphNodeInput> inputs, std::string outputFormat,
              std::string nodeId, std::string name, std::string description,
              GraphPortCounts portCounts);
    /// Create a node with one of the five typed output formats and an initial value.
    GraphNode(std::vector<GraphNodeInput> inputs, GraphValue output,
              std::string nodeId, std::string name, std::string description,
              GraphPortCounts portCounts);

    /// Return the stable logical ID supplied by the caller.
    [[nodiscard]] const std::string &id() const noexcept;
    /// Return the caller's input values in their original order.
    [[nodiscard]] const std::vector<GraphNodeInput> &inputs() const;
    /// Return the declared output format without interpreting it.
    [[nodiscard]] const std::string &outputFormat() const noexcept;
    /// Return the user-facing node name.
    [[nodiscard]] const std::string &name() const noexcept;
    /// Return the user-facing description, which may be empty.
    [[nodiscard]] const std::string &description() const noexcept;
    /// Return the current typed output, or no value for the legacy format-only constructor.
    [[nodiscard]] std::optional<GraphValue> outputValue() const;
    /// Update an existing input only when the value has its originally declared format.
    [[nodiscard]] std::expected<void, std::string> setInputValue(std::string_view id, GraphValue value) const;
    /// Publish a value only when a typed output exists and its format matches.
    [[nodiscard]] std::expected<void, std::string> setOutputValue(GraphValue value) const;
    /// Observe future changed outputs on the state owner thread; a typed output is required.
    [[nodiscard]] ii::core::ObservableSubscription observeOutput(std::function<void(const GraphValue &)> callback) const;
    /// Return stable, indexed ports for the requested edge.
    [[nodiscard]] const std::vector<GraphPort> &ports(GraphPortSide side) const noexcept;
    /// Create a reusable LVUI view source. Every mount receives its own view
    /// model; the optional callback receives the complete activated endpoint.
    [[nodiscard]] std::shared_ptr<ii::gui::NativeViewSource> viewSource(PortActivated onPortActivated = {}, ValueAction onValueAction = {}) const;

private:
    friend class GraphNodeViewModel;
    struct State;
    std::shared_ptr<State> state_;
    std::string outputFormat_, id_, name_, description_;
    std::vector<GraphPort> top_, bottom_, left_, right_;
};

} // namespace society::space
