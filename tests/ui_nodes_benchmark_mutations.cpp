#include "ui_nodes_benchmark_support.h"

#include <iostream>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

namespace Uni::GUI::Nodes::Benchmarks {
namespace {

constexpr std::size_t MutationSamples = 40;

void PrintMutation(
    const std::string_view scenario,
    const std::size_t scale,
    const Distribution& distribution) {
    std::cout << "suite=mutations scenario=" << scenario << " scale=" << scale
              << " p50_ms=" << distribution.p50_ms
              << " p95_ms=" << distribution.p95_ms
              << " max_ms=" << distribution.max_ms << '\n';
}

void CheckIncrementalOnly(const std::string_view scenario, const std::uint64_t maximum_records) {
    const auto metrics = GetTransactionMetrics();
    Check(metrics.full_structure_validations == 0,
          std::string{scenario} + " unexpectedly ran a full document structure audit");
    Check(metrics.incremental_records_validated != 0,
          std::string{scenario} + " did not validate its transaction journal");
    Check(metrics.incremental_records_validated <= maximum_records,
          std::string{scenario} + " finalized more journal records than its operation-specific bound");
}

std::unique_ptr<Command> PropertyBatch(
    const Fixture& fixture,
    const std::int64_t value,
    const std::size_t count) {
    std::vector<std::unique_ptr<Command>> commands;
    commands.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        commands.push_back(std::make_unique<SetNodePropertyCommand>(
            fixture.graph,
            fixture.nodes[index],
            "policy-batch",
            PropertyValue{value}));
    }
    return std::make_unique<CompoundCommand>("Policy mutation batch", std::move(commands));
}

class BulkSubgraphBindingsCommand final : public Command {
public:
    BulkSubgraphBindingsCommand(GraphId graph, GraphId target, std::vector<NodeId> nodes)
        : m_graph(graph), m_target(target), m_nodes(std::move(nodes)) {}

    [[nodiscard]] std::string_view Name() const noexcept override { return "Build subgraph caller fanout"; }

private:
    [[nodiscard]] Result<void> Apply(GraphTransaction& transaction, const RegistrySnapshot&) override {
        const SubgraphReference reference{
            .ownership = SubgraphOwnership::Referenced,
            .target = DocumentGraphTarget{m_target},
        };
        for (const NodeId node : m_nodes) {
            if (auto result = transaction.SetNodeSubgraph(m_graph, node, reference); !result) return result;
        }
        return {};
    }

    [[nodiscard]] Result<void> Revert(GraphTransaction& transaction) override {
        for (const NodeId node : m_nodes) {
            if (auto result = transaction.SetNodeSubgraph(m_graph, node, std::nullopt); !result) return result;
        }
        return {};
    }

    GraphId m_graph;
    GraphId m_target;
    std::vector<NodeId> m_nodes;
};

class BulkIntergraphLinksCommand final : public Command {
public:
    explicit BulkIntergraphLinksCommand(std::vector<IntergraphLink> links)
        : m_links(std::move(links)) {}

    [[nodiscard]] std::string_view Name() const noexcept override { return "Build intergraph fanout"; }

private:
    [[nodiscard]] Result<void> Apply(GraphTransaction& transaction, const RegistrySnapshot&) override {
        for (const auto& link : m_links) {
            if (auto result = transaction.AddIntergraphLink(link); !result) return result;
        }
        return {};
    }

    [[nodiscard]] Result<void> Revert(GraphTransaction& transaction) override {
        for (auto link = m_links.rbegin(); link != m_links.rend(); ++link) {
            if (auto result = transaction.RemoveIntergraphLink(link->id); !result) {
                return std::unexpected(std::move(result.error()));
            }
        }
        return {};
    }

    std::vector<IntergraphLink> m_links;
};

class BulkGraphsCommand final : public Command {
public:
    explicit BulkGraphsCommand(std::vector<Graph> graphs) : m_graphs(std::move(graphs)) {}

    [[nodiscard]] std::string_view Name() const noexcept override { return "Build dependency chain"; }

private:
    [[nodiscard]] Result<void> Apply(GraphTransaction& transaction, const RegistrySnapshot&) override {
        for (const Graph& graph : m_graphs) {
            if (auto added = transaction.AddGraph(graph); !added) {
                return std::unexpected(std::move(added.error()));
            }
        }
        return {};
    }

    [[nodiscard]] Result<void> Revert(GraphTransaction& transaction) override {
        for (auto graph = m_graphs.rbegin(); graph != m_graphs.rend(); ++graph) {
            if (auto removed = transaction.RemoveGraph(graph->id); !removed) {
                return std::unexpected(std::move(removed.error()));
            }
        }
        return {};
    }

    std::vector<Graph> m_graphs;
};

} // namespace

