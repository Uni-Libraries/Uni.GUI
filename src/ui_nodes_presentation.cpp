#include <uni/gui/nodes/presentation.h>

#include "ui_nodes_internal.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace Uni::GUI::Nodes {

struct PersistentRoutePointSequence::Impl final {
    static constexpr std::size_t IndexShardCount = 1024;

    struct Chunk final {
        std::uint64_t id{0};
        std::vector<RoutePoint> points;
    };

    struct ChunkLookup final {
        std::uint64_t id{0};
        std::shared_ptr<const Chunk> chunk;
    };

    struct IndexEntry final {
        RoutePointId point;
        std::uint64_t chunk{0};
    };

    struct IndexShard final {
        std::vector<IndexEntry> entries;
    };

    std::vector<std::shared_ptr<const Chunk>> chunks;
    std::vector<ChunkLookup> chunks_by_id;
    std::array<std::shared_ptr<const IndexShard>, IndexShardCount> index;
    std::size_t size{0};
    std::uint64_t next_chunk{1};
    std::uint64_t fingerprint{0};
    bool valid{true};

    Impl() = default;

    explicit Impl(std::vector<RoutePoint> points)
        : size(points.size()) {
        std::array<std::vector<IndexEntry>, IndexShardCount> entries;
        for (std::size_t offset = 0; offset < points.size(); offset += PersistentRoutePointSequence::ChunkCapacity) {
            const std::size_t count = std::min(PersistentRoutePointSequence::ChunkCapacity, points.size() - offset);
            auto chunk = std::make_shared<Chunk>();
            chunk->id = next_chunk++;
            chunk->points.insert(
                chunk->points.end(),
                std::make_move_iterator(points.begin() + static_cast<std::ptrdiff_t>(offset)),
                std::make_move_iterator(points.begin() + static_cast<std::ptrdiff_t>(offset + count)));
            for (const RoutePoint& point : chunk->points) {
                valid &= point.id && std::isfinite(point.position.x) && std::isfinite(point.position.y);
                fingerprint ^= Fingerprint(point);
                entries[ShardFor(point.id)].push_back(IndexEntry{point.id, chunk->id});
            }
            chunks.push_back(chunk);
            chunks_by_id.push_back(ChunkLookup{chunk->id, std::move(chunk)});
        }
        for (std::size_t shard = 0; shard < IndexShardCount; ++shard) {
            if (entries[shard].empty()) continue;
            std::ranges::sort(entries[shard], {}, &IndexEntry::point);
            valid &= std::adjacent_find(
                entries[shard].begin(),
                entries[shard].end(),
                [](const IndexEntry& left, const IndexEntry& right) {
                    return left.point == right.point;
                }) == entries[shard].end();
            auto value = std::make_shared<IndexShard>();
            value->entries = std::move(entries[shard]);
            index[shard] = std::move(value);
        }
    }

    [[nodiscard]] static std::size_t ShardFor(const RoutePointId point) noexcept {
        return IdHash{}(point) & (IndexShardCount - 1);
    }

    [[nodiscard]] static std::uint64_t Fingerprint(const RoutePoint& point) noexcept {
        const auto bits = [](const float value) noexcept {
            return value == 0.0f ? std::uint32_t{0} : std::bit_cast<std::uint32_t>(value);
        };
        std::uint64_t value = static_cast<std::uint64_t>(IdHash{}(point.id));
        value ^= static_cast<std::uint64_t>(bits(point.position.x)) +
            0x9E3779B97F4A7C15ULL + (value << 6U) + (value >> 2U);
        value ^= static_cast<std::uint64_t>(bits(point.position.y)) +
            0x9E3779B97F4A7C15ULL + (value << 6U) + (value >> 2U);
        return value;
    }

    [[nodiscard]] const Chunk* FindChunk(const std::uint64_t id) const noexcept {
        const auto found = std::ranges::lower_bound(chunks_by_id, id, {}, &ChunkLookup::id);
        return found != chunks_by_id.end() && found->id == id ? found->chunk.get() : nullptr;
    }

    [[nodiscard]] std::uint64_t FindChunkId(const RoutePointId point) const noexcept {
        const auto& shard = index[ShardFor(point)];
        if (!shard) return 0;
        const auto found = std::ranges::lower_bound(shard->entries, point, {}, &IndexEntry::point);
        return found != shard->entries.end() && found->point == point ? found->chunk : 0;
    }

    [[nodiscard]] std::shared_ptr<Impl> CloneRoot() const {
        auto copy = std::make_shared<Impl>(*this);
        const std::uint64_t handles = static_cast<std::uint64_t>(
            chunks.size() + chunks_by_id.size() + IndexShardCount);
        const std::uint64_t bytes = sizeof(Impl) +
            static_cast<std::uint64_t>(chunks.size()) * sizeof(chunks.front()) +
            static_cast<std::uint64_t>(chunks_by_id.size()) * sizeof(ChunkLookup);
        Detail::RecordCowClone(
            Detail::CowCopyDomain::RoutePointSequences,
            Detail::CowCloneKind::Root,
            handles,
            bytes);
        return copy;
    }

    static void RecordChunkClone(const Chunk& chunk) noexcept {
        Detail::RecordCowClone(
            Detail::CowCopyDomain::RoutePointSequences,
            Detail::CowCloneKind::Value,
            static_cast<std::uint64_t>(chunk.points.size()),
            sizeof(Chunk) + static_cast<std::uint64_t>(chunk.points.size()) * sizeof(RoutePoint));
    }

    void ReplaceChunk(const std::uint64_t id, std::shared_ptr<const Chunk> replacement) {
        const auto order = std::ranges::find(chunks, id, [](const std::shared_ptr<const Chunk>& chunk) {
            return chunk->id;
        });
        const auto lookup = std::ranges::lower_bound(chunks_by_id, id, {}, &ChunkLookup::id);
        *order = replacement;
        lookup->chunk = std::move(replacement);
    }

    [[nodiscard]] IndexShard& MutableIndexShard(
        const std::size_t shard,
        std::array<bool, IndexShardCount>& cloned) {
        if (!cloned[shard]) {
            if (index[shard]) {
                const auto& source = *index[shard];
                auto replacement = std::make_shared<IndexShard>(source);
                Detail::RecordCowClone(
                    Detail::CowCopyDomain::RoutePointSequences,
                    Detail::CowCloneKind::Shard,
                    static_cast<std::uint64_t>(source.entries.size()),
                    sizeof(IndexShard) +
                        static_cast<std::uint64_t>(source.entries.size()) * sizeof(IndexEntry));
                index[shard] = std::move(replacement);
            } else {
                index[shard] = std::make_shared<IndexShard>();
            }
            cloned[shard] = true;
        } else if (!index[shard]) {
            index[shard] = std::make_shared<IndexShard>();
        }
        return *const_cast<IndexShard*>(index[shard].get());
    }

    void SetIndex(
        const RoutePointId point,
        const std::uint64_t chunk,
        std::array<bool, IndexShardCount>& cloned) {
        auto& shard = MutableIndexShard(ShardFor(point), cloned);
        const auto found = std::ranges::lower_bound(shard.entries, point, {}, &IndexEntry::point);
        if (found != shard.entries.end() && found->point == point) {
            found->chunk = chunk;
        } else {
            shard.entries.insert(found, IndexEntry{point, chunk});
        }
    }

