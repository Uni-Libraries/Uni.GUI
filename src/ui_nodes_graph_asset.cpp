#include <uni/gui/nodes/graph_asset.h>
#include <uni/gui/nodes/io.h>

#include <algorithm>
#include <iterator>
#include <limits>
#include <map>
#include <set>
#include <utility>

namespace Uni::GUI::Nodes {
namespace {

[[nodiscard]] Error MakeError(const ErrorCode code, std::string message) {
    return Error{code, std::move(message)};
}

[[nodiscard]] std::array<std::uint8_t, 32> Sha256(const std::string_view input) {
    constexpr std::array<std::uint32_t, 64> constants{
        0x428A2F98U, 0x71374491U, 0xB5C0FBCFU, 0xE9B5DBA5U, 0x3956C25BU, 0x59F111F1U, 0x923F82A4U, 0xAB1C5ED5U,
        0xD807AA98U, 0x12835B01U, 0x243185BEU, 0x550C7DC3U, 0x72BE5D74U, 0x80DEB1FEU, 0x9BDC06A7U, 0xC19BF174U,
        0xE49B69C1U, 0xEFBE4786U, 0x0FC19DC6U, 0x240CA1CCU, 0x2DE92C6FU, 0x4A7484AAU, 0x5CB0A9DCU, 0x76F988DAU,
        0x983E5152U, 0xA831C66DU, 0xB00327C8U, 0xBF597FC7U, 0xC6E00BF3U, 0xD5A79147U, 0x06CA6351U, 0x14292967U,
        0x27B70A85U, 0x2E1B2138U, 0x4D2C6DFCU, 0x53380D13U, 0x650A7354U, 0x766A0ABBU, 0x81C2C92EU, 0x92722C85U,
        0xA2BFE8A1U, 0xA81A664BU, 0xC24B8B70U, 0xC76C51A3U, 0xD192E819U, 0xD6990624U, 0xF40E3585U, 0x106AA070U,
        0x19A4C116U, 0x1E376C08U, 0x2748774CU, 0x34B0BCB5U, 0x391C0CB3U, 0x4ED8AA4AU, 0x5B9CCA4FU, 0x682E6FF3U,
        0x748F82EEU, 0x78A5636FU, 0x84C87814U, 0x8CC70208U, 0x90BEFFFAU, 0xA4506CEBU, 0xBEF9A3F7U, 0xC67178F2U,
    };
    std::vector<std::uint8_t> message(input.begin(), input.end());
    const std::uint64_t bit_size = static_cast<std::uint64_t>(message.size()) * 8U;
    message.push_back(0x80U);
    while (message.size() % 64U != 56U) message.push_back(0U);
    for (int shift = 56; shift >= 0; shift -= 8) message.push_back(static_cast<std::uint8_t>(bit_size >> shift));

    std::array<std::uint32_t, 8> state{
        0x6A09E667U, 0xBB67AE85U, 0x3C6EF372U, 0xA54FF53AU,
        0x510E527FU, 0x9B05688CU, 0x1F83D9ABU, 0x5BE0CD19U,
    };
    const auto rotate = [](const std::uint32_t value, const unsigned bits) {
        return (value >> bits) | (value << (32U - bits));
    };
    for (std::size_t offset = 0; offset < message.size(); offset += 64U) {
        std::array<std::uint32_t, 64> words{};
        for (std::size_t index = 0; index < 16; ++index) {
            const std::size_t byte = offset + index * 4U;
            words[index] = (static_cast<std::uint32_t>(message[byte]) << 24U) |
                (static_cast<std::uint32_t>(message[byte + 1]) << 16U) |
                (static_cast<std::uint32_t>(message[byte + 2]) << 8U) | message[byte + 3];
        }
        for (std::size_t index = 16; index < words.size(); ++index) {
            const std::uint32_t first = rotate(words[index - 15], 7) ^ rotate(words[index - 15], 18) ^ (words[index - 15] >> 3U);
            const std::uint32_t second = rotate(words[index - 2], 17) ^ rotate(words[index - 2], 19) ^ (words[index - 2] >> 10U);
            words[index] = words[index - 16] + first + words[index - 7] + second;
        }
        auto [a, b, c, d, e, f, g, h] = state;
        for (std::size_t index = 0; index < words.size(); ++index) {
            const std::uint32_t choose = (e & f) ^ (~e & g);
            const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
            const std::uint32_t first = h + (rotate(e, 6) ^ rotate(e, 11) ^ rotate(e, 25)) + choose + constants[index] + words[index];
            const std::uint32_t second = (rotate(a, 2) ^ rotate(a, 13) ^ rotate(a, 22)) + majority;
            h = g; g = f; f = e; e = d + first; d = c; c = b; b = a; a = first + second;
        }
        state[0] += a; state[1] += b; state[2] += c; state[3] += d;
        state[4] += e; state[5] += f; state[6] += g; state[7] += h;
    }
    std::array<std::uint8_t, 32> output{};
    for (std::size_t index = 0; index < state.size(); ++index) {
        for (std::size_t byte = 0; byte < 4; ++byte) {
            output[index * 4 + byte] = static_cast<std::uint8_t>(state[index] >> (24U - byte * 8U));
        }
    }
    return output;
}

} // namespace

struct GraphAssetSubscription::State final {
    std::map<std::uint64_t, GraphAssetChangeFn> callbacks;
    std::uint64_t next_id{1};
};

GraphAssetSubscription::GraphAssetSubscription(std::weak_ptr<State> state, const std::uint64_t id) noexcept
    : m_state(std::move(state)), m_id(id) {}
GraphAssetSubscription::~GraphAssetSubscription() { Reset(); }
GraphAssetSubscription::GraphAssetSubscription(GraphAssetSubscription&& other) noexcept
    : m_state(std::move(other.m_state)), m_id(std::exchange(other.m_id, 0)) {}
GraphAssetSubscription& GraphAssetSubscription::operator=(GraphAssetSubscription&& other) noexcept {
    if (this != &other) {
        Reset();
        m_state = std::move(other.m_state);
        m_id = std::exchange(other.m_id, 0);
    }
    return *this;
}
void GraphAssetSubscription::Reset() noexcept {
    if (m_id != 0) {
        if (const auto state = m_state.lock()) state->callbacks.erase(m_id);
        m_id = 0;
        m_state.reset();
    }
}

GraphAssetRecord::GraphAssetRecord(
    GraphAsset&& asset,
    const GraphAssetGeneration generation,
    GraphAssetContentHash content_hash,
    std::vector<GraphAssetId> dependencies,
    std::vector<GraphAssetDependencyUse> dependency_uses,
    GraphInterface root_interface)
    : m_asset(std::move(asset)),
      m_generation(generation),
      m_content_hash(content_hash),
      m_dependencies(std::move(dependencies)),
      m_dependency_uses(std::move(dependency_uses)),
      m_root_interface(std::move(root_interface)) {}

const GraphAsset& GraphAssetRecord::Asset() const noexcept { return m_asset; }
GraphAssetGeneration GraphAssetRecord::Generation() const noexcept { return m_generation; }
const GraphAssetContentHash& GraphAssetRecord::ContentHash() const noexcept { return m_content_hash; }
std::span<const GraphAssetId> GraphAssetRecord::DirectDependencies() const noexcept { return m_dependencies; }
std::span<const GraphAssetDependencyUse> GraphAssetRecord::DependencyUses() const noexcept {
    return m_dependency_uses;
}
const GraphInterface& GraphAssetRecord::RootInterface() const noexcept { return m_root_interface; }

struct GraphAssetRegistry::Impl final {
    using RecordMap = std::map<GraphAssetId, GraphAssetRecordPtr>;
    using DependentSet = std::set<GraphAssetId>;
    using DependentSetPtr = std::shared_ptr<const DependentSet>;
    using ReverseMap = std::map<GraphAssetId, DependentSetPtr>;

