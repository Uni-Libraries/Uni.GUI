#include "nodes/commands/transaction_internal.h"

namespace Uni::GUI::Nodes {

using CommandDetail::Finite;
using CommandDetail::MakeError;
using namespace TransactionDetail;

#define Record(...) StoreOperationLazy([&]() -> OperationIntent { return OperationIntent __VA_ARGS__; })

Result<void> GraphTransaction::SetNodePresentation(const NodeId node, std::optional<NodePresentation> value) {
    if (value && (!ContainsNode(Document(), node) || !ValidNodePresentation(*value))) {
        return std::unexpected(MakeError(ErrorCode::InvalidArgument, "Node presentation is invalid"));
    }
    const auto* current = Presentation().FindNode(node);
    if ((!current && !value) || (current && value && *current == *value)) {
        return {};
    }
    GraphId graph = FindNodeGraph(Document(), node);
    if (!graph) graph = FindNodeGraph(m_impl->baseline_document, node);
    const auto* owner = graph ? Document().FindGraph(graph) : nullptr;
    if (m_impl->enforce_protection && owner != nullptr && m_impl->GraphReadOnly(graph)) {
        return std::unexpected(MakeError(ErrorCode::ReadOnly, "Graph is read-only"));
    }
    if (m_impl->enforce_protection && m_impl->NodeLocked(node) && (value || owner != nullptr)) {
        return std::unexpected(MakeError(ErrorCode::Locked, "Node presentation is locked"));
    }
    if (m_impl->enforce_protection && current != nullptr) {
        if (value && current->locked != value->locked) {
            return std::unexpected(MakeError(ErrorCode::Locked, "Node lock must be changed explicitly"));
        }
    }
    const OperationAction action = value ? OperationAction::Set : OperationAction::Erase;
    std::shared_ptr<const NodePresentation> intent_value;
    if (m_impl->record_operations) {
        intent_value = std::make_shared<const NodePresentation>(value ? *value : *current);
    }
    if (auto budget = m_impl->ConsumeOperations(); !budget) return budget;
    m_impl->staged_presentation.SetNode(node, std::move(value));
    m_impl->presentation_changed = true;
    m_impl->journal.node_presentations.insert(node);
    m_impl->Record(
        {OperationKind::SetNodePresentation, action, NodePresentationOperation{graph, node, std::move(intent_value)}});
    return {};
}

Result<void> GraphTransaction::SetLinkPresentation(const LinkId link, std::optional<LinkPresentation> value) {
    if (value && (!ContainsLink(Document(), link) || !ValidLinkPresentation(*value))) {
        return std::unexpected(MakeError(ErrorCode::InvalidArgument, "Link presentation is invalid"));
    }
    const auto* current = Presentation().FindLink(link);
    if ((!current && !value) || (current && value && *current == *value)) {
        return {};
    }
    GraphId graph = FindLinkGraph(Document(), link);
    if (!graph) graph = FindLinkGraph(m_impl->baseline_document, link);
    const auto* owner = graph ? Document().FindGraph(graph) : nullptr;
    if (m_impl->enforce_protection && owner != nullptr && m_impl->GraphReadOnly(graph)) {
        return std::unexpected(MakeError(ErrorCode::ReadOnly, "Graph is read-only"));
    }
    if (m_impl->enforce_protection && m_impl->LinkLocked(link) && (value || owner != nullptr)) {
        return std::unexpected(MakeError(ErrorCode::Locked, "Link presentation is locked"));
    }
    if (m_impl->enforce_protection && current != nullptr) {
        if (value && current->Style().locked != value->Style().locked) {
            return std::unexpected(MakeError(ErrorCode::Locked, "Link lock must be changed explicitly"));
        }
    }
    LinkPresentationImpact impact = LinkPresentationImpact::None;
    bool full_route_validation = false;
    if (current == nullptr || !value) {
        impact = LinkPresentationImpact::Lifecycle | LinkPresentationImpact::Style |
            LinkPresentationImpact::Route | LinkPresentationImpact::Geometry |
            LinkPresentationImpact::Protection;
        full_route_validation = true;
    } else {
        if (current->Style().router != value->Style().router) {
            impact |= LinkPresentationImpact::Style | LinkPresentationImpact::Geometry;
        }
        if (current->Style().color != value->Style().color) impact |= LinkPresentationImpact::Style;
        if (current->Style().locked != value->Style().locked) {
            impact |= LinkPresentationImpact::Style | LinkPresentationImpact::Protection;
        }
        if (current->Route() != value->Route()) {
            impact |= LinkPresentationImpact::Route | LinkPresentationImpact::Geometry;
            full_route_validation = true;
        }
    }
    const OperationAction action = value ? OperationAction::Set : OperationAction::Erase;
    std::shared_ptr<const LinkStyle> intent_style;
    PersistentRoutePointSequence intent_route;
    if (m_impl->record_operations) {
        const LinkPresentation& intent = value ? *value : *current;
        intent_style = std::make_shared<const LinkStyle>(intent.Style());
        intent_route = intent.Route();
    }
    if (auto budget = m_impl->ConsumeOperations(); !budget) return budget;
    auto changed_route = m_impl->staged_presentation.SetLink(link, std::move(value));
    if (!changed_route) {
        m_impl->ReleaseOperations();
        return std::unexpected(std::move(changed_route.error()));
    }
    m_impl->presentation_changed = true;
    std::vector<RoutePointId> route_changes = std::move(changed_route->added);
    route_changes.insert(
        route_changes.end(), changed_route->removed.begin(), changed_route->removed.end());
    m_impl->TouchLinkPresentation(link, impact, full_route_validation, route_changes);
    m_impl->Record({OperationKind::SetLinkPresentation, action,
                    LinkPresentationOperation{
                        graph, link, impact, std::move(intent_style), std::move(intent_route)}});
    return {};
}

Result<void> GraphTransaction::SetLinkRouter(const LinkId link, TypeId router) {
    if (!ContainsLink(Document(), link)) {
        return std::unexpected(MakeError(ErrorCode::LinkNotFound, "Link does not exist"));
    }
    const auto* current = Presentation().FindLink(link);
    const LinkStyle before = current != nullptr ? current->Style() : LinkStyle{};
    if (before.router == router) return {};
    const GraphId graph = FindLinkGraph(Document(), link);
    if (m_impl->enforce_protection &&
        (m_impl->GraphReadOnly(graph) || m_impl->LinkLocked(link))) {
        return std::unexpected(MakeError(
            m_impl->GraphReadOnly(graph) ? ErrorCode::ReadOnly : ErrorCode::Locked,
            "Link router cannot be changed"));
    }
    LinkStyle style{.router = std::move(router), .color = before.color, .locked = before.locked};
    std::shared_ptr<const LinkStyle> intent;
    if (m_impl->record_operations) intent = std::make_shared<const LinkStyle>(style);
    if (auto budget = m_impl->ConsumeOperations(); !budget) return budget;
    if (auto result = m_impl->staged_presentation.SetLinkRouter(link, std::move(style.router)); !result) {
        m_impl->ReleaseOperations();
        return result;
    }
    const auto* after = Presentation().FindLink(link);
    const OperationAction action = after != nullptr ? OperationAction::Set : OperationAction::Erase;
    constexpr LinkPresentationImpact impact =
        LinkPresentationImpact::Style | LinkPresentationImpact::Geometry;
    m_impl->presentation_changed = true;
    m_impl->TouchLinkPresentation(link, impact);
    m_impl->Record({OperationKind::SetLinkPresentation, action,
                    LinkPresentationOperation{graph, link, impact, std::move(intent), {}}});
    return {};
}

Result<void> GraphTransaction::SetLinkColor(
    const LinkId link,
    const std::optional<std::uint32_t> color) {
    if (!ContainsLink(Document(), link)) {
        return std::unexpected(MakeError(ErrorCode::LinkNotFound, "Link does not exist"));
    }
    const auto* current = Presentation().FindLink(link);
    const LinkStyle before = current != nullptr ? current->Style() : LinkStyle{};
    if (before.color == color) return {};
    const GraphId graph = FindLinkGraph(Document(), link);
    if (m_impl->enforce_protection &&
        (m_impl->GraphReadOnly(graph) || m_impl->LinkLocked(link))) {
        return std::unexpected(MakeError(
            m_impl->GraphReadOnly(graph) ? ErrorCode::ReadOnly : ErrorCode::Locked,
            "Link color cannot be changed"));
    }
    LinkStyle style{.router = before.router, .color = color, .locked = before.locked};
    std::shared_ptr<const LinkStyle> intent;
    if (m_impl->record_operations) intent = std::make_shared<const LinkStyle>(style);
    if (auto budget = m_impl->ConsumeOperations(); !budget) return budget;
    if (auto result = m_impl->staged_presentation.SetLinkColor(link, color); !result) {
        m_impl->ReleaseOperations();
        return result;
    }
    const auto* after = Presentation().FindLink(link);
    const OperationAction action = after != nullptr ? OperationAction::Set : OperationAction::Erase;
    constexpr LinkPresentationImpact impact = LinkPresentationImpact::Style;
    m_impl->presentation_changed = true;
    m_impl->TouchLinkPresentation(link, impact);
    m_impl->Record({OperationKind::SetLinkPresentation, action,
                    LinkPresentationOperation{graph, link, impact, std::move(intent), {}}});
    return {};
}

Result<void> GraphTransaction::SetLinkRoute(
    const LinkId link,
    PersistentRoutePointSequence route) {
    if (!ContainsLink(Document(), link)) {
        return std::unexpected(MakeError(ErrorCode::LinkNotFound, "Link does not exist"));
    }
    if (!ValidLinkPresentation(LinkPresentation{LinkStyle{}, route})) {
        return std::unexpected(MakeError(ErrorCode::InvalidArgument, "Link route is invalid"));
    }
    const auto* current = Presentation().FindLink(link);
    if (current != nullptr && current->Route() == route) return {};
    const GraphId graph = FindLinkGraph(Document(), link);
    if (m_impl->enforce_protection &&
        (m_impl->GraphReadOnly(graph) || m_impl->LinkLocked(link))) {
        return std::unexpected(MakeError(
            m_impl->GraphReadOnly(graph) ? ErrorCode::ReadOnly : ErrorCode::Locked,
            "Link route cannot be changed"));
    }
    PersistentRoutePointSequence intent = route;
    if (auto budget = m_impl->ConsumeOperations(); !budget) return budget;
    auto changed_route = m_impl->staged_presentation.SetLinkRoute(link, std::move(route));
    if (!changed_route) {
        m_impl->ReleaseOperations();
        return std::unexpected(std::move(changed_route.error()));
    }
    const auto* after = Presentation().FindLink(link);
    const OperationAction action = after != nullptr ? OperationAction::Set : OperationAction::Erase;
    constexpr LinkPresentationImpact impact =
        LinkPresentationImpact::Route | LinkPresentationImpact::Geometry;
    m_impl->presentation_changed = true;
    std::vector<RoutePointId> route_changes = std::move(changed_route->added);
    route_changes.insert(
        route_changes.end(), changed_route->removed.begin(), changed_route->removed.end());
    m_impl->TouchLinkPresentation(link, impact, true, route_changes);
    m_impl->Record({OperationKind::SetLinkPresentation, action,
                    LinkPresentationOperation{graph, link, impact, {}, std::move(intent)}});
    return {};
}

Result<void> GraphTransaction::InsertRoutePoint(
    const LinkId link,
    RoutePoint point,
    const std::size_t index) {
    if (!point.id || !Finite(point.position) || !ContainsLink(Document(), link)) {
        return std::unexpected(MakeError(ErrorCode::InvalidArgument, "Route point or link is invalid"));
    }
    const auto* current = Presentation().FindLink(link);
    if (index > (current != nullptr ? current->Route().size() : 0)) {
        return std::unexpected(MakeError(ErrorCode::InvalidArgument, "Route point index is out of range"));
    }
    const GraphId graph = FindLinkGraph(Document(), link);
    if (m_impl->enforce_protection &&
        (m_impl->GraphReadOnly(graph) || m_impl->LinkLocked(link))) {
        return std::unexpected(MakeError(
            m_impl->GraphReadOnly(graph) ? ErrorCode::ReadOnly : ErrorCode::Locked,
            "Link route cannot be changed"));
    }
    if (auto budget = m_impl->ConsumeOperations(); !budget) return budget;
    if (auto result = m_impl->staged_presentation.InsertRoutePoint(link, point, index); !result) {
        m_impl->ReleaseOperations();
        return result;
    }
    constexpr LinkPresentationImpact impact =
        LinkPresentationImpact::Route | LinkPresentationImpact::Geometry;
    m_impl->presentation_changed = true;
    const RoutePointId changed = point.id;
    m_impl->TouchLinkPresentation(link, impact, false, std::span{&changed, 1});
    const auto* after = Presentation().FindLink(link);
    m_impl->Record({OperationKind::SetLinkPresentation, OperationAction::Set,
                    LinkPresentationOperation{graph, link, impact, {}, after->Route()}});
    return {};
}

Result<void> GraphTransaction::MoveRoutePoint(
    const LinkId link,
    const RoutePointId point,
    const Vec2 position) {
    const auto* current = Presentation().FindLink(link);
    const auto* value = current != nullptr ? current->Route().Find(point) : nullptr;
    if (value == nullptr || !Finite(position)) {
        return std::unexpected(MakeError(ErrorCode::InvalidArgument, "Route point or position is invalid"));
    }
    if (value->position == position) return {};
    const GraphId graph = FindLinkGraph(Document(), link);
    if (m_impl->enforce_protection &&
        (m_impl->GraphReadOnly(graph) || m_impl->LinkLocked(link))) {
        return std::unexpected(MakeError(
            m_impl->GraphReadOnly(graph) ? ErrorCode::ReadOnly : ErrorCode::Locked,
            "Link route cannot be changed"));
    }
    if (auto budget = m_impl->ConsumeOperations(); !budget) return budget;
    if (auto result = m_impl->staged_presentation.MoveRoutePoint(link, point, position); !result) {
        m_impl->ReleaseOperations();
        return result;
    }
    constexpr LinkPresentationImpact impact =
        LinkPresentationImpact::Route | LinkPresentationImpact::Geometry;
    m_impl->presentation_changed = true;
    m_impl->TouchLinkPresentation(link, impact, false, std::span{&point, 1});
    const auto* after = Presentation().FindLink(link);
    m_impl->Record({OperationKind::SetLinkPresentation, OperationAction::Set,
                    LinkPresentationOperation{graph, link, impact, {}, after->Route()}});
    return {};
}

Result<void> GraphTransaction::RemoveRoutePoints(
    const LinkId link,
    const std::span<const RoutePointId> points) {
    if (points.empty()) return {};
    const auto* current = Presentation().FindLink(link);
    if (current == nullptr || std::ranges::any_of(points, [&](const RoutePointId point) {
            return current->Route().Find(point) == nullptr;
        })) {
        return std::unexpected(MakeError(ErrorCode::InvalidArgument, "Route point does not exist"));
    }
    const GraphId graph = FindLinkGraph(Document(), link);
    if (m_impl->enforce_protection &&
        (m_impl->GraphReadOnly(graph) || m_impl->LinkLocked(link))) {
        return std::unexpected(MakeError(
            m_impl->GraphReadOnly(graph) ? ErrorCode::ReadOnly : ErrorCode::Locked,
            "Link route cannot be changed"));
    }
    if (auto budget = m_impl->ConsumeOperations(); !budget) return budget;
    if (auto result = m_impl->staged_presentation.RemoveRoutePoints(link, points); !result) {
        m_impl->ReleaseOperations();
        return result;
    }
    constexpr LinkPresentationImpact impact =
        LinkPresentationImpact::Route | LinkPresentationImpact::Geometry;
    m_impl->presentation_changed = true;
    m_impl->TouchLinkPresentation(link, impact, false, points);
    const auto* after = Presentation().FindLink(link);
    m_impl->Record({OperationKind::SetLinkPresentation,
                    after != nullptr ? OperationAction::Set : OperationAction::Erase,
                    LinkPresentationOperation{
                        graph,
                        link,
                        impact,
                        {},
                        after != nullptr ? after->Route() : PersistentRoutePointSequence{}}});
    return {};
}

Result<void> GraphTransaction::AddGroup(GroupPresentation group) {
    const GroupId group_id = group.id;
    if (!ValidGroupPresentation(Document(), group) || Document().FindGraph(group.graph) == nullptr) {
        return std::unexpected(MakeError(ErrorCode::InvalidArgument, "Group presentation is invalid"));
    }
    const auto* graph = Document().FindGraph(group.graph);
    if (m_impl->enforce_protection && graph != nullptr && m_impl->GraphReadOnly(group.graph)) {
        return std::unexpected(MakeError(ErrorCode::ReadOnly, "Graph is read-only"));
    }
    std::shared_ptr<const GroupPresentation> value;
    if (m_impl->record_operations) value = std::make_shared<const GroupPresentation>(group);
    if (auto budget = m_impl->ConsumeOperations(); !budget) return budget;
    auto result = m_impl->staged_presentation.AddGroup(std::move(group));
    if (!result) m_impl->ReleaseOperations();
    if (result) {
        m_impl->presentation_changed = true;
        m_impl->journal.groups[group_id] |= GroupImpact::Lifecycle;
        m_impl->Record({OperationKind::AddGroup, OperationAction::Set,
                        GroupLifecycleOperation{value->graph, group_id, std::move(value)}});
    }
    return result;
}

Result<GroupPresentation> GraphTransaction::RemoveGroup(const GroupId group) {
    const auto* current = Presentation().FindGroup(group);
    if (m_impl->enforce_protection && current != nullptr) {
        const auto* graph = Document().FindGraph(current->graph);
        if (graph != nullptr && m_impl->GraphReadOnly(current->graph)) {
            return std::unexpected(MakeError(ErrorCode::ReadOnly, "Graph is read-only"));
        }
        if (m_impl->GroupLocked(group)) {
            return std::unexpected(MakeError(ErrorCode::Locked, "Group is locked"));
        }
    }
    if (auto budget = m_impl->ConsumeOperations(); !budget) {
        return std::unexpected(std::move(budget.error()));
    }
    auto result = m_impl->staged_presentation.RemoveGroup(group);
    if (!result) m_impl->ReleaseOperations();
    if (result) {
        m_impl->presentation_changed = true;
        m_impl->journal.groups[group] |= GroupImpact::Lifecycle;
        m_impl->Record(
            {OperationKind::RemoveGroup, OperationAction::Erase,
             GroupLifecycleOperation{result->graph, group, std::make_shared<const GroupPresentation>(*result)}});
    }
    return result;
}

Result<void> GraphTransaction::SetGroupPosition(const GroupId group, const Vec2 position) {
    const auto* current = Presentation().FindGroup(group);
    if (current == nullptr) return std::unexpected(MakeError(ErrorCode::InvalidArgument, "Group does not exist"));
    if (!Finite(position)) return std::unexpected(MakeError(ErrorCode::InvalidArgument, "Group position is invalid"));
    if (current->geometry.position == position) return {};
    const auto* graph = Document().FindGraph(current->graph);
    if (m_impl->enforce_protection && graph != nullptr &&
        (m_impl->GraphReadOnly(current->graph) || m_impl->GroupLocked(group))) {
        return std::unexpected(
            MakeError(m_impl->GraphReadOnly(current->graph) ? ErrorCode::ReadOnly : ErrorCode::Locked,
                      "Group cannot be changed"));
    }
    if (auto budget = m_impl->ConsumeOperations(); !budget) return budget;
    auto result = m_impl->staged_presentation.SetGroupPosition(group, position);
    if (!result) m_impl->ReleaseOperations();
    if (result) {
        m_impl->presentation_changed = true;
        m_impl->journal.groups[group] |= GroupImpact::Geometry;
        const auto* value = Presentation().FindGroup(group);
        m_impl->Record({OperationKind::SetGroupGeometry, OperationAction::Set, Impl::GroupGeometry(*value)});
    }
    return result;
}

Result<void> GraphTransaction::SetGroupSize(const GroupId group, const Vec2 size) {
    const auto* current = Presentation().FindGroup(group);
    if (current == nullptr) return std::unexpected(MakeError(ErrorCode::InvalidArgument, "Group does not exist"));
    if (!Finite(size) || size.x < 0.0f || size.y < 0.0f) {
        return std::unexpected(MakeError(ErrorCode::InvalidArgument, "Group size is invalid"));
    }
    if (current->geometry.size == size) return {};
    const auto* graph = Document().FindGraph(current->graph);
    if (m_impl->enforce_protection && graph != nullptr &&
        (m_impl->GraphReadOnly(current->graph) || m_impl->GroupLocked(group))) {
        return std::unexpected(
            MakeError(m_impl->GraphReadOnly(current->graph) ? ErrorCode::ReadOnly : ErrorCode::Locked,
                      "Group cannot be changed"));
    }
    if (auto budget = m_impl->ConsumeOperations(); !budget) return budget;
    auto result = m_impl->staged_presentation.SetGroupSize(group, size);
    if (!result) m_impl->ReleaseOperations();
    if (result) {
        m_impl->presentation_changed = true;
        m_impl->journal.groups[group] |= GroupImpact::Geometry;
        const auto* value = Presentation().FindGroup(group);
        m_impl->Record({OperationKind::SetGroupGeometry, OperationAction::Set, Impl::GroupGeometry(*value)});
    }
    return result;
}

Result<void> GraphTransaction::SetGroupCollapsed(const GroupId group, const bool collapsed) {
    const auto* current = Presentation().FindGroup(group);
    if (current == nullptr) return std::unexpected(MakeError(ErrorCode::InvalidArgument, "Group does not exist"));
    if (current->geometry.collapsed == collapsed) return {};
    const auto* graph = Document().FindGraph(current->graph);
    if (m_impl->enforce_protection && graph != nullptr &&
        (m_impl->GraphReadOnly(current->graph) || m_impl->GroupLocked(group))) {
        return std::unexpected(
            MakeError(m_impl->GraphReadOnly(current->graph) ? ErrorCode::ReadOnly : ErrorCode::Locked,
                      "Group cannot be changed"));
    }
    if (auto budget = m_impl->ConsumeOperations(); !budget) return budget;
    auto result = m_impl->staged_presentation.SetGroupCollapsed(group, collapsed);
    if (!result) m_impl->ReleaseOperations();
    if (result) {
        m_impl->presentation_changed = true;
        m_impl->journal.groups[group] |= GroupImpact::Geometry;
        const auto* value = Presentation().FindGroup(group);
        m_impl->Record({OperationKind::SetGroupGeometry, OperationAction::Set, Impl::GroupGeometry(*value)});
    }
    return result;
}

Result<void> GraphTransaction::SetGroupZOrder(const GroupId group, const std::uint64_t z_order) {
    const auto* current = Presentation().FindGroup(group);
    if (current == nullptr) return std::unexpected(MakeError(ErrorCode::InvalidArgument, "Group does not exist"));
    if (current->geometry.z_order == z_order) return {};
    const auto* graph = Document().FindGraph(current->graph);
    if (m_impl->enforce_protection && graph != nullptr &&
        (m_impl->GraphReadOnly(current->graph) || m_impl->GroupLocked(group))) {
        return std::unexpected(
            MakeError(m_impl->GraphReadOnly(current->graph) ? ErrorCode::ReadOnly : ErrorCode::Locked,
                      "Group cannot be changed"));
    }
    if (auto budget = m_impl->ConsumeOperations(); !budget) return budget;
    auto result = m_impl->staged_presentation.SetGroupZOrder(group, z_order);
    if (!result) m_impl->ReleaseOperations();
    if (result) {
        m_impl->presentation_changed = true;
        m_impl->journal.groups[group] |= GroupImpact::Geometry;
        const auto* value = Presentation().FindGroup(group);
        m_impl->Record({OperationKind::SetGroupGeometry, OperationAction::Set, Impl::GroupGeometry(*value)});
    }
    return result;
}

Result<void> GraphTransaction::SetGroupStyle(const GroupId group, GroupStyleHandle style) {
    if (!style || (style->kind != GroupKind::Group && style->kind != GroupKind::Comment)) {
        return std::unexpected(MakeError(ErrorCode::InvalidArgument, "Group style is invalid"));
    }
    const auto* current = Presentation().FindGroup(group);
    if (current == nullptr) return std::unexpected(MakeError(ErrorCode::InvalidArgument, "Group does not exist"));
    if (current->style == style || *current->style == *style) return {};
    const auto* graph = Document().FindGraph(current->graph);
    if (m_impl->enforce_protection && graph != nullptr && m_impl->GraphReadOnly(current->graph)) {
        return std::unexpected(MakeError(ErrorCode::ReadOnly, "Graph is read-only"));
    }
    if (m_impl->enforce_protection && m_impl->GroupLocked(group)) {
        return std::unexpected(MakeError(ErrorCode::Locked, "Group is locked"));
    }
    if (auto budget = m_impl->ConsumeOperations(); !budget) return budget;
    auto result = m_impl->staged_presentation.SetGroupStyle(group, std::move(style));
    if (!result) m_impl->ReleaseOperations();
    if (result) {
        m_impl->presentation_changed = true;
        m_impl->journal.groups[group] |= GroupImpact::Style;
        const auto* value = Presentation().FindGroup(group);
        m_impl->Record({OperationKind::SetGroupStyle, OperationAction::Set,
                        GroupStyleOperation{value->graph, group, value->style}});
    }
    return result;
}

Result<void> GraphTransaction::SetGroupMembers(const GroupId group, GroupMemberSet members) {
    const auto* current = Presentation().FindGroup(group);
    if (current == nullptr) return std::unexpected(MakeError(ErrorCode::InvalidArgument, "Group does not exist"));
    if (current->members == members) return {};
    if (!std::ranges::all_of(
            members, [&](const NodeId node) { return node && Document().FindNode(current->graph, node) != nullptr; })) {
        return std::unexpected(MakeError(ErrorCode::InvalidArgument, "Group members are invalid"));
    }
    const auto* graph = Document().FindGraph(current->graph);
    if (m_impl->enforce_protection && graph != nullptr &&
        (m_impl->GraphReadOnly(current->graph) || m_impl->GroupLocked(group))) {
        return std::unexpected(
            MakeError(m_impl->GraphReadOnly(current->graph) ? ErrorCode::ReadOnly : ErrorCode::Locked,
                      "Group cannot be changed"));
    }
    const GraphId graph_id = current->graph;
    std::shared_ptr<const GroupMembershipChange> change;
    if (m_impl->record_operations) change = Impl::GroupMemberDifference(current->members, members);
    if (auto budget = m_impl->ConsumeOperations(); !budget) return budget;
    auto result = m_impl->staged_presentation.SetGroupMembers(group, std::move(members));
    if (!result) m_impl->ReleaseOperations();
    if (result) {
        m_impl->presentation_changed = true;
        m_impl->journal.groups[group] |= GroupImpact::Membership;
        m_impl->Record({OperationKind::SetGroupMembers, OperationAction::Set,
                        GroupMembershipOperation{graph_id, group, std::move(change)}});
    }
    return result;
}

Result<void> GraphTransaction::AddGroupMembers(const GroupId group, const std::span<const NodeId> members) {
    const auto* current = Presentation().FindGroup(group);
    if (current == nullptr) return std::unexpected(MakeError(ErrorCode::InvalidArgument, "Group does not exist"));
    std::vector<NodeId> additions;
    additions.reserve(members.size());
    for (const NodeId node : members) {
        if (!node || Document().FindNode(current->graph, node) == nullptr) {
            return std::unexpected(MakeError(ErrorCode::InvalidArgument, "Group member is invalid"));
        }
        if (!current->members.contains(node) && !std::ranges::contains(additions, node)) additions.push_back(node);
    }
    if (additions.empty()) return {};
    const auto* graph = Document().FindGraph(current->graph);
    if (m_impl->enforce_protection && graph != nullptr &&
        (m_impl->GraphReadOnly(current->graph) || m_impl->GroupLocked(group))) {
        return std::unexpected(
            MakeError(m_impl->GraphReadOnly(current->graph) ? ErrorCode::ReadOnly : ErrorCode::Locked,
                      "Group cannot be changed"));
    }
    const GraphId graph_id = current->graph;
    std::shared_ptr<const GroupMembershipChange> change;
    if (m_impl->record_operations) {
        change = std::make_shared<const GroupMembershipChange>(GroupMembershipChange{.added = additions});
    }
    if (auto budget = m_impl->ConsumeOperations(); !budget) return budget;
    auto result = m_impl->staged_presentation.AddGroupMembers(group, additions);
    if (!result) m_impl->ReleaseOperations();
    if (result) {
        m_impl->presentation_changed = true;
        m_impl->journal.groups[group] |= GroupImpact::Membership;
        m_impl->Record({OperationKind::SetGroupMembers, OperationAction::Set,
                        GroupMembershipOperation{graph_id, group, std::move(change)}});
    }
    return result;
}

Result<void> GraphTransaction::RemoveGroupMembers(const GroupId group, const std::span<const NodeId> members) {
    const auto* current = Presentation().FindGroup(group);
    if (current == nullptr) return std::unexpected(MakeError(ErrorCode::InvalidArgument, "Group does not exist"));
    std::vector<NodeId> removals;
    removals.reserve(members.size());
    for (const NodeId node : members) {
        if (current->members.contains(node) && !std::ranges::contains(removals, node)) removals.push_back(node);
    }
    if (removals.empty()) return {};
    const auto* graph = Document().FindGraph(current->graph);
    if (m_impl->enforce_protection && graph != nullptr &&
        (m_impl->GraphReadOnly(current->graph) || m_impl->GroupLocked(group))) {
        return std::unexpected(
            MakeError(m_impl->GraphReadOnly(current->graph) ? ErrorCode::ReadOnly : ErrorCode::Locked,
                      "Group cannot be changed"));
    }
    const GraphId graph_id = current->graph;
    std::shared_ptr<const GroupMembershipChange> change;
    if (m_impl->record_operations) {
        change = std::make_shared<const GroupMembershipChange>(GroupMembershipChange{.removed = removals});
    }
    if (auto budget = m_impl->ConsumeOperations(); !budget) return budget;
    auto result = m_impl->staged_presentation.RemoveGroupMembers(group, removals);
    if (!result) m_impl->ReleaseOperations();
    if (result) {
        m_impl->presentation_changed = true;
        m_impl->journal.groups[group] |= GroupImpact::Membership;
        m_impl->Record({OperationKind::SetGroupMembers, OperationAction::Set,
                        GroupMembershipOperation{graph_id, group, std::move(change)}});
    }
    return result;
}

#undef Record

} // namespace Uni::GUI::Nodes