    void EraseIndex(
        const RoutePointId point,
        std::array<bool, IndexShardCount>& cloned) {
        const std::size_t index_shard = ShardFor(point);
        auto& shard = MutableIndexShard(index_shard, cloned);
        const auto found = std::ranges::lower_bound(shard.entries, point, {}, &IndexEntry::point);
        if (found != shard.entries.end() && found->point == point) shard.entries.erase(found);
        if (shard.entries.empty()) index[index_shard].reset();
    }
};

PersistentRoutePointSequence::const_iterator::const_iterator(
    std::shared_ptr<const Impl> impl,
    const std::size_t chunk,
    const std::size_t point) noexcept
    : m_impl(std::move(impl)), m_chunk(chunk), m_point(point) {
    Advance();
}

void PersistentRoutePointSequence::const_iterator::Advance() noexcept {
    while (m_impl && m_chunk < m_impl->chunks.size() &&
           m_point >= m_impl->chunks[m_chunk]->points.size()) {
        ++m_chunk;
        m_point = 0;
    }
}

PersistentRoutePointSequence::const_iterator::reference
PersistentRoutePointSequence::const_iterator::operator*() const noexcept {
    return m_impl->chunks[m_chunk]->points[m_point];
}

PersistentRoutePointSequence::const_iterator::pointer
PersistentRoutePointSequence::const_iterator::operator->() const noexcept {
    return &operator*();
}

PersistentRoutePointSequence::const_iterator&
PersistentRoutePointSequence::const_iterator::operator++() noexcept {
    ++m_point;
    Advance();
    return *this;
}

PersistentRoutePointSequence::const_iterator
PersistentRoutePointSequence::const_iterator::operator++(int) noexcept {
    auto copy = *this;
    ++*this;
    return copy;
}

PersistentRoutePointSequence::PersistentRoutePointSequence(std::vector<RoutePoint> points) {
    if (!points.empty()) m_impl = std::make_shared<const Impl>(std::move(points));
}

PersistentRoutePointSequence::PersistentRoutePointSequence(
    const std::initializer_list<RoutePoint> points)
    : PersistentRoutePointSequence(std::vector<RoutePoint>{points}) {}

bool PersistentRoutePointSequence::empty() const noexcept { return size() == 0; }

std::size_t PersistentRoutePointSequence::size() const noexcept { return m_impl ? m_impl->size : 0; }

const RoutePoint& PersistentRoutePointSequence::front() const {
    if (empty()) throw std::out_of_range("PersistentRoutePointSequence is empty");
    return m_impl->chunks.front()->points.front();
}

const RoutePoint& PersistentRoutePointSequence::operator[](std::size_t index) const {
    if (index >= size()) throw std::out_of_range("PersistentRoutePointSequence index is out of range");
    for (const auto& chunk : m_impl->chunks) {
        if (index < chunk->points.size()) return chunk->points[index];
        index -= chunk->points.size();
    }
    throw std::out_of_range("PersistentRoutePointSequence index is out of range");
}

const RoutePoint* PersistentRoutePointSequence::Find(const RoutePointId point) const noexcept {
    if (!m_impl) return nullptr;
    const std::uint64_t chunk_id = m_impl->FindChunkId(point);
    const auto* chunk = m_impl->FindChunk(chunk_id);
    if (chunk == nullptr) return nullptr;
    const auto found = std::ranges::find(chunk->points, point, &RoutePoint::id);
    return found != chunk->points.end() ? &*found : nullptr;
}

bool PersistentRoutePointSequence::contains(const RoutePointId point) const noexcept {
    return m_impl != nullptr && m_impl->FindChunkId(point) != 0;
}

bool PersistentRoutePointSequence::Valid() const noexcept { return !m_impl || m_impl->valid; }

bool PersistentRoutePointSequence::ValidateStructure() const noexcept {
    if (!m_impl) return true;
    if (!m_impl->valid || m_impl->chunks.empty() ||
        m_impl->chunks.size() != m_impl->chunks_by_id.size()) {
        return false;
    }
    std::size_t point_count = 0;
    std::uint64_t fingerprint = 0;
    std::uint64_t previous_chunk_id = 0;
    for (std::size_t index = 0; index < m_impl->chunks_by_id.size(); ++index) {
        const auto& lookup = m_impl->chunks_by_id[index];
        if (!lookup.id || !lookup.chunk || lookup.id != lookup.chunk->id ||
            (index != 0 && lookup.id <= previous_chunk_id)) {
            return false;
        }
        previous_chunk_id = lookup.id;
    }
    for (std::size_t index = 0; index < m_impl->chunks.size(); ++index) {
        const auto& chunk = m_impl->chunks[index];
        if (!chunk || !chunk->id || chunk->points.empty() || chunk->points.size() > ChunkCapacity) {
            return false;
        }
        const auto lookup = std::ranges::lower_bound(
            m_impl->chunks_by_id, chunk->id, {}, &Impl::ChunkLookup::id);
        if (lookup == m_impl->chunks_by_id.end() || lookup->id != chunk->id || lookup->chunk != chunk) {
            return false;
        }
        if (index != 0 &&
            m_impl->chunks[index - 1]->points.size() + chunk->points.size() <= ChunkCapacity) {
            return false;
        }
        point_count += chunk->points.size();
        for (const RoutePoint& point : chunk->points) {
            if (!point.id || !std::isfinite(point.position.x) || !std::isfinite(point.position.y) ||
                m_impl->FindChunkId(point.id) != chunk->id) {
                return false;
            }
            fingerprint ^= Impl::Fingerprint(point);
        }
    }
    std::size_t indexed_points = 0;
    for (std::size_t shard_index = 0; shard_index < Impl::IndexShardCount; ++shard_index) {
        const auto& shard = m_impl->index[shard_index];
        if (!shard) continue;
        RoutePointId previous;
        for (const auto& entry : shard->entries) {
            if (!entry.point || Impl::ShardFor(entry.point) != shard_index ||
                (previous && entry.point <= previous)) {
                return false;
            }
            const auto* chunk = m_impl->FindChunk(entry.chunk);
            if (chunk == nullptr ||
                std::ranges::find(chunk->points, entry.point, &RoutePoint::id) == chunk->points.end()) {
                return false;
            }
            previous = entry.point;
            ++indexed_points;
        }
    }
    return point_count == m_impl->size && indexed_points == m_impl->size &&
        fingerprint == m_impl->fingerprint && previous_chunk_id < m_impl->next_chunk;
}

RoutePointStorageStatistics PersistentRoutePointSequence::StorageStatistics() const noexcept {
    RoutePointStorageStatistics result;
    result.point_count = size();
    if (!m_impl) return result;
    result.chunk_count = m_impl->chunks.size();
    result.capacity_points = result.chunk_count * ChunkCapacity;
    result.min_chunk_points = ChunkCapacity;
    for (std::size_t index = 0; index < m_impl->chunks.size(); ++index) {
        const std::size_t count = m_impl->chunks[index]->points.size();
        result.min_chunk_points = std::min(result.min_chunk_points, count);
        result.max_chunk_points = std::max(result.max_chunk_points, count);
        if (count < ChunkCapacity / 2) ++result.under_half_full_chunks;
        if (index != 0 && m_impl->chunks[index - 1]->points.size() + count <= ChunkCapacity) {
            ++result.mergeable_adjacent_pairs;
        }
    }
    return result;
}

PersistentRoutePointSequence::const_iterator PersistentRoutePointSequence::begin() const noexcept {
    return const_iterator{m_impl, 0, 0};
}