    struct State final {
        RecordMap records;
        ReverseMap reverse;
    };

    std::shared_ptr<const State> state{std::make_shared<State>()};
    std::shared_ptr<GraphAssetSubscription::State> subscriptions{std::make_shared<GraphAssetSubscription::State>()};
    std::uint64_t dispatch_depth{0};
    std::uint64_t dispatch_epoch{0};
};

namespace {

struct RecordMetadata final {
    std::vector<GraphAssetId> dependencies;
    std::vector<GraphAssetDependencyUse> dependency_uses;
    GraphInterface root_interface;
};

struct PreparedWrite final {
    const GraphAsset* asset{nullptr};
    GraphAssetRecordPtr existing;
    GraphAssetGeneration generation{0};
    GraphAssetContentHash content_hash;
    RecordMetadata metadata;
    GraphAssetWriteStatus status{GraphAssetWriteStatus::Unchanged};
    GraphAssetChangeFlags changes{GraphAssetChangeFlags::None};
};

[[nodiscard]] Error AssetNotFoundError(const GraphAssetId& id) {
    return MakeError(ErrorCode::AssetNotFound, "Graph asset '" + id.Value() + "' is not registered");
}

[[nodiscard]] Error MovedFromRegistryError() {
    return MakeError(ErrorCode::CommandFailed, "Graph asset registry is in a moved-from state");
}

[[nodiscard]] Error ReentrantMutationError() {
    return MakeError(ErrorCode::CommandFailed, "Graph asset registry cannot be mutated from a change callback");
}

[[nodiscard]] Result<RecordMetadata> CollectRecordMetadata(const GraphAsset& asset) {
    if (auto valid = asset.document.ValidateStructure(); !valid) return std::unexpected(valid.error());
    if (auto valid = ValidateGraphPresentation(asset.document, asset.presentation); !valid) {
        return std::unexpected(valid.error());
    }
    const auto* root = asset.document.FindGraph(asset.document.RootGraph());
    if (root == nullptr) {
        return std::unexpected(MakeError(ErrorCode::InvalidGraph, "Graph asset root graph does not exist"));
    }

    std::set<GraphAssetId> dependencies;
    std::vector<GraphAssetDependencyUse> dependency_uses;
    for (const auto& graph_reference : asset.document.Graphs()) {
        for (const auto& [node_id, node] : graph_reference.get().nodes) {
            (void)node_id;
            if (!node.subgraph) continue;
            const auto* target = std::get_if<GraphAssetTarget>(&node.subgraph->target);
            if (target == nullptr) continue;
            dependencies.insert(target->asset);
            dependency_uses.push_back(GraphAssetDependencyUse{
                .dependency = target->asset,
                .expected_interface = target->interface,
            });
        }
    }
    return RecordMetadata{
        .dependencies = {dependencies.begin(), dependencies.end()},
        .dependency_uses = std::move(dependency_uses),
        .root_interface = root->interface,
    };
}

[[nodiscard]] Result<PreparedWrite> PrepareWrite(
    const GraphAssetRegistry::Impl::State& state,
    const GraphAsset& asset,
    const GraphAssetWriteMode mode) {
    if (asset.id.Empty()) {
        return std::unexpected(MakeError(ErrorCode::InvalidArgument, "Graph asset ID is required"));
    }
    const auto found = state.records.find(asset.id);
    if (mode == GraphAssetWriteMode::Insert && found != state.records.end()) {
        return std::unexpected(MakeError(ErrorCode::DuplicateId, "Graph asset ID is already registered"));
    }
    if (mode == GraphAssetWriteMode::Replace && found == state.records.end()) {
        return std::unexpected(AssetNotFoundError(asset.id));
    }

    auto metadata = CollectRecordMetadata(asset);
    if (!metadata) return std::unexpected(metadata.error());
    auto content_hash = ComputeGraphAssetContentHash(asset);
    if (!content_hash) return std::unexpected(content_hash.error());

    PreparedWrite prepared{
        .asset = &asset,
        .existing = found == state.records.end() ? nullptr : found->second,
        .content_hash = *content_hash,
        .metadata = std::move(*metadata),
    };
    if (!prepared.existing) {
        prepared.generation = 1;
        prepared.status = GraphAssetWriteStatus::Inserted;
        prepared.changes = GraphAssetChangeFlags::Content | GraphAssetChangeFlags::RootInterface;
        if (!asset.source_uri.empty()) prepared.changes |= GraphAssetChangeFlags::SourceUri;
        if (!prepared.metadata.dependencies.empty()) prepared.changes |= GraphAssetChangeFlags::Dependencies;
        return prepared;
    }

    const auto& existing = *prepared.existing;
    if (existing.ContentHash() != prepared.content_hash) prepared.changes |= GraphAssetChangeFlags::Content;
    if (existing.Asset().source_uri != asset.source_uri) prepared.changes |= GraphAssetChangeFlags::SourceUri;
    if (!std::ranges::equal(existing.DirectDependencies(), prepared.metadata.dependencies)) {
        prepared.changes |= GraphAssetChangeFlags::Dependencies;
    }
    if (existing.RootInterface() != prepared.metadata.root_interface) {
        prepared.changes |= GraphAssetChangeFlags::RootInterface;
    }
    if (prepared.changes == GraphAssetChangeFlags::None) {
        prepared.generation = existing.Generation();
        prepared.status = GraphAssetWriteStatus::Unchanged;
        return prepared;
    }
    if (existing.Generation() == std::numeric_limits<GraphAssetGeneration>::max()) {
        return std::unexpected(MakeError(ErrorCode::GenerationOverflow, "Graph asset generation is exhausted"));
    }
    prepared.generation = existing.Generation() + 1;
    prepared.status = GraphAssetWriteStatus::Replaced;
    return prepared;
}

[[nodiscard]] const GraphInterface* FindCandidateRootInterface(
    const GraphAssetRegistry::Impl::State& state,
    const PreparedWrite& prepared,
    const GraphAssetId& id) {
    if (id == prepared.asset->id) return &prepared.metadata.root_interface;
    const auto found = state.records.find(id);
    return found == state.records.end() ? nullptr : &found->second->RootInterface();
}

[[nodiscard]] std::span<const GraphAssetId> FindCandidateDependencies(
    const GraphAssetRegistry::Impl::State& state,
    const PreparedWrite& prepared,
    const GraphAssetId& id) {
    if (id == prepared.asset->id) return prepared.metadata.dependencies;
    return state.records.at(id)->DirectDependencies();
}

[[nodiscard]] Result<void> ValidateDependencyUses(
    const GraphAssetRegistry::Impl::State& state,
    const PreparedWrite& prepared,
    const GraphAssetId& owner,
    const std::span<const GraphAssetDependencyUse> uses) {
    for (const auto& use : uses) {
        const auto* dependency_interface = FindCandidateRootInterface(state, prepared, use.dependency);
        if (dependency_interface == nullptr) {
            return std::unexpected(MakeError(
                ErrorCode::AssetNotFound,
                "Graph asset '" + owner.Value() + "' depends on missing asset '" + use.dependency.Value() + "'"));
        }
        if (*dependency_interface != use.expected_interface) {
            return std::unexpected(MakeError(
                ErrorCode::InvalidGraph,
                "Graph asset '" + owner.Value() + "' has a stale interface for dependency '" +
                    use.dependency.Value() + "'"));
        }
    }
    return {};
}

[[nodiscard]] std::vector<GraphAssetId> CollectDependentClosure(
    const GraphAssetRegistry::Impl::State& state,
    const GraphAssetId& id) {
    std::set<GraphAssetId> closure;
    std::vector<GraphAssetId> pending{id};
    while (!pending.empty()) {
        const GraphAssetId current = pending.back();
        pending.pop_back();
        const auto found = state.reverse.find(current);
        if (found == state.reverse.end()) continue;
        for (const auto& dependent : *found->second) {
            if (closure.insert(dependent).second) pending.push_back(dependent);
        }
    }
    return {closure.begin(), closure.end()};
}

[[nodiscard]] Result<void> ValidateCandidate(
    const GraphAssetRegistry::Impl::State& state,
    const PreparedWrite& prepared) {
    const GraphAssetId& replacement = prepared.asset->id;
    if (auto valid = ValidateDependencyUses(
            state, prepared, replacement, prepared.metadata.dependency_uses);
        !valid) {
        return valid;
    }

    std::set<GraphAssetId> visited;
    std::vector<GraphAssetId> pending(
        prepared.metadata.dependencies.begin(), prepared.metadata.dependencies.end());
    while (!pending.empty()) {
        const GraphAssetId current = pending.back();
        pending.pop_back();
        if (current == replacement) {
            return std::unexpected(MakeError(
                ErrorCode::InvalidGraph,
                "Graph asset dependencies contain a recursive cycle at '" + replacement.Value() + "'"));
        }
        if (!visited.insert(current).second) continue;
        if (!state.records.contains(current)) {
            return std::unexpected(MakeError(
                ErrorCode::AssetNotFound,
                "Graph asset '" + replacement.Value() + "' depends on missing asset '" + current.Value() + "'"));
        }
        const auto dependencies = FindCandidateDependencies(state, prepared, current);
        pending.insert(pending.end(), dependencies.begin(), dependencies.end());
    }

    for (const auto& dependent : CollectDependentClosure(state, replacement)) {
        const auto found = state.records.find(dependent);
        if (found == state.records.end()) continue;
        if (auto valid = ValidateDependencyUses(
                state, prepared, dependent, found->second->DependencyUses());
            !valid) {
            return valid;
        }
    }
    return {};
}

[[nodiscard]] Result<void> ValidateRegistryState(const GraphAssetRegistry::Impl::State& state) {
    for (const auto& [id, record] : state.records) {
        if (!record || record->Asset().id != id) {
            return std::unexpected(MakeError(ErrorCode::InvalidGraph, "Graph asset record identity is inconsistent"));
        }
        if (record->Generation() == 0) {
            return std::unexpected(MakeError(ErrorCode::InvalidGraph, "Graph asset record generation is invalid"));
        }
        auto metadata = CollectRecordMetadata(record->Asset());
        if (!metadata) return std::unexpected(metadata.error());
        auto content_hash = ComputeGraphAssetContentHash(record->Asset());
        if (!content_hash) return std::unexpected(content_hash.error());
        if (!std::ranges::equal(record->DirectDependencies(), metadata->dependencies) ||
            !std::ranges::equal(record->DependencyUses(), metadata->dependency_uses) ||
            record->RootInterface() != metadata->root_interface ||
            record->ContentHash() != *content_hash) {
            return std::unexpected(MakeError(ErrorCode::InvalidGraph, "Graph asset record cache is inconsistent"));
        }
        for (const auto& use : record->DependencyUses()) {
            const auto dependency = state.records.find(use.dependency);
            if (dependency == state.records.end()) {
                return std::unexpected(MakeError(
                    ErrorCode::AssetNotFound,
                    "Graph asset '" + id.Value() + "' depends on missing asset '" + use.dependency.Value() + "'"));
            }
            if (dependency->second->RootInterface() != use.expected_interface) {
                return std::unexpected(MakeError(
                    ErrorCode::InvalidGraph,
                    "Graph asset '" + id.Value() + "' has a stale interface for dependency '" +
                        use.dependency.Value() + "'"));
            }
        }
        const auto reverse = state.reverse.find(id);
        if (reverse == state.reverse.end() || !reverse->second) {
            return std::unexpected(MakeError(ErrorCode::InvalidGraph, "Graph asset reverse index is incomplete"));
        }
        for (const auto& dependency : record->DirectDependencies()) {
            const auto bucket = state.reverse.find(dependency);
            if (bucket == state.reverse.end() || !bucket->second->contains(id)) {
                return std::unexpected(MakeError(ErrorCode::InvalidGraph, "Graph asset reverse index is inconsistent"));
            }
        }
    }
    for (const auto& [dependency, dependents] : state.reverse) {
        if (!state.records.contains(dependency) || !dependents) {
            return std::unexpected(MakeError(ErrorCode::InvalidGraph, "Graph asset reverse index is inconsistent"));
        }
        for (const auto& dependent : *dependents) {
            const auto record = state.records.find(dependent);
            const auto direct = record == state.records.end()
                ? std::span<const GraphAssetId>{}
                : record->second->DirectDependencies();
            if (record == state.records.end() || std::ranges::find(direct, dependency) == direct.end()) {
                return std::unexpected(MakeError(ErrorCode::InvalidGraph, "Graph asset reverse index is inconsistent"));
            }
        }
    }

    enum class Mark { Visiting, Visited };
    struct Frame final {
        GraphAssetId id;
        std::size_t next_dependency{0};
    };
    std::map<GraphAssetId, Mark> marks;
    for (const auto& [root, record] : state.records) {
        (void)record;
        if (marks.contains(root)) continue;
        marks.emplace(root, Mark::Visiting);
        std::vector<Frame> stack{{root, 0}};
        while (!stack.empty()) {
            auto& frame = stack.back();
            const auto dependencies = state.records.at(frame.id)->DirectDependencies();
            if (frame.next_dependency == dependencies.size()) {
                marks.insert_or_assign(frame.id, Mark::Visited);
                stack.pop_back();
                continue;
            }
            const GraphAssetId dependency = dependencies[frame.next_dependency++];
            const auto mark = marks.find(dependency);
            if (mark != marks.end()) {
                if (mark->second == Mark::Visiting) {
                    return std::unexpected(MakeError(
                        ErrorCode::InvalidGraph,
                        "Graph asset dependencies contain a recursive cycle at '" + dependency.Value() + "'"));
                }
                continue;
            }
            marks.emplace(dependency, Mark::Visiting);
            stack.push_back(Frame{dependency, 0});
        }
    }
    return {};
}

[[nodiscard]] GraphAssetRegistry::Impl::DependentSet CopyReverseBucket(
    const GraphAssetRegistry::Impl::State& state,
    const GraphAssetId& dependency) {
    const auto found = state.reverse.find(dependency);
    return found == state.reverse.end() || !found->second
        ? GraphAssetRegistry::Impl::DependentSet{}
        : *found->second;
}

void ApplyDependencyDelta(
    GraphAssetRegistry::Impl::State& candidate,
    const GraphAssetId& asset,
    const std::span<const GraphAssetId> old_dependencies,
    const std::span<const GraphAssetId> new_dependencies) {
    std::vector<GraphAssetId> removed;
    std::vector<GraphAssetId> added;
    std::ranges::set_difference(old_dependencies, new_dependencies, std::back_inserter(removed));
    std::ranges::set_difference(new_dependencies, old_dependencies, std::back_inserter(added));
    for (const auto& dependency : removed) {
        auto bucket = CopyReverseBucket(candidate, dependency);
        bucket.erase(asset);
        candidate.reverse.insert_or_assign(
            dependency,
            std::make_shared<const GraphAssetRegistry::Impl::DependentSet>(std::move(bucket)));
    }
    for (const auto& dependency : added) {
        auto bucket = CopyReverseBucket(candidate, dependency);
        bucket.insert(asset);
        candidate.reverse.insert_or_assign(
            dependency,
            std::make_shared<const GraphAssetRegistry::Impl::DependentSet>(std::move(bucket)));
    }
}

[[nodiscard]] std::vector<GraphAssetChangeFn> SnapshotCallbacks(const GraphAssetRegistry::Impl& impl) {
    std::vector<GraphAssetChangeFn> callbacks;
    callbacks.reserve(impl.subscriptions->callbacks.size());
    for (const auto& [id, callback] : impl.subscriptions->callbacks) {
        (void)id;
        callbacks.push_back(callback);
    }
    return callbacks;
}

class DispatchGuard final {
public:
    explicit DispatchGuard(std::shared_ptr<GraphAssetRegistry::Impl> impl) noexcept
        : m_impl(std::move(impl)) {
        ++m_impl->dispatch_depth;
        ++m_impl->dispatch_epoch;
    }

