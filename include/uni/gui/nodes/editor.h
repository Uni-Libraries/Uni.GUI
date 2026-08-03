#pragma once

#include <uni/gui/export.h>
#include <uni/gui/nodes/layout.h>
#include <uni/gui/nodes/graph_asset.h>
#include <uni/gui/nodes/node_ui.h>
#include <uni/gui/nodes/routing.h>

#include <cstdint>
#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace Uni::GUI::Nodes {

namespace Detail {
struct EditorAccess;
}

enum class LinkFlowDirection : std::uint8_t {
    OutputToInput,
    InputToOutput,
};

enum class EditorDebugOverlay : std::uint32_t {
    None = 0,
    NodeBounds = 1U << 0U,
    NodeBodyBounds = 1U << 1U,
    LinkBounds = 1U << 2U,
    PinNormals = 1U << 3U,
    LinkRoutes = 1U << 4U,
    EntityIds = 1U << 5U,
    WorldBounds = 1U << 6U,
    Metrics = 1U << 7U,
    All = (1U << 8U) - 1U,
};

[[nodiscard]] constexpr EditorDebugOverlay operator|(
    const EditorDebugOverlay first,
    const EditorDebugOverlay second) noexcept {
    return static_cast<EditorDebugOverlay>(
        static_cast<std::uint32_t>(first) | static_cast<std::uint32_t>(second));
}

[[nodiscard]] constexpr EditorDebugOverlay operator&(
    const EditorDebugOverlay first,
    const EditorDebugOverlay second) noexcept {
    return static_cast<EditorDebugOverlay>(
        static_cast<std::uint32_t>(first) & static_cast<std::uint32_t>(second));
}

constexpr EditorDebugOverlay& operator|=(
    EditorDebugOverlay& first,
    const EditorDebugOverlay second) noexcept {
    return first = first | second;
}

[[nodiscard]] constexpr bool HasEditorDebugOverlay(
    const EditorDebugOverlay overlays,
    const EditorDebugOverlay overlay) noexcept {
    return (static_cast<std::uint32_t>(overlays) & static_cast<std::uint32_t>(overlay)) != 0;
}

struct EditorStyle final {
    std::uint32_t background{0xFF181818U};
    std::uint32_t grid_minor{0x182F2F2FU};
    std::uint32_t grid_major{0x303F3F3FU};
    std::uint32_t node{0xFF303030U};
    std::uint32_t node_header{0xFF414141U};
    std::uint32_t node_border{0xFF606060U};
    std::uint32_t selection{0xFFFFA43AU};
    std::uint32_t link{0xFFB0B0B0U};
    std::uint32_t link_hovered{0xFFFFFFFFU};
    std::uint32_t pin{0xFFD0D0D0U};
    std::uint32_t compatible{0xFF55C878U};
    std::uint32_t convertible{0xFF49A8E8U};
    std::uint32_t rejected{0xFF4D4DDBU};
    std::uint32_t text{0xFFF0F0F0U};
    std::uint32_t group_border{0x80707070U};
    std::uint32_t comment{0x70404080U};
    std::uint32_t route_point{0xFFFFFFFFU};
    std::uint32_t minimap_background{0xD0202020U};
    std::uint32_t minimap_viewport{0xA0FFA43AU};
    std::uint32_t link_flow{0xFFFFA43AU};
    std::uint32_t link_flow_outline{0xFF181818U};
    std::uint32_t debug_node_bounds{0xFF55C878U};
    std::uint32_t debug_node_body_bounds{0xFF49A8E8U};
    std::uint32_t debug_link_bounds{0xFFFF4FD8U};
    std::uint32_t debug_pin_normals{0xFF49E8E8U};
    std::uint32_t debug_link_routes{0xFFFFA43AU};
    std::uint32_t debug_world_bounds{0xFF4D4DDBU};
    std::uint32_t debug_text{0xFFFFFFFFU};
    float node_rounding{6.0f};
    float node_border_width{1.0f};
    float link_width{3.0f};
    float pin_radius{5.0f};
    float resize_handle_size{10.0f};
    float route_point_radius{5.0f};
    float link_flow_marker_radius{4.0f};
    float link_flow_outline_width{1.5f};
    float debug_line_width{1.0f};
    float debug_pin_normal_length{24.0f};
};