PersistentRoutePointSequence::const_iterator PersistentRoutePointSequence::end() const noexcept {
    return const_iterator{m_impl, m_impl ? m_impl->chunks.size() : 0, 0};
}

bool PersistentRoutePointSequence::SharesStorageWith(
    const PersistentRoutePointSequence& other) const noexcept {
    return m_impl == other.m_impl;
}

std::vector<RoutePoint> PersistentRoutePointSequence::ToVector() const {
    return std::vector<RoutePoint>{begin(), end()};
}

RoutePointIdDelta PersistentRoutePointSequence::DifferenceIds(
    const PersistentRoutePointSequence& other) const {
    RoutePointIdDelta delta;
    if (SharesStorageWith(other)) return delta;
    if (!m_impl) {
        delta.added.reserve(other.size());
        for (const RoutePoint& point : other) delta.added.push_back(point.id);
        return delta;
    }
    if (!other.m_impl) {
        delta.removed.reserve(size());
        for (const RoutePoint& point : *this) delta.removed.push_back(point.id);
        return delta;
    }

    std::size_t left = 0;
    std::size_t right = 0;
    while (left < m_impl->chunks_by_id.size() || right < other.m_impl->chunks_by_id.size()) {
        const auto* before = left < m_impl->chunks_by_id.size()
            ? &m_impl->chunks_by_id[left]
            : nullptr;
        const auto* after = right < other.m_impl->chunks_by_id.size()
            ? &other.m_impl->chunks_by_id[right]
            : nullptr;
        if (after == nullptr || (before != nullptr && before->id < after->id)) {
            for (const RoutePoint& point : before->chunk->points) delta.removed.push_back(point.id);
            ++left;
            continue;
        }
        if (before == nullptr || after->id < before->id) {
            for (const RoutePoint& point : after->chunk->points) delta.added.push_back(point.id);
            ++right;
            continue;
        }
        if (before->chunk != after->chunk) {
            std::unordered_set<RoutePointId, IdHash> before_ids;
            std::unordered_set<RoutePointId, IdHash> after_ids;
            before_ids.reserve(before->chunk->points.size());
            after_ids.reserve(after->chunk->points.size());
            for (const RoutePoint& point : before->chunk->points) before_ids.insert(point.id);
            for (const RoutePoint& point : after->chunk->points) after_ids.insert(point.id);
            for (const RoutePoint& point : before->chunk->points) {
                if (!after_ids.contains(point.id)) delta.removed.push_back(point.id);
            }
            for (const RoutePoint& point : after->chunk->points) {
                if (!before_ids.contains(point.id)) delta.added.push_back(point.id);
            }
        }
        ++left;
        ++right;
    }
    if (!delta.added.empty() && !delta.removed.empty()) {
        const std::unordered_set<RoutePointId, IdHash> added(delta.added.begin(), delta.added.end());
        const std::unordered_set<RoutePointId, IdHash> removed(delta.removed.begin(), delta.removed.end());
        std::erase_if(delta.added, [&](const RoutePointId point) { return removed.contains(point); });
        std::erase_if(delta.removed, [&](const RoutePointId point) { return added.contains(point); });
    }
    return delta;
}

Result<PersistentRoutePointSequence> PersistentRoutePointSequence::WithMovedPoint(
    const RoutePointId point,
    const Vec2 position) const {
    PersistentRoutePointSequence result = *this;
    if (auto moved = result.Move(point, position); !moved) {
        return std::unexpected(std::move(moved.error()));
    }
    return result;
}

bool PersistentRoutePointSequence::operator==(const PersistentRoutePointSequence& other) const {
    return SharesStorageWith(other) ||
        (size() == other.size() && m_impl->fingerprint == other.m_impl->fingerprint &&
         std::ranges::equal(*this, other));
}

Result<void> PersistentRoutePointSequence::Insert(const std::size_t index, RoutePoint point) {
    if (index > size()) {
        return std::unexpected(Error{ErrorCode::InvalidArgument, "Route point index is out of range"});
    }
    if (!m_impl) {
        m_impl = std::make_shared<const Impl>(std::vector<RoutePoint>{std::move(point)});
        return {};
    }

    std::size_t chunk_index = 0;
    std::size_t local_index = index;
    while (chunk_index + 1 < m_impl->chunks.size() &&
           local_index > m_impl->chunks[chunk_index]->points.size()) {
        local_index -= m_impl->chunks[chunk_index]->points.size();
        ++chunk_index;
    }
    if (local_index > m_impl->chunks[chunk_index]->points.size()) {
        local_index = m_impl->chunks[chunk_index]->points.size();
    }
    const auto& source = *m_impl->chunks[chunk_index];
    Impl::RecordChunkClone(source);
    auto replacement = m_impl->CloneRoot();
    replacement->valid &= point.id && std::isfinite(point.position.x) && std::isfinite(point.position.y) &&
        !contains(point.id);
    std::array<bool, Impl::IndexShardCount> cloned{};

    std::vector<RoutePoint> points = source.points;
    points.insert(points.begin() + static_cast<std::ptrdiff_t>(local_index), point);
    if (points.size() <= ChunkCapacity) {
        auto chunk = std::make_shared<Impl::Chunk>(Impl::Chunk{source.id, std::move(points)});
        replacement->ReplaceChunk(source.id, std::move(chunk));
        replacement->SetIndex(point.id, source.id, cloned);
    } else {
        if (replacement->next_chunk == 0 || replacement->next_chunk == std::numeric_limits<std::uint64_t>::max()) {
            return std::unexpected(Error{ErrorCode::GenerationOverflow, "Route chunk ID space is exhausted"});
        }
        const std::size_t split = points.size() / 2;
        const std::uint64_t new_id = replacement->next_chunk++;
        auto left = std::make_shared<Impl::Chunk>();
        left->id = source.id;
        left->points.assign(
            std::make_move_iterator(points.begin()),
            std::make_move_iterator(points.begin() + static_cast<std::ptrdiff_t>(split)));
        auto right = std::make_shared<Impl::Chunk>();
        right->id = new_id;
        right->points.assign(
            std::make_move_iterator(points.begin() + static_cast<std::ptrdiff_t>(split)),
            std::make_move_iterator(points.end()));
        replacement->ReplaceChunk(source.id, left);
        replacement->chunks.insert(
            replacement->chunks.begin() + static_cast<std::ptrdiff_t>(chunk_index + 1), right);
        replacement->chunks_by_id.insert(
            std::ranges::lower_bound(replacement->chunks_by_id, new_id, {}, &Impl::ChunkLookup::id),
            Impl::ChunkLookup{new_id, right});
        replacement->SetIndex(point.id, local_index < split ? source.id : new_id, cloned);
        for (const RoutePoint& moved : right->points) replacement->SetIndex(moved.id, new_id, cloned);
    }
    ++replacement->size;
    replacement->fingerprint ^= Impl::Fingerprint(point);
    m_impl = std::move(replacement);
    return {};
}

