#include "ui_nodes_benchmark_support.h"

#include <imgui.h>

#include <cstdint>
#include <iostream>
#include <memory>
#include <string_view>

namespace Uni::GUI::Nodes::Benchmarks {
namespace {

constexpr std::size_t ColdSamples = 20;
constexpr std::size_t WarmSamples = 240;

class ImGuiScope final {
public:
    ImGuiScope() {
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = {900.0f, 700.0f};
        io.DeltaTime = 1.0f / 60.0f;
        unsigned char* pixels = nullptr;
        int width = 0;
        int height = 0;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        Check(pixels != nullptr, "Editor benchmark font atlas failed");
    }

    ~ImGuiScope() { ImGui::DestroyContext(); }
};

class EditorHarness final {
public:
    explicit EditorHarness(Fixture& fixture) : m_fixture(fixture) {
        m_config.show_minimap = false;
        m_config.show_breadcrumbs = false;
    }

    void DrawFrame() {
        ImGuiIO& io = ImGui::GetIO();
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({0.0f, 0.0f});
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("Nodes performance gate", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);
        (void)DrawEditor(m_editor, m_fixture.document, m_fixture.presentation, m_fixture.commands, m_fixture.node_types,
                         m_ui, m_routers, {800.0f, 600.0f}, {}, m_config);
        ImGui::End();
        ImGui::Render();
    }

    [[nodiscard]] Distribution MeasureColdFrames() {
        DrawFrame();
        return MeasureDistribution(ColdSamples, [&] {
            m_editor.InvalidateGeometry();
            m_editor.ResetMetrics();
            DrawFrame();
            const EditorMetrics metrics = m_editor.Metrics();
            Check(metrics.geometry_rebuilds == 1, "Cold frame did not rebuild geometry exactly once");
            Check(metrics.routed_links >= m_fixture.link_count, "Cold frame did not route the complete graph");
        });
    }

    [[nodiscard]] Distribution MeasureWarmFrames() {
        m_editor.ResetMetrics();
        Distribution result = MeasureDistribution(WarmSamples, [&] { DrawFrame(); });
        const EditorMetrics metrics = m_editor.Metrics();
        Check(metrics.geometry_rebuilds == 0 && metrics.routed_links == 0,
              "Warm frames rebuilt cached geometry or routes");
        Check(metrics.visible_nodes < m_fixture.nodes.size() * WarmSamples,
              "Warm frame spatial culling visited every graph node");
        return result;
    }

