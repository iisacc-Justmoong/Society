# Society graph node component

The Society-owned graph node is an embeddable LVUI/iiGUI view. Its constructor
accepts an ordered list of inputs, an output format, a stable node ID, a display
name and description, and separate counts for ports on the top, bottom, left and
right edges. The ID is an application-supplied identity, not a path, hash, or
generated display name. Input identifiers must be unique within the node; input
labels and values are display data and do not automatically map to a port. Each input
ID is nonempty, NUL-free and at most 1,024 bytes. Each edge accepts zero to 64
ports; invalid counts and input identifiers are rejected before mounting.

Each port has a stable address consisting of its node ID, edge and zero-based
index. Its semantic role is determined by the edge: left is Input, right is
Output, top is Inheritance, and bottom is Reference. The endpoint exposes the
role directly; accessible names include both the edge and role. Zero ports on an
edge is valid. Port activation reports that address to a caller-supplied handler;
the surrounding graph owns validation, connections, stream execution and
persistence. Merely activating a port does not create an edge or claim that
anything is connected. The component does not automatically map an input value
to a particular input port, or decide which connected node is the parent or
referent in an inheritance or reference relationship.

The view shows the supplied name, description, inputs and output format, and
renders the specified number of port controls on each edge using LVUI tokens.
Port controls have accessible names and can be activated with the keyboard.
The view is an embeddable component, not a complete Space screen; it does not
create sample graph data or modify the container's persisted idea graph.
The node body expands to accommodate the larger port count on opposing edges.
The current controls expose port activation; stream transport, compatibility
checking, connection state and the Figma variants for data versus stream are
not implemented by this component.

## Input and output value formats

Both input slots and the output use the same five distinct value alternatives:
`GraphLabelValue`, `GraphTextInputValue`, `GraphButtonValue`, `GraphImageValue`
and `GraphFileValue`. A label and an editable text value remain distinct even
when their contents match. A button holds its label; activation is an event,
not a boolean value or a file operation. An image holds an optional immutable
iiGUI raster preview, alternative text and optional resource/revision IDs. A
file holds its display name and optional resource/revision IDs. An identifier
or a preview is not a guarantee that the original resource is available.

Each input and output retains its initially declared alternative. A graph owner
may update an input or publish an output using a matching typed value; unknown
input IDs, absent typed outputs and incompatible alternatives are rejected
without replacing the previous value. Observable output changes can be
subscribed to and explicitly forwarded to a matching input by the owning graph.
Unchanged values do not generate another change; repeated button activations are
independent events. The component does not construct graph connections, transfer
files or automatically choose a resource. Its value state and observers use
iiSDKCore's owner-thread contract; callers must dispatch changes to that thread.

The LVUI view renders label text, editable text fields, actionable buttons,
image previews and file controls for both directions. Edited text updates the
matching typed slot and reports an action to the host. Button activations and
file choose/open requests are also sent to the host. An absent image preview
stays empty until a raster is supplied; a file control does not claim to have
opened or selected a file until the host performs that action. Buttons and file
controls are disabled when no action handler is attached.

## C++ integration

Include `Society/Space/GraphNode.h` and link `SocietyGraphNode`. A graph owner
can construct and embed a view source as follows:

```cpp
using namespace society::space;
GraphNode node({{"prompt", "Prompt", "", GraphValue{GraphTextInputValue{"A misty city"}}}},
               GraphValue{GraphImageValue{"", "", "Generated image", {}}},
               "node-01", "Image", "Create an image",
               {.top=0, .bottom=0, .left=1, .right=1});
auto source = node.viewSource([](const GraphPortEndpoint &port) {
    // Forward port.nodeId, port.side, port.index and port.role() to the graph.
});
auto outputSubscription = node.observeOutput([](const GraphValue &value) {
    // Route this typed value to a compatible input if the graph has a connection.
});
```

`source` implements iiGUI's `NativeViewSource` and can be embedded in an LVUI
`NativeView`. Each mount has its own LVUI view model observing shared node value
state. Calling `setInputValue` or `setOutputValue` on the owner thread updates
existing mounts. Copies of a node share that state. The callback's captured
objects must outlive any mount that can invoke them. No file or daemon operation
is performed by the component.
The earlier string-only output-format constructor remains available for existing
callers. It describes a legacy output but does not declare a typed output value;
typed publishing and subscriptions require the `GraphValue` constructor.

The [Society Figma component examples](https://www.figma.com/design/vzGhdYpJ2GeyNfwXADJeAu/Society?node-id=1178-612)
show the intended node and edge layout. The app component is linked into the
Society build for use by a future graph host; the existing Space navigation is
not replaced with a sample screen.
