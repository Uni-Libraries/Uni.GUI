#pragma once

#include <uni/gui/export.h>
#include <uni/gui/nodes/error.h>
#include <uni/gui/nodes/ids.h>

#include <algorithm>
#include <array>
#include <compare>
#include <cstdint>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace Uni::GUI::Nodes {

class CommandStack;
class GraphDocument;
class GraphDocumentSnapshot;
class GraphTransaction;
struct Graph;
[[nodiscard]] UNI_GUI_EXPORT GraphDocumentSnapshot CaptureGraphDocumentSnapshot(const GraphDocument& document);
namespace Detail {
struct GraphIoAccess;

enum class CowCopyDomain : std::uint8_t {
    None,
    Graphs,
    GraphRevisions,
    Nodes,
    Pins,
    Links,
    IntergraphLinks,
    NodePresentations,
    LinkPresentations,
    Groups,
    GroupStyles,
    GroupMemberships,
    RoutePointSequences,
    PresentationIndexes,
    SemanticIndexes,
};

enum class CowCloneKind : std::uint8_t {
    Root,
    Directory,
    Shard,
    Value,
};

UNI_GUI_EXPORT void RecordCowClone(CowCopyDomain domain, CowCloneKind kind, std::uint64_t copied_handles,
                                   std::uint64_t logical_bytes) noexcept;
} // namespace Detail

struct Vec2 final {
    float x{0.0f};
    float y{0.0f};

    auto operator<=>(const Vec2&) const = default;
};

[[nodiscard]] constexpr Vec2 operator+(const Vec2 lhs, const Vec2 rhs) noexcept {
    return {lhs.x + rhs.x, lhs.y + rhs.y};
}

[[nodiscard]] constexpr Vec2 operator-(const Vec2 lhs, const Vec2 rhs) noexcept {
    return {lhs.x - rhs.x, lhs.y - rhs.y};
}

[[nodiscard]] constexpr Vec2 operator*(const Vec2 value, const float scale) noexcept {
    return {value.x * scale, value.y * scale};
}

enum class PinDirection {
    Input,
    Output,
};

enum class PinKind {
    Data,
    Execution,
};

enum class PinCardinality {
    Single,
    Multiple,
};

enum class PinStorage {
    Static,
    Dynamic,
};

enum class NodeRole {
    Regular,
    Subgraph,
    BoundaryInput,
    BoundaryOutput,
    IntergraphInput,
    IntergraphOutput,
};

enum class GraphLifetime {
    Reusable,
    Owned,
};

enum class SubgraphOwnership {
    Referenced,
    Owned,
};

struct GraphInterfacePin final {
    std::string key;
    std::string label;
    TypeId type;
    PinDirection direction{PinDirection::Input};
    PinKind kind{PinKind::Data};
    PinCardinality caller_cardinality{PinCardinality::Single};
    PinCardinality boundary_cardinality{PinCardinality::Multiple};

    bool operator==(const GraphInterfacePin&) const = default;
};

struct GraphInterface final {
    std::uint32_t version{1};
    std::vector<GraphInterfacePin> pins;

    bool operator==(const GraphInterface&) const = default;
};

struct DocumentGraphTarget final {
    GraphId graph;

    bool operator==(const DocumentGraphTarget&) const = default;
};

struct GraphAssetTarget final {
    GraphAssetId asset;
    GraphInterface interface;

    bool operator==(const GraphAssetTarget&) const = default;
};

using SubgraphTarget = std::variant<DocumentGraphTarget, GraphAssetTarget>;

struct SubgraphReference final {
    SubgraphOwnership ownership{SubgraphOwnership::Referenced};
    SubgraphTarget target{DocumentGraphTarget{}};

    bool operator==(const SubgraphReference&) const = default;
};

struct AssetReference final {
    std::uint64_t id{0};

    bool operator==(const AssetReference&) const = default;
};

struct OpaqueJsonProperty final {
    // Canonical JSON object retained for property kinds unknown to this build.
    std::string canonical_json;

    bool operator==(const OpaqueJsonProperty&) const = default;
};

using PropertyValue = std::variant<bool, std::int64_t, double, std::string, Vec2, AssetReference, OpaqueJsonProperty>;
using PropertyBag = std::unordered_map<std::string, PropertyValue>;

struct PinInstance final {
    PinId id;
    NodeId node;
    std::string key;
    std::string label;
    TypeId type;
    PinDirection direction{PinDirection::Input};
    PinKind kind{PinKind::Data};
    PinCardinality cardinality{PinCardinality::Single};
    PinStorage storage{PinStorage::Static};
    bool read_only{false};

    bool operator==(const PinInstance&) const = default;
};

struct NodeInstance final {
    NodeId id;
    TypeId type;
    std::uint32_t type_version{1};
    std::string display_name;
    PropertyBag properties;
    std::vector<PinId> pins;
    std::optional<SubgraphReference> subgraph;
    bool read_only{false};
    NodeRole role{NodeRole::Regular};

    bool operator==(const NodeInstance&) const = default;
};

struct Link final {
    LinkId id;
    PinId output;
    PinId input;
    bool read_only{false};

    bool operator==(const Link&) const = default;
};

struct IntergraphEndpoint final {
    GraphId graph;
    NodeId node;
    PinId pin;

    bool operator==(const IntergraphEndpoint&) const = default;
};

