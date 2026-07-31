#pragma once

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
#include <vector>

namespace Uni::GUI::Nodes {

class UNI_GUI_EXPORT NodeUiContext final {
public:
    using CommandSink = std::function<void(std::unique_ptr<Command>)>;
    using PinIdAllocator = std::function<PinId()>;

    NodeUiContext(
        GraphId graph,
        const GraphDocument& document,
        const NodeInstance& node,
        float zoom,
        Vec2 available_size,
        CommandSink command_sink,
        PinIdAllocator pin_allocator,
        bool read_only = false);

    [[nodiscard]] GraphId Graph() const noexcept;
    [[nodiscard]] const NodeInstance& Node() const noexcept;
    [[nodiscard]] std::vector<PinId> Pins() const;
    [[nodiscard]] const PinInstance* FindPin(PinId pin) const noexcept;
    [[nodiscard]] float Zoom() const noexcept;
    [[nodiscard]] Vec2 AvailableSize() const noexcept;
    [[nodiscard]] bool ReadOnly() const noexcept;
    [[nodiscard]] float ToScreen(float logical_size) const noexcept;
    [[nodiscard]] Vec2 ToScreen(Vec2 logical_size) const noexcept;
    [[nodiscard]] const PropertyValue* FindProperty(std::string_view key) const;

    void Submit(std::unique_ptr<Command> command);
    void SetProperty(std::string key, std::optional<PropertyValue> value);
    // Call immediately after an ImGui editing item to coalesce its active gesture.
    void EditProperty(std::string key, PropertyValue value, bool changed);
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
    float m_zoom;
    Vec2 m_available_size;
    CommandSink m_command_sink;
    PinIdAllocator m_pin_allocator;
    bool m_read_only{false};
    std::vector<PinId> m_pin_order;
    std::unordered_map<PinId, PinInstance, IdHash> m_shadow_pins;
    std::unordered_set<PinId, IdHash> m_removed_pins;
};

// Callbacks must balance their ImGui stacks and queue mutations through NodeUiContext.
using DrawNodeBodyFn = std::function<void(NodeUiContext&)>;
using DrawNodeInspectorFn = std::function<void(NodeUiContext&)>;

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

struct NodeUiLayout final {
    std::vector<PinPlacement> pins;
    std::optional<GraphRect> body; // Node-local graph coordinates.
};

struct NodeUiLayoutContext final {
    const Graph& graph;
    const NodeInstance& node;
    Vec2 node_size;
    bool collapsed{false};
    float title_height{0.0f};
    float pin_spacing{0.0f};
};

using LayoutNodeUiFn = std::function<Result<NodeUiLayout>(const NodeUiLayoutContext&)>;

struct NodeUiDescriptor final {
    TypeId type;
    DrawNodeBodyFn draw_body;
    DrawNodeInspectorFn draw_inspector;
    Vec2 default_size; // Full node size in graph logical units.
    std::uint32_t header_color{0xFF353535U};
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

using PinStyleFn = std::function<PinStyle(const PinStyleContext&)>;

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
