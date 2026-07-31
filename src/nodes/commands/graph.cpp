#include "nodes/commands/internal.h"

namespace Uni::GUI::Nodes {

using CommandDetail::MakeError;

struct SetSchemaVersionCommand::Impl final {
    std::uint32_t value;
    std::uint32_t previous{0};
    bool captured{false};
};

SetSchemaVersionCommand::SetSchemaVersionCommand(const std::uint32_t version)
    : m_impl(std::make_unique<Impl>(Impl{.value = version})) {}
SetSchemaVersionCommand::~SetSchemaVersionCommand() = default;
std::string_view SetSchemaVersionCommand::Name() const noexcept { return "Set schema version"; }
Result<void> SetSchemaVersionCommand::Apply(GraphTransaction& transaction, const RegistrySnapshot&) {
    if (!m_impl->captured) {
        m_impl->previous = transaction.Document().SchemaVersion();
        m_impl->captured = true;
    }
    return transaction.SetSchemaVersion(m_impl->value);
}
Result<void> SetSchemaVersionCommand::Revert(GraphTransaction& transaction) {
    return transaction.SetSchemaVersion(m_impl->previous);
}

struct SetRootGraphCommand::Impl final {
    GraphId value;
    GraphId previous;
    bool captured{false};
};

SetRootGraphCommand::SetRootGraphCommand(const GraphId graph)
    : m_impl(std::make_unique<Impl>(Impl{.value = graph})) {}
SetRootGraphCommand::~SetRootGraphCommand() = default;
std::string_view SetRootGraphCommand::Name() const noexcept { return "Set root graph"; }
Result<void> SetRootGraphCommand::Apply(GraphTransaction& transaction, const RegistrySnapshot&) {
    if (!m_impl->captured) {
        m_impl->previous = transaction.Document().RootGraph();
        m_impl->captured = true;
    }
    return transaction.SetRootGraph(m_impl->value);
}
Result<void> SetRootGraphCommand::Revert(GraphTransaction& transaction) {
    return transaction.SetRootGraph(m_impl->previous);
}

struct AddGraphCommand::Impl final { Graph graph; };

AddGraphCommand::AddGraphCommand(Graph graph)
    : m_impl(std::make_unique<Impl>(Impl{std::move(graph)})) {}
AddGraphCommand::AddGraphCommand(const GraphId graph)
    : AddGraphCommand(Graph{.id = graph}) {}
AddGraphCommand::~AddGraphCommand() = default;
std::string_view AddGraphCommand::Name() const noexcept { return "Add graph"; }
Result<void> AddGraphCommand::Apply(GraphTransaction& transaction, const RegistrySnapshot&) {
    auto result = transaction.AddGraph(m_impl->graph);
    return result ? Result<void>{} : std::unexpected(std::move(result.error()));
}
Result<void> AddGraphCommand::Revert(GraphTransaction& transaction) {
    auto result = transaction.RemoveGraph(m_impl->graph.id);
    return result ? Result<void>{} : std::unexpected(std::move(result.error()));
}

struct RemoveGraphCommand::Impl final {
    struct GraphState final {
        Graph graph;
        std::vector<std::pair<NodeId, NodePresentation>> nodes;
        std::vector<std::pair<LinkId, LinkPresentation>> links;
        std::vector<GroupPresentation> groups;
    };

