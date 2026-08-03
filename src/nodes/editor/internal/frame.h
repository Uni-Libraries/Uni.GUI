#pragma once

#include "../internal.h"

#include <uni/gui/nodes/io.h>

#include <span>
#include <string_view>

namespace Uni::GUI::Nodes::EditorDetail {

constexpr float MaxEditorCoordinate = 1.0e9f;
constexpr float MaxCanvasDimension = 1.0e6f;

struct PinGeometry final {
    PinId id;
    ImVec2 position;
    Vec2 outward_normal;
    PinLabelPlacement label;
};

struct NodeGeometry final {
    NodeId id;
    ImVec2 min;
    ImVec2 max;
    ImVec2 visible_min;
    ImVec2 visible_max;
    ImVec2 title_max;
    ImVec2 body_min;
    ImVec2 body_max;
    ImVec2 collapse_min;
    ImVec2 collapse_max;
    ImVec2 resize_min;
    ImVec2 resize_max;
    std::vector<PinGeometry> pins;
};

struct GroupGeometry final {
    GroupId id;
    ImVec2 min;
    ImVec2 max;
    ImVec2 title_max;
    ImVec2 collapse_min;
    ImVec2 collapse_max;
    ImVec2 resize_min;
    ImVec2 resize_max;
};

struct RoutePointGeometry final {
    LinkId link;
    RoutePointId point;
    ImVec2 position;
};

struct HeaderItemGeometry final {
    NodeId node;
    std::size_t item_index{0};
    ImVec2 min;
    ImVec2 max;
};

[[nodiscard]] bool Contains(ImVec2 min, ImVec2 max, ImVec2 point) noexcept;
[[nodiscard]] bool Overlaps(ImVec2 first_min, ImVec2 first_max, ImVec2 second_min, ImVec2 second_max) noexcept;
[[nodiscard]] ImVec2 Min(ImVec2 first, ImVec2 second) noexcept;
[[nodiscard]] ImVec2 Max(ImVec2 first, ImVec2 second) noexcept;
[[nodiscard]] float DistanceSquared(ImVec2 first, ImVec2 second) noexcept;
[[nodiscard]] std::string Lower(std::string_view value);
[[nodiscard]] bool ValidConfig(const EditorConfig& config) noexcept;
[[nodiscard]] bool ValidStyle(const EditorStyle& style) noexcept;
[[nodiscard]] bool Finite(Vec2 value) noexcept;
[[nodiscard]] bool Bounded(Vec2 value) noexcept;
[[nodiscard]] Vec2 ClampPan(Vec2 value) noexcept;
[[nodiscard]] float Snap(float value, float spacing) noexcept;
[[nodiscard]] Vec2 Snap(Vec2 value, float spacing) noexcept;
void StoreText(std::span<char> destination, std::string_view value);

template<typename Set, typename Id>
bool SelectOnly(Set& selection, const Id id, const bool append) {
    if (!append) {
        const bool unchanged = selection.size() == 1 && selection.contains(id);
        selection.clear();
        selection.insert(id);
        return !unchanged;
    }
    return selection.insert(id).second;
}

class EditorFrame final {
public:
    EditorFrame(
        EditorContext& context,
        GraphDocument& document,
        GraphPresentation& presentation,
        CommandStack& commands,
        const RegistryCatalog& registry,
        const NodeUiRegistry& ui,
        const LinkRouterRegistry& routers,
        Vec2 requested_size,
        const EditorStyle& style,
        const EditorConfig& config,
        const EditorCallbacks& callbacks,
        const GraphPolicy& policy);
    ~EditorFrame();

    EditorFrame(const EditorFrame&) = delete;
    EditorFrame& operator=(const EditorFrame&) = delete;

    [[nodiscard]] EditorResult Draw();

private:
    [[nodiscard]] bool Initialize();
    [[nodiscard]] bool BeginCanvas();
    void CloseCanvas() noexcept;
    void RefreshGraph() noexcept;
    void RecordChange(const CommandResult& change) noexcept;
    [[nodiscard]] bool ClearSelection() noexcept;
    [[nodiscard]] bool PruneSelection();
    void NormalizeNavigation();
    void ApplyNavigation();
    void FlushPendingCommands();
    void UpdateLinkFlows();

    void UpdateInteraction();
    [[nodiscard]] bool EnsureGeometryCache();
    [[nodiscard]] bool BuildTransientGeometry();
    [[nodiscard]] bool PrepareNodeHeaders();
    [[nodiscard]] bool ApplyViewportNavigation();
    [[nodiscard]] Result<LinkPath> InvokeRouter(
        const LinkRouterDescriptor& descriptor,
        const LinkRoutingContext& routing_context,
        std::size_t route_point_count);
    [[nodiscard]] std::optional<NodeUiLayout> ResolveNodeLayout(
        const NodeInstance& node,
        Vec2 size,
        bool collapsed,
        Vec2 origin);
    [[nodiscard]] Vec2 NodeOffset(NodeId node) const;
    [[nodiscard]] std::optional<CachedPinGeometry> CurrentPin(PinId pin) const;
    [[nodiscard]] std::optional<GraphRect> CurrentNodeBounds(NodeId node) const;
    [[nodiscard]] const LinkPath* LinkPathFor(LinkId link) const;