Result<void> PersistentRoutePointSequence::Move(const RoutePointId point, const Vec2 position) {
    if (!std::isfinite(position.x) || !std::isfinite(position.y)) {
        return std::unexpected(Error{ErrorCode::InvalidArgument, "Route point position is invalid"});
    }
    if (!m_impl) {
        return std::unexpected(Error{ErrorCode::InvalidArgument, "Route point does not exist"});
    }
    const std::uint64_t chunk_id = m_impl->FindChunkId(point);
    const auto* source = m_impl->FindChunk(chunk_id);
    if (source == nullptr) {
        return std::unexpected(Error{ErrorCode::InvalidArgument, "Route point does not exist"});
    }
    const auto found = std::ranges::find(source->points, point, &RoutePoint::id);
    if (found == source->points.end()) {
        return std::unexpected(Error{ErrorCode::InvalidArgument, "Route point does not exist"});
    }
    if (found->position == position) return {};
    Impl::RecordChunkClone(*source);
    auto replacement = m_impl->CloneRoot();
    auto chunk = std::make_shared<Impl::Chunk>(*source);
    auto moved = std::ranges::find(chunk->points, point, &RoutePoint::id);
    replacement->fingerprint ^= Impl::Fingerprint(*moved);
    moved->position = position;
    replacement->fingerprint ^= Impl::Fingerprint(*moved);
    replacement->ReplaceChunk(chunk_id, std::move(chunk));
    m_impl = std::move(replacement);
    return {};
}

Result<void> PersistentRoutePointSequence::Remove(const std::span<const RoutePointId> points) {
    if (points.empty()) return {};
    if (!m_impl) {
        return std::unexpected(Error{ErrorCode::InvalidArgument, "Route point does not exist"});
    }
    std::unordered_set<RoutePointId, IdHash> unique;
    std::unordered_map<std::uint64_t, std::unordered_set<RoutePointId, IdHash>> removals;
    for (const RoutePointId point : points) {
        if (!unique.insert(point).second) continue;
        const std::uint64_t chunk = m_impl->FindChunkId(point);
        if (chunk == 0 || Find(point) == nullptr) {
            return std::unexpected(Error{ErrorCode::InvalidArgument, "Route point does not exist"});
        }
        removals[chunk].insert(point);
    }

    struct PlannedChunk final {
        std::shared_ptr<const Impl::Chunk> source;
        const std::unordered_set<RoutePointId, IdHash>* removed{nullptr};
        std::size_t survivors{0};
    };
    struct ChunkGroup final {
        std::size_t begin{0};
        std::size_t end{0};
        std::size_t size{0};
    };

    auto replacement = m_impl->CloneRoot();
    std::array<bool, Impl::IndexShardCount> cloned{};
    std::vector<PlannedChunk> plan;
    plan.reserve(m_impl->chunks.size());
    std::vector<ChunkGroup> groups;
    groups.reserve(m_impl->chunks.size());
    for (const auto& source : m_impl->chunks) {
        const auto removed = removals.find(source->id);
        const auto* removed_ids = removed != removals.end() ? &removed->second : nullptr;
        const std::size_t survivor_count =
            source->points.size() - (removed_ids != nullptr ? removed_ids->size() : 0);
        if (removed_ids != nullptr) {
            for (const RoutePoint& point : source->points) {
                if (removed_ids->contains(point.id)) {
                    replacement->fingerprint ^= Impl::Fingerprint(point);
                    replacement->EraseIndex(point.id, cloned);
                }
            }
        }
        if (survivor_count == 0) continue;
        const std::size_t planned = plan.size();
        plan.push_back(PlannedChunk{source, removed_ids, survivor_count});
        groups.push_back(ChunkGroup{
            .begin = planned,
            .end = planned + 1,
            .size = survivor_count,
        });
        while (groups.size() >= 2 &&
               groups[groups.size() - 2].size + groups.back().size <= ChunkCapacity) {
            auto right = std::move(groups.back());
            groups.pop_back();
            auto& left = groups.back();
            left.end = right.end;
            left.size += right.size;
        }
    }

    std::vector<std::shared_ptr<const Impl::Chunk>> compacted;
    compacted.reserve(groups.size());
    std::uint64_t chunk_merges = 0;
    std::uint64_t points_reindexed = 0;
    for (const ChunkGroup& group : groups) {
        if (group.end - group.begin == 1) {
            const PlannedChunk& chunk_plan = plan[group.begin];
            if (chunk_plan.removed == nullptr) {
                compacted.push_back(chunk_plan.source);
            } else {
                auto chunk = std::make_shared<Impl::Chunk>();
                chunk->id = chunk_plan.source->id;
                chunk->points.reserve(chunk_plan.survivors);
                for (const RoutePoint& point : chunk_plan.source->points) {
                    if (!chunk_plan.removed->contains(point.id)) chunk->points.push_back(point);
                }
                Impl::RecordChunkClone(*chunk);
                compacted.push_back(std::move(chunk));
            }
            continue;
        }

        chunk_merges += group.end - group.begin - 1;
        std::size_t survivor = group.begin;
        for (std::size_t index = group.begin + 1; index < group.end; ++index) {
            if (plan[index].survivors > plan[survivor].survivors) survivor = index;
        }
        const std::uint64_t survivor_id = plan[survivor].source->id;
        auto merged = std::make_shared<Impl::Chunk>();
        merged->id = survivor_id;
        merged->points.reserve(group.size);
        for (std::size_t index = group.begin; index < group.end; ++index) {
            const PlannedChunk& chunk_plan = plan[index];
            for (const RoutePoint& point : chunk_plan.source->points) {
                if (chunk_plan.removed != nullptr && chunk_plan.removed->contains(point.id)) continue;
                merged->points.push_back(point);
                if (index != survivor) {
                    replacement->SetIndex(point.id, survivor_id, cloned);
                    ++points_reindexed;
                }
            }
        }
        Impl::RecordChunkClone(*merged);
        compacted.push_back(std::move(merged));
    }
    struct ActiveChunk final {
        std::uint64_t id{0};
        std::shared_ptr<const Impl::Chunk> chunk;
    };
    const std::size_t active_capacity = std::bit_ceil(std::max<std::size_t>(2, compacted.size() * 2));
    std::vector<ActiveChunk> active_chunks(active_capacity);
    const auto active_slot = [active_capacity](const std::uint64_t id) {
        std::uint64_t hash = id;
        hash ^= hash >> 30U;
        hash *= 0xBF58476D1CE4E5B9ULL;
        hash ^= hash >> 27U;
        hash *= 0x94D049BB133111EBULL;
        hash ^= hash >> 31U;
        return static_cast<std::size_t>(hash) & (active_capacity - 1);
    };
    for (const auto& chunk : compacted) {
        std::size_t slot = active_slot(chunk->id);
        while (active_chunks[slot].id != 0) slot = (slot + 1) & (active_capacity - 1);
        active_chunks[slot] = ActiveChunk{chunk->id, chunk};
    }
    std::vector<Impl::ChunkLookup> compacted_lookup;
    compacted_lookup.reserve(compacted.size());
    for (const auto& lookup : replacement->chunks_by_id) {
        std::size_t slot = active_slot(lookup.id);
        while (active_chunks[slot].id != 0 && active_chunks[slot].id != lookup.id) {
            slot = (slot + 1) & (active_capacity - 1);
        }
        if (active_chunks[slot].id == lookup.id) {
            compacted_lookup.push_back(Impl::ChunkLookup{lookup.id, active_chunks[slot].chunk});
        }
    }
    replacement->chunks.assign(compacted.begin(), compacted.end());
    replacement->chunks_by_id.assign(compacted_lookup.begin(), compacted_lookup.end());
    replacement->size -= unique.size();
    Detail::RecordRouteCompaction(chunk_merges, points_reindexed);
    m_impl = replacement->size == 0 ? nullptr : std::move(replacement);
    return {};
}