struct IntergraphLink final {
    IntergraphLinkId id;
    IntergraphEndpoint source;
    IntergraphEndpoint destination;
    bool read_only{false};

    bool operator==(const IntergraphLink&) const = default;
};

struct PinOwner final {
    GraphId graph;
    NodeId node;

    bool operator==(const PinOwner&) const = default;
};

struct SubgraphCallSite final {
    GraphId graph;
    NodeId node;
    SubgraphOwnership ownership{SubgraphOwnership::Referenced};

    bool operator==(const SubgraphCallSite&) const = default;
};

template<typename Id, typename Value, Detail::CowCopyDomain Domain> class CowEntityMap final {
  private:
    struct Entry;
    struct Shard;
    struct Directory;
    struct Root;

  public:
    using key_type = Id;
    using mapped_type = Value;
    using value_type = std::pair<const Id, Value>;
    using size_type = std::size_t;

    class const_iterator final {
      public:
        struct reference final {
            const Id& first;
            const Value& second;
        };

        struct pointer final {
            reference value;
            [[nodiscard]] const reference* operator->() const noexcept {
                return &value;
            }
        };

        using iterator_category = std::forward_iterator_tag;
        using difference_type = std::ptrdiff_t;
        using value_type = reference;

        const_iterator() = default;

        [[nodiscard]] reference operator*() const noexcept {
            const auto& entry = CurrentEntry();
            return {entry.id, *entry.value};
        }

        [[nodiscard]] pointer operator->() const noexcept {
            return {operator*()};
        }

        const_iterator& operator++() noexcept {
            if (m_shard == ShardCount) {
                return *this;
            }
            ++m_entry;
            AdvanceToValue();
            return *this;
        }

        const_iterator operator++(int) noexcept {
            const_iterator copy = *this;
            ++*this;
            return copy;
        }

        bool operator==(const const_iterator&) const = default;

      private:
        explicit const_iterator(const Root* root, const size_type shard, const size_type entry = 0) noexcept
            : m_root(root), m_shard(shard), m_entry(entry) {
            AdvanceToValue();
        }

        [[nodiscard]] const Entry& CurrentEntry() const noexcept {
            const auto directory = m_root->directories[DirectoryIndex(m_shard)];
            return directory->shards[ShardOffset(m_shard)]->entries[m_entry];
        }

        void AdvanceToValue() noexcept {
            while (m_root != nullptr && m_shard < ShardCount) {
                const auto& directory = m_root->directories[DirectoryIndex(m_shard)];
                const auto& shard = directory ? directory->shards[ShardOffset(m_shard)] : EmptyShardPointer();
                if (shard && m_entry < shard->entries.size()) {
                    return;
                }
                ++m_shard;
                m_entry = 0;
            }
        }

        const Root* m_root{nullptr};
        size_type m_shard{ShardCount};
        size_type m_entry{0};

        friend class CowEntityMap;
    };

    CowEntityMap() = default;

    CowEntityMap(const CowEntityMap&) noexcept = default;
    CowEntityMap& operator=(const CowEntityMap&) noexcept = default;

    CowEntityMap(CowEntityMap&& other) noexcept : m_root(other.m_root) {}

    CowEntityMap& operator=(CowEntityMap&& other) noexcept {
        if (this != &other) m_root = other.m_root;
        return *this;
    }

    CowEntityMap(std::initializer_list<value_type> values) : CowEntityMap() {
        reserve(values.size());
        for (const auto& [id, value] : values) {
            emplace(id, value);
        }
    }

    [[nodiscard]] bool empty() const noexcept {
        return !m_root || m_root->size == 0;
    }
    [[nodiscard]] size_type size() const noexcept {
        return m_root ? m_root->size : 0;
    }
    [[nodiscard]] bool contains(const Id id) const {
        return find(id) != end();
    }
    [[nodiscard]] const_iterator begin() const noexcept {
        return m_root ? const_iterator{m_root.get(), 0} : end();
    }
    [[nodiscard]] const_iterator end() const noexcept {
        return const_iterator{m_root.get(), ShardCount};
    }
    [[nodiscard]] const_iterator begin() noexcept {
        return std::as_const(*this).begin();
    }
    [[nodiscard]] const_iterator end() noexcept {
        return std::as_const(*this).end();
    }

    [[nodiscard]] const_iterator find(const Id id) const {
        if (!m_root) return end();
        const size_type shard_index = ShardIndex(id);
        const auto& directory = m_root->directories[DirectoryIndex(shard_index)];
        if (!directory) {
            return end();
        }
        const auto& shard = directory->shards[ShardOffset(shard_index)];
        if (!shard) {
            return end();
        }
        const auto position = LowerBound(*shard, id);
        if (position == shard->entries.end() || position->id != id) {
            return end();
        }
        return const_iterator{m_root.get(), shard_index, static_cast<size_type>(position - shard->entries.begin())};
    }

    [[nodiscard]] const_iterator find(const Id id) {
        return std::as_const(*this).find(id);
    }

    [[nodiscard]] const Value& at(const Id id) const {
        const auto position = find(id);
        if (position == end()) {
            throw std::out_of_range("CowEntityMap key not found");
        }
        return position->second;
    }

    [[nodiscard]] const Value& at(const Id id) {
        return std::as_const(*this).at(id);
    }

    [[nodiscard]] std::shared_ptr<const Value> SharedAt(const Id id) const {
        if (!m_root) return {};
        const size_type shard_index = ShardIndex(id);
        const auto& directory = m_root->directories[DirectoryIndex(shard_index)];
        if (!directory) return {};
        const auto& shard = directory->shards[ShardOffset(shard_index)];
        if (!shard) return {};
        const auto position = LowerBound(*shard, id);
        return position != shard->entries.end() && position->id == id ? position->value : nullptr;
    }

    void reserve(const size_type count) {
        if (count == 0 || !empty()) {
            return;
        }
        if (!m_root || m_root.use_count() != 1) m_root = std::make_shared<Root>();
        m_root->reserved_per_shard = (count + ShardCount - 1) / ShardCount;
    }

    template<typename Mapped> std::pair<const_iterator, bool> emplace(const Id id, Mapped&& value) {
        if (const auto existing = find(id); existing != end()) {
            return {existing, false};
        }
        std::shared_ptr<const Value> immutable_value = std::make_shared<Value>(std::forward<Mapped>(value));
        const size_type shard_index = ShardIndex(id);
        auto& shard = MutableShard(shard_index);
        auto position = LowerBound(shard, id);
        position = shard.entries.insert(position, Entry{id, std::move(immutable_value)});
        ++m_root->size;
        return {const_iterator{m_root.get(), shard_index, static_cast<size_type>(position - shard.entries.begin())},
                true};
    }

    template<typename Mapped> std::pair<const_iterator, bool> insert_or_assign(const Id id, Mapped&& value) {
        const bool inserted = find(id) == end();
        std::shared_ptr<const Value> immutable_value = std::make_shared<Value>(std::forward<Mapped>(value));
        const size_type shard_index = ShardIndex(id);
        auto& shard = MutableShard(shard_index);
        auto position = LowerBound(shard, id);
        if (inserted) {
            position = shard.entries.insert(position, Entry{id, std::move(immutable_value)});
            ++m_root->size;
        } else {
            if (position->value.use_count() != 1) {
                Detail::RecordCowClone(Domain, Detail::CowCloneKind::Value, 0, EstimateEntityBytes(*position->value));
            }
            position->value = std::move(immutable_value);
        }
        return {const_iterator{m_root.get(), shard_index, static_cast<size_type>(position - shard.entries.begin())},
                inserted};
    }

    size_type erase(const Id id) {
        if (!contains(id)) {
            return 0;
        }
        auto& shard = MutableShard(ShardIndex(id));
        const auto position = LowerBound(shard, id);
        shard.entries.erase(position);
        --m_root->size;
        return 1;
    }

    const_iterator erase(const const_iterator position) {
        if (position.m_root != m_root.get() || position == end()) {
            return end();
        }
        const Id id = position->first;
        auto next = position;
        ++next;
        const std::optional<Id> next_id = next == end() ? std::nullopt : std::optional<Id>{next->first};
        (void)erase(id);
        return next_id ? find(*next_id) : end();
    }

    void clear() {
        m_root.reset();
    }

    bool operator==(const CowEntityMap& other) const {
        if (m_root == other.m_root) return true;
        if (size() != other.size()) return false;
        bool equal = true;
        ForEachDifference(other, [&](const Id, const Value*, const Value*) { equal = false; });
        return equal;
    }

    template<typename Callback> void ForEachDifference(const CowEntityMap& other, Callback&& callback) const {
        (void)ForEachDifferenceWhile(other, [&](const Id id, const Value* before, const Value* after) {
            callback(id, before, after);
            return true;
        });
    }

    template<typename Callback> bool ForEachDifferenceWhile(const CowEntityMap& other, Callback&& callback) const {
        if (m_root == other.m_root) return true;
        for (size_type shard_index = 0; shard_index < ShardCount; ++shard_index) {
            const auto* left_directory = m_root ? &m_root->directories[DirectoryIndex(shard_index)] : nullptr;
            const auto* right_directory =
                other.m_root ? &other.m_root->directories[DirectoryIndex(shard_index)] : nullptr;
            const auto& left = left_directory != nullptr && *left_directory
                                   ? (*left_directory)->shards[ShardOffset(shard_index)]
                                   : EmptyShardPointer();
            const auto& right = right_directory != nullptr && *right_directory
                                    ? (*right_directory)->shards[ShardOffset(shard_index)]
                                    : EmptyShardPointer();
            if (left == right) continue;
            const auto* left_entries = left ? &left->entries : nullptr;
            const auto* right_entries = right ? &right->entries : nullptr;
            size_type left_index = 0;
            size_type right_index = 0;
            while ((left_entries && left_index < left_entries->size()) ||
                   (right_entries && right_index < right_entries->size())) {
                const Entry* left_entry =
                    left_entries && left_index < left_entries->size() ? &(*left_entries)[left_index] : nullptr;
                const Entry* right_entry =
                    right_entries && right_index < right_entries->size() ? &(*right_entries)[right_index] : nullptr;
                if (right_entry == nullptr || (left_entry != nullptr && left_entry->id < right_entry->id)) {
                    if (!callback(left_entry->id, left_entry->value.get(), static_cast<const Value*>(nullptr))) {
                        return false;
                    }
                    ++left_index;
                } else if (left_entry == nullptr || right_entry->id < left_entry->id) {
                    if (!callback(right_entry->id, static_cast<const Value*>(nullptr), right_entry->value.get())) {
                        return false;
                    }
                    ++right_index;
                } else {
                    if (left_entry->value != right_entry->value && *left_entry->value != *right_entry->value) {
                        if (!callback(left_entry->id, left_entry->value.get(), right_entry->value.get())) {
                            return false;
                        }
                    }
                    ++left_index;
                    ++right_index;
                }
            }
        }
        return true;
    }

  private:
    static constexpr size_type ShardCount = 4096;
    static constexpr size_type DirectoryCount = 64;
    static constexpr size_type ShardsPerDirectory = ShardCount / DirectoryCount;

    struct Entry final {
        Id id;
        std::shared_ptr<const Value> value;
    };

    struct Shard final {
        std::vector<Entry> entries;
    };

    struct Directory final {
        std::array<std::shared_ptr<Shard>, ShardsPerDirectory> shards;
    };

    struct Root final {
        std::array<std::shared_ptr<Directory>, DirectoryCount> directories;
        size_type size{0};
        size_type reserved_per_shard{0};
    };

    [[nodiscard]] static size_type ShardIndex(const Id id) noexcept {
        return IdHash{}(id) & (ShardCount - 1);
    }

    [[nodiscard]] static size_type DirectoryIndex(const size_type shard) noexcept {
        return shard / ShardsPerDirectory;
    }

    [[nodiscard]] static size_type ShardOffset(const size_type shard) noexcept {
        return shard % ShardsPerDirectory;
    }

    [[nodiscard]] static const std::shared_ptr<Shard>& EmptyShardPointer() noexcept {
        static const std::shared_ptr<Shard> empty;
        return empty;
    }

    [[nodiscard]] static auto LowerBound(Shard& shard, const Id id) {
        return std::ranges::lower_bound(shard.entries, id, {}, &Entry::id);
    }

    [[nodiscard]] static auto LowerBound(const Shard& shard, const Id id) {
        return std::ranges::lower_bound(shard.entries, id, {}, &Entry::id);
    }

    void EnsureUniqueRoot() {
        if (!m_root) {
            m_root = std::make_shared<Root>();
            return;
        }
        if (m_root.use_count() == 1) {
            return;
        }
        auto replacement = std::make_shared<Root>(*m_root);
        Detail::RecordCowClone(Domain, Detail::CowCloneKind::Root, DirectoryCount, sizeof(Root));
        m_root = std::move(replacement);
    }

    [[nodiscard]] Shard& MutableShard(const size_type shard_index) {
        EnsureUniqueRoot();
        auto& directory = m_root->directories[DirectoryIndex(shard_index)];
        if (!directory) {
            directory = std::make_shared<Directory>();
        } else if (directory.use_count() != 1) {
            auto replacement = std::make_shared<Directory>(*directory);
            Detail::RecordCowClone(Domain, Detail::CowCloneKind::Directory, ShardsPerDirectory, sizeof(Directory));
            directory = std::move(replacement);
        }
        auto& shard = directory->shards[ShardOffset(shard_index)];
        if (!shard) {
            auto replacement = std::make_shared<Shard>();
            replacement->entries.reserve(m_root->reserved_per_shard);
            shard = std::move(replacement);
        } else if (shard.use_count() != 1) {
            const std::uint64_t handles = static_cast<std::uint64_t>(shard->entries.size());
            auto replacement = std::make_shared<Shard>(*shard);
            Detail::RecordCowClone(Domain, Detail::CowCloneKind::Shard, handles,
                                   sizeof(Shard) + handles * sizeof(Entry));
            shard = std::move(replacement);
        }
        return *shard;
    }

    [[nodiscard]] Value* EditValue(const Id id) {
        if (!contains(id)) return nullptr;
        auto& shard = MutableShard(ShardIndex(id));
        const auto position = LowerBound(shard, id);
        if (position->value.use_count() == 1) {
            return const_cast<Value*>(position->value.get());
        }
        auto replacement = std::make_shared<Value>(*position->value);
        Detail::RecordCowClone(Domain, Detail::CowCloneKind::Value, 0, EstimateEntityBytes(*position->value));
        position->value = replacement;
        return replacement.get();
    }

    static std::uint64_t EstimateEntityBytes(const Value& value) {
        std::uint64_t bytes = sizeof(Value);
        if constexpr (std::is_same_v<Value, NodeInstance>) {
            bytes += value.display_name.capacity();
            bytes += value.pins.capacity() * sizeof(PinId);
            bytes += value.properties.size() * sizeof(PropertyBag::value_type);
            for (const auto& [key, property] : value.properties) {
                bytes += key.capacity();
                std::visit(
                    [&](const auto& item) {
                        using Item = std::remove_cvref_t<decltype(item)>;
                        if constexpr (std::is_same_v<Item, std::string>) {
                            bytes += item.capacity();
                        } else if constexpr (std::is_same_v<Item, OpaqueJsonProperty>) {
                            bytes += item.canonical_json.capacity();
                        }
                    },
                    property);
            }
        } else if constexpr (std::is_same_v<Value, PinInstance>) {
            bytes += value.key.capacity() + value.label.capacity();
        } else if constexpr (std::is_same_v<Value, Graph>) {
            bytes += value.display_name.capacity();
            bytes += value.interface.pins.capacity() * sizeof(GraphInterfacePin);
            for (const auto& pin : value.interface.pins) {
                bytes += pin.key.capacity() + pin.label.capacity();
            }
        }
        return bytes;
    }

    std::shared_ptr<Root> m_root;

    friend class GraphDocument;
};

