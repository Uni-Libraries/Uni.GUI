#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <string>
#include <string_view>
#include <utility>

namespace Uni::GUI::Nodes {

template<typename Tag>
class StrongId final {
public:
    constexpr StrongId() noexcept = default;
    constexpr explicit StrongId(const std::uint64_t value) noexcept
        : m_value(value) {}

    [[nodiscard]] constexpr std::uint64_t Value() const noexcept { return m_value; }
    [[nodiscard]] constexpr explicit operator bool() const noexcept { return m_value != 0; }

    auto operator<=>(const StrongId&) const = default;

private:
    std::uint64_t m_value{0};
};

using GraphId = StrongId<struct GraphIdTag>;
using NodeId = StrongId<struct NodeIdTag>;
using PinId = StrongId<struct PinIdTag>;
using LinkId = StrongId<struct LinkIdTag>;
using GroupId = StrongId<struct GroupIdTag>;
using RoutePointId = StrongId<struct RoutePointIdTag>;
using IntergraphLinkId = StrongId<struct IntergraphLinkIdTag>;

struct IdHash final {
    template<typename Tag>
    [[nodiscard]] std::size_t operator()(const StrongId<Tag> id) const noexcept {
        std::uint64_t value = id.Value();
        value ^= value >> 30U;
        value *= 0xBF58476D1CE4E5B9ULL;
        value ^= value >> 27U;
        value *= 0x94D049BB133111EBULL;
        value ^= value >> 31U;
        return static_cast<std::size_t>(value);
    }
};

class TypeId final {
public:
    TypeId() = default;
    explicit TypeId(const char* value)
        : m_value(value != nullptr ? value : "") {}
    explicit TypeId(std::string value)
        : m_value(std::move(value)) {}
    explicit TypeId(const std::string_view value)
        : m_value(value) {}

    [[nodiscard]] const std::string& Value() const noexcept { return m_value; }
    [[nodiscard]] bool Empty() const noexcept { return m_value.empty(); }

    auto operator<=>(const TypeId&) const = default;

private:
    std::string m_value;
};

class GraphAssetId final {
public:
    GraphAssetId() = default;
    explicit GraphAssetId(const char* value)
        : m_value(value != nullptr ? value : "") {}
    explicit GraphAssetId(std::string value)
        : m_value(std::move(value)) {}
    explicit GraphAssetId(const std::string_view value)
        : m_value(value) {}

    [[nodiscard]] const std::string& Value() const noexcept { return m_value; }
    [[nodiscard]] bool Empty() const noexcept { return m_value.empty(); }

    auto operator<=>(const GraphAssetId&) const = default;

private:
    std::string m_value;
};

struct GraphAssetIdHash final {
    [[nodiscard]] std::size_t operator()(const GraphAssetId& id) const noexcept {
        return std::hash<std::string>{}(id.Value());
    }
};

struct TypeIdHash final {
    [[nodiscard]] std::size_t operator()(const TypeId& id) const noexcept {
        return std::hash<std::string>{}(id.Value());
    }
};

class IdGenerator final {
public:
    explicit constexpr IdGenerator(const std::uint64_t first = 1) noexcept
        : m_next(first == 0 ? 1 : first) {}

    template<typename Id>
    [[nodiscard]] constexpr Id Next() noexcept {
        if (m_next == 0) {
            return {};
        }
        const std::uint64_t value = m_next;
        m_next = value == std::numeric_limits<std::uint64_t>::max() ? 0 : value + 1;
        return Id{value};
    }

    template<typename Tag>
    constexpr void Observe(const StrongId<Tag> id) noexcept {
        if (m_next != 0 && id.Value() >= m_next) {
            m_next = id.Value() == std::numeric_limits<std::uint64_t>::max()
                ? 0
                : id.Value() + 1;
        }
    }

private:
    std::uint64_t m_next{1};
};

} // namespace Uni::GUI::Nodes
