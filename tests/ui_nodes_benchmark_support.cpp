#include "ui_nodes_benchmark_support.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>
#include <utility>

#if defined(_WIN32)
#include <psapi.h>
#include <windows.h>
#elif defined(__APPLE__) || (defined(__unix__) && !defined(__linux__))
#include <sys/resource.h>
#endif

namespace Uni::GUI::Nodes::Benchmarks {
namespace {

constexpr std::uint64_t Mebibyte = 1024ULL * 1024ULL;

class BulkGraphCommand final : public Command {
public:
    BulkGraphCommand(Graph graph, const GraphId previous_root,
                     std::vector<std::pair<NodeId, NodePresentation>> presentations)
        : m_graph_id(graph.id), m_previous_root(previous_root), m_graph(std::move(graph)),
          m_presentations(std::move(presentations)) {}

    [[nodiscard]] std::string_view Name() const noexcept override { return "Build benchmark graph"; }

private:
    [[nodiscard]] Result<void> Apply(GraphTransaction& transaction, const RegistrySnapshot&) override {
        auto added = transaction.AddGraph(std::move(m_graph));
        if (!added)
            return std::unexpected(std::move(added.error()));
        if (auto root = transaction.SetRootGraph(m_graph_id); !root)
            return root;
        for (const auto& [node, presentation] : m_presentations) {
            if (auto result = transaction.SetNodePresentation(node, presentation); !result) {
                return result;
            }
        }
        return {};
    }

    [[nodiscard]] Result<void> Revert(GraphTransaction& transaction) override {
        if (auto root = transaction.SetRootGraph(m_previous_root); !root)
            return root;
        for (const auto& [node, presentation] : m_presentations) {
            (void)presentation;
            if (auto result = transaction.SetNodePresentation(node, std::nullopt); !result) {
                return result;
            }
        }
        auto removed = transaction.RemoveGraph(m_graph_id);
        if (!removed)
            return std::unexpected(std::move(removed.error()));
        m_graph = std::move(*removed);
        return {};
    }

    GraphId m_graph_id;
    GraphId m_previous_root;
    Graph m_graph;
    std::vector<std::pair<NodeId, NodePresentation>> m_presentations;
};

[[nodiscard]] std::size_t LinkCount(const FixtureConfig& config) {
    switch (config.links) {
    case LinkPattern::None:
        return 0;
    case LinkPattern::Chain:
    case LinkPattern::Star:
        return config.node_count > 0 ? config.node_count - 1 : 0;
    case LinkPattern::Dense:
        if (config.dense_fanout >= config.node_count) {
            Fail("Dense benchmark fanout must be smaller than the node count");
        }
        if (config.dense_fanout != 0 &&
            config.node_count > std::numeric_limits<std::size_t>::max() / config.dense_fanout) {
            Fail("Dense benchmark link count overflows size_t");
        }
        return config.node_count * config.dense_fanout;
    }
    Fail("Unknown benchmark link pattern");
}

[[nodiscard]] std::size_t NearestRankIndex(const std::size_t count, const double quantile) {
    return static_cast<std::size_t>(std::ceil(quantile * static_cast<double>(count))) - 1;
}

} // namespace

[[noreturn]] void Fail(const std::string_view message) {
    std::cerr << message << '\n';
    std::exit(EXIT_FAILURE);
}

void Check(const bool condition, const std::string_view message) {
    if (!condition)
        Fail(message);
}

Distribution MeasureDistribution(const std::size_t sample_count, const std::function<void()>& sample) {
    Check(sample_count > 0, "A benchmark distribution requires at least one sample");
    std::vector<double> samples;
    samples.reserve(sample_count);
    for (std::size_t index = 0; index < sample_count; ++index) {
        samples.push_back(MeasureMilliseconds(sample));
    }
    std::ranges::sort(samples);
    return Distribution{
        .p50_ms = samples[NearestRankIndex(samples.size(), 0.50)],
        .p95_ms = samples[NearestRankIndex(samples.size(), 0.95)],
        .max_ms = samples.back(),
    };
}

void CheckTiming(const std::string_view name, const Distribution& distribution, const double p50_limit_ms,
                 const double p95_limit_ms) {
    if (distribution.p50_ms <= p50_limit_ms && distribution.p95_ms <= p95_limit_ms) {
        return;
    }
    std::ostringstream message;
    message << name << " timing gate failed: p50=" << distribution.p50_ms << "ms (limit " << p50_limit_ms
            << "ms), p95=" << distribution.p95_ms << "ms (limit " << p95_limit_ms << "ms)";
    Fail(message.str());
}

std::uint64_t PeakResidentBytes() noexcept {
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS counters{};
    counters.cb = sizeof(counters);
    if (GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)) == 0)
        return 0;
    return static_cast<std::uint64_t>(counters.PeakWorkingSetSize);
