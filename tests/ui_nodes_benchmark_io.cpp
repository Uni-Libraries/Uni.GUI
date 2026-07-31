#include "ui_nodes_benchmark_support.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace Uni::GUI::Nodes::Benchmarks {
namespace {

class TemporaryJsonFile final {
public:
    TemporaryJsonFile() {
        const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
        m_path =
            std::filesystem::temp_directory_path() / ("unigui-nodes-performance-" + std::to_string(suffix) + ".json");
    }

    ~TemporaryJsonFile() {
        std::error_code error;
        std::filesystem::remove(m_path, error);
    }

    [[nodiscard]] const std::filesystem::path& Path() const noexcept { return m_path; }

private:
    std::filesystem::path m_path;
};

template<typename Value> Value Require(Result<Value> result, const std::string_view context) {
    if (!result)
        Fail(std::string{context} + ": " + result.error().message);
    return std::move(*result);
}

void Require(Result<void> result, const std::string_view context) {
    if (!result)
        Fail(std::string{context} + ": " + result.error().message);
}

void CheckAbsoluteP95(const std::string_view name, const Distribution& distribution, const double limit_ms) {
    if (distribution.p95_ms <= limit_ms)
        return;
    Fail(std::string{name} + " exceeded its absolute p95 ceiling");
}

} // namespace

void RunIoSuite() {
    constexpr std::size_t MemorySamples = 20;
    constexpr std::size_t FileSamples = 10;
    auto fixture = MakeFixture(FixtureConfig{
        .node_count = 5'000,
        .links = LinkPattern::Chain,
        .columns = 100,
    });
    const std::string json =
        Require(SerializeGraphDocumentJson(fixture.document, fixture.presentation), "IO fixture serialization failed");
    auto round_trip =
        Require(DeserializeGraphDocumentJson(json, fixture.node_types), "IO fixture deserialization failed");
    const std::string canonical = Require(SerializeGraphDocumentJson(round_trip.document, round_trip.presentation),
                                          "IO round-trip serialization failed");
    Check(canonical == json, "IO benchmark fixture did not round-trip canonically");

    const Distribution serialize = MeasureDistribution(MemorySamples, [&] {
        auto result = SerializeGraphDocumentJson(fixture.document, fixture.presentation);
        if (!result || result->size() != json.size())
            Fail("Timed document serialization failed");
    });
    const Distribution deserialize = MeasureDistribution(MemorySamples, [&] {
        auto result = DeserializeGraphDocumentJson(json, fixture.node_types);
        if (!result || result->document.FindGraph(result->document.RootGraph()) == nullptr) {
            Fail("Timed document deserialization failed");
        }
    });

    TemporaryJsonFile file;
    Require(SaveGraphDocumentJson(file.Path().string(), fixture.document, fixture.presentation),
            "IO benchmark file initialization failed");
    const Distribution save = MeasureDistribution(FileSamples, [&] {
        Require(SaveGraphDocumentJson(file.Path().string(), fixture.document, fixture.presentation),
                "Timed document save failed");
    });
    const Distribution load = MeasureDistribution(FileSamples, [&] {
        auto result = LoadGraphDocumentJson(file.Path().string(), fixture.node_types);
        if (!result || result->document.FindGraph(result->document.RootGraph()) == nullptr) {
            Fail("Timed document load failed");
        }
    });

    CheckTiming("5k document serialization", serialize, 1'000.0, 2'000.0);
    CheckTiming("5k document deserialization", deserialize, 2'000.0, 4'000.0);
    CheckTiming("5k atomic document save", save, 2'000.0, 4'000.0);
    CheckTiming("5k document load", load, 2'500.0, 5'000.0);
    std::cout << "suite=io"
              << " nodes=" << fixture.nodes.size() << " links=" << fixture.link_count << " json_bytes=" << json.size()
              << " serialize_p50_ms=" << serialize.p50_ms << " serialize_p95_ms=" << serialize.p95_ms
              << " deserialize_p50_ms=" << deserialize.p50_ms << " deserialize_p95_ms=" << deserialize.p95_ms
              << " save_p50_ms=" << save.p50_ms << " save_p95_ms=" << save.p95_ms << " load_p50_ms=" << load.p50_ms
              << " load_p95_ms=" << load.p95_ms << '\n';
    CheckPeakResidentMemory("io", 96);
}