LinkPresentation::LinkPresentation() = default;

LinkPresentation::LinkPresentation(LinkStyle style, PersistentRoutePointSequence route)
    : m_style(style == LinkStyle{} ? nullptr : std::make_shared<const LinkStyle>(std::move(style))),
      m_route(std::move(route)) {}

LinkPresentation::LinkPresentation(
    std::shared_ptr<const LinkStyle> style,
    PersistentRoutePointSequence route) noexcept
    : m_style(std::move(style)), m_route(std::move(route)) {}

const LinkStyle& LinkPresentation::Style() const noexcept {
    static const LinkStyle empty;
    return m_style ? *m_style : empty;
}

const PersistentRoutePointSequence& LinkPresentation::Route() const noexcept { return m_route; }

bool LinkPresentation::SharesStyleWith(const LinkPresentation& other) const noexcept {
    return m_style == other.m_style;
}

bool LinkPresentation::operator==(const LinkPresentation& other) const {
    return (SharesStyleWith(other) || Style() == other.Style()) && m_route == other.m_route;
}

GroupStyleHandle DefaultGroupStyle() {
    static const GroupStyleHandle style = std::make_shared<const GroupStyle>();
    return style;
}

GroupStyleHandle MakeGroupStyle(GroupStyle style) {
    auto result = std::make_shared<const GroupStyle>(std::move(style));
    Detail::RecordCowClone(
        Detail::CowCopyDomain::GroupStyles,
        Detail::CowCloneKind::Value,
        0,
        sizeof(GroupStyle) + result->title.size() + result->body.size());
    return result;
}

bool GroupPresentation::SharesStyleWith(const GroupPresentation& other) const noexcept {
    return style == other.style;
}

bool GroupPresentation::operator==(const GroupPresentation& other) const {
    const bool same_style = SharesStyleWith(other) ||
        (style && other.style && *style == *other.style);
    return id == other.id && graph == other.graph && geometry == other.geometry && same_style &&
        members == other.members && protection == other.protection;
}

namespace {

std::atomic<std::uint64_t> NextPresentationIdentity{1};

[[nodiscard]] std::uint64_t AllocateIdentity() noexcept {
    std::uint64_t identity = NextPresentationIdentity.fetch_add(1, std::memory_order_relaxed);
    if (identity == 0) {
        identity = NextPresentationIdentity.fetch_add(1, std::memory_order_relaxed);
    }
    return identity;
}

[[nodiscard]] Error MakeError(const ErrorCode code, std::string message) {
    return Error{code, std::move(message)};
}

[[nodiscard]] bool Finite(const Vec2 value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y);
}

} // namespace

namespace {

[[nodiscard]] Result<void> ValidatePresentationMaps(
    const GraphDocument& document,
    const NodePresentationMap& nodes,
    const LinkPresentationMap& links,
    const GroupPresentationMap& groups) {
    std::unordered_set<NodeId, IdHash> document_nodes;
    std::unordered_set<LinkId, IdHash> document_links;
    for (const auto& graph_reference : document.Graphs()) {
        const auto& graph = graph_reference.get();
        for (const auto& [node_id, node] : graph.nodes) {
            (void)node;
            document_nodes.insert(node_id);
        }
        for (const auto& [link_id, link] : graph.links) {
            (void)link;
            document_links.insert(link_id);
        }
    }

    for (const auto& [node_id, value] : nodes) {
        if (!node_id || !document_nodes.contains(node_id) || !Finite(value.position) || !Finite(value.size) ||
            value.size.x < 0.0f || value.size.y < 0.0f) {
            return std::unexpected(MakeError(ErrorCode::InvalidGraph, "Node presentation is invalid or orphaned"));
        }
    }

    std::unordered_set<RoutePointId, IdHash> route_points;
    for (const auto& [link_id, value] : links) {
        if (!link_id || !document_links.contains(link_id) || !value.Route().ValidateStructure()) {
            return std::unexpected(MakeError(ErrorCode::InvalidGraph, "Link presentation is invalid or orphaned"));
        }
        for (const auto& point : value.Route()) {
            if (!point.id || !Finite(point.position) || !route_points.insert(point.id).second) {
                return std::unexpected(MakeError(ErrorCode::InvalidGraph, "Route point is invalid or duplicated"));
            }
        }
    }

    for (const auto& [group_id, group] : groups) {
        if (!group_id || group.id != group_id || !group.graph || document.FindGraph(group.graph) == nullptr ||
            !group.style || !Finite(group.geometry.position) || !Finite(group.geometry.size) ||
            group.geometry.size.x < 0.0f || group.geometry.size.y < 0.0f ||
            (group.style->kind != GroupKind::Group && group.style->kind != GroupKind::Comment) ||
            !std::ranges::all_of(group.members, [&](const NodeId member) {
                return member && document.FindNode(group.graph, member) != nullptr;
            })) {
            return std::unexpected(MakeError(ErrorCode::InvalidGraph, "Group presentation is invalid"));
        }
    }
    return {};
}

} // namespace

struct GraphPresentation::Impl final {
    using RoutePointOwners =
        CowEntityMap<RoutePointId, LinkId, Detail::CowCopyDomain::PresentationIndexes>;
    using NodeGroups =
        CowEntityMap<NodeId, GroupAdjacencyRange, Detail::CowCopyDomain::PresentationIndexes>;
    using GraphGroups =
        CowEntityMap<GraphId, GroupAdjacencyRange, Detail::CowCopyDomain::PresentationIndexes>;

    NodePresentationMap nodes;
    LinkPresentationMap links;
    GroupPresentationMap groups;
    RoutePointOwners route_point_owners;
    NodeGroups node_groups;
    GraphGroups graph_groups;
    IdGenerator group_ids;
    IdGenerator route_point_ids;
    std::uint64_t revision{0};
    std::uint64_t geometry_revision{0};
    std::uint64_t allocation_epoch{0};
    std::uint64_t identity{AllocateIdentity()};

    template<typename Index, typename Key>
    static void AddIndexedGroup(Index& index, const Key key, const GroupId group) {
        GroupAdjacencyRange values;
        if (const auto found = index.find(key); found != index.end()) values = found->second;
        values.insert_or_assign(group, group);
        index.insert_or_assign(key, std::move(values));
    }

    template<typename Index, typename Key>
    static void RemoveIndexedGroup(Index& index, const Key key, const GroupId group) {
        const auto found = index.find(key);
        if (found == index.end()) return;
        GroupAdjacencyRange values = found->second;
        values.erase(group);
        if (values.empty()) index.erase(key);
        else index.insert_or_assign(key, std::move(values));
    }

    void IndexGroup(const GroupPresentation& group) {
        AddIndexedGroup(graph_groups, group.graph, group.id);
        for (const NodeId member : group.members) AddIndexedGroup(node_groups, member, group.id);
    }

    void UnindexGroup(const GroupPresentation& group) {
        RemoveIndexedGroup(graph_groups, group.graph, group.id);
        for (const NodeId member : group.members) RemoveIndexedGroup(node_groups, member, group.id);
    }
};

GraphPresentation::GraphPresentation()
    : m_impl(std::make_unique<Impl>()) {}

GraphPresentation::~GraphPresentation() = default;

GraphPresentation::GraphPresentation(GraphPresentation&& other)
    : m_impl(std::move(other.m_impl)) {
    other.m_impl = std::make_unique<Impl>();
}

