#pragma once

/// @file
/// @brief Owning typed property trees, path-based edits and validation budgets.

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

/// Two coordinates; the enclosing API defines the coordinate system. Persisted components must be finite.
struct Vec2 final {
    float x{0.0f};
    float y{0.0f};

    auto operator<=>(const Vec2&) const = default;
};

/// A stored null value. Unlike std::nullopt in edit APIs, this does not erase the property.
struct PropertyNull final {
    bool operator==(const PropertyNull&) const = default;
};

/// Application-defined reference namespace and key; both must be non-empty UTF-8. Nodes does not resolve them.
struct PropertyReference final {
    std::string type;
    std::string value;

    bool operator==(const PropertyReference&) const = default;
};

/// Canonical typed JSON object retained when its property kind is unknown to this build.
struct OpaqueProperty final {
    std::string canonical_json;

    bool operator==(const OpaqueProperty&) const = default;
};

/// Recursive value with no implicit type coercion. Arrays preserve order; objects use ordered string
/// keys. Construction stores the selected alternative, not a validated tree (see ValidatePropertyValue()).
/// A valid value has finite numeric/vector components and valid UTF-8 strings and keys. References
/// require both strings to be non-empty. Opaque extension envelopes may occur at any nesting level.
/// A C-string argument must point to a null-terminated string and is copied.
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

    /// @name Typed access
    /// T must be a Storage alternative. GetIf returns null on mismatch, Get throws
    /// std::bad_variant_access, and Is only tests the tag. Pointers/references borrow the stored
    /// alternative: do not retain across replacement of the value or destruction of its owner.
    /// @{
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

    /// @}

    bool operator==(const PropertyValue&) const = default;
};

/// Root properties of a node. Root names must be non-empty; nested object keys may be empty.
using PropertyBag = PropertyValue::Object;

/// An object key (literal, with no dot syntax) or a zero-based array index.
using PropertyPathSegment = std::variant<std::string, std::size_t>;

/// Path from a property bag to a leaf. For lookup/edit the first segment must be a non-empty key.
/// A single string constructs a one-key path; constructors do not check path validity.
struct PropertyPath final {
    std::vector<PropertyPathSegment> segments;

    PropertyPath() = default;
    PropertyPath(const char* property) : segments{std::string{property}} {}
    PropertyPath(std::string property) : segments{std::move(property)} {}
    PropertyPath(std::initializer_list<PropertyPathSegment> path) : segments(path) {}

    /// Borrow the first key; null for an empty path or an index in first position.
    [[nodiscard]] const std::string* Root() const noexcept {
        return segments.empty() ? nullptr : std::get_if<std::string>(&segments.front());
    }
    /// Borrow all segments after the first; modifying the segment vector may invalidate the span.
    [[nodiscard]] std::span<const PropertyPathSegment> Nested() const noexcept {
        return segments.empty() ? std::span<const PropertyPathSegment>{}
                                : std::span<const PropertyPathSegment>{segments}.subspan(1);
    }
    /// Compare whole segments, not string prefixes; equal paths and the empty prefix match.
    [[nodiscard]] bool IsPrefixOf(const PropertyPath& other) const noexcept {
        return segments.size() <= other.segments.size() &&
               std::equal(segments.begin(), segments.end(), other.segments.begin());
    }

    auto operator<=>(const PropertyPath&) const = default;
};

/// Semantic validation budgets: depth starts at one for a value. Element/member/value counts
/// and total string bytes accumulate over the tree. Individual string lengths are in bytes.
struct PropertyLimits final {
    std::size_t max_depth{128};
    std::size_t max_values{400'000};
    std::size_t max_array_elements{400'000};
    std::size_t max_object_members{400'000};
    std::size_t max_string_bytes{16U * 1024U * 1024U};
    std::size_t max_total_string_bytes{64U * 1024U * 1024U};
};

/// Follow path in properties, without insertion. Returns null for a bad root, missing member,
/// wrong container type or out-of-bounds index. The result borrows the bag and its nested storage.
[[nodiscard]] UNI_GUI_EXPORT const PropertyValue* FindProperty(
    const PropertyBag& properties,
    const PropertyPath& path) noexcept;

/// Return an edited copy without modifying @p properties or validating its semantic budgets.
/// The root must be a nonempty key. Intermediate containers and array elements must exist.
/// A missing value erases an object member (a missing root is a no-op); erasing an array
/// element or traversing an invalid path returns InvalidArgument.
[[nodiscard]] UNI_GUI_EXPORT Result<PropertyBag> SetProperty(
    const PropertyBag& properties,
    const PropertyPath& path,
    std::optional<PropertyValue> value);

/// Validate one borrowed tree, with its root at depth one. Returns SizeLimitExceeded for
/// an exceeded inclusive budget, or InvalidArgument for invalid property data.
[[nodiscard]] UNI_GUI_EXPORT Result<void> ValidatePropertyValue(
    const PropertyValue& value,
    const PropertyLimits& limits = {});

/// Validate nonempty UTF-8 root keys and all values without copying their trees. Each
/// root value starts at depth one; value, array, nested-member and string-byte totals
/// are shared across roots. The bag's own size is also bounded by max_object_members,
/// separately from nested members. Error codes are the same as ValidatePropertyValue().
[[nodiscard]] UNI_GUI_EXPORT Result<void> ValidatePropertyBag(
    const PropertyBag& properties,
    const PropertyLimits& limits = {});

/// Estimate logical memory use, including nested storage and string/array capacities. Saturates
/// at UINT64_MAX. This is neither a serialized size nor a measurement of allocator overhead.
[[nodiscard]] UNI_GUI_EXPORT std::uint64_t EstimatePropertyBytes(const PropertyValue& value) noexcept;

} // namespace Uni::GUI::Nodes
