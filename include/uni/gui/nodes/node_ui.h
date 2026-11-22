#pragma once

/// @file
/// @brief Extension contracts for node layout, widgets, headers and pin rendering.

#include <uni/gui/export.h>
#include <uni/gui/nodes/commands.h>
#include <uni/gui/nodes/routing.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

struct ImDrawList;

namespace Uni::GUI::Nodes {

/// Callback-local view borrowing the document and node. Submit edits through the command sink;
/// read-only contexts discard submissions and do not allocate dynamic pin IDs.
class UNI_GUI_EXPORT NodeUiContext final {
public:
    using CommandSink = std::function<void(std::unique_ptr<Command>)>;
    using PinIdAllocator = std::function<PinId()>;

    NodeUiContext(
        GraphId graph,
        const GraphDocument& document,
        const NodeInstance& node,
        float ui_scale,
        float zoom,
        Vec2 available_size,
        CommandSink command_sink,
        PinIdAllocator pin_allocator,
        bool read_only = false);

    [[nodiscard]] GraphId Graph() const noexcept;
    [[nodiscard]] const NodeInstance& Node() const noexcept;
    /// Ordered copy including dynamic-pin edits queued earlier through this context.
    [[nodiscard]] std::vector<PinId> Pins() const;
    /// Looks up only pins owned by Node(), including pending additions/updates. Removed pins return nullptr.
    [[nodiscard]] const PinInstance* FindPin(PinId pin) const noexcept;
    [[nodiscard]] float UiScale() const noexcept;
    [[nodiscard]] float Zoom() const noexcept;
    [[nodiscard]] float ScreenScale() const noexcept;
    /// Available widget area in graph units; ToScreen() multiplies by UI scale and zoom once.
    [[nodiscard]] Vec2 AvailableSize() const noexcept;
    [[nodiscard]] bool ReadOnly() const noexcept;
    [[nodiscard]] float ToScreen(float logical_size) const noexcept;
    [[nodiscard]] Vec2 ToScreen(Vec2 logical_size) const noexcept;
    [[nodiscard]] const PropertyValue* FindProperty(const PropertyPath& path) const;