template<typename Key, typename Value = Key, Detail::CowCopyDomain Domain = Detail::CowCopyDomain::SemanticIndexes>
class CowAdjacencyMap final {
  private:
    struct Node;
    using NodePointer = std::shared_ptr<const Node>;

  public:
    using key_type = Key;
    using value_type = Value;
    using size_type = std::size_t;

    class const_iterator final {
      public:
        using iterator_category = std::forward_iterator_tag;
        using difference_type = std::ptrdiff_t;
        using value_type = Value;
        using reference = const Value&;
        using pointer = const Value*;

        const_iterator() = default;

        [[nodiscard]] reference operator*() const noexcept {
            return m_pending.back()->value;
        }
        [[nodiscard]] pointer operator->() const noexcept {
            return &m_pending.back()->value;
        }

        const_iterator& operator++() {
            const Node* current = m_pending.back();
            m_pending.pop_back();
            PushLeft(current->right.get());
            return *this;
        }

        const_iterator operator++(int) {
            const_iterator copy = *this;
            ++*this;
            return copy;
        }

        [[nodiscard]] bool operator==(const const_iterator& other) const noexcept {
            return Current() == other.Current();
        }

      private:
        explicit const_iterator(const Node* root) {
            PushLeft(root);
        }

        void PushLeft(const Node* node) {
            while (node != nullptr) {
                m_pending.push_back(node);
                node = node->left.get();
            }
        }