struct NodeHeaderLayout final {
    std::uint8_t maximum_text_lines{1};
    float minimum_height{28.0f};
    float horizontal_padding{10.0f};
    float vertical_padding{6.0f};
    float line_spacing{2.0f};
    float primary_text_scale{1.0f};
    float secondary_text_scale{0.8f};
    float item_height{14.0f};
    float item_spacing{6.0f};
    float collapse_width{24.0f};
    float minimum_text_width{24.0f};

    bool operator==(const NodeHeaderLayout&) const = default;
};

struct EditorConfig final {
    float min_zoom{0.25f};
    float max_zoom{2.0f};
    float zoom_step{1.15f};
    float grid_size{32.0f};
    float node_width{190.0f};
    NodeHeaderLayout node_header;
    float pin_spacing{24.0f};
    float link_hit_radius{8.0f};
    float link_flatten_tolerance{0.5f};
    float snap_size{16.0f};
    Vec2 minimum_node_size{120.0f, 50.0f};
    Vec2 minimum_group_size{120.0f, 60.0f};
    Vec2 minimap_size{190.0f, 130.0f};
    float link_flow_duration{1.0f};
    float link_flow_speed{120.0f};
    float link_flow_marker_spacing{32.0f};
    TypeId default_link_router{"uni.gui.nodes.router.bezier"};
    std::size_t maximum_router_segments{4096};
    EditorDebugOverlay debug_overlays{EditorDebugOverlay::None};
    bool show_grid{true};
    bool show_minimap{true};
    bool snap_to_grid{true};
    bool enable_shortcuts{true};
    bool enable_node_popup{true};
    bool enable_node_collapse{true};
    bool show_breadcrumbs{true};
};

enum class ContextMenuTargetKind {
    Canvas,
    Node,
    Pin,
    Link,
    Group,
    RoutePoint,
};

struct ContextMenuTarget final {
    ContextMenuTargetKind kind{ContextMenuTargetKind::Canvas};
    Vec2 position;
    NodeId node;
    PinId pin;
    LinkId link;
    GroupId group;
    RoutePointId route_point;
    std::size_t route_point_insert_index{0};
};

class UNI_GUI_EXPORT EditorMenuContext final {
public:
    EditorMenuContext(const EditorMenuContext&) = delete;
    EditorMenuContext& operator=(const EditorMenuContext&) = delete;
    EditorMenuContext(EditorMenuContext&&) = delete;
    EditorMenuContext& operator=(EditorMenuContext&&) = delete;

    [[nodiscard]] GraphId Graph() const noexcept;
    [[nodiscard]] const GraphDocument& Document() const noexcept;
    [[nodiscard]] const GraphPresentation& Presentation() const noexcept;
    [[nodiscard]] const GraphSelection& Selection() const noexcept;
    [[nodiscard]] const ContextMenuTarget& Target() const noexcept;
    void Submit(std::unique_ptr<Command> command);

private:
    EditorMenuContext(
        GraphId graph,
        const GraphDocument& document,
        const GraphPresentation& presentation,
        GraphSelection selection,
        ContextMenuTarget target,
        std::function<void(std::unique_ptr<Command>)> sink);

    GraphId m_graph;
    const GraphDocument* m_document;
    const GraphPresentation* m_presentation;
    GraphSelection m_selection;
    ContextMenuTarget m_target;
    std::function<void(std::unique_ptr<Command>)> m_sink;

    friend struct Detail::EditorAccess;
};

using DrawEditorContextMenuFn = std::function<void(EditorMenuContext&)>;
using DuplicateSelectionFn = std::function<void(const GraphSelection&)>;

struct ResolvedGraphAsset final {
    GraphAssetId asset;
    GraphAssetGeneration generation{0};
    GraphAssetContentHash content_hash;
    GraphId root_graph;
};

using GraphDocumentResolver = std::function<Result<ResolvedGraphAsset>(
    const GraphAssetId& asset,
    const GraphInterface& expected_interface)>;