GraphPresentation& GraphPresentation::operator=(GraphPresentation&& other) {
    if (this != &other) {
        auto replacement = std::make_unique<Impl>();
        m_impl = std::move(other.m_impl);
        other.m_impl = std::move(replacement);
    }
    return *this;
}

void GraphPresentation::Swap(GraphPresentation& other) noexcept {
    m_impl.swap(other.m_impl);
}

std::uint64_t GraphPresentation::PresentationRevision() const noexcept {
    return m_impl->revision;
}

std::uint64_t GraphPresentation::GeometryRevision() const noexcept {
    return m_impl->geometry_revision;
}

GroupId GraphPresentation::AllocateGroupId() noexcept {
    if (m_impl->allocation_epoch == std::numeric_limits<std::uint64_t>::max()) {
        return {};
    }
    const GroupId id = m_impl->group_ids.Next<GroupId>();
    if (id) {
        ++m_impl->allocation_epoch;
    }
    return id;
}

RoutePointId GraphPresentation::AllocateRoutePointId() noexcept {
    if (m_impl->allocation_epoch == std::numeric_limits<std::uint64_t>::max()) {
        return {};
    }
    const RoutePointId id = m_impl->route_point_ids.Next<RoutePointId>();
    if (id) {
        ++m_impl->allocation_epoch;
    }
    return id;
}

const NodePresentation* GraphPresentation::FindNode(const NodeId node) const noexcept {
    const auto found = m_impl->nodes.find(node);
    return found != m_impl->nodes.end() ? &found->second : nullptr;
}

const LinkPresentation* GraphPresentation::FindLink(const LinkId link) const noexcept {
    const auto found = m_impl->links.find(link);
    return found != m_impl->links.end() ? &found->second : nullptr;
}

const GroupPresentation* GraphPresentation::FindGroup(const GroupId group) const noexcept {
    const auto found = m_impl->groups.find(group);
    return found != m_impl->groups.end() ? &found->second : nullptr;
}

const RoutePoint* GraphPresentation::FindRoutePoint(
    const LinkId link,
    const RoutePointId point) const noexcept {
    const auto* state = FindLink(link);
    if (state == nullptr) {
        return nullptr;
    }
    return state->Route().Find(point);
}

const NodePresentationMap& GraphPresentation::Nodes() const noexcept {
    return m_impl->nodes;
}

const LinkPresentationMap& GraphPresentation::Links() const noexcept {
    return m_impl->links;
}

const GroupPresentationMap& GraphPresentation::Groups() const noexcept {
    return m_impl->groups;
}

LinkId GraphPresentation::RoutePointOwner(const RoutePointId point) const noexcept {
    const auto found = m_impl->route_point_owners.find(point);
    return found != m_impl->route_point_owners.end() ? found->second : LinkId{};
}

const GroupAdjacencyRange& GraphPresentation::GroupsForNode(const NodeId node) const noexcept {
    const auto found = m_impl->node_groups.find(node);
    return found != m_impl->node_groups.end()
        ? found->second
        : GroupAdjacencyRange::Empty();
}

const GroupAdjacencyRange& GraphPresentation::GroupsForGraph(const GraphId graph) const noexcept {
    const auto found = m_impl->graph_groups.find(graph);
    return found != m_impl->graph_groups.end()
        ? found->second
        : GroupAdjacencyRange::Empty();
}

void GraphPresentation::SetNode(const NodeId node, std::optional<NodePresentation> value) {
    if (value) {
        m_impl->nodes.insert_or_assign(node, std::move(*value));
    } else {
        m_impl->nodes.erase(node);
    }
}

Result<RoutePointIdDelta> GraphPresentation::SetLink(
    const LinkId link,
    std::optional<LinkPresentation> value) {
    const auto* current = FindLink(link);
    const bool same_route = current != nullptr && value && current->Route().SharesStorageWith(value->Route());
    RoutePointIdDelta delta;
    if (!same_route) {
        delta = (current != nullptr ? current->Route() : PersistentRoutePointSequence{}).DifferenceIds(
            value ? value->Route() : PersistentRoutePointSequence{});
        for (const RoutePointId point : delta.added) {
            const auto owner = m_impl->route_point_owners.find(point);
            if (owner != m_impl->route_point_owners.end() && owner->second != link) {
                return std::unexpected(MakeError(
                    ErrorCode::DuplicateId,
                    "Route point ID already belongs to another link"));
            }
        }
        for (const RoutePointId point : delta.removed) m_impl->route_point_owners.erase(point);
    }
    if (value) {
        if (!same_route) {
            for (const RoutePointId point : delta.added) {
                m_impl->route_point_ids.Observe(point);
                m_impl->route_point_owners.insert_or_assign(point, link);
            }
        }
        m_impl->links.insert_or_assign(link, std::move(*value));
    } else {
        m_impl->links.erase(link);
    }
    return delta;
}

namespace {

[[nodiscard]] bool EmptyLinkPresentation(const LinkStyle& style, const PersistentRoutePointSequence& route) {
    return style == LinkStyle{} && route.empty();
}

void RecordLinkStyleClone(const LinkStyle& style) noexcept {
    Detail::RecordCowClone(
        Detail::CowCopyDomain::LinkPresentations,
        Detail::CowCloneKind::Value,
        0,
        sizeof(LinkStyle) + style.router.Value().size());
}

} // namespace

Result<void> GraphPresentation::SetLinkRouter(const LinkId link, TypeId router) {
    const auto* current = FindLink(link);
    const LinkStyle& before = current != nullptr ? current->Style() : LinkPresentation{}.Style();
    if (before.router == router) return {};
    LinkStyle style{.router = std::move(router), .color = before.color, .locked = before.locked};
    if (current != nullptr) RecordLinkStyleClone(style);
    const auto shared_style = style == LinkStyle{} ? nullptr : std::make_shared<const LinkStyle>(std::move(style));
    const PersistentRoutePointSequence route = current != nullptr ? current->Route() : PersistentRoutePointSequence{};
    if (!shared_style && route.empty()) m_impl->links.erase(link);
    else m_impl->links.insert_or_assign(link, LinkPresentation{shared_style, route});
    return {};
}

Result<void> GraphPresentation::SetLinkColor(
    const LinkId link,
    const std::optional<std::uint32_t> color) {
    const auto* current = FindLink(link);
    const LinkStyle& before = current != nullptr ? current->Style() : LinkPresentation{}.Style();
    if (before.color == color) return {};
    LinkStyle style{.router = before.router, .color = color, .locked = before.locked};
    if (current != nullptr) RecordLinkStyleClone(style);
    const auto shared_style = style == LinkStyle{} ? nullptr : std::make_shared<const LinkStyle>(std::move(style));
    const PersistentRoutePointSequence route = current != nullptr ? current->Route() : PersistentRoutePointSequence{};
    if (!shared_style && route.empty()) m_impl->links.erase(link);
    else m_impl->links.insert_or_assign(link, LinkPresentation{shared_style, route});
    return {};
}

Result<void> GraphPresentation::SetLinkLocked(const LinkId link, const bool locked) {
    const auto* current = FindLink(link);
    if (current == nullptr) {
        return std::unexpected(MakeError(ErrorCode::InvalidArgument, "Link has no presentation state"));
    }
    if (current->Style().locked == locked) return {};
    LinkStyle style{
        .router = current->Style().router,
        .color = current->Style().color,
        .locked = locked,
    };
    RecordLinkStyleClone(style);
    const auto shared_style = style == LinkStyle{} ? nullptr : std::make_shared<const LinkStyle>(std::move(style));
    if (!shared_style && current->Route().empty()) m_impl->links.erase(link);
    else m_impl->links.insert_or_assign(link, LinkPresentation{shared_style, current->Route()});
    return {};
}

