#pragma once

#include <uni/gui/export.h>
#include <uni/gui/nodes/error.h>

#include <compare>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <initializer_list>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace Uni::GUI::Nodes {

struct Vec2 final {
    float x{0.0f};
    float y{0.0f};

    auto operator<=>(const Vec2&) const = default;
};

struct PropertyNull final {
    bool operator==(const PropertyNull&) const = default;
};

struct PropertyReference final {
    std::string type;
    std::string value;

    bool operator==(const PropertyReference&) const = default;
};

struct OpaqueProperty final {
    // Canonical JSON object retained for property kinds unknown to this build.
    std::string canonical_json;

    bool operator==(const OpaqueProperty&) const = default;
};

struct UNI_GUI_EXPORT PropertyValue final {
    using Array = std::vector<PropertyValue>;
    using Object = std::map<std::string, PropertyValue, std::less<>>;
    using Storage = std::variant<
        PropertyNull,
        bool,
        std::int64_t,
        double,
        std::string,
        Vec2,
        PropertyReference,
        Array,
        Object,
        OpaqueProperty>;

    Storage value{PropertyNull{}};

    PropertyValue() = default;
    PropertyValue(PropertyNull item) noexcept;
    PropertyValue(bool item) noexcept;
    PropertyValue(std::int64_t item) noexcept;
    PropertyValue(double item) noexcept;
    PropertyValue(std::string item);
    PropertyValue(const char* item);
    PropertyValue(Vec2 item) noexcept;
    PropertyValue(PropertyReference item);
    PropertyValue(Array item);
    PropertyValue(Object item);
    PropertyValue(OpaqueProperty item);

    template<typename T>
    [[nodiscard]] const T* GetIf() const noexcept {
        return std::get_if<T>(&value);
    }

    template<typename T>
    [[nodiscard]] T* GetIf() noexcept {
        return std::get_if<T>(&value);
    }

    template<typename T>
    [[nodiscard]] const T& Get() const {
        return std::get<T>(value);
    }

    template<typename T>
    [[nodiscard]] T& Get() {
        return std::get<T>(value);
    }

    template<typename T>
    [[nodiscard]] bool Is() const noexcept {
        return std::holds_alternative<T>(value);
    }

    bool operator==(const PropertyValue&) const = default;
};

using PropertyBag = PropertyValue::Object;

using PropertyPathSegment = std::variant<std::string, std::size_t>;

struct PropertyPath final {
    std::vector<PropertyPathSegment> segments;

    PropertyPath() = default;
    PropertyPath(const char* property) : segments{std::string{property}} {}
    PropertyPath(std::string property) : segments{std::move(property)} {}
    PropertyPath(std::initializer_list<PropertyPathSegment> path) : segments(path) {}

    [[nodiscard]] const std::string* Root() const noexcept {
        return segments.empty() ? nullptr : std::get_if<std::string>(&segments.front());
    }
    [[nodiscard]] std::span<const PropertyPathSegment> Nested() const noexcept {
        return segments.empty() ? std::span<const PropertyPathSegment>{}
                                : std::span<const PropertyPathSegment>{segments}.subspan(1);
    }
    [[nodiscard]] bool IsPrefixOf(const PropertyPath& other) const noexcept {
        return segments.size() <= other.segments.size() &&
               std::equal(segments.begin(), segments.end(), other.segments.begin());
    }

    auto operator<=>(const PropertyPath&) const = default;
};

struct PropertyLimits final {
    std::size_t max_depth{128};
    std::size_t max_values{400'000};
    std::size_t max_array_elements{400'000};
    std::size_t max_object_members{400'000};
    std::size_t max_string_bytes{16U * 1024U * 1024U};
    std::size_t max_total_string_bytes{64U * 1024U * 1024U};
};

[[nodiscard]] UNI_GUI_EXPORT const PropertyValue* FindProperty(
    const PropertyBag& properties,
    const PropertyPath& path) noexcept;

[[nodiscard]] UNI_GUI_EXPORT Result<PropertyBag> SetProperty(
    const PropertyBag& properties,
    const PropertyPath& path,
    std::optional<PropertyValue> value);

[[nodiscard]] UNI_GUI_EXPORT Result<void> ValidatePropertyValue(
    const PropertyValue& value,
    const PropertyLimits& limits = {});

[[nodiscard]] UNI_GUI_EXPORT Result<void> ValidatePropertyBag(
    const PropertyBag& properties,
    const PropertyLimits& limits = {});

[[nodiscard]] UNI_GUI_EXPORT std::uint64_t EstimatePropertyBytes(const PropertyValue& value) noexcept;

} // namespace Uni::GUI::Nodes
