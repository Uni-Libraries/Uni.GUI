#include "nodes/commands/internal.h"

namespace Uni::GUI::Nodes {

using CommandDetail::Finite;
using CommandDetail::MakeError;

struct ConnectPinsCommand::Impl final {
    GraphId graph;
    Link link;
    std::optional<LinkPresentation> previous;
    bool captured{false};
};

ConnectPinsCommand::ConnectPinsCommand(const GraphId graph, Link link)
    : m_impl(std::make_unique<Impl>(Impl{.graph = graph, .link = std::move(link)})) {}
ConnectPinsCommand::~ConnectPinsCommand() = default;
std::string_view ConnectPinsCommand::Name() const noexcept { return "Connect pins"; }
Result<void> ConnectPinsCommand::Apply(GraphTransaction& transaction, const RegistrySnapshot&) {
    if (!m_impl->captured) {
        if (const auto* value = transaction.Presentation().FindLink(m_impl->link.id)) {
            m_impl->previous = *value;
        }
        m_impl->captured = true;
    }
    return transaction.AddLink(m_impl->graph, m_impl->link);
}
Result<void> ConnectPinsCommand::Revert(GraphTransaction& transaction) {
    if (auto removed = transaction.RemoveLink(m_impl->graph, m_impl->link.id); !removed) {
        return std::unexpected(std::move(removed.error()));
    }
    return transaction.SetLinkPresentation(m_impl->link.id, m_impl->previous);
}

Result<std::unique_ptr<Command>> PrepareConnectionCommand(
    GraphDocument& document,
    const GraphPresentation& presentation,
    const RegistryCatalog& registry,
    const ConnectionRequest request,
    const Vec2 conversion_position,
    const GraphPolicy& policy,
    const std::span<const PinInstance> pending_pins,
    const std::span<const NodeInstance> pending_nodes) {
    const auto invocation = Detail::RegistryAccess::Invoke(registry);
    const RegistrySnapshot& snapshot = invocation.Snapshot();
    const auto find_pin = [&](const PinId id) -> const PinInstance* {
        if (const auto* pin = document.FindPin(request.graph, id)) return pin;
        const auto found = std::ranges::find(pending_pins, id, &PinInstance::id);
        return found != pending_pins.end() ? &*found : nullptr;
    };
    const auto compatibility = Detail::ValidateConnection(
        document, presentation, request, snapshot, policy, pending_pins, pending_nodes);
    if (compatibility.status == ConnectionResult::Status::Rejected) {
        return std::unexpected(MakeError(compatibility.error, compatibility.reason));
    }
    const auto* first = find_pin(request.first);
    const auto* second = find_pin(request.second);
    if (first == nullptr || second == nullptr) {
        return std::unexpected(MakeError(ErrorCode::PinNotFound, "Connection endpoint does not exist"));
    }
    const PinInstance& output = first->direction == PinDirection::Output ? *first : *second;
    const PinInstance& input = first->direction == PinDirection::Input ? *first : *second;
    if (compatibility.status == ConnectionResult::Status::Allowed) {
        if (request.replacing) {
            return std::unique_ptr<Command>{std::make_unique<ReconnectLinkCommand>(
                request.graph, request.replacing, output.id, input.id)};
        }
        const LinkId link = document.AllocateLinkId();
        if (!link) {
            return std::unexpected(MakeError(ErrorCode::InvalidArgument, "Link IDs are exhausted"));
        }
        return std::unique_ptr<Command>{std::make_unique<ConnectPinsCommand>(
            request.graph, Link{.id = link, .output = output.id, .input = input.id})};
    }
    if (!compatibility.recipe || !Finite(conversion_position)) {
        return std::unexpected(MakeError(ErrorCode::InvalidArgument, "Conversion plan is invalid"));
    }

    const ConversionRecipe recipe = *compatibility.recipe;
    const auto& descriptor = recipe.Descriptor();
    auto conversion = recipe.Instantiate(document);
    if (!conversion) {
        return std::unexpected(std::move(conversion.error()));
    }
    const auto conversion_input = std::ranges::find(
        conversion->pins, descriptor.input_pin, &PinInstance::key);
    const auto conversion_output = std::ranges::find(
        conversion->pins, descriptor.output_pin, &PinInstance::key);
    if (conversion_input == conversion->pins.end() || conversion_output == conversion->pins.end()) {
        return std::unexpected(MakeError(ErrorCode::InvalidGraph, "Conversion node is missing a semantic pin"));
    }
    const PinId conversion_input_id = conversion_input->id;
    const PinId conversion_output_id = conversion_output->id;

    LinkId first_link;
    LinkId second_link;
    if (request.replacing) {
        const auto* previous = document.FindLink(request.graph, request.replacing);
        if (previous == nullptr) {
            return std::unexpected(MakeError(ErrorCode::LinkNotFound, "Reconnected link does not exist"));
        }
        if (previous->input == input.id && previous->output != output.id) {
            second_link = request.replacing;
            first_link = document.AllocateLinkId();
        } else {
            first_link = request.replacing;
            second_link = document.AllocateLinkId();
        }
    } else {
        first_link = document.AllocateLinkId();
        second_link = document.AllocateLinkId();
    }
    if (!first_link || !second_link || first_link == second_link) {
        return std::unexpected(MakeError(ErrorCode::InvalidArgument, "Link IDs are exhausted"));
    }
    const NodePresentation conversion_presentation{.position = conversion_position};
    return std::unique_ptr<Command>{std::make_unique<InsertConversionCommand>(
        request.graph,
        recipe,
        std::move(*conversion),
        conversion_presentation,
        Link{.id = first_link, .output = output.id, .input = conversion_input_id},
        Link{.id = second_link, .output = conversion_output_id, .input = input.id},
        request.replacing)};
}

struct InsertConversionCommand::Impl final {
    GraphId graph;
    ConversionRecipe recipe;
    NodeCreation conversion;
    NodePresentation presentation;
    Link first;
    Link second;
    LinkId replacing;
    std::optional<Link> previous;
    std::optional<LinkPresentation> previous_presentation;
};

InsertConversionCommand::InsertConversionCommand(
    const GraphId graph,
    ConversionRecipe recipe,
    NodeCreation conversion,
    NodePresentation presentation,
    Link first,
    Link second,
    const LinkId replacing)
    : m_impl(std::make_unique<Impl>(Impl{
          graph,
          std::move(recipe),
          std::move(conversion),
          std::move(presentation),
          std::move(first),
          std::move(second),
          replacing,
      })) {}
InsertConversionCommand::~InsertConversionCommand() = default;
std::string_view InsertConversionCommand::Name() const noexcept { return "Insert conversion"; }
Result<void> InsertConversionCommand::Apply(GraphTransaction& transaction, const RegistrySnapshot& registry) {
    if (auto valid = registry.ValidateRecipe(m_impl->recipe); !valid) return valid;
    const auto& descriptor = m_impl->recipe.Descriptor();
    const auto conversion_input = std::ranges::find(
        m_impl->conversion.pins, m_impl->first.input, &PinInstance::id);
    const auto conversion_output = std::ranges::find(
        m_impl->conversion.pins, m_impl->second.output, &PinInstance::id);
    if (!m_impl->recipe.Matches(m_impl->conversion) || !m_impl->first.id || !m_impl->second.id ||
        m_impl->conversion.node.type != descriptor.node_type ||
        m_impl->first.id == m_impl->second.id || conversion_input == m_impl->conversion.pins.end() ||
        conversion_output == m_impl->conversion.pins.end() ||
        conversion_input->node != m_impl->conversion.node.id ||
        conversion_output->node != m_impl->conversion.node.id ||
        conversion_input->direction != PinDirection::Input ||
        conversion_output->direction != PinDirection::Output ||
        conversion_input->key != descriptor.input_pin ||
        conversion_output->key != descriptor.output_pin ||
        conversion_input->type != descriptor.key.source_type ||
        conversion_output->type != descriptor.key.destination_type ||
        conversion_input->kind != descriptor.key.kind ||
        conversion_output->kind != descriptor.key.kind ||
        std::ranges::find(m_impl->conversion.pins, m_impl->first.output, &PinInstance::id) !=
            m_impl->conversion.pins.end() ||
        std::ranges::find(m_impl->conversion.pins, m_impl->second.input, &PinInstance::id) !=
            m_impl->conversion.pins.end()) {
        return std::unexpected(MakeError(ErrorCode::InvalidGraph, "Conversion command does not form a valid chain"));
    }
    if (m_impl->replacing &&
        ((m_impl->first.id == m_impl->replacing) == (m_impl->second.id == m_impl->replacing))) {
        return std::unexpected(MakeError(
            ErrorCode::InvalidGraph,
            "Converted reconnect must preserve its link identity on exactly one leg"));
    }
    auto authorization = transaction.AuthorizeConnection(ConnectionRequest{
        m_impl->graph,
        m_impl->first.output,
        m_impl->second.input,
        m_impl->replacing,
    });
    if (!authorization) return std::unexpected(std::move(authorization.error()));
    if (authorization->status != ConnectionResult::Status::RequiresConversion ||
        !authorization->recipe || *authorization->recipe != m_impl->recipe) {
        return std::unexpected(MakeError(
            ErrorCode::IncompatiblePins,
            "Connection no longer resolves to the prepared conversion"));
    }
    if (m_impl->replacing) {
        const auto* current = transaction.Document().FindLink(m_impl->graph, m_impl->replacing);
        if (current == nullptr) {
            return std::unexpected(MakeError(ErrorCode::LinkNotFound, "Reconnected link does not exist"));
        }
        if (!m_impl->previous) {
            m_impl->previous = *current;
            if (const auto* state = transaction.Presentation().FindLink(m_impl->replacing)) {
                m_impl->previous_presentation = *state;
            }
        } else if (*current != *m_impl->previous) {
            return std::unexpected(MakeError(ErrorCode::CommandFailed, "Reconnected link changed before commit"));
        }
        if ((m_impl->first.id == m_impl->replacing && current->output != m_impl->first.output) ||
            (m_impl->second.id == m_impl->replacing && current->input != m_impl->second.input)) {
            return std::unexpected(MakeError(
                ErrorCode::InvalidGraph,
                "Converted reconnect link identity is not on the fixed endpoint"));
        }
    }
    if (auto added = transaction.AddNode(
            m_impl->graph, m_impl->conversion.node, m_impl->conversion.pins); !added) {
        return added;
    }
    if (auto state = transaction.SetNodePresentation(m_impl->conversion.node.id, m_impl->presentation); !state) {
        return state;
    }
    if (m_impl->replacing) {
        if (auto removed = transaction.RemovePlannedLink(m_impl->graph, m_impl->replacing); !removed) {
            return std::unexpected(std::move(removed.error()));
        }
    }
    if (auto connected = transaction.AddPlannedLink(m_impl->graph, m_impl->first); !connected) return connected;
    if (auto connected = transaction.AddPlannedLink(m_impl->graph, m_impl->second); !connected) return connected;
    if (m_impl->replacing && m_impl->previous_presentation) {
        return transaction.SetLinkRoute(m_impl->replacing, {});
    }
    return {};
}
Result<void> InsertConversionCommand::Revert(GraphTransaction& transaction) {
    if (auto state = transaction.SetLinkPresentation(m_impl->second.id, std::nullopt); !state) return state;
    if (auto removed = transaction.RemoveLink(m_impl->graph, m_impl->second.id); !removed) {
        return std::unexpected(std::move(removed.error()));
    }
    if (auto state = transaction.SetLinkPresentation(m_impl->first.id, std::nullopt); !state) return state;
    if (auto removed = transaction.RemoveLink(m_impl->graph, m_impl->first.id); !removed) {
        return std::unexpected(std::move(removed.error()));
    }
    if (auto state = transaction.SetNodePresentation(m_impl->conversion.node.id, std::nullopt); !state) return state;
    if (auto removed = transaction.RemoveNode(m_impl->graph, m_impl->conversion.node.id); !removed) {
        return std::unexpected(std::move(removed.error()));
    }
    if (m_impl->previous) {
        if (auto restored = transaction.AddLink(m_impl->graph, *m_impl->previous); !restored) return restored;
        return transaction.SetLinkPresentation(m_impl->replacing, m_impl->previous_presentation);
    }
    return {};
}

struct ReconnectLinkCommand::Impl final {
    GraphId graph;
    Link replacement;
    bool preserve_route;
    std::optional<Link> previous;
    std::optional<LinkPresentation> previous_presentation;
};

ReconnectLinkCommand::ReconnectLinkCommand(
    const GraphId graph,
    const LinkId link,
    const PinId output,
    const PinId input,
    const bool preserve_route)
    : m_impl(std::make_unique<Impl>(Impl{
          .graph = graph,
          .replacement = Link{.id = link, .output = output, .input = input},
          .preserve_route = preserve_route,
      })) {}
ReconnectLinkCommand::~ReconnectLinkCommand() = default;
std::string_view ReconnectLinkCommand::Name() const noexcept { return "Reconnect link"; }
Result<void> ReconnectLinkCommand::Apply(GraphTransaction& transaction, const RegistrySnapshot&) {
    const auto* current = transaction.Document().FindLink(m_impl->graph, m_impl->replacement.id);
    if (current == nullptr) {
        return std::unexpected(MakeError(ErrorCode::LinkNotFound, "Reconnected link does not exist"));
    }
    if (*current == m_impl->replacement) return {};
    if (!m_impl->previous) {
        m_impl->previous = *current;
        if (const auto* state = transaction.Presentation().FindLink(current->id)) {
            m_impl->previous_presentation = *state;
        }
    }
    if (auto result = transaction.AddLink(m_impl->graph, m_impl->replacement, *current); !result) {
        return result;
    }
    if (m_impl->previous_presentation && !m_impl->preserve_route) {
        return transaction.SetLinkRoute(m_impl->replacement.id, {});
    }
    return {};
}
Result<void> ReconnectLinkCommand::Revert(GraphTransaction& transaction) {
    if (!m_impl->previous) {
        return std::unexpected(MakeError(ErrorCode::CommandFailed, "Reconnect command was not executed"));
    }
    if (auto removed = transaction.RemoveLink(m_impl->graph, m_impl->replacement.id); !removed) {
        return std::unexpected(std::move(removed.error()));
    }
    if (auto restored = transaction.AddLink(m_impl->graph, *m_impl->previous); !restored) {
        return restored;
    }
    return transaction.SetLinkPresentation(m_impl->replacement.id, m_impl->previous_presentation);
}

struct SetLinkPresentationCommand::Impl final {
    LinkId link;
    LinkPresentation value;
    std::optional<LinkPresentation> previous;
    bool captured{false};
};

SetLinkPresentationCommand::SetLinkPresentationCommand(const LinkId link, LinkPresentation presentation)
    : m_impl(std::make_unique<Impl>(Impl{.link = link, .value = std::move(presentation)})) {}
SetLinkPresentationCommand::~SetLinkPresentationCommand() = default;
std::string_view SetLinkPresentationCommand::Name() const noexcept { return "Set link presentation"; }
Result<void> SetLinkPresentationCommand::Apply(GraphTransaction& transaction, const RegistrySnapshot&) {
    if (!m_impl->captured) {
        if (const auto* value = transaction.Presentation().FindLink(m_impl->link)) m_impl->previous = *value;
        m_impl->captured = true;
    }
    return transaction.SetLinkPresentation(m_impl->link, m_impl->value);
}
Result<void> SetLinkPresentationCommand::Revert(GraphTransaction& transaction) {
    return transaction.SetLinkPresentation(m_impl->link, m_impl->previous);
}

struct SetLinkRouterCommand::Impl final {
    LinkId link;
    TypeId value;
    std::optional<LinkPresentation> previous;
    bool captured{false};
};

SetLinkRouterCommand::SetLinkRouterCommand(const LinkId link, TypeId router)
    : m_impl(std::make_unique<Impl>(Impl{.link = link, .value = std::move(router)})) {}
SetLinkRouterCommand::~SetLinkRouterCommand() = default;
std::string_view SetLinkRouterCommand::Name() const noexcept { return "Set link router"; }
Result<void> SetLinkRouterCommand::Apply(GraphTransaction& transaction, const RegistrySnapshot&) {
    if (!m_impl->captured) {
        if (const auto* value = transaction.Presentation().FindLink(m_impl->link)) {
            m_impl->previous = *value;
        }
        m_impl->captured = true;
    }
    return transaction.SetLinkRouter(m_impl->link, m_impl->value);
}
Result<void> SetLinkRouterCommand::Revert(GraphTransaction& transaction) {
    return transaction.SetLinkPresentation(m_impl->link, m_impl->previous);
}

struct SetLinkColorCommand::Impl final {
    LinkId link;
    std::optional<std::uint32_t> value;
    std::optional<LinkPresentation> previous;
    bool captured{false};
};

SetLinkColorCommand::SetLinkColorCommand(
    const LinkId link,
    const std::optional<std::uint32_t> color)
    : m_impl(std::make_unique<Impl>(Impl{.link = link, .value = color})) {}
SetLinkColorCommand::~SetLinkColorCommand() = default;
std::string_view SetLinkColorCommand::Name() const noexcept { return "Set link color"; }
Result<void> SetLinkColorCommand::Apply(GraphTransaction& transaction, const RegistrySnapshot&) {
    if (!m_impl->captured) {
        if (const auto* value = transaction.Presentation().FindLink(m_impl->link)) {
            m_impl->previous = *value;
        }
        m_impl->captured = true;
    }
    return transaction.SetLinkColor(m_impl->link, m_impl->value);
}
Result<void> SetLinkColorCommand::Revert(GraphTransaction& transaction) {
    return transaction.SetLinkPresentation(m_impl->link, m_impl->previous);
}

struct SetLinkRoutePointsCommand::Impl final {
    LinkId link;
    PersistentRoutePointSequence value;
    std::optional<LinkPresentation> previous;
    bool captured{false};
};

SetLinkRoutePointsCommand::SetLinkRoutePointsCommand(
    const LinkId link,
    PersistentRoutePointSequence route_points)
    : m_impl(std::make_unique<Impl>(Impl{.link = link, .value = std::move(route_points)})) {}
SetLinkRoutePointsCommand::SetLinkRoutePointsCommand(
    const LinkId link,
    std::vector<RoutePoint> route_points)
    : SetLinkRoutePointsCommand(link, PersistentRoutePointSequence{std::move(route_points)}) {}
SetLinkRoutePointsCommand::~SetLinkRoutePointsCommand() = default;
std::string_view SetLinkRoutePointsCommand::Name() const noexcept { return "Set link route points"; }
Result<void> SetLinkRoutePointsCommand::Apply(GraphTransaction& transaction, const RegistrySnapshot&) {
    if (!m_impl->captured) {
        if (const auto* value = transaction.Presentation().FindLink(m_impl->link)) {
            m_impl->previous = *value;
        }
        m_impl->captured = true;
    }
    return transaction.SetLinkRoute(m_impl->link, m_impl->value);
}
Result<void> SetLinkRoutePointsCommand::Revert(GraphTransaction& transaction) {
    return transaction.SetLinkPresentation(m_impl->link, m_impl->previous);
}

struct InsertRoutePointCommand::Impl final {
    LinkId link;
    RoutePoint point;
    std::size_t index;
    std::optional<LinkPresentation> previous;
    bool captured{false};
};

InsertRoutePointCommand::InsertRoutePointCommand(
    const LinkId link,
    RoutePoint point,
    const std::size_t index)
    : m_impl(std::make_unique<Impl>(Impl{.link = link, .point = std::move(point), .index = index})) {}
InsertRoutePointCommand::~InsertRoutePointCommand() = default;
std::string_view InsertRoutePointCommand::Name() const noexcept { return "Insert route point"; }
Result<void> InsertRoutePointCommand::Apply(GraphTransaction& transaction, const RegistrySnapshot&) {
    if (!m_impl->point.id || !Finite(m_impl->point.position)) {
        return std::unexpected(MakeError(ErrorCode::InvalidArgument, "Route point is invalid"));
    }
    if (!m_impl->captured) {
        if (const auto* value = transaction.Presentation().FindLink(m_impl->link)) {
            m_impl->previous = *value;
        }
        m_impl->captured = true;
    }
    return transaction.InsertRoutePoint(m_impl->link, m_impl->point, m_impl->index);
}
Result<void> InsertRoutePointCommand::Revert(GraphTransaction& transaction) {
    return transaction.SetLinkPresentation(m_impl->link, m_impl->previous);
}

struct MoveRoutePointCommand::Impl final {
    LinkId link;
    RoutePointId point;
    Vec2 value;
    Vec2 previous;
    bool captured{false};
};

MoveRoutePointCommand::MoveRoutePointCommand(
    const LinkId link,
    const RoutePointId point,
    const Vec2 position)
    : m_impl(std::make_unique<Impl>(Impl{.link = link, .point = point, .value = position})) {}
MoveRoutePointCommand::~MoveRoutePointCommand() = default;
std::string_view MoveRoutePointCommand::Name() const noexcept { return "Move route point"; }
Result<void> MoveRoutePointCommand::Apply(GraphTransaction& transaction, const RegistrySnapshot&) {
    const auto* current = transaction.Presentation().FindRoutePoint(m_impl->link, m_impl->point);
    if (current == nullptr || !Finite(m_impl->value)) {
        return std::unexpected(MakeError(ErrorCode::InvalidArgument, "Route point or position is invalid"));
    }
    if (!m_impl->captured) {
        m_impl->previous = current->position;
        m_impl->captured = true;
    }
    return transaction.MoveRoutePoint(m_impl->link, m_impl->point, m_impl->value);
}
Result<void> MoveRoutePointCommand::Revert(GraphTransaction& transaction) {
    if (!m_impl->captured) {
        return std::unexpected(MakeError(ErrorCode::CommandFailed, "Move route point command was not executed"));
    }
    return transaction.MoveRoutePoint(m_impl->link, m_impl->point, m_impl->previous);
}

struct RemoveRoutePointsCommand::Impl final {
    std::vector<RoutePointRef> points;
    std::unordered_map<LinkId, LinkPresentation, IdHash> previous;
    bool captured{false};
};

RemoveRoutePointsCommand::RemoveRoutePointsCommand(std::vector<RoutePointRef> points)
    : m_impl(std::make_unique<Impl>()) {
    m_impl->points = std::move(points);
    std::ranges::sort(m_impl->points, [](const RoutePointRef& first, const RoutePointRef& second) {
        return first.link == second.link
            ? first.point.Value() < second.point.Value()
            : first.link.Value() < second.link.Value();
    });
    m_impl->points.erase(std::unique(m_impl->points.begin(), m_impl->points.end()), m_impl->points.end());
}
RemoveRoutePointsCommand::~RemoveRoutePointsCommand() = default;
std::string_view RemoveRoutePointsCommand::Name() const noexcept { return "Remove route points"; }
Result<void> RemoveRoutePointsCommand::Apply(GraphTransaction& transaction, const RegistrySnapshot&) {
    if (m_impl->points.empty()) {
        return std::unexpected(MakeError(ErrorCode::InvalidArgument, "No route points were selected"));
    }
    for (const auto& reference : m_impl->points) {
        if (transaction.Presentation().FindRoutePoint(reference.link, reference.point) == nullptr) {
            return std::unexpected(MakeError(ErrorCode::InvalidArgument, "Route point does not exist"));
        }
    }
    if (!m_impl->captured) {
        for (const auto& reference : m_impl->points) {
            if (!m_impl->previous.contains(reference.link)) {
                const auto* state = transaction.Presentation().FindLink(reference.link);
                if (state == nullptr) {
                    return std::unexpected(MakeError(ErrorCode::InvalidArgument, "Route point link has no presentation"));
                }
                m_impl->previous.emplace(reference.link, *state);
            }
        }
        m_impl->captured = true;
    }
    for (const auto& [link, previous] : m_impl->previous) {
        (void)previous;
        std::vector<RoutePointId> removals;
        for (const auto& reference : m_impl->points) {
            if (reference.link == link) removals.push_back(reference.point);
        }
        if (auto result = transaction.RemoveRoutePoints(link, removals); !result) {
            return result;
        }
    }
    return {};
}
Result<void> RemoveRoutePointsCommand::Revert(GraphTransaction& transaction) {
    for (const auto& [link, state] : m_impl->previous) {
        if (auto result = transaction.SetLinkPresentation(link, state); !result) {
            return result;
        }
    }
    return {};
}

} // namespace Uni::GUI::Nodes