void RunMigrationsSuite() {
    constexpr std::size_t Samples = 20;
    auto fixture = MakeFixture(FixtureConfig{
        .node_count = 2'000,
        .links = LinkPattern::Chain,
        .columns = 100,
    });
    const std::string json = Require(SerializeGraphDocumentJson(fixture.document, fixture.presentation),
                                     "Migration fixture serialization failed");

    DocumentMigrationRegistry document_migrations{2};
    Require(document_migrations.Register(1,
                                         [](DocumentMigrationContext& context) -> Result<void> {
                                             for (Graph& graph : context.archive.graphs) {
                                                 std::vector<NodeId> nodes;
                                                 nodes.reserve(graph.nodes.size());
                                                 for (const auto& [id, node] : graph.nodes) {
                                                     (void)node;
                                                     nodes.push_back(id);
                                                 }
                                                 for (const NodeId id : nodes) {
                                                     NodeInstance node = graph.nodes.at(id);
                                                     node.properties["document_v2"] = true;
                                                     graph.nodes.insert_or_assign(id, std::move(node));
                                                 }
                                             }
                                             return {};
                                         }),
            "Document migration registration failed");

    RegistryCatalog current_nodes;
    Require(current_nodes.RegisterNodeType(BenchmarkNodeDescriptor(
                2,
                [](NodeMigrationContext& context) -> Result<void> {
                    const auto output = std::ranges::find_if(context.creation.pins, [](const PinInstance& pin) {
                        return pin.direction == PinDirection::Output;
                    });
                    if (output == context.creation.pins.end()) {
                        return std::unexpected(Error{ErrorCode::MigrationFailed, "Benchmark output pin is missing"});
                    }
                    const PinId previous = output->id;
                    const PinId replacement = context.allocate_pin_id();
                    if (!replacement) {
                        return std::unexpected(Error{ErrorCode::MigrationFailed, "Benchmark pin IDs are exhausted"});
                    }
                    output->id = replacement;
                    for (PinId& pin : context.creation.node.pins) {
                        if (pin == previous)
                            pin = replacement;
                    }
                    context.remap_links(previous, replacement);
                    context.creation.node.properties["node_v2"] = true;
                    return {};
                })),
            "Node migration descriptor registration failed");

    auto plain_warmup =
        Require(DeserializeGraphDocumentJson(json, fixture.node_types), "Plain migration baseline warmup failed");
    auto document_warmup = Require(DeserializeGraphDocumentJson(json, fixture.node_types, &document_migrations),
                                   "Document migration warmup failed");
    auto node_warmup = Require(DeserializeGraphDocumentJson(json, current_nodes), "Node migration warmup failed");
    Check(plain_warmup.document.SchemaVersion() == 1 && document_warmup.document.SchemaVersion() == 2 &&
              node_warmup.document.FindNode(node_warmup.document.RootGraph(), fixture.nodes.front()) != nullptr,
          "Migration benchmark warmup produced invalid documents");

    const Distribution plain = MeasureDistribution(Samples, [&] {
        auto result = DeserializeGraphDocumentJson(json, fixture.node_types);
        if (!result)
            Fail("Timed plain deserialization failed");
    });
    const Distribution document = MeasureDistribution(Samples, [&] {
        auto result = DeserializeGraphDocumentJson(json, fixture.node_types, &document_migrations);
        if (!result || result->document.SchemaVersion() != 2)
            Fail("Timed document migration failed");
    });
    const Distribution node = MeasureDistribution(Samples, [&] {
        auto result = DeserializeGraphDocumentJson(json, current_nodes);
        if (!result)
            Fail("Timed node migration failed");
    });

    CheckTiming("2k migration baseline", plain, 1'500.0, 3'000.0);
    CheckTiming("2k document migration", document, plain.p50_ms * 1.75 + 250.0, plain.p95_ms * 1.75 + 250.0);
    CheckTiming("2k node pin-remap migration", node, plain.p50_ms * 2.5 + 1'000.0, plain.p95_ms * 2.5 + 1'000.0);
    CheckAbsoluteP95("Document migration", document, 5'000.0);
    CheckAbsoluteP95("Node pin-remap migration", node, 6'000.0);
    std::cout << "suite=migrations"
              << " nodes=" << fixture.nodes.size() << " links=" << fixture.link_count
              << " baseline_p50_ms=" << plain.p50_ms << " baseline_p95_ms=" << plain.p95_ms
              << " document_p50_ms=" << document.p50_ms << " document_p95_ms=" << document.p95_ms
              << " node_remap_p50_ms=" << node.p50_ms << " node_remap_p95_ms=" << node.p95_ms << '\n';
    CheckPeakResidentMemory("migrations", 64);
}

} // namespace Uni::GUI::Nodes::Benchmarks
