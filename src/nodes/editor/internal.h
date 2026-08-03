#pragma once

#include <uni/gui/nodes/editor.h>

#include "geometry.h"

#include <imgui.h>

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

namespace Uni::GUI::Nodes::EditorDetail {

struct Idle final {};
struct Panning final {};

struct DraggingNodes final {
    ImVec2 start;
    MoveNodesCommand::Positions before;
    Vec2 delta;
};

struct MarqueeSelecting final {
    ImVec2 start;
    ImVec2 current;
    bool append{false};
};

struct CreatingLink final {
    PinId origin;
    ImVec2 start;
    ImVec2 current;
    LinkId reconnect;
    PinId detached;
    bool dragging{false};
};

struct DraggingRoutePoint final {
    LinkId link;
    RoutePointId point;
    ImVec2 start;
    Vec2 before;
    Vec2 current;
};

struct DraggingGroup final {
    GroupId group;
    ImVec2 start;
    Vec2 before;
    MoveNodesCommand::Positions member_positions;
    Vec2 delta;
};

struct ResizingNode final {
    NodeId node;
    ImVec2 start;
    Vec2 before;
    Vec2 current;
};

struct ResizingGroup final {
    GroupId group;
    ImVec2 start;
    Vec2 before;
    Vec2 current;
};

struct NavigatingMinimap final {};

using Interaction = std::variant<
    Idle,
    Panning,
    DraggingNodes,
    MarqueeSelecting,
    CreatingLink,
    DraggingRoutePoint,
    DraggingGroup,
    ResizingNode,
    ResizingGroup,
    NavigatingMinimap>;

struct CachedPinGeometry final {
    PinId id;
    Vec2 position;
    Vec2 outward_normal;
    PinLabelPlacement label;
};

struct CachedNodeGeometry final {
    NodeId id;
    GraphRect bounds;
    GraphRect spatial_bounds;
    GraphRect title;
    GraphRect body;
    GraphRect collapse;
    GraphRect resize;
    std::vector<CachedPinGeometry> pins;
};

struct CachedGroupGeometry final {
    GroupId id;
    GraphRect bounds;
    GraphRect title;
    GraphRect collapse;
    GraphRect resize;
};

struct CachedLinkGeometry final {
    LinkId id;
    LinkPath path;
    GraphRect bounds;
    NodeId output_node;
    NodeId input_node;
};

struct CachedRoutePointGeometry final {
    LinkId link;
    RoutePointId point;
    Vec2 position;
};

struct GeometryCache final {
    bool valid{false};
    GraphId graph;
    std::uint64_t document_identity{0};
    std::uint64_t presentation_identity{0};
    std::uint64_t ui_identity{0};
    std::uint64_t router_identity{0};
    std::uint64_t topology_revision{0};
    std::uint64_t layout_revision{0};
    std::uint64_t presentation_geometry_revision{0};
    std::uint64_t ui_layout_revision{0};
    std::uint64_t router_revision{0};
    std::uint64_t manual_revision{0};
    const ImFont* font{nullptr};
    float reference_font_size{0.0f};
    float node_width{0.0f};
    float header_height{0.0f};
    NodeHeaderLayout header_layout;
    float pin_spacing{0.0f};
    float resize_handle_size{0.0f};
    Vec2 minimum_node_size;
    TypeId default_router;
    std::size_t maximum_router_segments{0};
    bool enable_node_collapse{true};
    std::vector<NodeId> ordered_nodes;
    NodePresentationMap resolved_nodes;
    std::unordered_set<NodeId, IdHash> hidden_nodes;
    std::unordered_map<NodeId, CachedNodeGeometry, IdHash> nodes;
    std::vector<GroupId> ordered_groups;
    std::unordered_map<GroupId, CachedGroupGeometry, IdHash> groups;
    std::unordered_map<PinId, CachedPinGeometry, IdHash> pins;
    std::unordered_map<LinkId, CachedLinkGeometry, IdHash> links;
    std::unordered_map<RoutePointId, CachedRoutePointGeometry, IdHash> route_points;
    std::vector<RoutingObstacle> obstacles;
    std::unordered_map<NodeId, std::size_t, IdHash> obstacle_indices;
    std::unordered_set<PinId, IdHash> connected_pins;
    Detail::SpatialIndex spatial;
    GraphRect world_bounds;
    bool has_world_bounds{false};
};

struct LinkFlowAnimation final {
    std::uint64_t document_identity{0};
    GraphId graph;
    LinkId link;
    LinkFlowDirection direction{LinkFlowDirection::OutputToInput};
    float elapsed{0.0f};
};

struct EditorState {
    struct ViewState final {
        Vec2 pan;
        float zoom{1.0f};
        GraphSelection selection;
    };

    Vec2 pan;
    float zoom{1.0f};
    std::unordered_set<NodeId, IdHash> selected_nodes;
    std::unordered_set<LinkId, IdHash> selected_links;
    std::unordered_set<GroupId, IdHash> selected_groups;
    std::unordered_map<RoutePointId, LinkId, IdHash> selected_route_points;
    GraphId selection_graph;
    Interaction interaction{Idle{}};
    std::optional<GraphId> active_graph;
    std::vector<Breadcrumb> navigation;
    std::unordered_map<GraphId, ViewState, IdHash> views;
    bool frame_all{false};
    bool frame_selection{false};
    Vec2 popup_position;
    PinId popup_origin;
    LinkId popup_reconnect;
    std::array<char, 128> popup_search{};
    std::array<char, 128> group_title{};
    std::array<char, 512> group_body{};
    std::optional<ContextMenuTarget> context_target;
    GraphId context_graph;
    GraphId node_popup_graph;
    bool copy_requested{false};
    bool duplicate_requested{false};
    std::optional<Vec2> paste_requested;
    std::optional<NodeAlignment> alignment_requested;
    std::optional<LayoutOptions> layout_requested;
    std::optional<Vec2> minimap_navigation;
    std::string last_error;
    GeometryCache geometry;
    std::vector<LinkFlowAnimation> link_flows;
    EditorMetrics metrics;
    std::uint64_t manual_geometry_revision{0};
    std::uint64_t external_revision{0};
    bool draw_active{false};
};

[[nodiscard]] bool InvokeContextMenuCallback(
    DrawEditorContextMenuFn callback,
    GraphId graph,
    GraphDocument& document,
    GraphPresentation& presentation,
    GraphSelection selection,
    ContextMenuTarget target,
    std::vector<std::unique_ptr<Command>>& pending_commands,
    std::string& error);

} // namespace Uni::GUI::Nodes::EditorDetail

namespace Uni::GUI::Nodes::Detail {

struct EditorAccess final {
    [[nodiscard]] static EditorDetail::EditorState& Session(EditorContext& context) noexcept;
    [[nodiscard]] static const EditorDetail::EditorState& Session(const EditorContext& context) noexcept;

    [[nodiscard]] static EditorMenuContext MakeMenuContext(
        GraphId graph,
        const GraphDocument& document,
        const GraphPresentation& presentation,
        GraphSelection selection,
        ContextMenuTarget target,
        std::function<void(std::unique_ptr<Command>)> sink);
};

} // namespace Uni::GUI::Nodes::Detail
