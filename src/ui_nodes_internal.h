#pragma once

#include <uni/gui/nodes/registry.h>

#include <cstddef>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace Uni::GUI::Nodes::Detail {

[[nodiscard]] inline Result<std::uint64_t> NextRegistryRevision(const std::uint64_t current,
                                                                const std::string_view domain) {
    if (current == std::numeric_limits<std::uint64_t>::max()) {
        return std::unexpected(Error{ErrorCode::GenerationOverflow, std::string{domain} + " revision is exhausted"});
    }
    return current + 1;
}

class RegistryAccess;

class RegistryInvocationSource final {
  public:
    RegistryInvocationSource() = default;
    RegistryInvocationSource(const RegistryInvocationSource&) = default;
    RegistryInvocationSource& operator=(const RegistryInvocationSource&) = default;
    RegistryInvocationSource(RegistryInvocationSource&&) noexcept = default;
    RegistryInvocationSource& operator=(RegistryInvocationSource&&) noexcept = default;
    [[nodiscard]] explicit operator bool() const noexcept {
        return static_cast<bool>(m_owner);
    }

  private:
    explicit RegistryInvocationSource(std::shared_ptr<RegistryOwner> owner) : m_owner(std::move(owner)) {}

    std::shared_ptr<RegistryOwner> m_owner;

    friend class RegistryAccess;
};

class RegistryInvocation final {
  public:
    RegistryInvocation(RegistryInvocation&&) noexcept = default;
    RegistryInvocation& operator=(RegistryInvocation&&) noexcept = default;
    RegistryInvocation(const RegistryInvocation&) = delete;
    RegistryInvocation& operator=(const RegistryInvocation&) = delete;
    [[nodiscard]] const RegistrySnapshot& Snapshot() const noexcept {
        return m_snapshot;
    }
    [[nodiscard]] const RegistryInvocationSource& Source() const noexcept {
        return m_source;
    }

  private:
    RegistryInvocation(RegistrySnapshot snapshot, RegistryInvocationSource source, std::shared_ptr<const void> lease)
        : m_snapshot(std::move(snapshot)), m_source(std::move(source)), m_lease(std::move(lease)) {}

    RegistrySnapshot m_snapshot;
    RegistryInvocationSource m_source;
    std::shared_ptr<const void> m_lease;

    friend class RegistryAccess;
};

class RegistryAccess final {
  public:
    [[nodiscard]] static RegistryInvocation Invoke(const RegistryCatalog& registry);
    [[nodiscard]] static RegistryInvocation Invoke(const RegistryInvocationSource& source);
    [[nodiscard]] static bool DependenciesCurrent(const RegistryInvocationSource& source,
                                                   const RegistrySnapshot& snapshot) noexcept;
    static void SealDependencies(const RegistrySnapshot& snapshot) noexcept;
    [[nodiscard]] static bool SameCatalog(const RegistryInvocationSource& left,
                                          const RegistryInvocationSource& right) noexcept;
    UNI_GUI_EXPORT static void SetRevisionsForTesting(RegistryCatalog& registry, std::uint64_t node_revision,
                                                      std::uint64_t conversion_revision, std::uint64_t generation,
                                                      std::uint64_t next_registration, bool registration_ids_exhausted);
};

[[nodiscard]] ConnectionResult ValidateConnection(const GraphDocument& document, const GraphPresentation& presentation,
                                                  const ConnectionRequest& request, const RegistrySnapshot& registry,
                                                  const GraphPolicy& policy = {},
                                                  std::span<const PinInstance> pending_pins = {},
                                                  std::span<const NodeInstance> pending_nodes = {});

void RecordJournalEntries(std::uint64_t entries) noexcept;
void RecordOperationIntent() noexcept;
void RecordCommandPath() noexcept;
void RecordIncrementalValidation(std::uint64_t records) noexcept;
void RecordFullStructureValidation() noexcept;
void RecordRouteCompaction(std::uint64_t chunk_merges, std::uint64_t points_reindexed) noexcept;
[[nodiscard]] bool ValidOpaqueJsonProperty(std::string_view json) noexcept;

[[nodiscard]] inline std::optional<GraphId> LocalSubgraph(const std::optional<SubgraphReference>& reference) noexcept {
    if (!reference) {
        return std::nullopt;
    }
    if (const auto* local = std::get_if<DocumentGraphTarget>(&reference->target)) {
        return local->graph;
    }
    return std::nullopt;
}

} // namespace Uni::GUI::Nodes::Detail