        [[nodiscard]] const Node* Current() const noexcept {
            return m_pending.empty() ? nullptr : m_pending.back();
        }

        std::vector<const Node*> m_pending;

        friend class CowAdjacencyMap;
    };

    CowAdjacencyMap() = default;
    CowAdjacencyMap(const CowAdjacencyMap&) noexcept = default;
    CowAdjacencyMap& operator=(const CowAdjacencyMap&) noexcept = default;
    CowAdjacencyMap(CowAdjacencyMap&&) noexcept = default;
    CowAdjacencyMap& operator=(CowAdjacencyMap&&) noexcept = default;

    [[nodiscard]] bool empty() const noexcept {
        return !m_root;
    }
    [[nodiscard]] size_type size() const noexcept {
        return Size(m_root);
    }
    [[nodiscard]] const_iterator begin() const {
        return const_iterator{m_root.get()};
    }
    [[nodiscard]] const_iterator end() const {
        return {};
    }
    [[nodiscard]] const_iterator begin() {
        return std::as_const(*this).begin();
    }
    [[nodiscard]] const_iterator end() {
        return std::as_const(*this).end();
    }

    [[nodiscard]] const Value* Find(const Key& key) const noexcept {
        const Node* node = m_root.get();
        while (node != nullptr) {
            if (key == node->key) return &node->value;
            node = key < node->key ? node->left.get() : node->right.get();
        }
        return nullptr;
    }