struct GraphAssetNavigation final {
    GraphAssetId asset;
    GraphInterface expected_interface;
    GraphAssetGeneration generation{0};
    GraphAssetContentHash content_hash;
    GraphId root_graph;
};

struct EditorCallbacks final {
    DrawEditorContextMenuFn draw_context_menu;
    DuplicateSelectionFn duplicate_selection;
    GraphDocumentResolver resolve_graph_asset;
};

struct NodeHeaderAction final {
    GraphId graph;
    NodeId node;
    std::string item;
    std::string action;
};

struct EditorResult final {
    bool model_changed{false};
    bool presentation_changed{false};
    bool selection_changed{false};
    bool animation_active{false};
    std::optional<NodeId> activated_node;
    std::optional<GraphAssetNavigation> open_graph_asset;
    GraphId active_graph;
    bool active_graph_changed{false};
    GraphSelection selection;
    std::vector<NodeHeaderAction> header_actions;
};

struct Breadcrumb final {
    GraphId graph;
    NodeId via_node;
    std::string label;
};

struct EditorMetrics final {
    std::uint64_t geometry_rebuilds{0};
    std::uint64_t routed_links{0};
    std::uint64_t spatial_queries{0};
    std::uint64_t spatial_candidates{0};
    std::uint64_t adaptive_segments{0};
    std::uint64_t visible_nodes{0};
    std::uint64_t visible_link_segments{0};
};

class UNI_GUI_EXPORT EditorContext final {
public:
    EditorContext();
    ~EditorContext();
    EditorContext(EditorContext&& other);
    EditorContext& operator=(EditorContext&& other);
    EditorContext(const EditorContext&) = delete;
    EditorContext& operator=(const EditorContext&) = delete;
    void Swap(EditorContext& other) noexcept;

    // Pan uses reference UI units; node and route geometry use graph units.
    void SetPan(Vec2 pan) noexcept;
    [[nodiscard]] Vec2 Pan() const noexcept;
    void SetZoom(float zoom) noexcept;
    [[nodiscard]] float Zoom() const noexcept;

    [[nodiscard]] GraphId ActiveGraph() const noexcept;
    [[nodiscard]] std::vector<Breadcrumb> Breadcrumbs() const;
    [[nodiscard]] Result<void> ResetNavigation(const GraphDocument& document, GraphId graph = {});
    [[nodiscard]] Result<void> EnterSubgraph(const GraphDocument& document, NodeId node);
    [[nodiscard]] bool NavigateBack() noexcept;
    [[nodiscard]] bool NavigateToBreadcrumb(std::size_t index) noexcept;

    void ClearSelection() noexcept;
    void SetSelection(GraphSelection selection);
    [[nodiscard]] GraphSelection Selection() const;

    void CopySelection() noexcept;
    void PasteAt(Vec2 position) noexcept;
    void DuplicateSelection() noexcept;
    void AlignSelection(NodeAlignment alignment) noexcept;
    void AutoLayoutSelection(LayoutOptions options = {}) noexcept;

    void FrameAll() noexcept;
    void FrameSelection() noexcept;
    [[nodiscard]] Result<void> TriggerLinkFlow(
        const GraphDocument& document,
        GraphId graph,
        LinkId link,
        LinkFlowDirection direction = LinkFlowDirection::OutputToInput);
    void ClearLinkFlow(const GraphDocument& document, GraphId graph, LinkId link) noexcept;
    void ClearLinkFlows() noexcept;
    void InvalidateGeometry() noexcept;
    void ResetMetrics() noexcept;
    [[nodiscard]] EditorMetrics Metrics() const noexcept;
    [[nodiscard]] std::string LastError() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;

    friend struct Detail::EditorAccess;
};

[[nodiscard]] UNI_GUI_EXPORT EditorResult DrawEditor(
    EditorContext& context,
    GraphDocument& document,
    GraphPresentation& presentation,
    CommandStack& commands,
    const RegistryCatalog& registry,
    const NodeUiRegistry& ui,
    const LinkRouterRegistry& routers,
    Vec2 size = {},
    const EditorStyle& style = {},
    const EditorConfig& config = {},
    const EditorCallbacks& callbacks = {},
    const GraphPolicy& policy = {});

} // namespace Uni::GUI::Nodes