#elif defined(__linux__)
    std::ifstream status{"/proc/self/status"};
    std::string key;
    while (status >> key) {
        if (key == "VmHWM:") {
            std::uint64_t kibibytes = 0;
            status >> kibibytes;
            return kibibytes * 1024ULL;
        }
        status.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    }
    return 0;
#elif defined(__unix__) || defined(__APPLE__)
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) != 0)
        return 0;
#if defined(__APPLE__)
    return static_cast<std::uint64_t>(usage.ru_maxrss);
#else
    return static_cast<std::uint64_t>(usage.ru_maxrss) * 1024ULL;
#endif
#else
    return 0;
#endif
}

void CheckPeakResidentMemory(const std::string_view suite, const std::size_t baseline_mib) {
    const std::uint64_t peak = PeakResidentBytes();
    if (peak == 0) {
        Fail(std::string{suite} + " memory gate could not read process peak RSS");
    }
    const std::uint64_t baseline = static_cast<std::uint64_t>(baseline_mib) * Mebibyte;
    const std::uint64_t tolerance = std::max<std::uint64_t>(baseline / 5, 32ULL * Mebibyte);
    const std::uint64_t limit = baseline + tolerance;
    if (peak > limit) {
        std::ostringstream message;
        message << suite
                << " memory gate failed: peak_rss=" << static_cast<double>(peak) / static_cast<double>(Mebibyte)
                << "MiB, baseline=" << baseline_mib
                << "MiB, limit=" << static_cast<double>(limit) / static_cast<double>(Mebibyte) << "MiB";
        Fail(message.str());
    }
    std::cout << "suite=" << suite << " peak_rss_mib=" << static_cast<double>(peak) / static_cast<double>(Mebibyte)
              << " memory_limit_mib=" << static_cast<double>(limit) / static_cast<double>(Mebibyte) << '\n';
}

NodeTypeDescriptor BenchmarkNodeDescriptor(const std::uint32_t version, MigrateNodeFn migrate) {
    return NodeTypeDescriptor{
        .type = TypeId{"benchmark"},
        .display_name = "Benchmark",
        .version = version,
        .pin_schema =
            {
                PinDescriptor{
                    .key = "out",
                    .type = TypeId{"float"},
                    .direction = PinDirection::Output,
                    .cardinality = PinCardinality::Multiple,
                },
                PinDescriptor{
                    .key = "in",
                    .type = TypeId{"float"},
                    .direction = PinDirection::Input,
                    .cardinality = PinCardinality::Multiple,
                },
            },
        .default_properties = {{"slider", PropertyValue{std::int64_t{0}}}},
        .property_impacts = {{"slider", PropertyImpact::RuntimeOnly}},
        .behavior = migrate
            ? std::make_shared<const NodeBehavior>(NodeBehavior{.migrate = std::move(migrate)})
            : nullptr,
    };
}

