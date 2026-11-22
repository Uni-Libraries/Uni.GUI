#pragma once

/// @file
/// @brief Bounded JSON persistence and migrations for documents, graph assets and clipboard fragments.

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

/// Library wire version; readers require an exact match. Independent of application and node type versions.
inline constexpr std::uint32_t GraphJsonFormatVersion = 5;

/// Editable, owning transfer representation for schema migration, before document indexes are rebuilt.
struct GraphArchive final {
    std::uint32_t schema_version{1};
    GraphId root_graph;
    std::vector<Graph> graphs;
    std::vector<IntergraphLink> intergraph_links;
    NodePresentationMap nodes;
    LinkPresentationMap links;
    GroupPresentationMap groups;
};

/// One synchronous N to N+1 step. Modify the borrowed archive in place; the registry advances
/// its schema version after success. Do not retain the context or register steps from the callback.
struct DocumentMigrationContext final {
    std::uint32_t from_version{0};
    std::uint32_t to_version{0};
    GraphArchive& archive;
};

using DocumentMigrationFn = std::function<Result<void>(DocumentMigrationContext&)>;

/// UI-thread registry of contiguous, forward-only application migrations. A zero target selects version one.
class UNI_GUI_EXPORT DocumentMigrationRegistry final {
public:
    explicit DocumentMigrationRegistry(std::uint32_t target_version = 1);
    ~DocumentMigrationRegistry();
    DocumentMigrationRegistry(DocumentMigrationRegistry&& other);
    DocumentMigrationRegistry& operator=(DocumentMigrationRegistry&& other);
    DocumentMigrationRegistry(const DocumentMigrationRegistry&) = delete;
    DocumentMigrationRegistry& operator=(const DocumentMigrationRegistry&) = delete;

    /// Final application schema accepted by Migrate().
    [[nodiscard]] std::uint32_t TargetVersion() const noexcept;
    /// Register one step keyed by nonzero from_version. Empty callback: InvalidArgument;
    /// duplicate step: DuplicateId; registration during migration: CommandFailed.
    [[nodiscard]] Result<void> Register(std::uint32_t from_version, DocumentMigrationFn migration);
    /// Apply steps directly to archive; use a temporary if rollback is required. Zero archive version
    /// is InvalidFormat; a newer schema is UnsupportedVersion; gaps return MigrationMissing. A callback
    /// failure or exception becomes MigrationFailed. Earlier mutations are not undone by this method.
    [[nodiscard]] Result<void> Migrate(GraphArchive& archive) const;

private:
    struct Impl;
    std::shared_ptr<Impl> m_impl;
};

/// Inclusive resource ceilings for encoding and decoding, including the post-migration result.
/// max_bytes limits the JSON byte stream; string limits are in decoded bytes. Depth and JSON
/// value counts bound the parser independently of semantic entity and property-tree counts.
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

    /// Derive property-tree limits: max_properties bounds values and container counts; max_bytes
    /// bounds total string bytes in addition to the single-string and depth limits.
    [[nodiscard]] PropertyLimits PropertyValueLimits() const noexcept {
        return PropertyLimits{
            .max_depth = max_depth,
            .max_values = max_properties,
            .max_array_elements = max_properties,
            .max_object_members = max_properties,
            .max_string_bytes = max_string_bytes,
            .max_total_string_bytes = max_bytes,
        };
    }
};

/// Nonfatal import diagnostic with source IDs where applicable. Unknown/newer node types and
/// unresolved external subgraphs can be preserved without a working plugin or asset registry.
struct GraphIoWarning final {
    std::string message;
    GraphId graph;
    NodeId node;
};

/// New, independent owners with imported IDs observed and revisions reset; no history or registry data.
struct LoadedGraphDocument final {
    GraphDocument document;
    GraphPresentation presentation;
    std::vector<GraphIoWarning> warnings;
};

/// Locally validated asset and nonfatal warnings; loading does not register it or resolve dependencies.
struct LoadedGraphAsset final {
    GraphAsset asset;
    std::vector<GraphIoWarning> warnings;
};

/// Encode committed model and presentation as canonical, newline-terminated UTF-8 JSON.
/// Presentation entries must be valid for @p document; orphaned IDs return InvalidGraph.
/// Returns SizeLimitExceeded if the encoded document exceeds @p limits.
[[nodiscard]] UNI_GUI_EXPORT Result<std::string> SerializeGraphDocumentJson(
    const GraphDocument& document,
    const GraphPresentation& presentation,
    const GraphIoLimits& limits = {});

