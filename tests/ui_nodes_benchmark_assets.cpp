#include "ui_nodes_benchmark_support.h"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Uni::GUI::Nodes::Benchmarks {
namespace {

constexpr std::size_t LeafSamples = 30;
constexpr std::size_t NoOpSamples = 50;
constexpr std::size_t CommonDependentCount = 1'000;
constexpr std::size_t CommonSamples = 20;
constexpr std::size_t MaximumChainDepth = 1'000;

template<typename Value> Value Require(Result<Value> result, const std::string_view context) {
    if (!result)
        Fail(std::string{context} + ": " + result.error().message);
    return std::move(*result);
}

void ExecuteAsset(GraphAsset& asset, std::unique_ptr<Command> command, const std::string_view context) {
    CommandStack commands;
    RegistryCatalog types;
    auto result = commands.Execute(std::move(command), asset.document, asset.presentation, types);
    if (!result)
        Fail(std::string{context} + ": " + result.error().message);
}

[[nodiscard]] GraphAsset MakeAsset(const GraphAssetId& id, const std::uint32_t schema_version = 1) {
    GraphAsset asset;
    asset.id = id;
    if (schema_version != 1) {
        ExecuteAsset(
            asset,
            std::make_unique<SetSchemaVersionCommand>(schema_version),
            "Asset schema setup failed");
    }
    return asset;
}

[[nodiscard]] GraphAsset MakeAssetWithInterface(const GraphAssetId& id, GraphInterface interface) {
    GraphAsset asset;
    asset.id = id;
    ExecuteAsset(
        asset,
        std::make_unique<SetGraphInterfaceCommand>(asset.document.RootGraph(), std::move(interface)),
        "Asset interface setup failed");
    return asset;
}

[[nodiscard]] GraphAsset MakeDependent(
    const GraphAssetId& id,
    const GraphAssetId& dependency,
    GraphInterface expected_interface = {}) {
    GraphAsset asset;
    asset.id = id;
    CommandStack commands;
    RegistryCatalog types;
    const NodeId node = asset.document.AllocateNodeId();
    NodeCreation creation;
    creation.node.id = node;
    creation.node.type = TypeId{"benchmark.asset.call"};
    auto added = commands.Execute(
        std::make_unique<AddNodeCommand>(asset.document.RootGraph(), std::move(creation)),
        asset.document,
        asset.presentation,
        types);
    if (!added)
        Fail("Asset dependency node setup failed: " + added.error().message);
    auto bound = commands.Execute(
        std::make_unique<SetNodeSubgraphCommand>(
            asset.document.RootGraph(),
            node,
            SubgraphReference{
                .ownership = SubgraphOwnership::Referenced,
                .target = GraphAssetTarget{
                    .asset = dependency,
                    .interface = std::move(expected_interface),
                },
            }),
        asset.document,
        asset.presentation,
        types);
    if (!bound)
        Fail("Asset dependency binding setup failed: " + bound.error().message);
    return asset;
}

[[nodiscard]] std::vector<GraphAsset> MakeContentReplacements(
    const GraphAssetId& id,
    const std::size_t count) {
    std::vector<GraphAsset> replacements;
    replacements.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        replacements.push_back(MakeAsset(id, index % 2 == 0 ? 2 : 3));
    }
    return replacements;
}

void PrintDistribution(
    const std::string_view scenario,
    const std::size_t scale,
    const std::size_t samples,
    const Distribution& distribution) {
    std::cout << "suite=graph-assets"
              << " scenario=" << scenario
              << " scale=" << scale
              << " samples=" << samples
              << " p50_ms=" << distribution.p50_ms
              << " p95_ms=" << distribution.p95_ms
              << " max_ms=" << distribution.max_ms << '\n';
}

void CheckUnchangedHandles(
    const GraphAssetRegistry& assets,
    const std::vector<GraphAssetId>& ids,
    const std::vector<GraphAssetRecordPtr>& records,
    const GraphAssetId& changed) {
    Check(ids.size() == records.size(), "Asset benchmark handle fixture is inconsistent");
    for (std::size_t index = 0; index < ids.size(); ++index) {
        if (ids[index] == changed)
            continue;
        Check(assets.Find(ids[index]) == records[index], "Replacement changed an unrelated record handle");
    }
}