Fixture::Fixture(const FixtureConfig& config) {
    Check(config.node_count > 0, "Benchmark fixtures require at least one node");
    Check(config.columns > 0, "Benchmark fixture columns cannot be zero");

    auto& fixture = *this;
    auto registered = fixture.node_types.RegisterNodeType(BenchmarkNodeDescriptor());
    if (!registered)
        Fail("Benchmark node descriptor failed to register: " + registered.error().message);
    fixture.commands.SetHistoryLimit(0);

    const GraphId previous_root = fixture.document.RootGraph();
    fixture.graph = fixture.document.AllocateGraphId();
    Check(static_cast<bool>(fixture.graph), "Benchmark graph ID allocation failed");
    fixture.link_count = LinkCount(config);

    Graph graph{
        .id = fixture.graph,
        .display_name = "Performance benchmark",
    };
    graph.nodes.reserve(config.node_count);
    graph.pins.reserve(config.node_count * 2);
    graph.links.reserve(fixture.link_count);
    fixture.nodes.reserve(config.node_count);
    fixture.outputs.reserve(config.node_count);
    fixture.inputs.reserve(config.node_count);
    fixture.links.reserve(fixture.link_count);
    std::vector<std::pair<NodeId, NodePresentation>> presentations;
    presentations.reserve(config.node_count);

    for (std::size_t index = 0; index < config.node_count; ++index) {
        const NodeId node = fixture.document.AllocateNodeId();
        const PinId output = fixture.document.AllocatePinId();
        const PinId input = fixture.document.AllocatePinId();
        Check(node && output && input, "Benchmark entity ID allocation failed");
        auto [node_position, node_inserted] =
            graph.nodes.emplace(node, NodeInstance{
                                          .id = node,
                                          .type = TypeId{"benchmark"},
                                          .properties = {{"slider", PropertyValue{std::int64_t{0}}}},
                                          .pins = {output, input},
                                      });
        auto [output_position, output_inserted] =
            graph.pins.emplace(output, PinInstance{
                                           .id = output,
                                           .node = node,
                                           .key = "out",
                                           .type = TypeId{"float"},
                                           .direction = PinDirection::Output,
                                           .cardinality = PinCardinality::Multiple,
                                       });
        auto [input_position, input_inserted] = graph.pins.emplace(input, PinInstance{
                                                                              .id = input,
                                                                              .node = node,
                                                                              .key = "in",
                                                                              .type = TypeId{"float"},
                                                                              .direction = PinDirection::Input,
                                                                              .cardinality = PinCardinality::Multiple,
                                                                          });
        (void)node_position;
        (void)output_position;
        (void)input_position;
        Check(node_inserted && output_inserted && input_inserted, "Benchmark fixture contains duplicate IDs");
        fixture.nodes.push_back(node);
        fixture.outputs.push_back(output);
        fixture.inputs.push_back(input);
        presentations.emplace_back(node, NodePresentation{.position = {
                                                              static_cast<float>(index % config.columns) * 240.0f,
                                                              static_cast<float>(index / config.columns) * 150.0f,
                                                          }});
    }

    const auto add_link = [&](const PinId output, const PinId input) {
        const LinkId link = fixture.document.AllocateLinkId();
        Check(static_cast<bool>(link), "Benchmark link ID allocation failed");
        const auto [position, inserted] = graph.links.emplace(link, Link{
                                                                        .id = link,
                                                                        .output = output,
                                                                        .input = input,
                                                                    });
        (void)position;
        Check(inserted, "Benchmark fixture contains a duplicate link ID");
        fixture.links.push_back(link);
    };
    if (config.links == LinkPattern::Chain) {
        for (std::size_t index = 0; index + 1 < config.node_count; ++index) {
            add_link(fixture.outputs[index], fixture.inputs[index + 1]);
        }
    } else if (config.links == LinkPattern::Star) {
        for (std::size_t index = 1; index < config.node_count; ++index) {
            add_link(fixture.outputs.front(), fixture.inputs[index]);
        }
    } else if (config.links == LinkPattern::Dense) {
        for (std::size_t source = 0; source < config.node_count; ++source) {
            for (std::size_t offset = 1; offset <= config.dense_fanout; ++offset) {
                const std::size_t target = (source + offset) % config.node_count;
                add_link(fixture.outputs[source], fixture.inputs[target]);
            }
        }
    }

    auto result = fixture.commands.Execute(
        std::make_unique<BulkGraphCommand>(std::move(graph), previous_root, std::move(presentations)), fixture.document,
        fixture.presentation, fixture.types);
    if (!result)
        Fail("Benchmark fixture transaction failed: " + result.error().message);
    fixture.commands.Clear();
    ResetTransactionMetrics();
}

Fixture MakeFixture(const FixtureConfig& config) { return Fixture{config}; }

void Execute(Fixture& fixture, std::unique_ptr<Command> command, const std::string_view context) {
    auto result = fixture.commands.Execute(
        std::move(command), fixture.document, fixture.presentation, fixture.types);
    if (!result)
        Fail(std::string{context} + ": " + result.error().message);
    if (!result->model_changed && !result->presentation_changed) {
        Fail(std::string{context} + ": benchmark sample became a no-op");
    }
}

} // namespace Uni::GUI::Nodes::Benchmarks
