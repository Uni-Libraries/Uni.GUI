#pragma once

/// @file
/// @brief Immutable publications of graph assets and dependency-aware registry notifications.

#include <uni/gui/export.h>
#include <uni/gui/nodes/presentation.h>
#include <uni/gui/nodes/registry.h>

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Uni::GUI::Nodes {

/// Per-record generation: starts at one on insertion and advances on content or URI replacement.
using GraphAssetGeneration = std::uint64_t;

/// SHA-256 digest of canonical asset JSON (including the asset key; excluding source_uri).
struct GraphAssetContentHash final {
    std::array<std::uint8_t, 32> bytes{};

    /// Encode all 32 bytes as 64 lowercase hexadecimal digits.
    [[nodiscard]] UNI_GUI_EXPORT std::string ToHex() const;
    auto operator<=>(const GraphAssetContentHash&) const = default;
};

/// Standalone semantic document with matching presentation and application-owned source metadata.
struct GraphAsset final {
    GraphAssetId id;
    GraphDocument document;
    GraphPresentation presentation;
    std::string source_uri;
};

/// Insert requires a missing key, Replace requires an existing key, Upsert permits either.
enum class GraphAssetWriteMode {
    Insert,
    Replace,
    Upsert,
};

/// One external call site's key and required root interface. Multiple uses can name the same asset.
struct GraphAssetDependencyUse final {
    GraphAssetId dependency;
    GraphInterface expected_interface;

    bool operator==(const GraphAssetDependencyUse&) const = default;
};

class GraphAssetRegistry;

/// Immutable publication. All reference/span getters borrow the record: retaining its GraphAssetRecordPtr
/// keeps them valid across registry writes, unregister, moves and destruction.
class UNI_GUI_EXPORT GraphAssetRecord final {
public:
    [[nodiscard]] const GraphAsset& Asset() const noexcept;
    [[nodiscard]] GraphAssetGeneration Generation() const noexcept;
    [[nodiscard]] const GraphAssetContentHash& ContentHash() const noexcept;
    /// Sorted distinct external asset IDs used by this publication (not transitive).
    [[nodiscard]] std::span<const GraphAssetId> DirectDependencies() const noexcept;
    /// All interface expectations, not deduplicated by ID.
    [[nodiscard]] std::span<const GraphAssetDependencyUse> DependencyUses() const noexcept;
    /// Signature that callers of this asset must project.
    [[nodiscard]] const GraphInterface& RootInterface() const noexcept;

private:
    GraphAssetRecord(
        GraphAsset&& asset,
        GraphAssetGeneration generation,
        GraphAssetContentHash content_hash,
        std::vector<GraphAssetId> dependencies,
        std::vector<GraphAssetDependencyUse> dependency_uses,
        GraphInterface root_interface);

    GraphAsset m_asset;
    GraphAssetGeneration m_generation{0};
    GraphAssetContentHash m_content_hash;
    std::vector<GraphAssetId> m_dependencies;
    std::vector<GraphAssetDependencyUse> m_dependency_uses;
    GraphInterface m_root_interface;

    friend class GraphAssetRegistry;
};

using GraphAssetRecordPtr = std::shared_ptr<const GraphAssetRecord>;

/// Unchanged means the existing record was reused, without a generation bump or notification.
enum class GraphAssetWriteStatus {
    Inserted,
    Replaced,
    Unchanged,
};

/// Bitmask describing what changed in the published record, or in the triggering dependency.
enum class GraphAssetChangeFlags : std::uint8_t {
    None = 0,
    Content = 1U << 0U,
    SourceUri = 1U << 1U,
    Dependencies = 1U << 2U,
    RootInterface = 1U << 3U,
};

[[nodiscard]] constexpr GraphAssetChangeFlags operator|(
    const GraphAssetChangeFlags left,
    const GraphAssetChangeFlags right) noexcept {
    return static_cast<GraphAssetChangeFlags>(
        static_cast<std::uint8_t>(left) | static_cast<std::uint8_t>(right));
}

constexpr GraphAssetChangeFlags& operator|=(
    GraphAssetChangeFlags& left,
    const GraphAssetChangeFlags right) noexcept {
    left = left | right;
    return left;
}

[[nodiscard]] constexpr GraphAssetChangeFlags operator&(
    const GraphAssetChangeFlags left,
    const GraphAssetChangeFlags right) noexcept {
    return static_cast<GraphAssetChangeFlags>(
        static_cast<std::uint8_t>(left) & static_cast<std::uint8_t>(right));
}

/// True when at least one requested bit is set (None never matches).
[[nodiscard]] constexpr bool HasGraphAssetChange(
    const GraphAssetChangeFlags changes,
    const GraphAssetChangeFlags flag) noexcept {
    return (changes & flag) != GraphAssetChangeFlags::None;
}

/// Successful write: owning handle to the current record plus the publication outcome and changed fields.
struct GraphAssetWriteResult final {
    GraphAssetWriteStatus status{GraphAssetWriteStatus::Unchanged};
    GraphAssetChangeFlags changes{GraphAssetChangeFlags::None};
    GraphAssetRecordPtr record;
};

/// A direct publication/removal or a notification that a transitive dependency's content changed.
enum class GraphAssetChangeKind {
    Inserted,
    Replaced,
    Removed,
    DependencyChanged,
};

/// Post-commit event. before is null on insertion; after is null on removal. For DependencyChanged,
/// asset identifies the unchanged dependent (before == after), while changed_dependency and its
/// two record handles describe the publication that triggered the notification.
struct GraphAssetChange final {
    GraphAssetChangeKind kind{GraphAssetChangeKind::Inserted};
    GraphAssetId asset;
    GraphAssetRecordPtr before;
    GraphAssetRecordPtr after;
    GraphAssetChangeFlags changes{GraphAssetChangeFlags::None};
    GraphAssetId changed_dependency;
    GraphAssetRecordPtr dependency_before;
    GraphAssetRecordPtr dependency_after;
};

/// Synchronous UI-thread callback after publication. The event reference is borrowed; copying it retains
/// its records. Exceptions are swallowed. Do not call Write() or Unregister() on the dispatching registry.
using GraphAssetChangeFn = std::function<void(const GraphAssetChange&)>;

/// Move-only subscription. Keep the handle to continue receiving notifications; destruction
/// unsubscribes and does not require the registry owner to be alive.
class UNI_GUI_EXPORT GraphAssetSubscription final {
public:
    GraphAssetSubscription() = default;
    ~GraphAssetSubscription();
    GraphAssetSubscription(GraphAssetSubscription&& other) noexcept;
    GraphAssetSubscription& operator=(GraphAssetSubscription&& other) noexcept;
    GraphAssetSubscription(const GraphAssetSubscription&) = delete;
    GraphAssetSubscription& operator=(const GraphAssetSubscription&) = delete;
    /// Unsubscribe from future batches. Does not alter the callbacks captured for active dispatch.
    void Reset() noexcept;

private:
    struct State;
    GraphAssetSubscription(std::weak_ptr<State> state, std::uint64_t id) noexcept;
    std::weak_ptr<State> m_state;
    std::uint64_t m_id{0};

    friend class GraphAssetRegistry;
};

/// UI-thread registry of immutable asset generations with an acyclic dependency graph. Register
/// dependencies before their users; registered call sites must agree with the target's root interface.
/// Replacing content notifies transitive dependents without changing their records. A URI-only
/// replacement notifies the changed asset only. Generation overflow fails rather than wrapping.
class UNI_GUI_EXPORT GraphAssetRegistry final {
public:
    struct Impl;

    GraphAssetRegistry();
    ~GraphAssetRegistry();
    GraphAssetRegistry(GraphAssetRegistry&& other) noexcept;
    GraphAssetRegistry& operator=(GraphAssetRegistry&& other) noexcept;
    GraphAssetRegistry(const GraphAssetRegistry&) = delete;
    GraphAssetRegistry& operator=(const GraphAssetRegistry&) = delete;

    /// Validate and publish an immutable asset, synchronously notifying subscribers after commit.
    /// Insert requires absence; Replace requires presence; Upsert accepts either. Other modes
    /// return InvalidArgument. An identical upsert/replacement retains the record without notification.
    [[nodiscard]] Result<GraphAssetWriteResult> Write(GraphAsset asset, GraphAssetWriteMode mode);
    /// Check mode, asset and dependency compatibility against the current registry without
    /// mutation or notification. Like Write(), an unknown mode yields InvalidArgument.
    [[nodiscard]] Result<void> ValidateWrite(const GraphAsset& asset, GraphAssetWriteMode mode) const;
    /// Remove a registered asset without dependents. Reports AssetNotFound, AssetInUse or
    /// CommandFailed on reentrant mutation during dispatch. Previously retained records stay valid.
    [[nodiscard]] Result<void> Unregister(const GraphAssetId& id);
    /// Own the current publication; null when absent.
    [[nodiscard]] GraphAssetRecordPtr Find(const GraphAssetId& id) const noexcept;
    /// @name Dependency traversal
    /// Return sorted unique IDs; closures are transitive, exclude the starting asset and are not
    /// topological order. Dependencies are the assets used; dependents are their users. A registered
    /// leaf may yield an empty vector, whereas an unknown starting ID gives AssetNotFound.
    /// @{
    [[nodiscard]] Result<std::vector<GraphAssetId>> DirectDependencies(const GraphAssetId& id) const;
    [[nodiscard]] Result<std::vector<GraphAssetId>> DependencyClosure(const GraphAssetId& id) const;
    [[nodiscard]] Result<std::vector<GraphAssetId>> DirectDependents(const GraphAssetId& id) const;
    [[nodiscard]] Result<std::vector<GraphAssetId>> DependentClosure(const GraphAssetId& id) const;
    /// @}
    /// Audit every record, cached hash, interface, dependency and reverse index; detect missing
    /// targets or cycles. Expensive diagnostic, with no publication or callbacks.
    [[nodiscard]] Result<void> ValidateAll() const;
    /// Retain one callable until unsubscribed; its captured state persists across event batches.
    /// Each synchronous batch retains the subscribers present at its start, including those
    /// reset during dispatch. Empty callbacks and moved-from registries yield an inert handle.
    [[nodiscard]] GraphAssetSubscription Subscribe(GraphAssetChangeFn callback);

private:
    std::shared_ptr<Impl> m_impl;
};

/// Hash the canonical serialized asset, propagating SerializeGraphAssetJson() errors. URI-only
/// changes do not alter this hash, though they advance a registered asset's generation.
[[nodiscard]] UNI_GUI_EXPORT Result<GraphAssetContentHash> ComputeGraphAssetContentHash(
    const GraphAsset& asset);

/// Check a working document's external call sites against assets without registering the document.
/// Return diagnostics for missing targets, stale interfaces or unresolved nested dependencies;
/// empty means no such problems were found (not a full graph/type audit).
[[nodiscard]] UNI_GUI_EXPORT std::vector<ValidationIssue> ValidateGraphDependencies(
    const GraphDocument& document,
    const GraphAssetRegistry& assets);

} // namespace Uni::GUI::Nodes