    template<typename Lookup, typename Compare>
    [[nodiscard]] const Value* FindEquivalent(const Lookup& key, Compare compare) const noexcept {
        const Node* node = m_root.get();
        while (node != nullptr) {
            const int ordering = compare(key, node->key);
            if (ordering == 0) return &node->value;
            node = ordering < 0 ? node->left.get() : node->right.get();
        }
        return nullptr;
    }

    [[nodiscard]] bool contains(const Key& key) const noexcept {
        return Find(key) != nullptr;
    }

    bool insert_or_assign(const Key key, Value value, std::uint64_t* path_copies = nullptr) {
        if (path_copies != nullptr) *path_copies = 0;
        const Value* current = Find(key);
        if (current != nullptr && *current == value) return false;
        const bool inserted = current == nullptr;
        std::uint64_t copied = 0;
        NodePointer replacement = InsertNode(m_root, key, std::move(value), copied);
        m_root = std::move(replacement);
        RecordCopies(copied);
        if (path_copies != nullptr) *path_copies = copied;
        return inserted;
    }

    size_type erase(const Key key, std::uint64_t* path_copies = nullptr) {
        if (path_copies != nullptr) *path_copies = 0;
        if (!contains(key)) return 0;
        std::uint64_t copied = 0;
        NodePointer replacement = EraseNode(m_root, key, copied);
        m_root = std::move(replacement);
        RecordCopies(copied);
        if (path_copies != nullptr) *path_copies = copied;
        return 1;
    }

    [[nodiscard]] bool SharesStorageWith(const CowAdjacencyMap& other) const noexcept {
        return m_root == other.m_root;
    }

    [[nodiscard]] bool operator==(const CowAdjacencyMap& other) const {
        if (m_root == other.m_root) return true;
        if (size() != other.size()) return false;
        std::vector<const Node*> left;
        std::vector<const Node*> right;
        PushLeft(m_root.get(), left);
        PushLeft(other.m_root.get(), right);
        while (!left.empty()) {
            const Node* left_node = left.back();
            const Node* right_node = right.back();
            left.pop_back();
            right.pop_back();
            if (left_node->key != right_node->key || left_node->value != right_node->value) return false;
            PushLeft(left_node->right.get(), left);
            PushLeft(right_node->right.get(), right);
        }
        return right.empty();
    }

    [[nodiscard]] static const CowAdjacencyMap& Empty() {
        static const CowAdjacencyMap empty;
        return empty;
    }

  private:
    struct Node final {
        Key key;
        Value value;
        NodePointer left;
        NodePointer right;
        size_type size;
        std::uint32_t height;
    };

