#pragma once

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

using GraphAssetGeneration = std::uint64_t;

struct GraphAssetContentHash final {
    std::array<std::uint8_t, 32> bytes{};

    [[nodiscard]] UNI_GUI_EXPORT std::string ToHex() const;
    auto operator<=>(const GraphAssetContentHash&) const = default;
};

struct GraphAsset final {
    GraphAssetId id;
    GraphDocument document;
    GraphPresentation presentation;
    std::string source_uri;
};

enum class GraphAssetWriteMode {
    Insert,
    Replace,
    Upsert,
};

struct GraphAssetDependencyUse final {
    GraphAssetId dependency;
    GraphInterface expected_interface;

    bool operator==(const GraphAssetDependencyUse&) const = default;
};

class GraphAssetRegistry;

class UNI_GUI_EXPORT GraphAssetRecord final {
public:
    [[nodiscard]] const GraphAsset& Asset() const noexcept;
    [[nodiscard]] GraphAssetGeneration Generation() const noexcept;
    [[nodiscard]] const GraphAssetContentHash& ContentHash() const noexcept;
    [[nodiscard]] std::span<const GraphAssetId> DirectDependencies() const noexcept;
    [[nodiscard]] std::span<const GraphAssetDependencyUse> DependencyUses() const noexcept;
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

enum class GraphAssetWriteStatus {
    Inserted,
    Replaced,
    Unchanged,
};

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

[[nodiscard]] constexpr bool HasGraphAssetChange(
    const GraphAssetChangeFlags changes,
    const GraphAssetChangeFlags flag) noexcept {
    return (changes & flag) != GraphAssetChangeFlags::None;
}

struct GraphAssetWriteResult final {
    GraphAssetWriteStatus status{GraphAssetWriteStatus::Unchanged};
    GraphAssetChangeFlags changes{GraphAssetChangeFlags::None};
    GraphAssetRecordPtr record;
};

enum class GraphAssetChangeKind {
    Inserted,
    Replaced,
    Removed,
    DependencyChanged,
};

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

using GraphAssetChangeFn = std::function<void(const GraphAssetChange&)>;

class UNI_GUI_EXPORT GraphAssetSubscription final {
public:
    GraphAssetSubscription() = default;
    ~GraphAssetSubscription();
    GraphAssetSubscription(GraphAssetSubscription&& other) noexcept;
    GraphAssetSubscription& operator=(GraphAssetSubscription&& other) noexcept;
    GraphAssetSubscription(const GraphAssetSubscription&) = delete;
    GraphAssetSubscription& operator=(const GraphAssetSubscription&) = delete;
    void Reset() noexcept;

private:
    struct State;
    GraphAssetSubscription(std::weak_ptr<State> state, std::uint64_t id) noexcept;
    std::weak_ptr<State> m_state;
    std::uint64_t m_id{0};

    friend class GraphAssetRegistry;
};

class UNI_GUI_EXPORT GraphAssetRegistry final {
public:
    struct Impl;

    GraphAssetRegistry();
    ~GraphAssetRegistry();
    GraphAssetRegistry(GraphAssetRegistry&& other) noexcept;
    GraphAssetRegistry& operator=(GraphAssetRegistry&& other) noexcept;
    GraphAssetRegistry(const GraphAssetRegistry&) = delete;
    GraphAssetRegistry& operator=(const GraphAssetRegistry&) = delete;

    [[nodiscard]] Result<GraphAssetWriteResult> Write(GraphAsset asset, GraphAssetWriteMode mode);
    [[nodiscard]] Result<void> ValidateWrite(const GraphAsset& asset, GraphAssetWriteMode mode) const;
    [[nodiscard]] Result<void> Unregister(const GraphAssetId& id);
    [[nodiscard]] GraphAssetRecordPtr Find(const GraphAssetId& id) const noexcept;
    [[nodiscard]] Result<std::vector<GraphAssetId>> DirectDependencies(const GraphAssetId& id) const;
    [[nodiscard]] Result<std::vector<GraphAssetId>> DependencyClosure(const GraphAssetId& id) const;
    [[nodiscard]] Result<std::vector<GraphAssetId>> DirectDependents(const GraphAssetId& id) const;
    [[nodiscard]] Result<std::vector<GraphAssetId>> DependentClosure(const GraphAssetId& id) const;
    [[nodiscard]] Result<void> ValidateAll() const;
    [[nodiscard]] GraphAssetSubscription Subscribe(GraphAssetChangeFn callback);

private:
    std::shared_ptr<Impl> m_impl;
};

[[nodiscard]] UNI_GUI_EXPORT Result<GraphAssetContentHash> ComputeGraphAssetContentHash(
    const GraphAsset& asset);

[[nodiscard]] UNI_GUI_EXPORT std::vector<ValidationIssue> ValidateGraphDependencies(
    const GraphDocument& document,
    const GraphAssetRegistry& assets);

} // namespace Uni::GUI::Nodes
