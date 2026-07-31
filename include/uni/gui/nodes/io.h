#pragma once

#include <uni/gui/export.h>
#include <uni/gui/nodes/commands.h>
#include <uni/gui/nodes/graph_asset.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace Uni::GUI::Nodes {

inline constexpr std::uint32_t GraphJsonFormatVersion = 4;

struct GraphArchive final {
    std::uint32_t schema_version{1};
    GraphId root_graph;
    std::vector<Graph> graphs;
    std::vector<IntergraphLink> intergraph_links;
    NodePresentationMap nodes;
    LinkPresentationMap links;
    GroupPresentationMap groups;
};

struct DocumentMigrationContext final {
    std::uint32_t from_version{0};
    std::uint32_t to_version{0};
    GraphArchive& archive;
};

using DocumentMigrationFn = std::function<Result<void>(DocumentMigrationContext&)>;

class UNI_GUI_EXPORT DocumentMigrationRegistry final {
public:
    explicit DocumentMigrationRegistry(std::uint32_t target_version = 1);
    ~DocumentMigrationRegistry();
    DocumentMigrationRegistry(DocumentMigrationRegistry&& other);
    DocumentMigrationRegistry& operator=(DocumentMigrationRegistry&& other);
    DocumentMigrationRegistry(const DocumentMigrationRegistry&) = delete;
    DocumentMigrationRegistry& operator=(const DocumentMigrationRegistry&) = delete;

    [[nodiscard]] std::uint32_t TargetVersion() const noexcept;
    [[nodiscard]] Result<void> Register(std::uint32_t from_version, DocumentMigrationFn migration);
    [[nodiscard]] Result<void> Migrate(GraphArchive& archive) const;

private:
    struct Impl;
    std::shared_ptr<Impl> m_impl;
};

struct GraphIoLimits final {
    std::size_t max_bytes{64U * 1024U * 1024U};
    std::size_t max_depth{128};
    std::size_t max_string_bytes{16U * 1024U * 1024U};
    std::size_t max_json_values{2'000'000};
    std::size_t max_graphs{100'000};
    std::size_t max_nodes{100'000};
    std::size_t max_pins{400'000};
    std::size_t max_links{400'000};
    std::size_t max_groups{100'000};
    std::size_t max_route_points{400'000};
    std::size_t max_properties{400'000};
};

struct GraphIoWarning final {
    std::string message;
    GraphId graph;
    NodeId node;
};

struct LoadedGraphDocument final {
    GraphDocument document;
    GraphPresentation presentation;
    std::vector<GraphIoWarning> warnings;
};

struct LoadedGraphAsset final {
    GraphAsset asset;
    std::vector<GraphIoWarning> warnings;
};

[[nodiscard]] UNI_GUI_EXPORT Result<std::string> SerializeGraphDocumentJson(
    const GraphDocument& document,
    const GraphPresentation& presentation,
    const GraphIoLimits& limits = {});

[[nodiscard]] UNI_GUI_EXPORT Result<LoadedGraphDocument> DeserializeGraphDocumentJson(
    std::string_view json,
    const RegistryCatalog& registry,
    const DocumentMigrationRegistry* migrations = nullptr,
    const GraphIoLimits& limits = {});

[[nodiscard]] UNI_GUI_EXPORT Result<void> SaveGraphDocumentJson(
    std::string_view path,
    const GraphDocument& document,
    const GraphPresentation& presentation,
    const GraphIoLimits& limits = {});

[[nodiscard]] UNI_GUI_EXPORT Result<LoadedGraphDocument> LoadGraphDocumentJson(
    std::string_view path,
    const RegistryCatalog& registry,
    const DocumentMigrationRegistry* migrations = nullptr,
    const GraphIoLimits& limits = {});

[[nodiscard]] UNI_GUI_EXPORT Result<std::string> SerializeGraphFragmentJson(
    const GraphFragment& fragment,
    const GraphIoLimits& limits = {});

[[nodiscard]] UNI_GUI_EXPORT Result<GraphFragment> DeserializeGraphFragmentJson(
    std::string_view json,
    const RegistryCatalog& registry,
    const GraphIoLimits& limits = {});

[[nodiscard]] UNI_GUI_EXPORT Result<std::string> SerializeGraphAssetJson(
    const GraphAsset& asset,
    const GraphIoLimits& limits = {});

[[nodiscard]] UNI_GUI_EXPORT Result<LoadedGraphAsset> DeserializeGraphAssetJson(
    std::string_view json,
    const RegistryCatalog& registry,
    const DocumentMigrationRegistry* migrations = nullptr,
    const GraphIoLimits& limits = {});

[[nodiscard]] UNI_GUI_EXPORT Result<void> SaveGraphAssetJson(
    std::string_view path,
    const GraphAsset& asset,
    const GraphIoLimits& limits = {});

[[nodiscard]] UNI_GUI_EXPORT Result<LoadedGraphAsset> LoadGraphAssetJson(
    std::string_view path,
    const RegistryCatalog& registry,
    const DocumentMigrationRegistry* migrations = nullptr,
    const GraphIoLimits& limits = {});

} // namespace Uni::GUI::Nodes