    [[nodiscard]] static size_type Size(const NodePointer& node) noexcept {
        return node ? node->size : 0;
    }

    [[nodiscard]] static std::uint32_t Height(const NodePointer& node) noexcept {
        return node ? node->height : 0;
    }

    static void PushLeft(const Node* node, std::vector<const Node*>& pending) {
        while (node != nullptr) {
            pending.push_back(node);
            node = node->left.get();
        }
    }

    [[nodiscard]] static NodePointer MakeNode(const Key key, Value value, NodePointer left, NodePointer right) {
        const size_type size = 1 + Size(left) + Size(right);
        const std::uint32_t height = 1 + std::max(Height(left), Height(right));
        return std::make_shared<const Node>(Node{
            .key = key,
            .value = std::move(value),
            .left = std::move(left),
            .right = std::move(right),
            .size = size,
            .height = height,
        });
    }

    [[nodiscard]] static NodePointer CopyNode(const Key key, Value value, NodePointer left, NodePointer right,
                                              std::uint64_t& copied) {
        ++copied;
        return MakeNode(key, std::move(value), std::move(left), std::move(right));
    }

    [[nodiscard]] static int BalanceFactor(const NodePointer& node) noexcept {
        return static_cast<int>(Height(node->left)) - static_cast<int>(Height(node->right));
    }

    [[nodiscard]] static NodePointer RotateLeft(const NodePointer& root, std::uint64_t& copied) {
        const NodePointer pivot = root->right;
        NodePointer moved = CopyNode(root->key, root->value, root->left, pivot->left, copied);
        return CopyNode(pivot->key, pivot->value, std::move(moved), pivot->right, copied);
    }

    [[nodiscard]] static NodePointer RotateRight(const NodePointer& root, std::uint64_t& copied) {
        const NodePointer pivot = root->left;
        NodePointer moved = CopyNode(root->key, root->value, pivot->right, root->right, copied);
        return CopyNode(pivot->key, pivot->value, pivot->left, std::move(moved), copied);
    }

    [[nodiscard]] static NodePointer Balance(NodePointer root, std::uint64_t& copied) {
        const int factor = BalanceFactor(root);
        if (factor > 1) {
            if (BalanceFactor(root->left) < 0) {
                NodePointer left = RotateLeft(root->left, copied);
                root = CopyNode(root->key, root->value, std::move(left), root->right, copied);
            }
            return RotateRight(root, copied);
        }
        if (factor < -1) {
            if (BalanceFactor(root->right) > 0) {
                NodePointer right = RotateRight(root->right, copied);
                root = CopyNode(root->key, root->value, root->left, std::move(right), copied);
            }
            return RotateLeft(root, copied);
        }
        return root;
    }

    [[nodiscard]] static NodePointer InsertNode(const NodePointer& root, const Key key, Value value,
                                                std::uint64_t& copied) {
        if (!root) return MakeNode(key, std::move(value), {}, {});
        if (key < root->key) {
            NodePointer left = InsertNode(root->left, key, std::move(value), copied);
            return Balance(CopyNode(root->key, root->value, std::move(left), root->right, copied), copied);
        }
        if (root->key < key) {
            NodePointer right = InsertNode(root->right, key, std::move(value), copied);
            return Balance(CopyNode(root->key, root->value, root->left, std::move(right), copied), copied);
        }
        return CopyNode(key, std::move(value), root->left, root->right, copied);
    }

    [[nodiscard]] static NodePointer EraseNode(const NodePointer& root, const Key key, std::uint64_t& copied) {
        if (key < root->key) {
            NodePointer left = EraseNode(root->left, key, copied);
            return Balance(CopyNode(root->key, root->value, std::move(left), root->right, copied), copied);
        }
        if (root->key < key) {
            NodePointer right = EraseNode(root->right, key, copied);
            return Balance(CopyNode(root->key, root->value, root->left, std::move(right), copied), copied);
        }
        if (!root->left) return root->right;
        if (!root->right) return root->left;
        const Node* successor = root->right.get();
        while (successor->left)
            successor = successor->left.get();
        NodePointer right = EraseNode(root->right, successor->key, copied);
        return Balance(CopyNode(successor->key, successor->value, root->left, std::move(right), copied), copied);
    }

    static void RecordCopies(const std::uint64_t copied) noexcept {
        if (copied == 0) return;
        if constexpr (Domain != Detail::CowCopyDomain::None) {
            Detail::RecordCowClone(Domain, Detail::CowCloneKind::Value, copied, copied * sizeof(Node));
        }
    }

    NodePointer m_root;
};

using NodeMap = CowEntityMap<NodeId, NodeInstance, Detail::CowCopyDomain::Nodes>;
using PinMap = CowEntityMap<PinId, PinInstance, Detail::CowCopyDomain::Pins>;
using LinkMap = CowEntityMap<LinkId, Link, Detail::CowCopyDomain::Links>;
using IntergraphLinkMap = CowEntityMap<IntergraphLinkId, IntergraphLink, Detail::CowCopyDomain::IntergraphLinks>;
using IncidentLinkRange = CowAdjacencyMap<LinkId>;
using SubgraphCallerRange = CowAdjacencyMap<NodeId, SubgraphCallSite>;
using IntergraphLinkRange = CowAdjacencyMap<IntergraphLinkId>;
using BoundaryNodeRange = CowAdjacencyMap<NodeId>;

struct SemanticRevisionSet final {
    std::uint64_t serial{0};
    std::uint64_t topology{0};
    std::uint64_t value{0};
    std::uint64_t layout{0};

