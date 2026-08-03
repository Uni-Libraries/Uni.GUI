#include "internal/frame.h"

#if defined(IMGUI_ENABLE_TEST_ENGINE)
#include <imgui_internal.h>
#endif

#include <algorithm>
#include <variant>

namespace Uni::GUI::Nodes::EditorDetail {

void EditorFrame::PrepareMinimap() {
    const auto& cache = session.geometry;
    world_min = cache.has_world_bounds
        ? ImVec2{cache.world_bounds.min.x, cache.world_bounds.min.y}
        : ImVec2{-100.0f, -100.0f};
    world_max = cache.has_world_bounds
        ? ImVec2{cache.world_bounds.max.x, cache.world_bounds.max.y}
        : ImVec2{100.0f, 100.0f};
    const auto include_transient_world = [&](const GraphRect bounds) {
        world_min = Min(world_min, {bounds.min.x, bounds.min.y});
        world_max = Max(world_max, {bounds.max.x, bounds.max.y});
    };
    const auto include_transient_node = [&](const NodeId node) {
        const auto geometry = std::ranges::find(node_geometry, node, &NodeGeometry::id);
        if (geometry != node_geometry.end()) {
            include_transient_world(Detail::Normalize({
                ToGraph(geometry->visible_min),
                ToGraph(geometry->visible_max),
            }));
        }
    };
    if (const auto* dragging = std::get_if<DraggingNodes>(&session.interaction)) {
        for (const auto& [node, before] : dragging->before) {
            (void)before;
            include_transient_node(node);
        }
    } else if (const auto* dragging = std::get_if<DraggingGroup>(&session.interaction)) {
        for (const auto& [node, before] : dragging->member_positions) {
            (void)before;
            include_transient_node(node);
        }
    } else if (const auto* resizing = std::get_if<ResizingNode>(&session.interaction)) {
        include_transient_node(resizing->node);
    }
    if (std::holds_alternative<DraggingGroup>(session.interaction) ||
        std::holds_alternative<ResizingGroup>(session.interaction)) {
        for (const auto& geometry : group_geometry) {
            include_transient_world(Detail::Normalize({ToGraph(geometry.min), ToGraph(geometry.max)}));
        }
    }
    if (const auto* dragging = std::get_if<DraggingRoutePoint>(&session.interaction)) {
        include_transient_world({dragging->current, dragging->current});
    }
    for (const auto& [link, path] : overridden_link_paths) {
        (void)link;
        include_transient_world(Detail::PathBounds(path));
    }

    const float MinimapPadding = ScaleUi(8.0f);
    minimap_max = canvas_max - ImVec2{MinimapPadding, MinimapPadding};
    const ImVec2 minimap_size{ScaleUi(config.minimap_size.x), ScaleUi(config.minimap_size.y)};
    minimap_min = minimap_max - minimap_size;
    minimap_visible = config.show_minimap && minimap_size.x + MinimapPadding < canvas_size.x &&
        minimap_size.y + MinimapPadding < canvas_size.y;
    minimap_hovered = minimap_visible && Contains(minimap_min, minimap_max, mouse);
#if defined(IMGUI_ENABLE_TEST_ENGINE)
    if (minimap_visible) {
        ImGuiContext& g = *GImGui;
        const ImGuiID id = ImGui::GetID("Minimap");
        const ImRect bounds{minimap_min, minimap_max};
        IMGUI_TEST_ENGINE_ITEM_ADD(id, bounds, nullptr);
        IMGUI_TEST_ENGINE_ITEM_INFO(id, "Minimap", ImGuiItemStatusFlags_None);
    }
#endif
    minimap_scale = std::min(
        (minimap_size.x - ScaleUi(12.0f)) / std::max(world_max.x - world_min.x, 1.0f),
        (minimap_size.y - ScaleUi(12.0f)) / std::max(world_max.y - world_min.y, 1.0f));

    if (std::holds_alternative<Idle>(session.interaction) && minimap_hovered &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        session.interaction = NavigatingMinimap{};
        NavigateMinimap();
    }
    if (std::holds_alternative<NavigatingMinimap>(session.interaction)) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            NavigateMinimap();
        } else {
            session.interaction = Idle{};
        }
    }
}

void EditorFrame::NavigateMinimap() {
    const ImVec2 local{
        std::clamp(mouse.x, minimap_min.x + ScaleUi(6.0f), minimap_max.x - ScaleUi(6.0f)),
        std::clamp(mouse.y, minimap_min.y + ScaleUi(6.0f), minimap_max.y - ScaleUi(6.0f)),
    };
    session.minimap_navigation = Vec2{
        world_min.x + (local.x - minimap_min.x - ScaleUi(6.0f)) / minimap_scale,
        world_min.y + (local.y - minimap_min.y - ScaleUi(6.0f)) / minimap_scale,
    };
}

ImVec2 EditorFrame::ToMinimap(const Vec2 point) const noexcept {
    return minimap_min + ImVec2{ScaleUi(6.0f), ScaleUi(6.0f)} +
        ImVec2{(point.x - world_min.x) * minimap_scale, (point.y - world_min.y) * minimap_scale};
}

void EditorFrame::RenderMinimap() {
    if (!minimap_visible) return;
    const auto& cache = session.geometry;
    draw_list->AddRectFilled(minimap_min, minimap_max, style.minimap_background, ScaleUi(4.0f));
    draw_list->PushClipRect(minimap_min, minimap_max, true);
    for (const auto& [group_id, geometry] : cache.groups) {
        const auto group = presentation.Groups().find(group_id);
        if (group == presentation.Groups().end()) continue;
        GraphRect bounds = geometry.bounds;
        if (const auto frame = std::ranges::find(group_geometry, group_id, &GroupGeometry::id);
            frame != group_geometry.end()) {
            bounds = Detail::Normalize({ToGraph(frame->min), ToGraph(frame->max)});
        }
        draw_list->AddRectFilled(
            ToMinimap(bounds.min),
            ToMinimap(bounds.max),
            group->second.style->kind == GroupKind::Comment
                ? style.comment
                : group->second.style->color,
            ScaleUi(1.0f));
    }
    for (const auto& [node_id, geometry] : cache.nodes) {
        const GraphRect bounds = CurrentNodeBounds(node_id).value_or(geometry.bounds);
        const auto* node = presentation.FindNode(node_id);
        draw_list->AddRectFilled(
            ToMinimap(bounds.min),
            ToMinimap(bounds.max),
            node != nullptr ? node->color.value_or(style.node) : style.node,
            ScaleUi(1.0f));
    }
    draw_list->AddRect(
        ToMinimap(ToGraph(canvas_origin)),
        ToMinimap(ToGraph(canvas_max)),
        style.minimap_viewport,
        ScaleUi(1.0f),
        0,
        ScaleUi(2.0f));
    draw_list->PopClipRect();
    draw_list->AddRect(
        minimap_min,
        minimap_max,
        minimap_hovered ? style.selection : style.group_border,
        ScaleUi(4.0f),
        0,
        ScaleUi(minimap_hovered ? 2.0f : 1.0f));
}

} // namespace Uni::GUI::Nodes::EditorDetail
