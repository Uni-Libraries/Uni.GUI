#include "ui_nodes_benchmark_support.h"

#include <cstdlib>
#include <string_view>

int main(const int argc, const char* const* argv) {
    using namespace Uni::GUI::Nodes::Benchmarks;
    if (argc != 3 || std::string_view{argv[1]} != "--suite") {
        Fail("Usage: unigui_nodes_benchmarks --suite "
              "<frames-10k|property-10k|property-1m|sparse-100k|dense-links|graph-assets|io|migrations|mutations|conversion-catalog>");
    }

    const std::string_view suite{argv[2]};
    if (suite == "frames-10k") {
        RunFrames10kSuite();
    } else if (suite == "property-10k") {
        RunProperty10kSuite();
    } else if (suite == "property-1m") {
        RunProperty1mSuite();
    } else if (suite == "sparse-100k") {
        RunSparse100kSuite();
    } else if (suite == "dense-links") {
        RunDenseLinksSuite();
    } else if (suite == "graph-assets") {
        RunGraphAssetsSuite();
    } else if (suite == "io") {
        RunIoSuite();
    } else if (suite == "migrations") {
        RunMigrationsSuite();
    } else if (suite == "mutations") {
        RunMutationsSuite();
    } else if (suite == "conversion-catalog") {
        RunConversionCatalogSuite();
    } else {
        Fail("Unknown nodes benchmark suite");
    }
    return EXIT_SUCCESS;
}