    ~DispatchGuard() { --m_impl->dispatch_depth; }

private:
    std::shared_ptr<GraphAssetRegistry::Impl> m_impl;
};

void Dispatch(
    std::shared_ptr<GraphAssetRegistry::Impl> impl,
    const std::vector<GraphAssetChange>& events,
    const std::vector<GraphAssetChangeFn>& callbacks) noexcept {
    DispatchGuard guard{std::move(impl)};
    for (const auto& event : events) {
        for (const auto& callback : callbacks) {
            try {
                callback(event);
            } catch (...) {
            }
        }
    }
}

[[nodiscard]] GraphAssetChangeFlags RemovalFlags(const GraphAssetRecord& record) {
    auto flags = GraphAssetChangeFlags::Content | GraphAssetChangeFlags::RootInterface;
    if (!record.Asset().source_uri.empty()) flags |= GraphAssetChangeFlags::SourceUri;
    if (!record.DirectDependencies().empty()) flags |= GraphAssetChangeFlags::Dependencies;
    return flags;
}

} // namespace

std::string GraphAssetContentHash::ToHex() const {
    constexpr std::string_view digits = "0123456789abcdef";
    std::string output;
    output.reserve(bytes.size() * 2);
    for (const auto byte : bytes) {
        output.push_back(digits[byte >> 4U]);
        output.push_back(digits[byte & 0x0FU]);
    }
    return output;
}

GraphAssetRegistry::GraphAssetRegistry() : m_impl(std::make_shared<Impl>()) {}
GraphAssetRegistry::~GraphAssetRegistry() = default;
GraphAssetRegistry::GraphAssetRegistry(GraphAssetRegistry&& other) noexcept = default;
GraphAssetRegistry& GraphAssetRegistry::operator=(GraphAssetRegistry&& other) noexcept = default;

Result<GraphAssetContentHash> ComputeGraphAssetContentHash(const GraphAsset& asset) {
    auto serialized = SerializeGraphAssetJson(asset);
    if (!serialized) return std::unexpected(serialized.error());
    return GraphAssetContentHash{.bytes = Sha256(*serialized)};
}

Result<GraphAssetWriteResult> GraphAssetRegistry::Write(GraphAsset asset, const GraphAssetWriteMode mode) {
    const auto impl = m_impl;
    if (!impl) return std::unexpected(MovedFromRegistryError());
    if (impl->dispatch_depth != 0) return std::unexpected(ReentrantMutationError());
    const auto state = impl->state;
    auto prepared = PrepareWrite(*state, asset, mode);
    if (!prepared) return std::unexpected(prepared.error());
    if (prepared->status == GraphAssetWriteStatus::Unchanged) {
        return GraphAssetWriteResult{
            .status = prepared->status,
            .changes = prepared->changes,
            .record = prepared->existing,
        };
    }
    if (auto valid = ValidateCandidate(*state, *prepared); !valid) {
        return std::unexpected(valid.error());
    }

    std::vector<GraphAssetId> dependents;
    if (prepared->status == GraphAssetWriteStatus::Replaced &&
        HasGraphAssetChange(prepared->changes, GraphAssetChangeFlags::Content)) {
        dependents = CollectDependentClosure(*state, asset.id);
    }
    const GraphAssetId id = asset.id;
    const auto before = prepared->existing;
    GraphAssetRecordPtr record{new GraphAssetRecord(
        std::move(asset),
        prepared->generation,
        prepared->content_hash,
        std::move(prepared->metadata.dependencies),
        std::move(prepared->metadata.dependency_uses),
        std::move(prepared->metadata.root_interface))};

    auto candidate = std::make_shared<Impl::State>(*state);
    const std::span<const GraphAssetId> old_dependencies = before
        ? before->DirectDependencies()
        : std::span<const GraphAssetId>{};
    ApplyDependencyDelta(*candidate, id, old_dependencies, record->DirectDependencies());
    candidate->records.insert_or_assign(id, record);
    if (!candidate->reverse.contains(id)) {
        candidate->reverse.emplace(id, std::make_shared<const Impl::DependentSet>());
    }

    std::vector<GraphAssetChange> events;
    events.reserve(1 + dependents.size());
    events.push_back(GraphAssetChange{
        .kind = prepared->status == GraphAssetWriteStatus::Inserted
            ? GraphAssetChangeKind::Inserted
            : GraphAssetChangeKind::Replaced,
        .asset = id,
        .before = before,
        .after = record,
        .changes = prepared->changes,
        .changed_dependency = {},
        .dependency_before = nullptr,
        .dependency_after = nullptr,
    });
    for (const auto& dependent : dependents) {
        const auto affected = state->records.at(dependent);
        events.push_back(GraphAssetChange{
            .kind = GraphAssetChangeKind::DependencyChanged,
            .asset = dependent,
            .before = affected,
            .after = affected,
            .changes = prepared->changes,
            .changed_dependency = id,
            .dependency_before = before,
            .dependency_after = record,
        });
    }
    const auto callbacks = SnapshotCallbacks(*impl);
    impl->state = std::move(candidate);
    Dispatch(impl, events, callbacks);
    return GraphAssetWriteResult{
        .status = prepared->status,
        .changes = prepared->changes,
        .record = std::move(record),
    };
}

Result<void> GraphAssetRegistry::ValidateWrite(const GraphAsset& asset, const GraphAssetWriteMode mode) const {
    const auto impl = m_impl;
    if (!impl) return std::unexpected(MovedFromRegistryError());
    const auto state = impl->state;
    auto prepared = PrepareWrite(*state, asset, mode);
    if (!prepared) return std::unexpected(prepared.error());
    if (prepared->status == GraphAssetWriteStatus::Unchanged) return {};
    return ValidateCandidate(*state, *prepared);
}

Result<void> GraphAssetRegistry::Unregister(const GraphAssetId& id) {
    const auto impl = m_impl;
    if (!impl) return std::unexpected(MovedFromRegistryError());
    if (impl->dispatch_depth != 0) return std::unexpected(ReentrantMutationError());
    const auto state = impl->state;
    const auto found = state->records.find(id);
    if (found == state->records.end()) return std::unexpected(AssetNotFoundError(id));
    const auto reverse = state->reverse.find(id);
    if (reverse != state->reverse.end() && !reverse->second->empty()) {
        return std::unexpected(MakeError(ErrorCode::AssetInUse, "Graph asset has registered dependents"));
    }

    const auto before = found->second;
    auto candidate = std::make_shared<Impl::State>(*state);
    ApplyDependencyDelta(
        *candidate,
        id,
        before->DirectDependencies(),
        std::span<const GraphAssetId>{});
    candidate->records.erase(id);
    candidate->reverse.erase(id);
    std::vector<GraphAssetChange> events{{
        .kind = GraphAssetChangeKind::Removed,
        .asset = id,
        .before = before,
        .after = nullptr,
        .changes = RemovalFlags(*before),
        .changed_dependency = {},
        .dependency_before = nullptr,
        .dependency_after = nullptr,
    }};
    const auto callbacks = SnapshotCallbacks(*impl);
    impl->state = std::move(candidate);
    Dispatch(impl, events, callbacks);
    return {};
}

GraphAssetRecordPtr GraphAssetRegistry::Find(const GraphAssetId& id) const noexcept {
    const auto impl = m_impl;
    if (!impl) return nullptr;
    const auto state = impl->state;
    const auto found = state->records.find(id);
    return found == state->records.end() ? nullptr : found->second;
}

Result<std::vector<GraphAssetId>> GraphAssetRegistry::DirectDependencies(const GraphAssetId& id) const {
    const auto impl = m_impl;
    if (!impl) return std::unexpected(AssetNotFoundError(id));
    const auto state = impl->state;
    const auto found = state->records.find(id);
    if (found == state->records.end()) return std::unexpected(AssetNotFoundError(id));
    return std::vector<GraphAssetId>{
        found->second->DirectDependencies().begin(),
        found->second->DirectDependencies().end(),
    };
}

Result<std::vector<GraphAssetId>> GraphAssetRegistry::DependencyClosure(const GraphAssetId& id) const {
    const auto impl = m_impl;
    if (!impl) return std::unexpected(AssetNotFoundError(id));
    const auto state = impl->state;
    if (!state->records.contains(id)) return std::unexpected(AssetNotFoundError(id));
    std::set<GraphAssetId> closure;
    std::vector<GraphAssetId> pending{id};
    while (!pending.empty()) {
        const GraphAssetId current = pending.back();
        pending.pop_back();
        for (const auto& dependency : state->records.at(current)->DirectDependencies()) {
            if (closure.insert(dependency).second) pending.push_back(dependency);
        }
    }
    return std::vector<GraphAssetId>{closure.begin(), closure.end()};
}

Result<std::vector<GraphAssetId>> GraphAssetRegistry::DirectDependents(const GraphAssetId& id) const {
    const auto impl = m_impl;
    if (!impl) return std::unexpected(AssetNotFoundError(id));
    const auto state = impl->state;
    if (!state->records.contains(id)) return std::unexpected(AssetNotFoundError(id));
    const auto found = state->reverse.find(id);
    if (found == state->reverse.end()) return std::vector<GraphAssetId>{};
    return std::vector<GraphAssetId>{found->second->begin(), found->second->end()};
}

Result<std::vector<GraphAssetId>> GraphAssetRegistry::DependentClosure(const GraphAssetId& id) const {
    const auto impl = m_impl;
    if (!impl) return std::unexpected(AssetNotFoundError(id));
    const auto state = impl->state;
    if (!state->records.contains(id)) return std::unexpected(AssetNotFoundError(id));
    return CollectDependentClosure(*state, id);
}

Result<void> GraphAssetRegistry::ValidateAll() const {
    const auto impl = m_impl;
    if (!impl) return std::unexpected(MovedFromRegistryError());
    const auto state = impl->state;
    return ValidateRegistryState(*state);
}

GraphAssetSubscription GraphAssetRegistry::Subscribe(GraphAssetChangeFn callback) {
    const auto impl = m_impl;
    if (!impl || !callback) return {};
    auto& state = *impl->subscriptions;
    while (state.next_id == 0 || state.callbacks.contains(state.next_id)) ++state.next_id;
    const auto id = state.next_id++;
    state.callbacks.emplace(id, std::move(callback));
    return GraphAssetSubscription{impl->subscriptions, id};
}

std::vector<ValidationIssue> ValidateGraphDependencies(const GraphDocument& document, const GraphAssetRegistry& assets) {
    std::vector<ValidationIssue> issues;
    std::set<GraphAssetId> validated;
    for (const auto& graph_reference : document.Graphs()) {
        const auto& graph = graph_reference.get();
        for (const auto& [node_id, node] : graph.nodes) {
            if (!node.subgraph) continue;
            const auto* target = std::get_if<GraphAssetTarget>(&node.subgraph->target);
            if (target == nullptr) continue;
            const auto asset = assets.Find(target->asset);
            if (!asset) {
                ValidationIssue issue;
                issue.severity = ValidationSeverity::Error;
                issue.message = "Graph asset '" + target->asset.Value() + "' is not registered";
                issue.graph = graph.id;
                issue.node = node_id;
                issues.push_back(std::move(issue));
                continue;
            }
            if (asset->RootInterface() != target->interface) {
                ValidationIssue issue;
                issue.severity = ValidationSeverity::Error;
                issue.message = "Graph asset interface does not match the stored call-site contract";
                issue.graph = graph.id;
                issue.node = node_id;
                issues.push_back(std::move(issue));
            }
            if (!validated.insert(target->asset).second) continue;
            const auto closure = assets.DependencyClosure(target->asset);
            if (!closure) {
                ValidationIssue issue;
                issue.severity = ValidationSeverity::Error;
                issue.message = closure.error().message;
                issue.graph = graph.id;
                issue.node = node_id;
                issues.push_back(std::move(issue));
            }
        }
    }
    return issues;
}

} // namespace Uni::GUI::Nodes