void RunMutationsSuite() {
    auto fixture = MakeFixture(FixtureConfig{
        .node_count = 100'000,
        .links = LinkPattern::Chain,
        .columns = 500,
    });

    bool toggle = false;
    ResetTransactionMetrics();
    const Distribution move_node = MeasureDistribution(MutationSamples, [&] {
        NodePresentation state = *fixture.presentation.FindNode(fixture.nodes.front());
        state.position = toggle ? Vec2{10.0f, 20.0f} : Vec2{20.0f, 30.0f};
        toggle = !toggle;
        Execute(fixture,
                std::make_unique<SetNodePresentationCommand>(fixture.nodes.front(), state),
                "move-one-node-100k failed");
    });
    CheckIncrementalOnly("move-one-node-100k", MutationSamples);
    CheckTiming("move-one-node-100k", move_node, 5.0, 12.0);
    PrintMutation("move-one-node-100k", fixture.nodes.size(), move_node);

    const LinkId routed_link = fixture.links.front();
    const RoutePointId route_point = fixture.presentation.AllocateRoutePointId();
    ResetTransactionMetrics();
    const Distribution route_link = MeasureDistribution(MutationSamples, [&] {
        const Vec2 position = toggle ? Vec2{80.0f, 40.0f} : Vec2{90.0f, 50.0f};
        toggle = !toggle;
        Execute(fixture,
                std::make_unique<SetLinkRoutePointsCommand>(
                    routed_link,
                    std::vector<RoutePoint>{{route_point, position}}),
                "route-one-link-100k failed");
    });
    CheckIncrementalOnly("route-one-link-100k", MutationSamples);
    CheckTiming("route-one-link-100k", route_link, 5.0, 12.0);
    PrintMutation("route-one-link-100k", fixture.nodes.size(), route_link);

    constexpr std::size_t LargeRouteSize = 400'000;
    constexpr std::size_t LargeRouteSamples = 20;
    std::vector<RoutePoint> large_route_points;
    large_route_points.reserve(LargeRouteSize);
    RoutePointId moved_route_point;
    for (std::size_t index = 0; index < LargeRouteSize; ++index) {
        const RoutePointId point = fixture.presentation.AllocateRoutePointId();
        Check(static_cast<bool>(point), "large route point allocation failed");
        if (index == LargeRouteSize / 2) moved_route_point = point;
        large_route_points.push_back(RoutePoint{
            .id = point,
            .position = {static_cast<float>(index), static_cast<float>(index % 101)},
        });
    }
    const PersistentRoutePointSequence large_route{std::move(large_route_points)};
    Execute(fixture,
            std::make_unique<SetLinkRoutePointsCommand>(routed_link, large_route),
            "large route fixture failed");
    fixture.commands.Clear();

    ResetTransactionMetrics();
    const Distribution recolor_large_route = MeasureDistribution(LargeRouteSamples, [&] {
        toggle = !toggle;
        Execute(fixture,
                std::make_unique<SetLinkColorCommand>(
                    routed_link,
                    toggle ? std::optional<std::uint32_t>{0xFF102030U}
                           : std::optional<std::uint32_t>{0xFFA0B0C0U}),
                "recolor-link-400k-route failed");
    });
    const auto recolor_metrics = GetTransactionMetrics();
    Check(recolor_metrics.route_point_sequences.logical_bytes == 0 &&
              recolor_metrics.presentation_indexes.logical_bytes == 0,
          "recolor-link-400k-route copied route storage or owner indexes");
    CheckTiming("recolor-link-400k-route", recolor_large_route, 20.0, 50.0);
    PrintMutation("recolor-link-400k-route", LargeRouteSize, recolor_large_route);

    fixture.commands.Clear();
    ResetTransactionMetrics();
    const Distribution lock_large_route = MeasureDistribution(LargeRouteSamples, [&] {
        toggle = !toggle;
        Execute(fixture,
                std::make_unique<SetLinkLockedCommand>(routed_link, toggle),
                "lock-link-400k-route failed");
    });
    const auto lock_metrics = GetTransactionMetrics();
    Check(lock_metrics.route_point_sequences.logical_bytes == 0 &&
              lock_metrics.presentation_indexes.logical_bytes == 0,
          "lock-link-400k-route copied route storage or owner indexes");
    CheckTiming("lock-link-400k-route", lock_large_route, 20.0, 50.0);
    PrintMutation("lock-link-400k-route", LargeRouteSize, lock_large_route);
    if (fixture.presentation.FindLink(routed_link)->Style().locked) {
        Execute(fixture,
                std::make_unique<SetLinkLockedCommand>(routed_link, false),
                "large route unlock failed");
    }

    fixture.commands.Clear();
    ResetTransactionMetrics();
    const Distribution router_large_route = MeasureDistribution(LargeRouteSamples, [&] {
        toggle = !toggle;
        Execute(fixture,
                std::make_unique<SetLinkRouterCommand>(
                    routed_link,
                    toggle ? StraightLinkRouterType() : OrthogonalLinkRouterType()),
                "router-change-400k-route failed");
    });
    const auto router_metrics = GetTransactionMetrics();
    Check(router_metrics.route_point_sequences.logical_bytes == 0 &&
              router_metrics.presentation_indexes.logical_bytes == 0,
          "router-change-400k-route copied route storage or owner indexes");
    CheckTiming("router-change-400k-route", router_large_route, 20.0, 50.0);
    PrintMutation("router-change-400k-route", LargeRouteSize, router_large_route);

    fixture.commands.Clear();
    ResetTransactionMetrics();
    const Distribution move_large_route = MeasureDistribution(LargeRouteSamples, [&] {
        toggle = !toggle;
        Execute(fixture,
                std::make_unique<MoveRoutePointCommand>(
                    routed_link,
                    moved_route_point,
                    toggle ? Vec2{200'003.0f, 40.0f} : Vec2{200'007.0f, 44.0f}),
                "move-one-point-400k-route failed");
    });
    const auto move_large_metrics = GetTransactionMetrics();
    Check(move_large_metrics.route_point_sequences.root_clones == LargeRouteSamples &&
              move_large_metrics.route_point_sequences.value_clones == LargeRouteSamples &&
              move_large_metrics.route_point_sequences.shard_clones == 0 &&
              move_large_metrics.route_point_sequences.logical_bytes <
                  LargeRouteSamples * 128U * 1024U &&
              move_large_metrics.presentation_indexes.logical_bytes == 0,
          "move-one-point-400k-route exceeded its exact root/chunk structural bound");
    CheckTiming("move-one-point-400k-route", move_large_route, 30.0, 70.0);
    PrintMutation("move-one-point-400k-route", LargeRouteSize, move_large_route);

    fixture.commands.Clear();
    ResetTransactionMetrics();
    const Distribution insert_remove_large_route = MeasureDistribution(LargeRouteSamples, [&] {
        const RoutePointId point = fixture.presentation.AllocateRoutePointId();
        Execute(fixture,
                std::make_unique<InsertRoutePointCommand>(
                    routed_link,
                    RoutePoint{point, {200'000.0f, 80.0f}},
                    LargeRouteSize / 2),
                "insert-point-400k-route failed");
        Execute(fixture,
                std::make_unique<RemoveRoutePointsCommand>(
                    std::vector<RoutePointRef>{{routed_link, point}}),
                "remove-point-400k-route failed");
        Check(fixture.presentation.RoutePointOwner(point) == LinkId{},
              "insert/remove 400k route left a stale owner index entry");
    });
    const auto insert_remove_metrics = GetTransactionMetrics();
    Check(insert_remove_metrics.route_point_sequences.logical_bytes != 0 &&
               insert_remove_metrics.route_point_sequences.logical_bytes <
                   LargeRouteSamples * 4U * 1024U * 1024U &&
               insert_remove_metrics.presentation_indexes.logical_bytes != 0 &&
               insert_remove_metrics.route_chunk_merges == LargeRouteSamples &&
               fixture.presentation.FindLink(routed_link)->Route()
                       .StorageStatistics().mergeable_adjacent_pairs == 0,
           "insert/remove 400k route did not use bounded route and owner-index deltas");
    CheckTiming("insert-remove-point-400k-route", insert_remove_large_route, 60.0, 140.0);
    PrintMutation("insert-remove-point-400k-route", LargeRouteSize, insert_remove_large_route);

    fixture.commands.Clear();
    fixture.commands.SetHistoryLimit(4);
    auto alternate_route = large_route.WithMovedPoint(moved_route_point, {200'100.0f, 55.0f});
    Check(alternate_route.has_value(), "alternate persistent route root creation failed");
    Execute(fixture,
            std::make_unique<SetLinkRoutePointsCommand>(routed_link, std::move(*alternate_route)),
            "route history fixture failed");
    ResetTransactionMetrics();
    const Distribution route_history = MeasureDistribution(5, [&] {
        auto undone = fixture.commands.Undo(fixture.document, fixture.presentation, fixture.node_types);
        if (!undone) Fail("400k route-only undo failed: " + undone.error().message);
        auto redone = fixture.commands.Redo(
            fixture.document, fixture.presentation, fixture.types);
        if (!redone) Fail("400k route-only redo failed: " + redone.error().message);
    });
    const auto route_history_metrics = GetTransactionMetrics();
    Check(route_history_metrics.route_point_sequences.logical_bytes == 0 &&
              route_history_metrics.presentation_indexes.logical_bytes == 0,
          "400k route-only undo/redo copied retained roots or rewrote unchanged owner IDs");
    CheckTiming("route-only-undo-redo-400k", route_history, 500.0, 1'200.0);
    PrintMutation("route-only-undo-redo-400k", LargeRouteSize, route_history);

    constexpr std::size_t AdversarialChunks = 64;
    std::vector<RoutePoint> adversarial_points;
    adversarial_points.reserve(AdversarialChunks * PersistentRoutePointSequence::ChunkCapacity);
    for (std::size_t index = 0;
         index < AdversarialChunks * PersistentRoutePointSequence::ChunkCapacity;
         ++index) {
        adversarial_points.push_back(RoutePoint{
            fixture.presentation.AllocateRoutePointId(),
            Vec2{static_cast<float>(index), static_cast<float>(index % 23)},
        });
    }
    Execute(fixture,
            std::make_unique<SetLinkRoutePointsCommand>(routed_link, adversarial_points),
            "adversarial route fixture failed");
    for (std::size_t chunk = 0; chunk < AdversarialChunks; ++chunk) {
        Execute(fixture,
                std::make_unique<InsertRoutePointCommand>(
                    routed_link,
                    RoutePoint{
                        fixture.presentation.AllocateRoutePointId(),
                        Vec2{-1.0f, static_cast<float>(chunk)},
                    },
                    chunk * (PersistentRoutePointSequence::ChunkCapacity + 1) +
                        PersistentRoutePointSequence::ChunkCapacity / 2),
                "adversarial route split failed");
    }
    std::unordered_set<RoutePointId, IdHash> adversarial_survivors;
    for (std::size_t chunk = 0; chunk < AdversarialChunks; ++chunk) {
        adversarial_survivors.insert(
            adversarial_points[chunk * PersistentRoutePointSequence::ChunkCapacity].id);
        adversarial_survivors.insert(
            adversarial_points[(chunk + 1) * PersistentRoutePointSequence::ChunkCapacity - 1].id);
    }
    std::vector<RoutePointRef> adversarial_removals;
    for (const RoutePoint& point : fixture.presentation.FindLink(routed_link)->Route()) {
        if (!adversarial_survivors.contains(point.id)) {
            adversarial_removals.push_back(RoutePointRef{routed_link, point.id});
        }
    }
    fixture.commands.Clear();
    ResetTransactionMetrics();
    constexpr std::size_t CompactionSamples = 5;
    const Distribution compact_adversarial_route = MeasureDistribution(CompactionSamples, [&] {
        Execute(fixture,
                std::make_unique<RemoveRoutePointsCommand>(adversarial_removals),
                "measured adversarial route compaction failed");
        const auto undone = fixture.commands.Undo(
            fixture.document, fixture.presentation, fixture.node_types);
        if (!undone) Fail("adversarial route compaction undo failed: " + undone.error().message);
    });
    const auto compaction_metrics = GetTransactionMetrics();
    Check(compaction_metrics.route_chunk_merges ==
              CompactionSamples * (AdversarialChunks * 2 - 1) &&
              compaction_metrics.route_point_sequences.value_clones == CompactionSamples &&
              compaction_metrics.route_point_sequences.logical_bytes <
                  CompactionSamples * 2U * 1024U * 1024U,
          "adversarial route compaction exceeded its exact chunk/index bound");
    CheckTiming("compact-adversarial-route", compact_adversarial_route, 60.0, 120.0);
    PrintMutation(
        "compact-adversarial-route",
        AdversarialChunks * PersistentRoutePointSequence::ChunkCapacity,
        compact_adversarial_route);
    fixture.commands.Clear();
    Execute(fixture,
            std::make_unique<RemoveRoutePointsCommand>(adversarial_removals),
            "adversarial route compaction failed");
    const auto compacted_statistics =
        fixture.presentation.FindLink(routed_link)->Route().StorageStatistics();
    Check(compacted_statistics.point_count == adversarial_survivors.size() &&
              compacted_statistics.chunk_count == 1 &&
              compacted_statistics.mergeable_adjacent_pairs == 0,
          "adversarial route did not compact to the bounded representation");
    const RoutePointId compacted_move_point = *adversarial_survivors.begin();
    fixture.commands.Clear();
    ResetTransactionMetrics();
    const Distribution move_compacted_route = MeasureDistribution(MutationSamples, [&] {
        toggle = !toggle;
        Execute(fixture,
                std::make_unique<MoveRoutePointCommand>(
                    routed_link,
                    compacted_move_point,
                    toggle ? Vec2{5.0f, 7.0f} : Vec2{11.0f, 13.0f}),
                "move-after-adversarial-route-compaction failed");
    });
    const auto move_compacted_metrics = GetTransactionMetrics();
    Check(move_compacted_metrics.route_point_sequences.root_clones == MutationSamples &&
              move_compacted_metrics.route_point_sequences.value_clones == MutationSamples &&
              move_compacted_metrics.route_point_sequences.shard_clones == 0 &&
              move_compacted_metrics.route_point_sequences.logical_bytes <
                  MutationSamples * 32U * 1024U &&
              move_compacted_metrics.presentation_indexes.logical_bytes == 0,
          "move-after-adversarial-route-compaction retained historical fragmentation cost");
    CheckTiming("move-after-adversarial-route-compaction", move_compacted_route, 5.0, 12.0);
    PrintMutation(
        "move-after-adversarial-route-compaction",
        adversarial_survivors.size(),
        move_compacted_route);

    ResetTransactionMetrics();
    const Distribution add_remove_link = MeasureDistribution(MutationSamples, [&] {
        const LinkId link = fixture.document.AllocateLinkId();
        Execute(fixture,
                std::make_unique<ConnectPinsCommand>(
                    fixture.graph,
                    Link{.id = link, .output = fixture.outputs.front(), .input = fixture.inputs[2]}),
                "add-link-100k failed");
        Execute(fixture,
                std::make_unique<DeleteElementsCommand>(fixture.graph, std::vector<NodeId>{}, std::vector<LinkId>{link}),
                "remove-link-100k failed");
    });
    CheckIncrementalOnly("add-remove-link-100k", MutationSamples * 2);
    CheckTiming("add-remove-link-100k", add_remove_link, 10.0, 24.0);
    PrintMutation("add-remove-link-100k", fixture.nodes.size(), add_remove_link);

    auto star = MakeFixture(FixtureConfig{
        .node_count = 20'000,
        .links = LinkPattern::Star,
        .columns = 250,
    });
    ResetTransactionMetrics();
    const Distribution star_link = MeasureDistribution(MutationSamples, [&] {
        const LinkId link = star.document.AllocateLinkId();
        Execute(star,
                std::make_unique<ConnectPinsCommand>(
                    star.graph,
                    Link{.id = link, .output = star.outputs.front(), .input = star.inputs.front()}),
                "star-pin add-link failed");
        Execute(star,
                std::make_unique<DeleteElementsCommand>(
                    star.graph, std::vector<NodeId>{}, std::vector<LinkId>{link}),
                "star-pin remove-link failed");
    });
    const auto star_metrics = GetTransactionMetrics();
    CheckIncrementalOnly("star-pin-add-remove-20k", MutationSamples * 2);
    Check(star_metrics.semantic_indexes.value_clones != 0 &&
              star_metrics.semantic_indexes.copied_handles <= MutationSamples * 2 * 512,
          "star-pin reverse index updates exceeded the persistent path-copy bound");
    CheckTiming("star-pin-add-remove-20k", star_link, 10.0, 24.0);
    PrintMutation("star-pin-add-remove-20k", star.document.IncidentLinks(star.outputs.front()).size(), star_link);

    const LinkId reconnect = fixture.links.front();
    ResetTransactionMetrics();
    const Distribution reconnect_link = MeasureDistribution(MutationSamples, [&] {
        const PinId input = toggle ? fixture.inputs[1] : fixture.inputs[2];
        toggle = !toggle;
        Execute(fixture,
                std::make_unique<ReconnectLinkCommand>(
                    fixture.graph, reconnect, fixture.outputs.front(), input, true),
                "reconnect-link-100k failed");
    });
    CheckIncrementalOnly("reconnect-link-100k", MutationSamples);
    CheckTiming("reconnect-link-100k", reconnect_link, 7.0, 16.0);
    PrintMutation("reconnect-link-100k", fixture.nodes.size(), reconnect_link);

    const GroupId group = fixture.presentation.AllocateGroupId();
    std::vector<NodeId> members(fixture.nodes.begin(), fixture.nodes.end());
    constexpr std::size_t LargeGroupBodySize = 16U * 1024U * 1024U;
    const GroupStyleHandle large_group_style = MakeGroupStyle(GroupStyle{
        .title = "Large benchmark group",
        .body = std::string(LargeGroupBodySize, 'x'),
    });
    Execute(fixture,
            std::make_unique<AddGroupCommand>(GroupPresentation{
                .id = group,
                .graph = fixture.graph,
                .style = large_group_style,
                .members = members,
            }),
            "Large group fixture failed");
    const auto check_large_group_shell = [&](const std::string_view scenario,
                                             const TransactionMetrics& metrics,
                                             const std::uint64_t operations) {
        Check(fixture.presentation.FindGroup(group)->style == large_group_style,
              std::string{scenario} + " replaced the immutable style generation");
        Check(metrics.group_styles.logical_bytes == 0,
              std::string{scenario} + " copied the 16 MiB group style");
        Check(metrics.groups.logical_bytes < operations * 16U * 1024U,
              std::string{scenario} + " exceeded the bounded outer group shell path-copy budget");
    };
    ResetTransactionMetrics();
    const Distribution collapse_group = MeasureDistribution(MutationSamples, [&] {
        toggle = !toggle;
        Execute(fixture,
                std::make_unique<SetGroupCollapsedCommand>(group, toggle),
                "collapse-group-large failed");
    });
    const auto collapse_metrics = GetTransactionMetrics();
    CheckIncrementalOnly("collapse-group-large", MutationSamples);
    Check(collapse_metrics.group_memberships.logical_bytes == 0 &&
               collapse_metrics.presentation_indexes.logical_bytes == 0,
           "collapse-group-100k-members copied or revalidated membership state");
    check_large_group_shell("collapse-group-large-body", collapse_metrics, MutationSamples);
    CheckTiming("collapse-group-large", collapse_group, 8.0, 20.0);
    PrintMutation("collapse-group-large", members.size(), collapse_group);

    ResetTransactionMetrics();
    const Distribution move_group = MeasureDistribution(MutationSamples, [&] {
        const Vec2 position = toggle ? Vec2{40.0f, 40.0f} : Vec2{60.0f, 60.0f};
        toggle = !toggle;
        Execute(fixture,
                std::make_unique<MoveGroupCommand>(group, position),
                "move-group-100k-members failed");
    });
    const auto move_group_metrics = GetTransactionMetrics();
    CheckIncrementalOnly("move-group-100k-members", MutationSamples);
    Check(move_group_metrics.group_memberships.logical_bytes == 0 &&
               move_group_metrics.presentation_indexes.logical_bytes == 0,
           "move-group-100k-members copied or reindexed membership state");
    check_large_group_shell("move-group-large-body", move_group_metrics, MutationSamples);
    CheckTiming("move-group-100k-members", move_group, 8.0, 20.0);
    PrintMutation("move-group-100k-members", members.size(), move_group);

    ResetTransactionMetrics();
    const Distribution resize_group = MeasureDistribution(MutationSamples, [&] {
        const Vec2 size = toggle ? Vec2{420.0f, 240.0f} : Vec2{460.0f, 280.0f};
        toggle = !toggle;
        Execute(fixture,
                std::make_unique<ResizeGroupCommand>(group, size),
                "resize-group-100k-members failed");
    });
    const auto resize_group_metrics = GetTransactionMetrics();
    CheckIncrementalOnly("resize-group-100k-members", MutationSamples);
    Check(resize_group_metrics.group_memberships.logical_bytes == 0 &&
               resize_group_metrics.presentation_indexes.logical_bytes == 0,
           "resize-group-100k-members copied or reindexed membership state");
    check_large_group_shell("resize-group-large-body", resize_group_metrics, MutationSamples);
    CheckTiming("resize-group-100k-members", resize_group, 8.0, 20.0);
    PrintMutation("resize-group-100k-members", members.size(), resize_group);

    ResetTransactionMetrics();
    const Distribution z_order_group = MeasureDistribution(MutationSamples, [&] {
        toggle = !toggle;
        Execute(fixture,
                std::make_unique<SetGroupZOrderCommand>(group, toggle ? 10U : 20U),
                "z-order-group-large-body failed");
    });
    const auto z_order_group_metrics = GetTransactionMetrics();
    Check(z_order_group_metrics.group_memberships.logical_bytes == 0 &&
              z_order_group_metrics.presentation_indexes.logical_bytes == 0,
          "z-order-group-large-body copied or reindexed membership state");
    check_large_group_shell("z-order-group-large-body", z_order_group_metrics, MutationSamples);
    CheckTiming("z-order-group-large-body", z_order_group, 8.0, 20.0);
    PrintMutation("z-order-group-large-body", LargeGroupBodySize, z_order_group);

    fixture.commands.Clear();
    ResetTransactionMetrics();
    const Distribution lock_group = MeasureDistribution(MutationSamples, [&] {
        toggle = !toggle;
        Execute(fixture,
                std::make_unique<SetGroupLockedCommand>(group, toggle),
                "lock-group-large-body failed");
    });
    const auto lock_group_metrics = GetTransactionMetrics();
    Check(lock_group_metrics.group_memberships.logical_bytes == 0 &&
              lock_group_metrics.presentation_indexes.logical_bytes == 0,
          "lock-group-large-body copied or reindexed membership state");
    check_large_group_shell("lock-group-large-body", lock_group_metrics, MutationSamples);
    CheckTiming("lock-group-large-body", lock_group, 8.0, 20.0);
    PrintMutation("lock-group-large-body", LargeGroupBodySize, lock_group);
    if (fixture.presentation.FindGroup(group)->protection.locked) {
        Execute(fixture,
                std::make_unique<SetGroupLockedCommand>(group, false),
                "large group unlock failed");
    }

    ResetTransactionMetrics();
    const NodeId toggled_member = fixture.nodes.back();
    const Distribution membership_delta = MeasureDistribution(MutationSamples, [&] {
        Execute(fixture,
                std::make_unique<ChangeGroupMembersCommand>(
                    group,
                    std::vector<NodeId>{},
                    std::vector<NodeId>{toggled_member}),
                "remove-member-100k-group failed");
        Execute(fixture,
                std::make_unique<ChangeGroupMembersCommand>(
                    group,
                    std::vector<NodeId>{toggled_member},
                    std::vector<NodeId>{}),
                "add-member-100k-group failed");
    });
    const auto membership_delta_metrics = GetTransactionMetrics();
    CheckIncrementalOnly("single-member-delta-100k-group", MutationSamples * 2);
    Check(membership_delta_metrics.group_memberships.value_clones != 0 &&
               membership_delta_metrics.group_memberships.copied_handles <= MutationSamples * 2 * 128,
           "single-member-delta-100k-group exceeded persistent path-copy bounds");
    check_large_group_shell(
        "single-member-delta-large-body", membership_delta_metrics, MutationSamples * 2);
    CheckTiming("single-member-delta-100k-group", membership_delta, 10.0, 24.0);
    PrintMutation("single-member-delta-100k-group", members.size(), membership_delta);

    fixture.commands.Clear();
    ResetTransactionMetrics();
    const GroupStyleHandle replacement_style = MakeGroupStyle(GroupStyle{
        .title = "Replacement benchmark group",
        .body = std::string(LargeGroupBodySize, 'y'),
        .kind = GroupKind::Comment,
    });
    Execute(fixture,
            std::make_unique<SetGroupStyleCommand>(group, replacement_style),
            "set-style-large-body failed");
    const auto style_set_metrics = GetTransactionMetrics();
    Check(fixture.presentation.FindGroup(group)->style == replacement_style &&
              style_set_metrics.group_styles.value_clones == 1 &&
              style_set_metrics.group_styles.logical_bytes >= LargeGroupBodySize &&
              style_set_metrics.group_styles.logical_bytes < LargeGroupBodySize + 1024U,
          "set-style-large-body did not allocate and retain exactly one immutable generation");

    ResetTransactionMetrics();
    const Distribution style_history = MeasureDistribution(5, [&] {
        auto undone = fixture.commands.Undo(fixture.document, fixture.presentation, fixture.node_types);
        if (!undone) Fail("large group style undo failed: " + undone.error().message);
        Check(fixture.presentation.FindGroup(group)->style == large_group_style,
              "large group style undo did not restore the retained original generation");
        auto redone = fixture.commands.Redo(
            fixture.document, fixture.presentation, fixture.types);
        if (!redone) Fail("large group style redo failed: " + redone.error().message);
        Check(fixture.presentation.FindGroup(group)->style == replacement_style,
              "large group style redo did not restore the retained replacement generation");
    });
    const auto style_history_metrics = GetTransactionMetrics();
    Check(style_history_metrics.group_styles.logical_bytes == 0,
          "large group style undo/redo repeatedly copied retained strings");
    CheckTiming("style-undo-redo-large-body", style_history, 20.0, 50.0);
    PrintMutation("style-undo-redo-large-body", LargeGroupBodySize, style_history);

    std::vector<std::unique_ptr<Command>> shared_member_groups;
    shared_member_groups.reserve(5'000);
    for (std::size_t index = 0; index < 5'000; ++index) {
        shared_member_groups.push_back(std::make_unique<AddGroupCommand>(GroupPresentation{
            .id = fixture.presentation.AllocateGroupId(),
            .graph = fixture.graph,
            .members = {fixture.nodes.back()},
        }));
    }
    Execute(fixture,
            std::make_unique<CompoundCommand>("High-membership group fixture", std::move(shared_member_groups)),
            "High-membership group fixture failed");
    ResetTransactionMetrics();
    const Distribution group_membership = MeasureDistribution(MutationSamples, [&] {
        const GroupId added = fixture.presentation.AllocateGroupId();
        Execute(fixture,
                std::make_unique<AddGroupCommand>(GroupPresentation{
                    .id = added,
                    .graph = fixture.graph,
                    .members = {fixture.nodes.back()},
                }),
                "high-membership add-group failed");
        Execute(fixture,
                std::make_unique<RemoveGroupCommand>(added),
                "high-membership remove-group failed");
    });
    const auto membership_metrics = GetTransactionMetrics();
    CheckIncrementalOnly("group-membership-add-remove-5k", MutationSamples * 2);
    Check(membership_metrics.presentation_indexes.value_clones != 0 &&
              membership_metrics.presentation_indexes.copied_handles <= MutationSamples * 2 * 512,
          "group membership reverse index updates exceeded the persistent path-copy bound");
    CheckTiming("group-membership-add-remove-5k", group_membership, 10.0, 24.0);
    PrintMutation(
        "group-membership-add-remove-5k",
        fixture.presentation.GroupsForNode(fixture.nodes.back()).size(),
        group_membership);

    ResetTransactionMetrics();
    const Distribution add_remove_node = MeasureDistribution(MutationSamples, [&] {
        const NodeId node = fixture.document.AllocateNodeId();
        Execute(fixture,
                std::make_unique<AddNodeCommand>(
                    fixture.graph,
                    NodeCreation{.node = NodeInstance{.id = node, .type = TypeId{"benchmark"}}}),
                "add-node-100k failed");
        Execute(fixture,
                std::make_unique<DeleteElementsCommand>(
                    fixture.graph, std::vector<NodeId>{node}, std::vector<LinkId>{}),
                "remove-node-100k failed");
    });
    CheckIncrementalOnly("add-remove-node-100k", MutationSamples * 4);
    CheckTiming("add-remove-node-100k", add_remove_node, 10.0, 24.0);
    PrintMutation("add-remove-node-100k", fixture.nodes.size(), add_remove_node);

    const GraphId hierarchy_target = fixture.document.AllocateGraphId();
    Execute(fixture,
            std::make_unique<AddGraphCommand>(Graph{
                .id = hierarchy_target,
                .display_name = "Hierarchy benchmark target",
            }),
            "Hierarchy target graph fixture failed");
    const NodeId hierarchy_node = fixture.nodes[fixture.nodes.size() / 2];
    const SubgraphReference hierarchy_reference{
        .ownership = SubgraphOwnership::Referenced,
        .target = DocumentGraphTarget{hierarchy_target},
    };
    GraphPolicy hierarchy_policy;
    hierarchy_policy.evaluate_operation = [](const OperationPolicyContext&, const OperationIntent&)
        -> OperationPolicyDecision { return AllowOperation{}; };
    bool bound = false;
    ResetTransactionMetrics();
    const Distribution set_subgraph = MeasureDistribution(MutationSamples, [&] {
        bound = !bound;
        auto result = fixture.commands.Execute(
            std::make_unique<SetNodeSubgraphCommand>(
                fixture.graph,
                hierarchy_node,
                bound ? std::optional<SubgraphReference>{hierarchy_reference} : std::nullopt),
            fixture.document,
            fixture.presentation,
            fixture.types,
            hierarchy_policy);
        if (!result) Fail("set-node-subgraph-100k failed: " + result.error().message);
        Check(result->model_changed, "set-node-subgraph-100k became a no-op");
    });
    CheckIncrementalOnly("set-node-subgraph-100k", MutationSamples);
    CheckTiming("set-node-subgraph-100k", set_subgraph, 8.0, 20.0);
    PrintMutation("set-node-subgraph-100k", fixture.nodes.size(), set_subgraph);

    const GraphInterface initial_interface{
        .version = 2,
        .pins = {GraphInterfacePin{
            .key = "value",
            .label = "Value A",
            .type = TypeId{"float"},
            .direction = PinDirection::Input,
        }},
    };
    Execute(fixture,
            std::make_unique<SetGraphInterfaceCommand>(hierarchy_target, initial_interface),
            "Hierarchy interface fixture failed");
    Execute(fixture,
            std::make_unique<SetNodeSubgraphCommand>(fixture.graph, hierarchy_node, hierarchy_reference),
            "Hierarchy caller fixture failed");
    bool alternate_interface = false;
    ResetTransactionMetrics();
    const Distribution set_interface = MeasureDistribution(MutationSamples, [&] {
        alternate_interface = !alternate_interface;
        GraphInterface value = initial_interface;
        value.version = alternate_interface ? 3 : 4;
        value.pins.front().label = alternate_interface ? "Value B" : "Value A";
        auto result = fixture.commands.Execute(
            std::make_unique<SetGraphInterfaceCommand>(hierarchy_target, std::move(value)),
            fixture.document,
            fixture.presentation,
            fixture.types,
            hierarchy_policy);
        if (!result) Fail("set-graph-interface-100k failed: " + result.error().message);
        Check(result->model_changed, "set-graph-interface-100k became a no-op");
    });
    CheckIncrementalOnly("set-graph-interface-100k", MutationSamples * 2);
    CheckTiming("set-graph-interface-100k", set_interface, 10.0, 24.0);
    PrintMutation("set-graph-interface-100k", fixture.nodes.size(), set_interface);

    const GraphId caller_target = fixture.document.AllocateGraphId();
    Execute(fixture,
            std::make_unique<AddGraphCommand>(Graph{
                .id = caller_target,
                .display_name = "Caller fanout target",
            }),
            "Caller fanout target fixture failed");
    std::vector<NodeId> fanout_nodes;
    fanout_nodes.reserve(fixture.nodes.size() - 2);
    for (const NodeId node : fixture.nodes) {
        if (node != hierarchy_node && node != fixture.nodes.back()) fanout_nodes.push_back(node);
    }
    Execute(fixture,
            std::make_unique<BulkSubgraphBindingsCommand>(fixture.graph, caller_target, fanout_nodes),
            "Caller fanout fixture failed");
    const NodeId fanout_probe = fixture.nodes.back();
    const SubgraphReference caller_reference{
        .ownership = SubgraphOwnership::Referenced,
        .target = DocumentGraphTarget{caller_target},
    };
    bool caller_bound = false;
    ResetTransactionMetrics();
    const Distribution caller_fanout = MeasureDistribution(MutationSamples, [&] {
        caller_bound = !caller_bound;
        auto result = fixture.commands.Execute(
            std::make_unique<SetNodeSubgraphCommand>(
                fixture.graph,
                fanout_probe,
                caller_bound ? std::optional<SubgraphReference>{caller_reference} : std::nullopt),
            fixture.document,
            fixture.presentation,
            fixture.types,
            hierarchy_policy);
        if (!result) Fail("caller-fanout-set-subgraph failed: " + result.error().message);
        Check(result->model_changed, "caller-fanout-set-subgraph became a no-op");
    });
    const auto caller_metrics = GetTransactionMetrics();
    CheckIncrementalOnly("caller-fanout-set-subgraph-100k", MutationSamples);
    Check(caller_metrics.semantic_indexes.value_clones != 0 &&
               caller_metrics.semantic_indexes.copied_handles <= MutationSamples * 512,
           "caller reverse index updates exceeded the persistent path-copy bound");
    Check(caller_metrics.dependency_searches == 0 && caller_metrics.ownership_summary_lookups != 0,
          "existing-edge caller mutations must use dependency and ownership summary fast paths");
    CheckTiming("caller-fanout-set-subgraph-100k", caller_fanout, 8.0, 20.0);
    PrintMutation(
        "caller-fanout-set-subgraph-100k",
        fixture.document.SubgraphCallers(caller_target).size(),
        caller_fanout);

    const GraphId channel_source = fixture.document.AllocateGraphId();
    const GraphId channel_destination = fixture.document.AllocateGraphId();
    Graph source_graph{.id = channel_source, .display_name = "Channel source"};
    Graph destination_graph{.id = channel_destination, .display_name = "Channel destination"};
    constexpr std::size_t ChannelFanout = 5'000;
    std::vector<NodeId> source_nodes;
    std::vector<NodeId> destination_nodes;
    std::vector<PinId> source_pins;
    std::vector<PinId> destination_pins;
    source_nodes.reserve(ChannelFanout + 1);
    destination_nodes.reserve(ChannelFanout + 1);
    source_pins.reserve(ChannelFanout + 1);
    destination_pins.reserve(ChannelFanout + 1);
    for (std::size_t index = 0; index <= ChannelFanout; ++index) {
        const NodeId source_node = fixture.document.AllocateNodeId();
        const NodeId destination_node = fixture.document.AllocateNodeId();
        const PinId source_pin = fixture.document.AllocatePinId();
        const PinId destination_pin = fixture.document.AllocatePinId();
        source_nodes.push_back(source_node);
        destination_nodes.push_back(destination_node);
        source_pins.push_back(source_pin);
        destination_pins.push_back(destination_pin);
        source_graph.nodes.emplace(source_node, NodeInstance{
            .id = source_node,
            .type = TypeId{"benchmark.channel-source"},
            .pins = {source_pin},
            .role = NodeRole::IntergraphOutput,
        });
        source_graph.pins.emplace(source_pin, PinInstance{
            .id = source_pin,
            .node = source_node,
            .key = "channel",
            .type = TypeId{"float"},
            .direction = PinDirection::Input,
            .storage = PinStorage::Dynamic,
        });
        destination_graph.nodes.emplace(destination_node, NodeInstance{
            .id = destination_node,
            .type = TypeId{"benchmark.channel-destination"},
            .pins = {destination_pin},
            .role = NodeRole::IntergraphInput,
        });
        destination_graph.pins.emplace(destination_pin, PinInstance{
            .id = destination_pin,
            .node = destination_node,
            .key = "channel",
            .type = TypeId{"float"},
            .direction = PinDirection::Output,
            .storage = PinStorage::Dynamic,
        });
    }
    Execute(fixture, std::make_unique<AddGraphCommand>(std::move(source_graph)), "Channel source fixture failed");
    Execute(
        fixture,
        std::make_unique<AddGraphCommand>(std::move(destination_graph)),
        "Channel destination fixture failed");
    std::vector<IntergraphLink> channels;
    channels.reserve(ChannelFanout);
    for (std::size_t index = 0; index < ChannelFanout; ++index) {
        channels.push_back(IntergraphLink{
            .id = fixture.document.AllocateIntergraphLinkId(),
            .source = {channel_source, source_nodes[index], source_pins[index]},
            .destination = {channel_destination, destination_nodes[index], destination_pins[index]},
        });
    }
    Execute(
        fixture,
        std::make_unique<BulkIntergraphLinksCommand>(std::move(channels)),
        "Intergraph fanout fixture failed");
    ResetTransactionMetrics();
    const Distribution channel_fanout = MeasureDistribution(MutationSamples, [&] {
        const IntergraphLinkId link = fixture.document.AllocateIntergraphLinkId();
        Execute(fixture,
                std::make_unique<ConnectIntergraphCommand>(IntergraphLink{
                    .id = link,
                    .source = {channel_source, source_nodes.back(), source_pins.back()},
                    .destination = {
                        channel_destination, destination_nodes.back(), destination_pins.back()},
                }),
                "intergraph-fanout add failed");
        Execute(fixture,
                std::make_unique<DisconnectIntergraphCommand>(link),
                "intergraph-fanout remove failed");
    });
    const auto channel_metrics = GetTransactionMetrics();
    CheckIncrementalOnly("intergraph-fanout-add-remove-5k", MutationSamples * 2);
    Check(channel_metrics.intergraph_links.copied_handles <= MutationSamples * 2 * 512,
          "Intergraph link COW copied " + std::to_string(channel_metrics.intergraph_links.copied_handles) +
              " handles, exceeding the sharded bound");
    Check(channel_metrics.semantic_indexes.copied_handles <= MutationSamples * 2 * 1'024,
          "Intergraph indexes copied " + std::to_string(channel_metrics.semantic_indexes.copied_handles) +
               " handles, exceeding the persistent bound");
    Check(channel_metrics.dependency_searches == 0,
          "existing intergraph dependency edges must not trigger graph traversal");
    CheckTiming("intergraph-fanout-add-remove-5k", channel_fanout, 10.0, 24.0);
    PrintMutation(
        "intergraph-fanout-add-remove-5k",
        fixture.document.IntergraphLinksForGraph(channel_source).size(),
        channel_fanout);

    constexpr std::size_t DependencyDepth = 10'000;
    GraphDocument dependency_document;
    GraphPresentation dependency_presentation;
    RegistryCatalog dependency_types;
    CommandStack dependency_commands;
    dependency_commands.SetHistoryLimit(0);
    std::vector<GraphId> dependency_graphs;
    dependency_graphs.reserve(DependencyDepth);
    for (std::size_t index = 0; index < DependencyDepth; ++index) {
        dependency_graphs.push_back(dependency_document.AllocateGraphId());
    }
    const NodeId dependency_probe = dependency_document.AllocateNodeId();
    std::vector<Graph> dependency_chain;
    dependency_chain.reserve(DependencyDepth);
    for (std::size_t offset = 0; offset < DependencyDepth; ++offset) {
        const std::size_t index = DependencyDepth - 1 - offset;
        Graph graph{.id = dependency_graphs[index]};
        if (index + 1 < DependencyDepth) {
            const NodeId caller = dependency_document.AllocateNodeId();
            graph.nodes.emplace(caller, NodeInstance{
                .id = caller,
                .type = TypeId{"benchmark.dependency"},
                .subgraph = SubgraphReference{
                    .ownership = SubgraphOwnership::Referenced,
                    .target = DocumentGraphTarget{dependency_graphs[index + 1]},
                },
                .role = NodeRole::Subgraph,
            });
        } else {
            graph.nodes.emplace(dependency_probe, NodeInstance{
                .id = dependency_probe,
                .type = TypeId{"benchmark.dependency-probe"},
            });
        }
        dependency_chain.push_back(std::move(graph));
    }
    auto dependency_setup = dependency_commands.Execute(
        std::make_unique<BulkGraphsCommand>(std::move(dependency_chain)),
        dependency_document,
        dependency_presentation,
        dependency_types);
    if (!dependency_setup) Fail("Dependency chain fixture failed: " + dependency_setup.error().message);
    const SubgraphReference recursive_dependency{
        .ownership = SubgraphOwnership::Referenced,
        .target = DocumentGraphTarget{dependency_graphs.front()},
    };
    ResetTransactionMetrics();
    const Distribution dependency_cycle = MeasureDistribution(MutationSamples, [&] {
        auto rejected = dependency_commands.Execute(
            std::make_unique<SetNodeSubgraphCommand>(
                dependency_graphs.back(), dependency_probe, recursive_dependency),
            dependency_document,
            dependency_presentation,
            dependency_types);
        Check(!rejected && rejected.error().code == ErrorCode::InvalidGraph,
              "Dependency cycle probe must be rejected atomically");
    });
    const auto dependency_metrics = GetTransactionMetrics();
    Check(dependency_metrics.dependency_searches == MutationSamples &&
              dependency_metrics.dependency_vertices_visited >= MutationSamples * DependencyDepth,
          "Deep dependency cycle checks must report their bounded traversal work");
    CheckTiming("dependency-cycle-depth-10k", dependency_cycle, 8.0, 20.0);
    PrintMutation("dependency-cycle-depth-10k", DependencyDepth, dependency_cycle);

    GraphFragment fragment;
    fragment.nodes.reserve(1'000);
    for (std::size_t index = 1; index <= 1'000; ++index) {
        fragment.nodes.push_back(GraphFragmentNode{
            .creation = NodeCreation{
                .node = NodeInstance{.id = NodeId{index}, .type = TypeId{"benchmark"}},
            },
            .presentation = NodePresentation{.position = Vec2{static_cast<float>(index), 0.0f}},
        });
    }
    auto prepared = PrepareGraphFragmentPaste(
        fixture.document,
        fixture.presentation,
        fixture.types,
        fragment,
        fixture.graph,
        Vec2{500.0f, 500.0f});
    Check(prepared.has_value(), "large fragment preparation failed");

    fixture.commands.SetHistoryLimit(4);
    Execute(fixture,
            std::make_unique<PasteGraphFragmentCommand>(std::move(*prepared)),
            "Large fragment history fixture failed");
    ResetTransactionMetrics();
    const Distribution fragment_history = MeasureDistribution(10, [&] {
        auto undone = fixture.commands.Undo(fixture.document, fixture.presentation, fixture.node_types);
        Check(undone.has_value(), "large fragment undo failed");
        auto redone = fixture.commands.Redo(
            fixture.document, fixture.presentation, fixture.types);
        Check(redone.has_value(), "large fragment redo failed");
    });
    CheckIncrementalOnly("fragment-paste-1k-into-100k", 50'000);
    CheckTiming("fragment-paste-1k-into-100k", fragment_history, 50.0, 100.0);
    PrintMutation("fragment-paste-1k-into-100k", 1'000, fragment_history);

    fixture.commands.Clear();
    fixture.commands.SetHistoryLimit(4);
    Execute(fixture,
            std::make_unique<ResizeNodeCommand>(fixture.nodes.front(), Vec2{240.0f, 140.0f}),
            "Presentation history fixture failed");
    ResetTransactionMetrics();
    const Distribution presentation_history = MeasureDistribution(MutationSamples, [&] {
        auto undone = fixture.commands.Undo(fixture.document, fixture.presentation, fixture.node_types);
        Check(undone.has_value(), "presentation-only undo failed");
        auto redone = fixture.commands.Redo(
            fixture.document, fixture.presentation, fixture.types);
        Check(redone.has_value(), "presentation-only redo failed");
    });
    CheckIncrementalOnly("presentation-only-undo-redo", MutationSamples * 2);
    CheckTiming("presentation-only-undo-redo", presentation_history, 10.0, 24.0);
    PrintMutation("presentation-only-undo-redo", fixture.nodes.size(), presentation_history);

    auto empty_policy = MakeFixture(FixtureConfig{.node_count = 10'000});
    ResetTransactionMetrics();
    std::int64_t batch_value = 1;
    const Distribution empty_batch = MeasureDistribution(10, [&] {
        auto result = empty_policy.commands.Execute(
            PropertyBatch(empty_policy, batch_value++, 1'000),
            empty_policy.document,
            empty_policy.presentation,
            empty_policy.types);
        Check(result.has_value() && result->model_changed, "empty-policy batch failed or became a no-op");
    });
    const auto empty_metrics = GetTransactionMetrics();
    Check(empty_metrics.operation_intents == 0 && empty_metrics.command_paths == 0,
          "empty-policy batch built authorization payloads");
    PrintMutation("empty-policy-batch-1k", 1'000, empty_batch);

    auto allow_policy = MakeFixture(FixtureConfig{.node_count = 10'000});
    GraphPolicy allow;
    std::size_t leaf_callbacks = 0;
    allow.evaluate_operation = [&](const OperationPolicyContext&, const OperationIntent&)
        -> OperationPolicyDecision {
        ++leaf_callbacks;
        return AllowOperation{};
    };
    std::size_t batch_callbacks = 0;
    allow.evaluate_batch = [&](const BatchPolicyContext&, const std::span<const OperationIntent> batch)
        -> BatchPolicyDecision {
        ++batch_callbacks;
        Check(batch.size() == 1'000, "allow-policy batch callback received an incomplete batch");
        return AllowBatch{};
    };
    ResetTransactionMetrics();
    const Distribution allow_batch = MeasureDistribution(10, [&] {
        auto result = allow_policy.commands.Execute(
            PropertyBatch(allow_policy, batch_value++, 1'000),
            allow_policy.document,
            allow_policy.presentation,
            allow_policy.types,
            allow);
        Check(result.has_value() && result->model_changed, "allow-policy batch failed or became a no-op");
    });
    const auto allow_metrics = GetTransactionMetrics();
    Check(allow_metrics.operation_intents == 10'000 && leaf_callbacks == 10'000 && batch_callbacks == 10,
          "allow-policy batch did not invoke every leaf and batch callback");
    CheckTiming("empty-policy-batch-1k", empty_batch, 12.0, 25.0);
    CheckTiming("allow-policy-batch-1k", allow_batch, 18.0, 35.0);
    Check(allow_batch.p95_ms <= empty_batch.p95_ms * 3.0 + 0.5,
          "allow-only policy overhead exceeded the calibrated empty-policy ratio");
    PrintMutation("allow-policy-batch-1k", 1'000, allow_batch);

    CheckPeakResidentMemory("mutations", 640);
}

} // namespace Uni::GUI::Nodes::Benchmarks