    GraphId graph;
    std::vector<GraphState> graphs;
    std::vector<IntergraphLink> intergraph_links;
    bool captured{false};
};

RemoveGraphCommand::RemoveGraphCommand(const GraphId graph)
    : m_impl(std::make_unique<Impl>()) { m_impl->graph = graph; }
RemoveGraphCommand::~RemoveGraphCommand() = default;
std::string_view RemoveGraphCommand::Name() const noexcept { return "Remove graph"; }
Result<void> RemoveGraphCommand::Apply(GraphTransaction& transaction, const RegistrySnapshot&) {
    const auto* graph = transaction.Document().FindGraph(m_impl->graph);
    if (graph == nullptr) {
        return std::unexpected(MakeError(ErrorCode::GraphNotFound, "Graph does not exist"));
    }
    if (!m_impl->captured) {
        std::vector<GraphId> pending{m_impl->graph};
        std::unordered_set<GraphId, IdHash> captured;
        while (!pending.empty()) {
            const GraphId graph_id = pending.back();
            pending.pop_back();
            if (!captured.insert(graph_id).second) continue;
            const auto* current = transaction.Document().FindGraph(graph_id);
            if (current == nullptr) {
                return std::unexpected(MakeError(ErrorCode::GraphNotFound, "Owned graph descendant does not exist"));
            }
            Impl::GraphState state{.graph = *current};
            for (const auto& [node_id, node] : current->nodes) {
                if (const auto* presentation = transaction.Presentation().FindNode(node_id)) {
                    state.nodes.emplace_back(node_id, *presentation);
                }
                if (node.subgraph && node.subgraph->ownership == SubgraphOwnership::Owned) {
                    if (const auto child = Detail::LocalSubgraph(node.subgraph)) pending.push_back(*child);
                }
            }
            for (const auto& [link_id, link] : current->links) {
                (void)link;
                if (const auto* presentation = transaction.Presentation().FindLink(link_id)) {
                    state.links.emplace_back(link_id, *presentation);
                }
            }
            for (const GroupId group : transaction.Presentation().GroupsForGraph(graph_id)) {
                if (const auto* value = transaction.Presentation().FindGroup(group)) {
                    state.groups.push_back(*value);
                }
            }
            m_impl->graphs.push_back(std::move(state));
        }
        std::unordered_set<IntergraphLinkId, IdHash> links;
        for (const GraphId graph_id : captured) {
            const auto incident = transaction.Document().IntergraphLinksForGraph(graph_id);
            links.insert(incident.begin(), incident.end());
        }
        for (const IntergraphLinkId link : links) {
            if (const auto* value = transaction.Document().FindIntergraphLink(link)) {
                m_impl->intergraph_links.push_back(*value);
            }
        }
        m_impl->captured = true;
    }

    for (const auto& link : m_impl->intergraph_links) {
        if (auto removed = transaction.RemoveIntergraphLink(link.id); !removed) {
            return std::unexpected(std::move(removed.error()));
        }
    }
    for (const auto& state : m_impl->graphs) {
        for (const auto& group : state.groups) {
            if (auto result = transaction.RemoveGroup(group.id); !result) {
                return std::unexpected(std::move(result.error()));
            }
        }
        for (const auto& [link, presentation] : state.links) {
            (void)presentation;
            if (auto result = transaction.SetLinkPresentation(link, std::nullopt); !result) return result;
        }
        for (const auto& [node, presentation] : state.nodes) {
            (void)presentation;
            if (auto result = transaction.SetNodePresentation(node, std::nullopt); !result) return result;
        }
        if (auto removed = transaction.RemoveGraph(state.graph.id); !removed) {
            return std::unexpected(std::move(removed.error()));
        }
    }
    return {};
}
Result<void> RemoveGraphCommand::Revert(GraphTransaction& transaction) {
    if (!m_impl->captured) {
        return std::unexpected(MakeError(ErrorCode::CommandFailed, "Remove graph command was not executed"));
    }
    for (auto state = m_impl->graphs.rbegin(); state != m_impl->graphs.rend(); ++state) {
        if (auto result = transaction.RestoreGraph(state->graph); !result) return result;
        for (const auto& [node, presentation] : state->nodes) {
            if (auto result = transaction.SetNodePresentation(node, presentation); !result) return result;
        }
        for (const auto& [link, presentation] : state->links) {
            if (auto result = transaction.SetLinkPresentation(link, presentation); !result) return result;
        }
        for (const auto& group : state->groups) {
            if (auto result = transaction.AddGroup(group); !result) return result;
        }
    }
    for (const auto& link : m_impl->intergraph_links) {
        if (auto result = transaction.AddIntergraphLink(link); !result) return result;
    }
    return {};
}

} // namespace Uni::GUI::Nodes