void RunLeafScale(const std::size_t asset_count, const bool include_no_op) {
    GraphAssetRegistry assets;
    std::vector<GraphAssetId> ids;
    ids.reserve(asset_count);
    const double setup_ms = MeasureMilliseconds([&] {
        for (std::size_t index = 0; index < asset_count; ++index) {
            GraphAssetId id{"benchmark.leaf." + std::to_string(index)};
            auto written = Require(
                assets.Write(MakeAsset(id), GraphAssetWriteMode::Insert),
                "Leaf registry setup failed");
            Check(written.status == GraphAssetWriteStatus::Inserted, "Leaf registry setup did not insert");
            ids.push_back(std::move(id));
        }
    });

    std::vector<GraphAssetRecordPtr> records;
    records.reserve(ids.size());
    for (const auto& id : ids) {
        const auto record = assets.Find(id);
        Check(record != nullptr, "Leaf registry setup record is missing");
        records.push_back(record);
    }

    const GraphAssetId& target = ids.back();
    const GraphAssetGeneration initial_generation = records.back()->Generation();
    auto replacements = MakeContentReplacements(target, LeafSamples);
    std::size_t replacement_index = 0;
    const Distribution replace = MeasureDistribution(LeafSamples, [&] {
        auto written = Require(
            assets.Write(std::move(replacements[replacement_index++]), GraphAssetWriteMode::Replace),
            "Timed leaf replacement failed");
        Check(written.status == GraphAssetWriteStatus::Replaced &&
                  HasGraphAssetChange(written.changes, GraphAssetChangeFlags::Content),
              "Timed leaf replacement returned incorrect metadata");
    });
    const auto replaced = assets.Find(target);
    Check(replaced && replaced->Generation() == initial_generation + LeafSamples,
          "Leaf replacement generation is incorrect");
    CheckUnchangedHandles(assets, ids, records, target);
    CheckTiming(
        std::to_string(asset_count) + " asset leaf replacement",
        replace,
        asset_count == 100 ? 2.0 : 5.0,
        asset_count == 100 ? 5.0 : 15.0);
    std::cout << "suite=graph-assets scenario=leaf-setup scale=" << asset_count
              << " setup_ms=" << setup_ms << '\n';
    PrintDistribution("replace-leaf", asset_count, LeafSamples, replace);

    if (!include_no_op)
        return;

    const GraphAssetId& no_op_id = ids.front();
    const auto no_op_record = assets.Find(no_op_id);
    Check(no_op_record != nullptr, "No-op benchmark target is missing");
    const GraphAssetGeneration no_op_generation = no_op_record->Generation();
    std::uint64_t event_count = 0;
    auto subscription = assets.Subscribe([&](const GraphAssetChange&) { ++event_count; });
    std::vector<GraphAsset> no_ops;
    no_ops.reserve(NoOpSamples);
    for (std::size_t index = 0; index < NoOpSamples; ++index)
        no_ops.push_back(MakeAsset(no_op_id));
    std::size_t no_op_index = 0;
    const Distribution no_op = MeasureDistribution(NoOpSamples, [&] {
        auto written = Require(
            assets.Write(std::move(no_ops[no_op_index++]), GraphAssetWriteMode::Upsert),
            "Timed no-op upsert failed");
        Check(written.status == GraphAssetWriteStatus::Unchanged &&
                  written.changes == GraphAssetChangeFlags::None && written.record == no_op_record,
              "No-op upsert changed its immutable record");
    });
    Check(event_count == 0 && assets.Find(no_op_id) == no_op_record &&
              no_op_record->Generation() == no_op_generation,
          "No-op upsert emitted events or changed generation/identity");
    CheckTiming(std::to_string(asset_count) + " asset no-op upsert", no_op, 2.0, 5.0);
    PrintDistribution("no-op-upsert", asset_count, NoOpSamples, no_op);

    const auto before_failure = assets.Find(target);
    const auto before_failure_events = event_count;
    auto failed = assets.Write(
        MakeDependent(target, GraphAssetId{"benchmark.missing"}),
        GraphAssetWriteMode::Replace);
    Check(!failed && failed.error().code == ErrorCode::AssetNotFound &&
              assets.Find(target) == before_failure && event_count == before_failure_events,
          "Rejected leaf replacement was not atomic");
}