/// Decode the complete document envelope, migrate the archive, then migrate registered node types
/// using a pinned catalog generation. A null migrations pointer accepts only document schema one.
/// On success returns independent model/presentation and warnings. Malformed or unexpected fields,
/// unsupported versions, exceeded limits, migration failures and invalid structure are errors;
/// no partially imported live owner is returned. json and callback arguments are borrowed for this call.
[[nodiscard]] UNI_GUI_EXPORT Result<LoadedGraphDocument> DeserializeGraphDocumentJson(
    std::string_view json,
    const RegistryCatalog& registry,
    const DocumentMigrationRegistry* migrations = nullptr,
    const GraphIoLimits& limits = {});

/// Serialize before touching the destination, then atomically replace it via a sibling
/// temporary file. The path must be nonempty and NUL-free. On POSIX a non-regular
/// destination (including a FIFO with no writer) is rejected with IoWrite. Directory
/// synchronization may fail after rename, when the new bytes are already visible.
[[nodiscard]] UNI_GUI_EXPORT Result<void> SaveGraphDocumentJson(
    std::string_view path,
    const GraphDocument& document,
    const GraphPresentation& presentation,
    const GraphIoLimits& limits = {});

/// Read at most limits.max_bytes from path (non-empty, no embedded NUL), then apply
/// DeserializeGraphDocumentJson(). File access may return IoRead; parsing/migration errors propagate.
[[nodiscard]] UNI_GUI_EXPORT Result<LoadedGraphDocument> LoadGraphDocumentJson(
    std::string_view path,
    const RegistryCatalog& registry,
    const DocumentMigrationRegistry* migrations = nullptr,
    const GraphIoLimits& limits = {});

/// Validate and encode a portable fragment including owned graphs and presentation, retaining its
/// source IDs and origin. Does not copy live descriptor registrations; returns JSON or validation/limit error.
/// The owned closure requires one owner per child, matching intergraph proxies and acyclic dependencies.
[[nodiscard]] UNI_GUI_EXPORT Result<std::string> SerializeGraphFragmentJson(
    const GraphFragment& fragment,
    const GraphIoLimits& limits = {});

/// Decode a fragment, validate and run registered node migrations, and recheck limits. No document
/// schema migration, destination ID allocation or paste takes place here. See PrepareGraphFragmentPaste().
[[nodiscard]] UNI_GUI_EXPORT Result<GraphFragment> DeserializeGraphFragmentJson(
    std::string_view json,
    const RegistryCatalog& registry,
    const GraphIoLimits& limits = {});

/// Validate asset ID (non-empty UTF-8), semantic structure and presentation. Return canonical asset
/// JSON under limits. Includes asset ID, not source_uri; no external dependencies need be loaded.
[[nodiscard]] UNI_GUI_EXPORT Result<std::string> SerializeGraphAssetJson(
    const GraphAsset& asset,
    const GraphIoLimits& limits = {});

/// Decode the asset envelope with the same version, migration and limit rules as document import.
/// Returns a standalone asset with empty source_uri. External interface bindings remain unresolved.
[[nodiscard]] UNI_GUI_EXPORT Result<LoadedGraphAsset> DeserializeGraphAssetJson(
    std::string_view json,
    const RegistryCatalog& registry,
    const DocumentMigrationRegistry* migrations = nullptr,
    const GraphIoLimits& limits = {});

/// Save canonical asset content with the path, replacement and synchronization semantics
/// of SaveGraphDocumentJson(). Serialization errors leave an existing file untouched.
[[nodiscard]] UNI_GUI_EXPORT Result<void> SaveGraphAssetJson(
    std::string_view path,
    const GraphAsset& asset,
    const GraphIoLimits& limits = {});

/// Bounded file counterpart of DeserializeGraphAssetJson(); path must be non-empty with no NUL.
/// Does not assign path to the returned asset's source_uri. Reports file and import errors.
[[nodiscard]] UNI_GUI_EXPORT Result<LoadedGraphAsset> LoadGraphAssetJson(
    std::string_view path,
    const RegistryCatalog& registry,
    const DocumentMigrationRegistry* migrations = nullptr,
    const GraphIoLimits& limits = {});

} // namespace Uni::GUI::Nodes