    void Submit(std::unique_ptr<Command> command);
    void SetProperty(PropertyPath path, std::optional<PropertyValue> value);
    /// Call immediately after an ImGui editing item to coalesce its active gesture.
    void EditProperty(PropertyPath path, PropertyValue value, bool changed);
    /// Reserve a new ID and queue insertion (append by default); returns a null ID on rejection.
    [[nodiscard]] PinId AddDynamicPin(
        PinDescriptor descriptor,
        std::size_t index = std::numeric_limits<std::size_t>::max());
    void RemoveDynamicPin(PinId pin);
    void UpdateDynamicPin(PinInstance pin);
    void ReorderDynamicPins(std::vector<PinId> order);

private:
    GraphId m_graph;
    const GraphDocument* m_document;
    const NodeInstance* m_node;
    float m_ui_scale;
    float m_zoom;
    Vec2 m_available_size;
    CommandSink m_command_sink;
    PinIdAllocator m_pin_allocator;
    bool m_read_only{false};
    std::vector<PinId> m_pin_order;
    std::unordered_map<PinId, PinInstance, IdHash> m_shadow_pins;
    std::unordered_set<PinId, IdHash> m_removed_pins;
};

/// Draw widgets synchronously; balance ImGui stacks and queue mutations through NodeUiContext.
using DrawNodeBodyFn = std::function<void(NodeUiContext&)>;
/// Same command and ImGui-stack contract as DrawNodeBodyFn, but without canvas zoom.
using DrawNodeInspectorFn = std::function<void(NodeUiContext&)>;

struct NodeHeaderGlyph final {
    std::string id;
};

struct NodeHeaderBadge final {
    std::string text;
};

using NodeHeaderItemContent = std::variant<NodeHeaderGlyph, NodeHeaderBadge>;

struct NodeHeaderItem final {
    std::string id;
    NodeHeaderItemContent content;
    std::optional<std::uint32_t> color;
    bool active{false};
    bool enabled{true};
    std::string action;
    std::string tooltip;
};

struct NodeHeaderContext final {
    GraphId graph;
    const Graph& graph_data;
    const NodeInstance& node;
    bool collapsed{false};
    bool selected{false};
    float ui_scale{1.0f};
    float zoom{1.0f};
};

struct NodeHeaderPresentation final {
    std::vector<std::string> lines;
    std::vector<NodeHeaderItem> items;
    std::optional<std::uint32_t> color;
};

/// Pure per-visible-node callback returning owning data for this frame.
using ResolveNodeHeaderFn = std::function<NodeHeaderPresentation(const NodeHeaderContext&)>;

struct NodeHeaderGlyphDrawContext final {
    ImDrawList& draw_list;
    Vec2 min;
    Vec2 max;
    std::uint32_t color{0xFFFFFFFFU};
    bool active{false};
    bool enabled{true};
};

/// Append only draw primitives to the supplied draw list, inside the supplied screen rectangle.
using DrawNodeHeaderGlyphFn = std::function<void(const NodeHeaderGlyphDrawContext&)>;

struct NodeHeaderGlyphDescriptor final {
    std::string id;
    float aspect_ratio{1.0f};
    DrawNodeHeaderGlyphFn draw;
};

/// Offset from the pin in graph units and fractional alignment within the measured label (both axes 0..1 for pivot).
struct PinLabelPlacement final {
    Vec2 offset;
    Vec2 pivot{0.5f, 0.5f};
    bool visible{true};
};

struct PinPlacement final {
    PinId pin;
    Vec2 position; // Node-local graph coordinates.
    Vec2 outward_normal;
    PinLabelPlacement label;
};

/// Supply each owned pin once, with bounded node-local coordinates and a nonzero normal.
/// An absent body uses the default widget area; a supplied body must have ordered corners.
struct NodeUiLayout final {
    std::vector<PinPlacement> pins;
    std::optional<GraphRect> body; // Node-local graph coordinates.
};

struct NodeUiLayoutContext final {
    const Graph& graph;
    const NodeInstance& node;
    Vec2 node_size;
    bool collapsed{false};
    float header_height{0.0f};
    float pin_spacing{0.0f};
};

/// Pure, cacheable callback; do not mutate editor state or retain borrowed graph/node references.
using LayoutNodeUiFn = std::function<Result<NodeUiLayout>(const NodeUiLayoutContext&)>;

struct NodeUiDescriptor final {
    TypeId type;
    DrawNodeBodyFn draw_body;
    DrawNodeInspectorFn draw_inspector;
    Vec2 default_size; // Full node size in graph logical units.
    std::optional<std::uint32_t> header_color;
    ResolveNodeHeaderFn resolve_header;
    LayoutNodeUiFn layout;
};

enum class PinShape {
    Circle,
    Square,
    Diamond,
};

struct PinStyle final {
    std::optional<std::uint32_t> color;
    std::optional<float> radius; // Radius in graph logical units.
    std::optional<PinShape> shape;
};

struct PinStyleContext final {
    const NodeInstance& node;
    const PinInstance& pin;
    float zoom{1.0f};
    bool hovered{false};
    bool connected{false};
};

/// Pure styling callback evaluated as needed for the current frame, not part of persistent geometry.
using PinStyleFn = std::function<PinStyle(const PinStyleContext&)>;

/// UI-thread registry. Lookups borrow descriptors/callbacks until removal or replacement;
/// registration rejects duplicate keys. LayoutRevision() excludes glyph and pin-style changes.
class UNI_GUI_EXPORT NodeUiRegistry final {
public:
    NodeUiRegistry();
    ~NodeUiRegistry();
    NodeUiRegistry(NodeUiRegistry&& other);
    NodeUiRegistry& operator=(NodeUiRegistry&& other);
    NodeUiRegistry(const NodeUiRegistry&) = delete;
    NodeUiRegistry& operator=(const NodeUiRegistry&) = delete;

    [[nodiscard]] Result<void> Register(NodeUiDescriptor descriptor);
    [[nodiscard]] bool Unregister(const TypeId& type);
    [[nodiscard]] const NodeUiDescriptor* Find(const TypeId& type) const noexcept;
    [[nodiscard]] std::uint64_t Identity() const noexcept;
    [[nodiscard]] std::uint64_t Revision() const noexcept;
    [[nodiscard]] std::uint64_t LayoutRevision() const noexcept;

    [[nodiscard]] Result<void> RegisterHeaderGlyph(NodeHeaderGlyphDescriptor descriptor);
    [[nodiscard]] bool UnregisterHeaderGlyph(std::string_view id);
    [[nodiscard]] const NodeHeaderGlyphDescriptor* FindHeaderGlyph(std::string_view id) const noexcept;

    [[nodiscard]] Result<void> RegisterPinStyle(TypeId type, PinStyleFn style);
    [[nodiscard]] bool UnregisterPinStyle(const TypeId& type);
    [[nodiscard]] const PinStyleFn* FindPinStyle(const TypeId& type) const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

struct NodeUiResult final {
    bool model_changed{false};
    bool presentation_changed{false};
    std::string error;
};

/// Invoke the node inspector in an active ImGui window and apply its queued commands as one history
/// operation. A missing descriptor/inspector is a no-op; node, callback and command errors are reported.
[[nodiscard]] UNI_GUI_EXPORT NodeUiResult DrawNodeInspector(
    const NodeUiRegistry& ui,
    GraphDocument& document,
    GraphPresentation& presentation,
    CommandStack& commands,
    const RegistryCatalog& registry,
    GraphId graph,
    NodeId node,
    const GraphPolicy& policy = {});

} // namespace Uni::GUI::Nodes