void RunCommonDependency() {
    GraphAssetRegistry assets;
    const GraphAssetId common_id{"benchmark.common"};
    Require(assets.Write(MakeAsset(common_id), GraphAssetWriteMode::Insert), "Common dependency setup failed");
    std::vector<GraphAssetId> dependent_ids;
    dependent_ids.reserve(CommonDependentCount);
    const double setup_ms = MeasureMilliseconds([&] {
        for (std::size_t index = 0; index < CommonDependentCount; ++index) {
            GraphAssetId id{"benchmark.common.user." + std::to_string(index)};
            Require(
                assets.Write(MakeDependent(id, common_id), GraphAssetWriteMode::Insert),
                "Common dependent setup failed");
            dependent_ids.push_back(std::move(id));
        }
    });
    std::vector<GraphAssetRecordPtr> dependent_records;
    dependent_records.reserve(dependent_ids.size());
    for (const auto& id : dependent_ids) {
        const auto record = assets.Find(id);
        Check(record != nullptr, "Common dependent record is missing");
        dependent_records.push_back(record);
    }

    std::uint64_t callback_count = 0;
    auto subscription = assets.Subscribe([&](const GraphAssetChange&) { ++callback_count; });
    auto replacements = MakeContentReplacements(common_id, CommonSamples);
    std::size_t replacement_index = 0;
    const Distribution replace = MeasureDistribution(CommonSamples, [&] {
        auto written = Require(
            assets.Write(std::move(replacements[replacement_index++]), GraphAssetWriteMode::Replace),
            "Timed common dependency replacement failed");
        Check(written.status == GraphAssetWriteStatus::Replaced,
              "Common dependency replacement returned incorrect status");
    });
    const std::uint64_t expected_callbacks =
        static_cast<std::uint64_t>(CommonSamples) * (CommonDependentCount + 1);
    Check(callback_count == expected_callbacks, "Common dependency callback count is incorrect");
    for (std::size_t index = 0; index < dependent_ids.size(); ++index) {
        Check(assets.Find(dependent_ids[index]) == dependent_records[index],
              "Common dependency replacement changed a dependent record handle");
    }
    CheckTiming("1k dependent common replacement", replace, 10.0, 30.0);
    std::cout << "suite=graph-assets scenario=common-setup dependents=" << CommonDependentCount
              << " setup_ms=" << setup_ms << '\n';
    PrintDistribution("replace-common", CommonDependentCount, CommonSamples, replace);

    const auto before_failure = assets.Find(common_id);
    const auto before_failure_callbacks = callback_count;
    GraphInterface incompatible;
    incompatible.version = 2;
    auto failed = assets.Write(
        MakeAssetWithInterface(common_id, incompatible),
        GraphAssetWriteMode::Replace);
    Check(!failed && failed.error().code == ErrorCode::InvalidGraph &&
              assets.Find(common_id) == before_failure && callback_count == before_failure_callbacks,
          "Incompatible common dependency replacement was not atomic");
    for (std::size_t index = 0; index < dependent_ids.size(); ++index) {
        Check(assets.Find(dependent_ids[index]) == dependent_records[index],
              "Failed common replacement changed a dependent record handle");
    }
}