Result<RoutePointIdDelta> GraphPresentation::SetLinkRoute(
    const LinkId link,
    PersistentRoutePointSequence route) {
    const auto* current = FindLink(link);
    if (current != nullptr && current->Route().SharesStorageWith(route)) return {};
    return SetLink(
        link,
        EmptyLinkPresentation(current != nullptr ? current->Style() : LinkStyle{}, route)
            ? std::optional<LinkPresentation>{}
            : std::optional<LinkPresentation>{LinkPresentation{
                  current != nullptr ? current->m_style : nullptr,
                  std::move(route)}});
}

Result<void> GraphPresentation::InsertRoutePoint(
    const LinkId link,
    RoutePoint point,
    const std::size_t index) {
    const auto owner = m_impl->route_point_owners.find(point.id);
    if (owner != m_impl->route_point_owners.end()) {
        return std::unexpected(MakeError(ErrorCode::DuplicateId, "Route point ID already exists"));
    }
    const auto* current = FindLink(link);
    PersistentRoutePointSequence route = current != nullptr ? current->Route() : PersistentRoutePointSequence{};
    if (auto inserted = route.Insert(index, point); !inserted) return inserted;
    m_impl->route_point_ids.Observe(point.id);
    m_impl->route_point_owners.insert_or_assign(point.id, link);
    m_impl->links.insert_or_assign(
        link,
        LinkPresentation{current != nullptr ? current->m_style : nullptr, std::move(route)});
    return {};
}

Result<void> GraphPresentation::MoveRoutePoint(
    const LinkId link,
    const RoutePointId point,
    const Vec2 position) {
    const auto* current = FindLink(link);
    if (current == nullptr) {
        return std::unexpected(MakeError(ErrorCode::InvalidArgument, "Route point does not exist"));
    }
    PersistentRoutePointSequence route = current->Route();
    if (auto moved = route.Move(point, position); !moved) return moved;
    m_impl->links.insert_or_assign(link, LinkPresentation{current->m_style, std::move(route)});
    return {};
}

Result<void> GraphPresentation::RemoveRoutePoints(
    const LinkId link,
    const std::span<const RoutePointId> points) {
    const auto* current = FindLink(link);
    if (current == nullptr) {
        return std::unexpected(MakeError(ErrorCode::InvalidArgument, "Route point does not exist"));
    }
    PersistentRoutePointSequence route = current->Route();
    if (auto removed = route.Remove(points); !removed) return removed;
    std::unordered_set<RoutePointId, IdHash> unique(points.begin(), points.end());
    for (const RoutePointId point : unique) m_impl->route_point_owners.erase(point);
    if (EmptyLinkPresentation(current->Style(), route)) m_impl->links.erase(link);
    else m_impl->links.insert_or_assign(link, LinkPresentation{current->m_style, std::move(route)});
    return {};
}

Result<void> GraphPresentation::AddGroup(GroupPresentation group) {
    if (!group.id || !group.graph || !group.style) {
        return std::unexpected(MakeError(ErrorCode::InvalidArgument, "Group IDs and style must be non-null"));
    }
    if (m_impl->groups.contains(group.id)) {
        return std::unexpected(MakeError(ErrorCode::DuplicateId, "Group ID already exists"));
    }
    m_impl->group_ids.Observe(group.id);
    m_impl->IndexGroup(group);
    m_impl->groups.emplace(group.id, std::move(group));
    return {};
}

Result<GroupPresentation> GraphPresentation::RemoveGroup(const GroupId group) {
    const auto found = m_impl->groups.find(group);
    if (found == m_impl->groups.end()) {
        return std::unexpected(MakeError(ErrorCode::InvalidArgument, "Group does not exist"));
    }
    GroupPresentation removed = found->second;
    m_impl->UnindexGroup(removed);
    m_impl->groups.erase(found);
    return removed;
}

Result<void> GraphPresentation::SetGroupPosition(const GroupId group, const Vec2 position) {
    const auto found = m_impl->groups.find(group);
    if (found == m_impl->groups.end()) {
        return std::unexpected(MakeError(ErrorCode::InvalidArgument, "Group does not exist"));
    }
    GroupPresentation value = found->second;
    value.geometry.position = position;
    m_impl->groups.insert_or_assign(group, std::move(value));
    return {};
}

Result<void> GraphPresentation::SetGroupSize(const GroupId group, const Vec2 size) {
    const auto found = m_impl->groups.find(group);
    if (found == m_impl->groups.end()) {
        return std::unexpected(MakeError(ErrorCode::InvalidArgument, "Group does not exist"));
    }
    GroupPresentation value = found->second;
    value.geometry.size = size;
    m_impl->groups.insert_or_assign(group, std::move(value));
    return {};
}

Result<void> GraphPresentation::SetGroupCollapsed(const GroupId group, const bool collapsed) {
    const auto found = m_impl->groups.find(group);
    if (found == m_impl->groups.end()) {
        return std::unexpected(MakeError(ErrorCode::InvalidArgument, "Group does not exist"));
    }
    GroupPresentation value = found->second;
    value.geometry.collapsed = collapsed;
    m_impl->groups.insert_or_assign(group, std::move(value));
    return {};
}

Result<void> GraphPresentation::SetGroupZOrder(const GroupId group, const std::uint64_t z_order) {
    const auto found = m_impl->groups.find(group);
    if (found == m_impl->groups.end()) {
        return std::unexpected(MakeError(ErrorCode::InvalidArgument, "Group does not exist"));
    }
    GroupPresentation value = found->second;
    value.geometry.z_order = z_order;
    m_impl->groups.insert_or_assign(group, std::move(value));
    return {};
}

Result<void> GraphPresentation::SetGroupStyle(const GroupId group, GroupStyleHandle style) {
    const auto found = m_impl->groups.find(group);
    if (found == m_impl->groups.end()) {
        return std::unexpected(MakeError(ErrorCode::InvalidArgument, "Group does not exist"));
    }
    if (!style) {
        return std::unexpected(MakeError(ErrorCode::InvalidArgument, "Group style must be non-null"));
    }
    GroupPresentation value = found->second;
    value.style = std::move(style);
    m_impl->groups.insert_or_assign(group, std::move(value));
    return {};
}

Result<void> GraphPresentation::SetGroupMembers(const GroupId group, GroupMemberSet members) {
    const auto found = m_impl->groups.find(group);
    if (found == m_impl->groups.end()) {
        return std::unexpected(MakeError(ErrorCode::InvalidArgument, "Group does not exist"));
    }
    GroupPresentation value = found->second;
    for (const NodeId member : found->second.members) {
        if (!members.contains(member)) Impl::RemoveIndexedGroup(m_impl->node_groups, member, group);
    }
    for (const NodeId member : members) {
        if (!found->second.members.contains(member)) Impl::AddIndexedGroup(m_impl->node_groups, member, group);
    }
    value.members = std::move(members);
    m_impl->groups.insert_or_assign(group, std::move(value));
    return {};
}