    void CheckRuntimePropertyCache() {
        m_editor.ResetMetrics();
        Execute(m_fixture,
                std::make_unique<SetNodePropertyCommand>(m_fixture.graph, m_fixture.nodes.front(), "slider",
                                                         PropertyValue{std::int64_t{999}}),
                "Runtime-only property update failed");
        DrawFrame();
        const EditorMetrics metrics = m_editor.Metrics();
        Check(metrics.geometry_rebuilds == 0 && metrics.routed_links == 0,
              "Runtime-only property update invalidated editor geometry");
    }

private:
    Fixture& m_fixture;
    EditorContext m_editor;
    NodeUiRegistry m_ui;
    LinkRouterRegistry m_routers;
    EditorConfig m_config;
};

void PrintEditorDistribution(const std::string_view suite, const Fixture& fixture, const Distribution& cold,
                             const Distribution& warm) {
    std::cout << "suite=" << suite << " nodes=" << fixture.nodes.size() << " links=" << fixture.link_count
              << " cold_p50_ms=" << cold.p50_ms << " cold_p95_ms=" << cold.p95_ms << " cold_max_ms=" << cold.max_ms
              << " warm_p50_ms=" << warm.p50_ms << " warm_p95_ms=" << warm.p95_ms << " warm_max_ms=" << warm.max_ms
              << '\n';
}

void RunEditorSuite(const std::string_view name, const FixtureConfig& config, const double cold_p50_limit,
                    const double cold_p95_limit, const double warm_p50_limit, const double warm_p95_limit,
                    const std::size_t memory_baseline_mib) {
    auto fixture = MakeFixture(config);
    ImGuiScope imgui;
    EditorHarness editor{fixture};
    const Distribution cold = editor.MeasureColdFrames();
    const Distribution warm = editor.MeasureWarmFrames();
    editor.CheckRuntimePropertyCache();
    CheckTiming(std::string{name} + " cold frame", cold, cold_p50_limit, cold_p95_limit);
    CheckTiming(std::string{name} + " warm frame", warm, warm_p50_limit, warm_p95_limit);
    PrintEditorDistribution(name, fixture, cold, warm);
    CheckPeakResidentMemory(name, memory_baseline_mib);
}

void CheckCowMapSemantics() {
    constexpr std::size_t EntityCount = 8'192;
    NodeMap ascending;
    NodeMap descending;
    ascending.reserve(EntityCount);
    descending.reserve(EntityCount);
    for (std::size_t value = 1; value <= EntityCount; ++value) {
        const NodeId id{value};
        Check(ascending.emplace(id, NodeInstance{.id = id, .type = TypeId{"benchmark"}}).second,
              "COW semantic gate could not insert ascending entity");
    }
    for (std::size_t value = EntityCount; value > 0; --value) {
        const NodeId id{value};
        Check(descending.emplace(id, NodeInstance{.id = id, .type = TypeId{"benchmark"}}).second,
              "COW semantic gate could not insert descending entity");
    }
    Check(ascending == descending, "COW equality depends on insertion or shard iteration order");

    NodeMap snapshot = ascending;
    const NodeId changed_id{EntityCount / 2};
    NodeInstance changed = ascending.at(changed_id);
    changed.display_name = "changed";
    const auto [changed_position, inserted] = ascending.insert_or_assign(changed_id, std::move(changed));
    Check(!inserted && changed_position->second.display_name == "changed" &&
              snapshot.at(changed_id).display_name.empty(),
          "COW replacement did not preserve snapshot isolation");

    std::size_t erased = 0;
    for (auto position = descending.begin(); position != descending.end();) {
        position = descending.erase(position);
        ++erased;
    }
    Check(erased == EntityCount && descending.empty(),
          "COW erase(iterator) did not advance correctly across shards");
}

} // namespace

void RunFrames10kSuite() {
    RunEditorSuite("frames-10k",
                   FixtureConfig{
                       .node_count = 10'000,
                       .links = LinkPattern::Chain,
                       .columns = 100,
                   },
                   500.0, 1'000.0, 8.0, 16.7, 64);
}

void RunProperty10kSuite() {
    constexpr std::size_t Samples = 200;
    auto fixture = MakeFixture(FixtureConfig{
        .node_count = 10'000,
        .links = LinkPattern::Chain,
        .columns = 100,
    });
    Execute(fixture,
            std::make_unique<SetNodePropertyCommand>(fixture.graph, fixture.nodes.front(), "slider",
                                                     PropertyValue{std::int64_t{-1}}),
            "Property benchmark warmup failed");
    ResetTransactionMetrics();

    std::int64_t value = 0;
    const Distribution properties = MeasureDistribution(Samples, [&] {
        Execute(fixture,
                std::make_unique<SetNodePropertyCommand>(fixture.graph, fixture.nodes.front(), "slider",
                                                         PropertyValue{value++}),
                "Property benchmark update failed");
    });
    const TransactionMetrics metrics = GetTransactionMetrics();
    Check(metrics.graphs.root_clones == Samples &&
               metrics.graphs.directory_clones == Samples &&
               metrics.graphs.shard_clones == Samples &&
               metrics.graphs.value_clones == Samples &&
               metrics.graphs.copied_handles / Samples <= 256 &&
               metrics.graph_revisions.root_clones == Samples &&
               metrics.graph_revisions.directory_clones == Samples &&
               metrics.graph_revisions.shard_clones == Samples &&
               metrics.node_maps.root_clones == Samples &&
               metrics.node_maps.directory_clones == Samples &&
               metrics.node_maps.shard_clones == Samples &&
               metrics.node_maps.page_clones == 0 &&
               metrics.node_maps.value_clones == Samples &&
               metrics.node_maps.copied_handles / Samples <= 256 &&
                metrics.pin_maps.logical_bytes == 0 &&
                metrics.link_maps.logical_bytes == 0 && metrics.intergraph_links.logical_bytes == 0 &&
                metrics.node_presentations.logical_bytes == 0 &&
                metrics.link_presentations.logical_bytes == 0 &&
                metrics.groups.logical_bytes == 0 && metrics.journal_entries == Samples,
           "Property gate copied unrelated state or produced an incomplete transaction journal");
    constexpr std::uint64_t LogicalBytesPerEditLimit = 16ULL * 1024ULL;
    Check(metrics.copied_logical_bytes / Samples <= LogicalBytesPerEditLimit,
          "Property gate exceeded its logical copied-byte budget");
    CheckTiming("10k runtime property edit", properties, 5.0, 12.0);
    std::cout << "suite=property-10k"
              << " samples=" << Samples << " p50_ms=" << properties.p50_ms << " p95_ms=" << properties.p95_ms
              << " max_ms=" << properties.max_ms
              << " copied_handles_per_edit=" << metrics.node_maps.copied_handles / Samples
              << " logical_bytes_per_edit=" << metrics.copied_logical_bytes / Samples
              << " shard_clones=" << metrics.node_maps.shard_clones << '\n';
    CheckPeakResidentMemory("property-10k", 128);
}

void RunProperty1mSuite() {
    constexpr std::size_t EntityCount = 1'000'000;
    constexpr std::size_t Samples = 50;
    CheckCowMapSemantics();

    NodeMap nodes;
    nodes.reserve(EntityCount);
    for (std::size_t value = 1; value <= EntityCount; ++value) {
        const NodeId id{value};
        const auto [position, inserted] = nodes.emplace(id, NodeInstance{
            .id = id,
            .type = TypeId{"benchmark"},
        });
        (void)position;
        Check(inserted, "1m property fixture contains a duplicate node ID");
    }

    const NodeId edited{1};
    {
        NodeMap baseline = nodes;
        NodeInstance changed = nodes.at(edited);
        changed.properties.insert_or_assign("slider", PropertyValue{std::int64_t{-1}});
        Check(!nodes.insert_or_assign(edited, std::move(changed)).second,
              "1m property benchmark warmup inserted its target node");
    }
    ResetTransactionMetrics();

    std::int64_t value = 0;
    const Distribution properties = MeasureDistribution(Samples, [&] {
        NodeMap baseline = nodes;
        NodeInstance changed = nodes.at(edited);
        changed.properties.insert_or_assign("slider", PropertyValue{value++});
        Check(!nodes.insert_or_assign(edited, std::move(changed)).second,
              "1m property benchmark update inserted its target node");
    });
    const TransactionMetrics metrics = GetTransactionMetrics();
    Check(metrics.graphs.logical_bytes == 0 && metrics.graph_revisions.logical_bytes == 0 &&
               metrics.node_maps.root_clones == Samples &&
               metrics.node_maps.directory_clones == Samples &&
               metrics.node_maps.shard_clones == Samples &&
               metrics.node_maps.page_clones == 0 &&
               metrics.node_maps.value_clones == Samples &&
               metrics.node_maps.copied_handles / Samples <= 512 &&
                metrics.pin_maps.logical_bytes == 0 && metrics.link_maps.logical_bytes == 0 &&
                metrics.intergraph_links.logical_bytes == 0 &&
                metrics.node_presentations.logical_bytes == 0 &&
                metrics.link_presentations.logical_bytes == 0 && metrics.groups.logical_bytes == 0 &&
                metrics.journal_entries == 0,
           "1m property gate exceeded bounded COW structure or copied unrelated state");
    constexpr std::uint64_t LogicalBytesPerEditLimit = 32ULL * 1024ULL;
    Check(metrics.copied_logical_bytes / Samples <= LogicalBytesPerEditLimit,
          "1m property gate exceeded its logical copied-byte budget");
    CheckTiming("1m runtime property edit", properties, 5.0, 12.0);
    std::cout << "suite=property-1m"
              << " entities=" << EntityCount << " samples=" << Samples
              << " p50_ms=" << properties.p50_ms << " p95_ms=" << properties.p95_ms
              << " max_ms=" << properties.max_ms
              << " copied_handles_per_edit=" << metrics.node_maps.copied_handles / Samples
              << " logical_bytes_per_edit=" << metrics.copied_logical_bytes / Samples
              << " shard_clones=" << metrics.node_maps.shard_clones << '\n';
    CheckPeakResidentMemory("property-1m", 320);
}

void RunSparse100kSuite() {
    RunEditorSuite("sparse-100k",
                   FixtureConfig{
                       .node_count = 100'000,
                       .links = LinkPattern::Chain,
                       .columns = 500,
                   },
                   4'000.0, 8'000.0, 8.0, 16.7, 480);
}

void RunDenseLinksSuite() {
    RunEditorSuite("dense-links",
                   FixtureConfig{
                       .node_count = 512,
                       .links = LinkPattern::Dense,
                       .dense_fanout = 128,
                       .columns = 32,
                   },
                   2'500.0, 5'000.0, 16.7, 25.0, 96);
}

} // namespace Uni::GUI::Nodes::Benchmarks