void RunDependencyChains() {
    GraphAssetRegistry assets;
    std::vector<GraphAssetId> ids;
    ids.reserve(MaximumChainDepth + 1);
    const double setup_ms = MeasureMilliseconds([&] {
        ids.emplace_back("benchmark.chain.0");
        Require(assets.Write(MakeAsset(ids.back()), GraphAssetWriteMode::Insert), "Chain root setup failed");
        for (std::size_t depth = 1; depth <= MaximumChainDepth; ++depth) {
            ids.emplace_back("benchmark.chain." + std::to_string(depth));
            Require(
                assets.Write(MakeDependent(ids.back(), ids[depth - 1]), GraphAssetWriteMode::Insert),
                "Dependency chain setup failed");
        }
    });

    struct ChainGate final {
        std::size_t depth;
        std::size_t samples;
        double p50_limit_ms;
        double p95_limit_ms;
    };
    constexpr ChainGate gates[]{
        {10, 50, 1.0, 3.0},
        {100, 30, 2.0, 5.0},
        {1'000, 10, 5.0, 15.0},
    };
    for (const auto& gate : gates) {
        const Distribution closure = MeasureDistribution(gate.samples, [&] {
            auto result = Require(
                assets.DependencyClosure(ids[gate.depth]),
                "Timed dependency closure failed");
            Check(result.size() == gate.depth &&
                      std::ranges::find(result, ids.front()) != result.end(),
                  "Dependency closure returned an incomplete chain");
        });
        CheckTiming(
            std::to_string(gate.depth) + " deep dependency closure",
            closure,
            gate.p50_limit_ms,
            gate.p95_limit_ms);
        PrintDistribution("dependency-chain", gate.depth, gate.samples, closure);
    }
    Check(assets.ValidateAll().has_value(), "Deep dependency registry failed its full audit");

    std::uint64_t events = 0;
    auto subscription = assets.Subscribe([&](const GraphAssetChange&) { ++events; });
    const auto root_before = assets.Find(ids.front());
    auto cycle = assets.Write(
        MakeDependent(ids.front(), ids.back()),
        GraphAssetWriteMode::Replace);
    Check(!cycle && cycle.error().code == ErrorCode::InvalidGraph &&
              assets.Find(ids.front()) == root_before && events == 0,
          "Depth-1000 cycle replacement was not rejected atomically");
    std::cout << "suite=graph-assets scenario=chain-setup depth=" << MaximumChainDepth
              << " setup_ms=" << setup_ms << '\n';
}

void RunDispatchScale(const std::size_t subscriber_count, const std::size_t samples) {
    GraphAssetRegistry assets;
    const GraphAssetId id{"benchmark.dispatch"};
    Require(assets.Write(MakeAsset(id), GraphAssetWriteMode::Insert), "Dispatch fixture setup failed");
    std::uint64_t callback_count = 0;
    std::vector<GraphAssetSubscription> subscriptions;
    subscriptions.reserve(subscriber_count);
    for (std::size_t index = 0; index < subscriber_count; ++index) {
        subscriptions.push_back(assets.Subscribe([&](const GraphAssetChange&) { ++callback_count; }));
    }
    auto replacements = MakeContentReplacements(id, samples);
    std::size_t replacement_index = 0;
    const Distribution dispatch = MeasureDistribution(samples, [&] {
        auto written = Require(
            assets.Write(std::move(replacements[replacement_index++]), GraphAssetWriteMode::Replace),
            "Timed event dispatch replacement failed");
        Check(written.status == GraphAssetWriteStatus::Replaced,
              "Event dispatch replacement returned incorrect status");
    });
    Check(callback_count == static_cast<std::uint64_t>(subscriber_count) * samples,
          "Event dispatch callback count is incorrect");
    const double p50_limit = subscriber_count == 1'000 ? 5.0 : 2.0;
    const double p95_limit = subscriber_count == 1'000 ? 15.0 : 5.0;
    CheckTiming(
        std::to_string(subscriber_count) + " subscriber event dispatch",
        dispatch,
        p50_limit,
        p95_limit);
    PrintDistribution("event-dispatch", subscriber_count, samples, dispatch);
}

} // namespace

void RunGraphAssetsSuite() {
    RunLeafScale(100, false);
    RunLeafScale(1'000, false);
    RunLeafScale(10'000, true);
    RunCommonDependency();
    RunDependencyChains();
    RunDispatchScale(1, 50);
    RunDispatchScale(100, 30);
    RunDispatchScale(1'000, 20);
    std::cout << "suite=graph-assets max_registry_scale=10000" << '\n';
    CheckPeakResidentMemory("graph-assets", 64);
}

} // namespace Uni::GUI::Nodes::Benchmarks
