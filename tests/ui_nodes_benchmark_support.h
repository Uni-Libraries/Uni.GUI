#pragma once

#include <uni/gui/nodes/nodes.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Uni::GUI::Nodes::Benchmarks {

using BenchmarkClock = std::chrono::steady_clock;

[[noreturn]] void Fail(std::string_view message);
void Check(bool condition, std::string_view message);

template<typename Callback> double MeasureMilliseconds(Callback&& callback) {
    const auto start = BenchmarkClock::now();
    std::forward<Callback>(callback)();
    return std::chrono::duration<double, std::milli>(BenchmarkClock::now() - start).count();
}

struct Distribution final {
    double p50_ms{0.0};
    double p95_ms{0.0};
    double max_ms{0.0};
};

[[nodiscard]] Distribution MeasureDistribution(std::size_t sample_count, const std::function<void()>& sample);
void CheckTiming(std::string_view name, const Distribution& distribution, double p50_limit_ms, double p95_limit_ms);

[[nodiscard]] std::uint64_t PeakResidentBytes() noexcept;
void CheckPeakResidentMemory(std::string_view suite, std::size_t baseline_mib);

enum class LinkPattern {
    None,
    Chain,
    Star,
    Dense,
};

struct FixtureConfig final {
    std::size_t node_count{0};
    LinkPattern links{LinkPattern::None};
    std::size_t dense_fanout{0};
    std::size_t columns{100};
};

struct Fixture final {
    explicit Fixture(const FixtureConfig& config);

    GraphDocument document;
    GraphPresentation presentation;
    RegistryCatalog node_types;
    RegistryCatalog& types{node_types};
    CommandStack commands;
    GraphId graph;
    std::vector<NodeId> nodes;
    std::vector<PinId> outputs;
    std::vector<PinId> inputs;
    std::vector<LinkId> links;
    std::size_t link_count{0};
};

[[nodiscard]] NodeTypeDescriptor BenchmarkNodeDescriptor(std::uint32_t version = 1, MigrateNodeFn migrate = {});
[[nodiscard]] Fixture MakeFixture(const FixtureConfig& config);

void Execute(Fixture& fixture, std::unique_ptr<Command> command, std::string_view context);

void RunFrames10kSuite();
void RunProperty10kSuite();
void RunProperty1mSuite();
void RunSparse100kSuite();
void RunDenseLinksSuite();
void RunGraphAssetsSuite();
void RunIoSuite();
void RunMigrationsSuite();
void RunMutationsSuite();
void RunConversionCatalogSuite();

} // namespace Uni::GUI::Nodes::Benchmarks
