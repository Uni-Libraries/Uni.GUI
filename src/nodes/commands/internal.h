#pragma once

#include <uni/gui/nodes/commands.h>

#include "ui_nodes_internal.h"

#include <algorithm>
#include <cmath>
#include <concepts>
#include <functional>
#include <limits>
#include <set>
#include <type_traits>
#include <unordered_set>
#include <utility>

namespace Uni::GUI::Nodes::CommandDetail {

[[nodiscard]] inline Error MakeError(const ErrorCode code, std::string message) {
    return Error{code, std::move(message)};
}

template<typename Id> void Deduplicate(std::vector<Id>& values) {
    std::unordered_set<Id, IdHash> seen;
    std::erase_if(values, [&](const Id id) { return !seen.insert(id).second; });
}

[[nodiscard]] inline bool Finite(const Vec2 value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y);
}

[[nodiscard]] inline bool ValidGraphInterface(const GraphInterface& interface) {
    if (interface.version == 0) {
        return false;
    }
    std::unordered_set<std::string> keys;
    return std::ranges::all_of(interface.pins, [&](const GraphInterfacePin& pin) {
        const bool direction = pin.direction == PinDirection::Input || pin.direction == PinDirection::Output;
        const bool kind = pin.kind == PinKind::Data || pin.kind == PinKind::Execution;
        const bool caller =
            pin.caller_cardinality == PinCardinality::Single || pin.caller_cardinality == PinCardinality::Multiple;
        const bool boundary =
            pin.boundary_cardinality == PinCardinality::Single || pin.boundary_cardinality == PinCardinality::Multiple;
        return !pin.key.empty() && !pin.type.Empty() && direction && kind && caller && boundary &&
               keys.insert(pin.key).second;
    });
}

[[nodiscard]] inline Result<void> ValidateNodeCreationSchema(const RegistrySnapshot& registry, const NodeInstance& node,
                                                             const std::span<const PinInstance> pins,
                                                             const NodeTypeDescriptorPtr& prepared_descriptor = {}) {
    const NodeTypeDescriptorPtr current = registry.Find(node.type);
    if (!prepared_descriptor) return {};
    if (!current) {
        return std::unexpected(MakeError(ErrorCode::TypeNotFound, "The node descriptor used to prepare this "
                                                                  "creation is no longer registered"));
    }
    if (node.type_version != current->version || node.pins.size() != pins.size()) {
        return std::unexpected(MakeError(ErrorCode::InvalidGraph, "Prepared node creation does not match the current "
                                                                  "descriptor version or pin ownership"));
    }
    auto resolved = registry.ResolvePinSchema(node.type, node.properties);
    if (!resolved)
        return std::unexpected(std::move(resolved.error()));
    std::size_t static_index = 0;
    for (std::size_t index = 0; index < pins.size(); ++index) {
        const PinInstance& pin = pins[index];
        if (node.pins[index] != pin.id) {
            return std::unexpected(
                MakeError(ErrorCode::InvalidGraph, "Prepared node creation pin order does not match node ownership"));
        }
        if (pin.storage == PinStorage::Dynamic) continue;
        if (static_index == resolved->size()) {
            return std::unexpected(MakeError(ErrorCode::InvalidGraph, "Prepared node creation has static pins "
                                                                      "absent from the current descriptor"));
        }
        const PinDescriptor& expected = (*resolved)[static_index++];
        if (pin.key != expected.key || pin.label != expected.label || pin.type != expected.type ||
            pin.direction != expected.direction || pin.kind != expected.kind ||
            pin.cardinality != expected.cardinality || pin.node != node.id || pin.storage != PinStorage::Static) {
            return std::unexpected(MakeError(ErrorCode::InvalidGraph, "Prepared node creation static pins do "
                                                                      "not match the current descriptor"));
        }
    }
    if (static_index != resolved->size()) {
        return std::unexpected(MakeError(ErrorCode::InvalidGraph, "Prepared node creation is missing static "
                                                                  "pins from the current descriptor"));
    }
    return {};
}

[[nodiscard]] inline std::vector<GraphId> OwnedClosure(const GraphDocument& document, const NodeInstance& owner) {
    std::vector<GraphId> result;
    if (!owner.subgraph || owner.subgraph->ownership != SubgraphOwnership::Owned) {
        return result;
    }
    const auto root = Detail::LocalSubgraph(owner.subgraph);
    if (!root) {
        return result;
    }
    std::vector<GraphId> pending{*root};
    std::unordered_set<GraphId, IdHash> visited;
    while (!pending.empty()) {
        const GraphId graph_id = pending.back();
        pending.pop_back();
        if (!visited.insert(graph_id).second) {
            continue;
        }
        const auto* graph = document.FindGraph(graph_id);
        if (graph == nullptr) {
            continue;
        }
        result.push_back(graph_id);
        for (const auto& [node_id, node] : graph->nodes) {
            (void)node_id;
            if (node.subgraph && node.subgraph->ownership == SubgraphOwnership::Owned) {
                if (const auto child = Detail::LocalSubgraph(node.subgraph)) {
                    pending.push_back(*child);
                }
            }
        }
    }
    return result;
}

} // namespace Uni::GUI::Nodes::CommandDetail