Result<void> GraphPresentation::AddGroupMembers(
    const GroupId group,
    const std::span<const NodeId> members) {
    const auto found = m_impl->groups.find(group);
    if (found == m_impl->groups.end()) {
        return std::unexpected(MakeError(ErrorCode::InvalidArgument, "Group does not exist"));
    }
    GroupPresentation value = found->second;
    for (const NodeId member : members) {
        if (value.members.Insert(member)) Impl::AddIndexedGroup(m_impl->node_groups, member, group);
    }
    m_impl->groups.insert_or_assign(group, std::move(value));
    return {};
}

Result<void> GraphPresentation::RemoveGroupMembers(
    const GroupId group,
    const std::span<const NodeId> members) {
    const auto found = m_impl->groups.find(group);
    if (found == m_impl->groups.end()) {
        return std::unexpected(MakeError(ErrorCode::InvalidArgument, "Group does not exist"));
    }
    GroupPresentation value = found->second;
    for (const NodeId member : members) {
        if (value.members.Erase(member) != 0) Impl::RemoveIndexedGroup(m_impl->node_groups, member, group);
    }
    m_impl->groups.insert_or_assign(group, std::move(value));
    return {};
}

Result<void> GraphPresentation::SetGroupLocked(const GroupId group, const bool locked) {
    const auto found = m_impl->groups.find(group);
    if (found == m_impl->groups.end()) {
        return std::unexpected(MakeError(ErrorCode::InvalidArgument, "Group does not exist"));
    }
    GroupPresentation value = found->second;
    value.protection.locked = locked;
    m_impl->groups.insert_or_assign(group, std::move(value));
    return {};
}

Result<void> GraphPresentation::Import(
    const GraphDocument& document,
    NodePresentationMap nodes,
    LinkPresentationMap links,
    GroupPresentationMap groups) {
    if (auto valid = ValidatePresentationMaps(document, nodes, links, groups); !valid) {
        return valid;
    }

    auto replacement = std::make_unique<Impl>();
    replacement->nodes = std::move(nodes);
    replacement->links = std::move(links);
    replacement->groups = std::move(groups);
    std::unordered_map<GraphId, std::vector<GroupId>, IdHash> graph_groups;
    std::unordered_map<NodeId, std::vector<GroupId>, IdHash> node_groups;
    for (const auto& [group_id, group] : replacement->groups) {
        replacement->group_ids.Observe(group_id);
        graph_groups[group.graph].push_back(group_id);
        for (const NodeId member : group.members) node_groups[member].push_back(group_id);
    }
    for (const auto& [graph, values] : graph_groups) {
        for (const GroupId group : values) replacement->AddIndexedGroup(replacement->graph_groups, graph, group);
    }
    for (const auto& [node, values] : node_groups) {
        for (const GroupId group : values) replacement->AddIndexedGroup(replacement->node_groups, node, group);
    }
    for (const auto& [link_id, link] : replacement->links) {
        for (const auto& point : link.Route()) {
            replacement->route_point_ids.Observe(point.id);
            replacement->route_point_owners.emplace(point.id, link_id);
        }
    }
    m_impl = std::move(replacement);
    return {};
}

Result<void> ValidateGraphPresentation(
    const GraphDocument& document,
    const GraphPresentation& presentation) {
    if (auto valid = ValidatePresentationMaps(
            document, presentation.Nodes(), presentation.Links(), presentation.Groups()); !valid) {
        return valid;
    }
    if (!presentation.ValidateIndexes()) {
        return std::unexpected(MakeError(
            ErrorCode::InvalidGraph,
            "Derived presentation indexes do not match retained presentation state"));
    }
    return {};
}

bool GraphPresentation::ValidateIndexes() const {
    struct NodeMembership final {
        NodeId node;
        GroupId group;
        bool operator==(const NodeMembership&) const = default;
    };
    struct GraphMembership final {
        GraphId graph;
        GroupId group;
        bool operator==(const GraphMembership&) const = default;
    };
    struct NodeMembershipHash final {
        std::size_t operator()(const NodeMembership& value) const noexcept {
            std::size_t hash = IdHash{}(value.group);
            hash ^= IdHash{}(value.node) + 0x9E3779B9U + (hash << 6U) + (hash >> 2U);
            return hash;
        }
    };
    struct GraphMembershipHash final {
        std::size_t operator()(const GraphMembership& value) const noexcept {
            std::size_t hash = IdHash{}(value.group);
            hash ^= IdHash{}(value.graph) + 0x9E3779B9U + (hash << 6U) + (hash >> 2U);
            return hash;
        }
    };

    std::size_t route_points = 0;
    for (const auto& [link_id, link] : m_impl->links) {
        for (const auto& point : link.Route()) {
            ++route_points;
            const auto owner = m_impl->route_point_owners.find(point.id);
            if (owner == m_impl->route_point_owners.end() || owner->second != link_id) return false;
        }
    }
    if (route_points != m_impl->route_point_owners.size()) return false;

    std::unordered_set<NodeMembership, NodeMembershipHash> expected_node_memberships;
    std::unordered_set<GraphMembership, GraphMembershipHash> expected_graph_memberships;
    for (const auto& [group_id, group] : m_impl->groups) {
        expected_graph_memberships.insert(GraphMembership{group.graph, group_id});
        for (const NodeId member : group.members) {
            expected_node_memberships.insert(NodeMembership{member, group_id});
        }
    }
    std::unordered_set<NodeMembership, NodeMembershipHash> indexed_node_memberships;
    for (const auto& [node, groups] : m_impl->node_groups) {
        for (const GroupId group : groups) {
            if (!indexed_node_memberships.insert(NodeMembership{node, group}).second) return false;
        }
    }
    std::unordered_set<GraphMembership, GraphMembershipHash> indexed_graph_memberships;
    for (const auto& [graph, groups] : m_impl->graph_groups) {
        for (const GroupId group : groups) {
            if (!indexed_graph_memberships.insert(GraphMembership{graph, group}).second) return false;
        }
    }
    return expected_node_memberships == indexed_node_memberships &&
        expected_graph_memberships == indexed_graph_memberships;
}

GraphPresentation GraphPresentation::SnapshotForTransaction() const {
    GraphPresentation snapshot;
    *snapshot.m_impl = *m_impl;
    return snapshot;
}

std::uint64_t GraphPresentation::AllocationEpoch() const noexcept {
    return m_impl->allocation_epoch;
}

bool GraphPresentation::CanCommit(
    const std::uint64_t identity,
    const std::uint64_t revision,
    const std::uint64_t allocation_epoch,
    const bool changed,
    const bool geometry_changed) const noexcept {
    return Identity() == identity && m_impl->revision == revision &&
        m_impl->allocation_epoch == allocation_epoch &&
        (!changed || revision != std::numeric_limits<std::uint64_t>::max()) &&
        (!geometry_changed || m_impl->geometry_revision != std::numeric_limits<std::uint64_t>::max());
}

void GraphPresentation::CommitFrom(
    GraphPresentation&& staged,
    const bool changed,
    const bool geometry_changed) noexcept {
    if (!changed) {
        return;
    }
    const std::uint64_t identity = m_impl->identity;
    const std::uint64_t next_revision = m_impl->revision + 1;
    const std::uint64_t next_geometry_revision = m_impl->geometry_revision + (geometry_changed ? 1 : 0);
    m_impl = std::move(staged.m_impl);
    m_impl->identity = identity;
    m_impl->revision = next_revision;
    m_impl->geometry_revision = next_geometry_revision;
}

std::uint64_t GraphPresentation::Identity() const noexcept {
    return m_impl->identity;
}

} // namespace Uni::GUI::Nodes
