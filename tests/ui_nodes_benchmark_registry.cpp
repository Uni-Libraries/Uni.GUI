#include "ui_nodes_benchmark_support.h"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace Uni::GUI::Nodes::Benchmarks {
namespace {

constexpr std::size_t CatalogSize = 10'000;
constexpr std::size_t ReloadSize = 100;
constexpr std::size_t Samples = 20;

template<typename Value>
Value Require(Result<Value> result, const std::string_view context) {
    if (!result) Fail(std::string{context} + ": " + result.error().message);
    return std::move(*result);
}

void Require(Result<void> result, const std::string_view context) {
    if (!result) Fail(std::string{context} + ": " + result.error().message);
}

[[nodiscard]] TypeId SourceType(const std::size_t index) {
    return TypeId{"benchmark.registry.source." + std::to_string(index)};
}

[[nodiscard]] TypeId DestinationType(const std::size_t index) {
    return TypeId{"benchmark.registry.destination." + std::to_string(index)};
}

[[nodiscard]] TypeId ConverterType(const std::size_t index) {
    return TypeId{"benchmark.registry.converter." + std::to_string(index)};
}

[[nodiscard]] NodeTypeDescriptor ConverterDescriptor(const std::size_t index) {
    return NodeTypeDescriptor{
        .type = ConverterType(index),
        .display_name = "Registry converter " + std::to_string(index),
        .static_pins = {
            PinDescriptor{.key = "input", .type = SourceType(index)},
            PinDescriptor{
                .key = "output",
                .type = DestinationType(index),
                .direction = PinDirection::Output,
            },
        },
    };
}

[[nodiscard]] ConversionDescriptor Conversion(const std::size_t index) {
    return ConversionDescriptor{
        .key = ConversionKey{SourceType(index), DestinationType(index), PinKind::Data},
        .node_type = ConverterType(index),
        .input_pin = "input",
        .output_pin = "output",
    };
}

void PrintDistribution(
    const std::string_view scenario,
    const Distribution& distribution) {
    std::cout << "suite=conversion-catalog"
              << " scenario=" << scenario
              << " scale=" << CatalogSize
              << " samples=" << Samples
              << " p50_ms=" << distribution.p50_ms
              << " p95_ms=" << distribution.p95_ms
              << " max_ms=" << distribution.max_ms << '\n';
}

} // namespace

void RunConversionCatalogSuite() {
    RegistryCatalog catalog;
    auto setup = Require(catalog.BeginUpdate(), "Catalog setup begin failed");
    for (std::size_t index = 0; index < CatalogSize; ++index) {
        Require(setup.RegisterNodeType(ConverterDescriptor(index)), "Catalog node staging failed");
        Require(setup.RegisterConversion(Conversion(index)), "Catalog conversion staging failed");
    }
    RegistryUpdateResult setup_result;
    const double setup_ms = MeasureMilliseconds([&] {
        setup_result = Require(setup.Commit(), "Catalog setup commit failed");
    });
    Check(setup_result.registrations.size() == CatalogSize &&
               setup_result.statistics.path_copies > 0 &&
               setup_result.statistics.touched_records == CatalogSize * 2 &&
              setup_result.statistics.recipes_built == CatalogSize &&
              setup_result.statistics.published_generations == 1,
        "Catalog setup did not use one atomic persistent publication");
    std::cout << "suite=conversion-catalog scenario=setup scale=" << CatalogSize
              << " elapsed_ms=" << setup_ms << '\n';

    std::vector<ConversionRegistrationToken> registrations = setup_result.registrations;
    const RegistrySnapshot retained = catalog.Snapshot();
    const auto retained_recipe = retained.Check(
        SourceType(0), DestinationType(0), PinKind::Data).recipe;
    Check(retained_recipe.has_value(), "Catalog retained snapshot missed its recipe");

    const Distribution replace_one = MeasureDistribution(Samples, [&] {
        const std::uint64_t generation = catalog.Generation();
        Require(catalog.ReplaceConversion(registrations.back(), Conversion(CatalogSize - 1)),
            "Single no-op recipe replacement failed");
        Check(catalog.Generation() == generation, "Identical replacement published a generation");
    });
    PrintDistribution("replace-one-no-op", replace_one);
    CheckTiming("conversion-catalog.replace-one", replace_one, 30.0, 60.0);

    const Distribution reload = MeasureDistribution(Samples, [&] {
        const std::uint64_t generation = catalog.Generation();
        auto update = Require(catalog.BeginUpdate(), "Reload begin failed");
        for (std::size_t index = 0; index < ReloadSize; ++index) {
            Require(update.ReplaceConversion(registrations[index], Conversion(index)),
                "Reload replacement staging failed");
        }
        const RegistryUpdateResult committed = Require(update.Commit(), "Reload commit failed");
        Check(committed.statistics.no_op_records >= ReloadSize &&
                   committed.statistics.recipes_built == 0 &&
                   committed.statistics.published_generations == 0 &&
                   catalog.Generation() == generation,
            "Identical reload did not remain a semantic no-op");
    });
    PrintDistribution("reload-100", reload);
    CheckTiming("conversion-catalog.reload-100", reload, 50.0, 100.0);

    const Distribution plugin_cycle = MeasureDistribution(Samples, [&] {
        auto update = Require(catalog.BeginUpdate(), "Plugin cycle begin failed");
        for (std::size_t index = 0; index < ReloadSize; ++index) {
            Require(update.UnregisterConversion(registrations[index]),
                "Plugin unregister staging failed");
        }
        for (std::size_t index = 0; index < ReloadSize; ++index) {
            Require(update.RegisterConversion(Conversion(index)),
                "Plugin register staging failed");
        }
        const RegistryUpdateResult committed = Require(update.Commit(), "Plugin cycle commit failed");
        Check(committed.registrations.size() == ReloadSize &&
                   committed.statistics.path_copies > 0 &&
                  committed.statistics.recipes_built == ReloadSize &&
                  committed.statistics.published_generations == 1,
            "Plugin cycle did not use one atomic persistent publication");
        std::copy(
            committed.registrations.begin(),
            committed.registrations.end(),
            registrations.begin());
    });
    PrintDistribution("plugin-cycle-100", plugin_cycle);
    CheckTiming("conversion-catalog.plugin-cycle-100", plugin_cycle, 60.0, 120.0);

    const auto current = catalog.Check(SourceType(0), DestinationType(0), PinKind::Data);
    Check(current.recipe && *current.recipe != *retained_recipe &&
               retained.Check(SourceType(0), DestinationType(0), PinKind::Data).recipe ==
                  retained_recipe,
        "Catalog snapshot retention lost immutable recipe identity");
    CheckPeakResidentMemory("conversion-catalog", 256);
}

} // namespace Uni::GUI::Nodes::Benchmarks