    bool operator==(const SemanticRevisionSet&) const = default;
};

struct Graph final {
    GraphId id;
    std::string display_name;
    GraphLifetime lifetime{GraphLifetime::Reusable};
    GraphInterface interface;
    bool read_only{false};
    NodeMap nodes;
    PinMap pins;
    LinkMap links;
    bool operator==(const Graph& other) const {
        return id == other.id && display_name == other.display_name && lifetime == other.lifetime &&
               interface == other.interface && read_only == other.read_only && nodes == other.nodes &&
               pins == other.pins && links == other.links;
    }
};

using GraphMap = CowEntityMap<GraphId, Graph, Detail::CowCopyDomain::Graphs>;
using GraphRevisionMap = CowEntityMap<GraphId, SemanticRevisionSet, Detail::CowCopyDomain::GraphRevisions>;

struct RemovedNode final {
    NodeInstance node;
    std::vector<PinInstance> pins;
    std::vector<Link> links;
};

struct RemovedPin final {
    PinInstance pin;
    std::size_t index{0};
    std::vector<Link> links;
};

struct CopyDomainMetrics final {
    std::uint64_t root_clones{0};
    std::uint64_t directory_clones{0};
    std::uint64_t shard_clones{0};
    std::uint64_t page_clones{0};
    std::uint64_t value_clones{0};
    std::uint64_t copied_handles{0};
    std::uint64_t logical_bytes{0};
};

struct TransactionMetrics final {
    CopyDomainMetrics graphs;
    CopyDomainMetrics graph_revisions;
    CopyDomainMetrics node_maps;
    CopyDomainMetrics pin_maps;
    CopyDomainMetrics link_maps;
    CopyDomainMetrics intergraph_links;
    CopyDomainMetrics node_presentations;
    CopyDomainMetrics link_presentations;
    CopyDomainMetrics groups;
    CopyDomainMetrics group_styles;
    CopyDomainMetrics group_memberships;
    CopyDomainMetrics route_point_sequences;
    CopyDomainMetrics semantic_indexes;
    CopyDomainMetrics presentation_indexes;
    std::uint64_t journal_entries{0};
    std::uint64_t operation_intents{0};
    std::uint64_t command_paths{0};
    std::uint64_t incremental_records_validated{0};
    std::uint64_t full_structure_validations{0};
    std::uint64_t ownership_summary_lookups{0};
    std::uint64_t dependency_searches{0};
    std::uint64_t dependency_vertices_visited{0};
    std::uint64_t route_chunk_merges{0};
    std::uint64_t route_points_reindexed{0};
    std::uint64_t copied_logical_bytes{0};
};

[[nodiscard]] UNI_GUI_EXPORT TransactionMetrics GetTransactionMetrics() noexcept;
UNI_GUI_EXPORT void ResetTransactionMetrics() noexcept;

class UNI_GUI_EXPORT GraphDocument final {
  public:
    GraphDocument();
    ~GraphDocument();
    GraphDocument(GraphDocument&& other);
    GraphDocument& operator=(GraphDocument&& other);
    GraphDocument(const GraphDocument&) = delete;
    GraphDocument& operator=(const GraphDocument&) = delete;
    void Swap(GraphDocument& other) noexcept;

    [[nodiscard]] std::uint32_t SchemaVersion() const noexcept;
    [[nodiscard]] GraphId RootGraph() const noexcept;
    [[nodiscard]] std::uint64_t ModelRevision() const noexcept;
    [[nodiscard]] SemanticRevisionSet SemanticRevisions() const noexcept;
    [[nodiscard]] SemanticRevisionSet GraphRevisions(GraphId graph) const noexcept;
    [[nodiscard]] std::uint64_t Identity() const noexcept;
    [[nodiscard]] std::uint64_t AllocationEpoch() const noexcept;

    [[nodiscard]] GraphId AllocateGraphId() noexcept;
    [[nodiscard]] NodeId AllocateNodeId() noexcept;
    [[nodiscard]] PinId AllocatePinId() noexcept;
    [[nodiscard]] LinkId AllocateLinkId() noexcept;
    [[nodiscard]] IntergraphLinkId AllocateIntergraphLinkId() noexcept;

    [[nodiscard]] const Graph* FindGraph(GraphId id) const noexcept;
    [[nodiscard]] const NodeInstance* FindNode(GraphId graph, NodeId id) const noexcept;
    [[nodiscard]] const PinInstance* FindPin(GraphId graph, PinId id) const noexcept;
    [[nodiscard]] const Link* FindLink(GraphId graph, LinkId id) const noexcept;
    [[nodiscard]] const IntergraphLink* FindIntergraphLink(IntergraphLinkId id) const noexcept;
    [[nodiscard]] const IntergraphLinkMap& IntergraphLinks() const noexcept;
    [[nodiscard]] GraphId FindNodeGraph(NodeId node) const noexcept;
    [[nodiscard]] std::optional<PinOwner> FindPinOwner(PinId pin) const noexcept;
    [[nodiscard]] GraphId FindLinkGraph(LinkId link) const noexcept;
    [[nodiscard]] LinkId FindLinkBetween(PinId output, PinId input) const noexcept;
    [[nodiscard]] const IncidentLinkRange& IncidentLinks(PinId pin) const noexcept;
    [[nodiscard]] const SubgraphCallerRange& SubgraphCallers(GraphId graph) const noexcept;
    [[nodiscard]] std::size_t OwnedSubgraphCallerCount(GraphId graph) const noexcept;
    [[nodiscard]] IntergraphLinkId IntergraphLinkForPin(PinId pin) const noexcept;
    [[nodiscard]] const IntergraphLinkRange& IntergraphLinksForGraph(GraphId graph) const noexcept;
    [[nodiscard]] const BoundaryNodeRange& BoundaryNodes(GraphId graph, NodeRole role) const noexcept;
    [[nodiscard]] bool HasDependencyPath(GraphId from, GraphId target) const;