    void RegisterTestItems();
    [[nodiscard]] NodeHeaderPresentation ResolveNodeHeader(const NodeInstance& node);
    [[nodiscard]] PinStyle ResolvePinStyle(const PinInstance& pin, bool hovered);
    [[nodiscard]] bool HitTest();
    [[nodiscard]] bool PointerOverUiBody() const;

    [[nodiscard]] bool RenderScene();
    void RenderLinkFlows();
    void RenderCanvasControls();
    [[nodiscard]] bool RenderOverlays();
    void RenderDebugOverlays();
    void EndRendering() noexcept;

    void PrepareMinimap();
    void NavigateMinimap();
    void RenderMinimap();
    [[nodiscard]] ImVec2 ToMinimap(Vec2 point) const noexcept;

    void ProcessGestures();
    void ProcessClipboardRequests();
    void ProcessShortcuts();
    [[nodiscard]] bool CopySelection();
    void PasteFragment(const GraphFragment& fragment, Vec2 position);
    void PasteClipboard(Vec2 position);
    void DuplicateSelection();
    void ApplyLayout(Result<NodeLayout> layout);

    [[nodiscard]] bool DrawMenus();
    [[nodiscard]] bool DrawContextMenu();
    [[nodiscard]] bool DrawCreatePalette(bool open_create_popup);
    void OpenContextMenu();

    [[nodiscard]] ImVec2 ToScreen(Vec2 position) const noexcept;
    [[nodiscard]] Vec2 ToGraph(ImVec2 position) const noexcept;
    [[nodiscard]] float UiScale() const noexcept;
    [[nodiscard]] float GraphScale() const noexcept;
    [[nodiscard]] float ScaleUi(float value) const noexcept;
    [[nodiscard]] float ScaleGraph(float value) const noexcept;
    [[nodiscard]] Vec2 TextSizeInGraph(std::string_view text) const;
    [[nodiscard]] Vec2 MinimumNodeSize() const noexcept;

    EditorContext& context;
    GraphDocument& document;
    GraphPresentation& presentation;
    CommandStack& commands;
    const RegistryCatalog& registry;
    const NodeUiRegistry& ui;
    const LinkRouterRegistry& routers;
    Vec2 requested_size;
    const EditorStyle& style;
    const EditorConfig& config;
    const EditorCallbacks& callbacks;
    const GraphPolicy& policy;
    EditorState& session;

    EditorResult result;
    GraphId graph_id;
    const Graph* graph{nullptr};
    std::vector<std::unique_ptr<Command>> pending_commands;
    std::optional<NodeId> enter_subgraph;
    std::optional<std::size_t> navigate_breadcrumb;

    bool child_open{false};
    ImVec2 canvas_origin;
    ImVec2 canvas_size;
    ImVec2 canvas_max;
    ImVec2 mouse;
    bool canvas_hovered{false};
    float ui_scale{1.0f};
    float header_height{28.0f};

    NodeId resized_layout_node;
    std::optional<NodeUiLayout> resized_layout;
    std::vector<NodeGeometry> node_geometry;
    std::vector<GroupGeometry> group_geometry;
    std::vector<RoutePointGeometry> route_point_geometry;
    std::unordered_map<NodeId, NodeHeaderPresentation, IdHash> node_headers;
    std::vector<HeaderItemGeometry> header_item_geometry;
    std::unordered_map<NodeId, std::pair<std::size_t, std::size_t>, IdHash> header_item_ranges;
    std::unordered_map<PinId, ImVec2, IdHash> pin_positions;
    std::unordered_set<LinkId, IdHash> visible_links;
    std::unordered_map<LinkId, LinkPath, IdHash> overridden_link_paths;
    std::unordered_set<LinkId, IdHash> suppressed_links;

    bool router_callback_invalidated{false};
    bool dynamic_callback_invalidated{false};
    bool ui_callback_invalidated{false};
    std::unordered_map<PinId, PinStyle, IdHash> base_pin_styles;
    std::unordered_map<PinId, PinStyle, IdHash> rendered_pin_styles;
    std::unordered_set<NodeId, IdHash> hovered_bodies;

    PinId hovered_pin;
    NodeId hovered_node;
    std::optional<std::size_t> hovered_header_item;
    bool hovered_node_collapse{false};
    bool hovered_node_resize{false};
    RoutePointId hovered_route_point;
    LinkId hovered_route_link;
    LinkId hovered_link;
    std::size_t hovered_link_segment{0};
    GroupId hovered_group;
    bool hovered_group_collapse{false};
    bool hovered_group_resize{false};
    GroupId membership_drop_group;
    bool membership_drop_active{false};

    ConnectionResult link_preview;
    bool has_link_preview{false};
    bool append_selection{false};
    bool open_create_popup{false};
    bool context_state_invalidated{false};
    bool owns_draw_lease{false};

    ImDrawList* draw_list{nullptr};
    ImDrawListSplitter splitter;
    bool splitter_active{false};
    int hovered_channel{0};
    int overlay_channel{0};

    ImVec2 world_min;
    ImVec2 world_max;
    ImVec2 minimap_min;
    ImVec2 minimap_max;
    float minimap_scale{1.0f};
    bool minimap_visible{false};
    bool minimap_hovered{false};
};

} // namespace Uni::GUI::Nodes::EditorDetail