    [[nodiscard]] std::vector<std::reference_wrapper<const Graph>> Graphs() const;
    [[nodiscard]] Result<void> ValidateStructure() const;

  private:
    struct SnapshotTag final {};
    explicit GraphDocument(SnapshotTag);
    void SetSchemaVersion(std::uint32_t version) noexcept;
    [[nodiscard]] Result<void> SetRootGraph(GraphId graph);
    [[nodiscard]] Result<GraphId> AddGraph(GraphId id = {});
    [[nodiscard]] Result<Graph> RemoveGraph(GraphId id);
    [[nodiscard]] Result<void> RestoreGraph(Graph graph);
    [[nodiscard]] Result<void> AddNode(GraphId graph, NodeInstance node, std::span<const PinInstance> pins);
    [[nodiscard]] Result<RemovedNode> RemoveNode(GraphId graph, NodeId node);
    [[nodiscard]] Result<void> RestoreNode(GraphId graph, RemovedNode removed);
    [[nodiscard]] Result<void> AddDynamicPin(GraphId graph, PinInstance pin, std::size_t index);
    [[nodiscard]] Result<RemovedPin> RemoveDynamicPin(GraphId graph, PinId pin);
    [[nodiscard]] Result<void> RestoreDynamicPin(GraphId graph, RemovedPin removed);
    [[nodiscard]] Result<void> UpdateDynamicPin(GraphId graph, PinInstance pin);
    [[nodiscard]] Result<void> ReorderDynamicPins(GraphId graph, NodeId node, std::vector<PinId> order);
    [[nodiscard]] Result<void> SetDescriptorPins(GraphId graph, NodeId node, std::span<const PinInstance> pins);
    [[nodiscard]] Result<void> AddLink(GraphId graph, Link link);
    [[nodiscard]] Result<Link> RemoveLink(GraphId graph, LinkId link);
    [[nodiscard]] Result<void> SetNodeProperty(GraphId graph, NodeId node, std::string key,
                                               std::optional<PropertyValue> value);
    [[nodiscard]] Result<void> SetNodeDisplayName(GraphId graph, NodeId node, std::string name);
    [[nodiscard]] Result<void> SetNodeSubgraph(GraphId graph, NodeId node, std::optional<SubgraphReference> subgraph);
    [[nodiscard]] Result<void> ReplaceGraph(Graph graph);
    [[nodiscard]] Result<void> AddIntergraphLink(IntergraphLink link);
    [[nodiscard]] Result<IntergraphLink> RemoveIntergraphLink(IntergraphLinkId link);
    [[nodiscard]] Result<void> SetGraphReadOnly(GraphId graph, bool read_only);
    [[nodiscard]] Result<void> SetNodeReadOnly(GraphId graph, NodeId node, bool read_only);
    [[nodiscard]] Result<void> SetPinReadOnly(GraphId graph, PinId pin, bool read_only);
    [[nodiscard]] Result<void> SetLinkReadOnly(GraphId graph, LinkId link, bool read_only);
    [[nodiscard]] Result<void> Import(std::uint32_t schema_version, GraphId root_graph, std::vector<Graph> graphs,
                                      std::vector<IntergraphLink> intergraph_links);
    [[nodiscard]] Result<void> ValidateReplacement(const Graph& before) const;
    [[nodiscard]] Result<void> ValidateNodeRelations(GraphId graph, NodeId node) const;
    [[nodiscard]] Result<void> ValidateBoundaryRelations(GraphId graph) const;
    [[nodiscard]] Result<void> ValidateTargetOwnership(GraphId graph) const;
    [[nodiscard]] Result<void> ValidateIntergraphLinkStructure(IntergraphLinkId link) const;
    [[nodiscard]] Result<void> ValidateLocalLinkStructure(GraphId graph, LinkId link) const;

    [[nodiscard]] Graph* FindGraphMutable(GraphId id);
    [[nodiscard]] GraphDocument SnapshotForTransaction() const;
    [[nodiscard]] bool
    CanCommit(std::uint64_t identity, std::uint64_t revision, std::uint64_t allocation_epoch,
              const SemanticRevisionSet& changes,
              const std::unordered_map<GraphId, SemanticRevisionSet, IdHash>& graph_changes) const noexcept;
    void CommitFrom(GraphDocument&& staged, const SemanticRevisionSet& changes,
                    const std::unordered_map<GraphId, SemanticRevisionSet, IdHash>& graph_changes) noexcept;
    struct Impl;
    std::unique_ptr<Impl> m_impl;

    friend class CommandStack;
    friend UNI_GUI_EXPORT GraphDocumentSnapshot CaptureGraphDocumentSnapshot(const GraphDocument& document);
    friend class GraphTransaction;
    friend struct Detail::GraphIoAccess;
};

} // namespace Uni::GUI::Nodes
