#include <uni/gui/nodes/graph.h>

#include "ui_nodes_internal.h"
#include "ui_nodes_json.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace Uni::GUI::Nodes {
namespace {

[[nodiscard]] Error MakeError(const ErrorCode code, std::string message) {
    return Error{code, std::move(message)};
}

[[nodiscard]] bool AddWithin(std::size_t& value, const std::size_t added, const std::size_t limit) noexcept {
    if (value > limit || added > limit - value) return false;
    value += added;
    return true;
}

void SaturatingAdd(std::uint64_t& value, const std::uint64_t added) noexcept {
    const auto maximum = std::numeric_limits<std::uint64_t>::max();
    value = value > maximum - added ? maximum : value + added;
}

void SaturatingMultiplyAdd(std::uint64_t& value, const std::size_t count, const std::size_t size) noexcept {
    const auto maximum = std::numeric_limits<std::uint64_t>::max();
    if (count != 0 && size > maximum / count) {
        value = maximum;
        return;
    }
    SaturatingAdd(value, static_cast<std::uint64_t>(count * size));
}

} // namespace

PropertyValue::PropertyValue(PropertyNull item) noexcept : value(item) {}
PropertyValue::PropertyValue(const bool item) noexcept : value(item) {}
PropertyValue::PropertyValue(const std::int64_t item) noexcept : value(item) {}
PropertyValue::PropertyValue(const double item) noexcept : value(item) {}
PropertyValue::PropertyValue(std::string item) : value(std::move(item)) {}
PropertyValue::PropertyValue(const char* item) : value(std::string{item}) {}
PropertyValue::PropertyValue(const Vec2 item) noexcept : value(item) {}
PropertyValue::PropertyValue(PropertyReference item) : value(std::move(item)) {}
PropertyValue::PropertyValue(Array item) : value(std::move(item)) {}
PropertyValue::PropertyValue(Object item) : value(std::move(item)) {}
PropertyValue::PropertyValue(OpaqueProperty item) : value(std::move(item)) {}

Result<void> ValidatePropertyValue(const PropertyValue& value, const PropertyLimits& limits) {
    struct Pending final {
        const PropertyValue* value;
        std::size_t depth;
    };

    std::vector<Pending> pending{{&value, 1}};
    std::size_t value_count = 0;
    std::size_t array_elements = 0;
    std::size_t object_members = 0;
    std::size_t string_bytes = 0;

    while (!pending.empty()) {
        const Pending current = pending.back();
        pending.pop_back();
        if (current.depth > limits.max_depth) {
            return std::unexpected(MakeError(ErrorCode::SizeLimitExceeded, "Property value exceeds max_depth"));
        }
        if (!AddWithin(value_count, 1, limits.max_values)) {
            return std::unexpected(MakeError(ErrorCode::SizeLimitExceeded, "Property value exceeds max_values"));
        }

        auto valid = std::visit(
            [&](const auto& item) -> Result<void> {
                using Item = std::remove_cvref_t<decltype(item)>;
                if constexpr (std::same_as<Item, double>) {
                    if (!std::isfinite(item)) {
                        return std::unexpected(MakeError(ErrorCode::InvalidArgument, "Property number must be finite"));
                    }
                } else if constexpr (std::same_as<Item, std::string>) {
                    if (item.size() > limits.max_string_bytes ||
                        !AddWithin(string_bytes, item.size(), limits.max_total_string_bytes)) {
                        return std::unexpected(MakeError(ErrorCode::SizeLimitExceeded, "Property string exceeds its configured limit"));
                    }
                    if (!Detail::ValidUtf8(item))
                        return std::unexpected(MakeError(ErrorCode::InvalidArgument, "Property string is not valid UTF-8"));
                } else if constexpr (std::same_as<Item, Vec2>) {
                    if (!std::isfinite(item.x) || !std::isfinite(item.y)) {
                        return std::unexpected(MakeError(ErrorCode::InvalidArgument, "Property vector must be finite"));
                    }
                } else if constexpr (std::same_as<Item, PropertyReference>) {
                    if (item.type.empty() || item.value.empty() || !Detail::ValidUtf8(item.type) ||
                        !Detail::ValidUtf8(item.value)) {
                        return std::unexpected(MakeError(ErrorCode::InvalidArgument, "Property reference is invalid"));
                    }
                    if (item.type.size() > limits.max_string_bytes || item.value.size() > limits.max_string_bytes ||
                        !AddWithin(string_bytes, item.type.size(), limits.max_total_string_bytes) ||
                        !AddWithin(string_bytes, item.value.size(), limits.max_total_string_bytes)) {
                        return std::unexpected(MakeError(ErrorCode::SizeLimitExceeded, "Property reference exceeds its configured limit"));
                    }
                } else if constexpr (std::same_as<Item, PropertyValue::Array>) {
                    if (!AddWithin(array_elements, item.size(), limits.max_array_elements)) {
                        return std::unexpected(MakeError(ErrorCode::SizeLimitExceeded, "Property array exceeds max_array_elements"));
                    }
                    pending.reserve(pending.size() + item.size());
                    for (auto child = item.rbegin(); child != item.rend(); ++child) {
                        pending.push_back({&*child, current.depth + 1});
                    }
                } else if constexpr (std::same_as<Item, PropertyValue::Object>) {
                    if (!AddWithin(object_members, item.size(), limits.max_object_members)) {
                        return std::unexpected(MakeError(ErrorCode::SizeLimitExceeded, "Property object exceeds max_object_members"));
                    }
                    pending.reserve(pending.size() + item.size());
                    for (auto child = item.rbegin(); child != item.rend(); ++child) {
                        if (child->first.size() > limits.max_string_bytes || !Detail::ValidUtf8(child->first)) {
                            return std::unexpected(MakeError(ErrorCode::InvalidArgument, "Property object key is invalid"));
                        }
                        if (!AddWithin(string_bytes, child->first.size(), limits.max_total_string_bytes)) {
                            return std::unexpected(MakeError(ErrorCode::SizeLimitExceeded, "Property strings exceed max_total_string_bytes"));
                        }
                        pending.push_back({&child->second, current.depth + 1});
                    }
                } else if constexpr (std::same_as<Item, OpaqueProperty>) {
                    if (item.canonical_json.size() > limits.max_string_bytes) {
                        return std::unexpected(MakeError(ErrorCode::SizeLimitExceeded, "Opaque property exceeds max_string_bytes"));
                    }
                    if (!Detail::ValidOpaqueJsonProperty(item.canonical_json)) {
                        return std::unexpected(MakeError(ErrorCode::InvalidArgument, "Opaque property is invalid"));
                    }
                    if (!AddWithin(string_bytes, item.canonical_json.size(), limits.max_total_string_bytes)) {
                        return std::unexpected(MakeError(ErrorCode::SizeLimitExceeded, "Property strings exceed max_total_string_bytes"));
                    }
                }
                return {};
            },
            current.value->value);
        if (!valid) return valid;
    }
    return {};
}

Result<void> ValidatePropertyBag(const PropertyBag& properties, const PropertyLimits& limits) {
    if (properties.size() > limits.max_object_members) {
        return std::unexpected(MakeError(ErrorCode::SizeLimitExceeded, "Property bag exceeds max_object_members"));
    }
    PropertyValue root{properties};
    PropertyLimits bag_limits = limits;
    if (bag_limits.max_depth != std::numeric_limits<std::size_t>::max()) ++bag_limits.max_depth;
    if (bag_limits.max_values != std::numeric_limits<std::size_t>::max()) ++bag_limits.max_values;
    if (bag_limits.max_object_members <= std::numeric_limits<std::size_t>::max() - properties.size())
        bag_limits.max_object_members += properties.size();
    if (auto valid = ValidatePropertyValue(root, bag_limits); !valid) return valid;
    for (const auto& [key, value] : properties) {
        (void)value;
        if (key.empty() || key.size() > limits.max_string_bytes || !Detail::ValidUtf8(key)) {
            return std::unexpected(MakeError(ErrorCode::InvalidArgument, "Property key is invalid"));
        }
    }
    return {};
}

std::uint64_t EstimatePropertyBytes(const PropertyValue& value) noexcept {
    std::uint64_t bytes = sizeof(PropertyValue);
    std::vector<const PropertyValue*> pending{&value};
    while (!pending.empty()) {
        const PropertyValue* current = pending.back();
        pending.pop_back();
        std::visit(
            [&](const auto& item) {
                using Item = std::remove_cvref_t<decltype(item)>;
                if constexpr (std::same_as<Item, std::string>) {
                    SaturatingAdd(bytes, item.capacity());
                } else if constexpr (std::same_as<Item, PropertyReference>) {
                    SaturatingAdd(bytes, item.type.capacity());
                    SaturatingAdd(bytes, item.value.capacity());
                } else if constexpr (std::same_as<Item, PropertyValue::Array>) {
                    SaturatingMultiplyAdd(bytes, item.capacity(), sizeof(PropertyValue));
                    for (const auto& child : item) pending.push_back(&child);
                } else if constexpr (std::same_as<Item, PropertyValue::Object>) {
                    SaturatingMultiplyAdd(bytes, item.size(), sizeof(PropertyValue::Object::value_type));
                    for (const auto& [key, child] : item) {
                        SaturatingAdd(bytes, key.capacity());
                        pending.push_back(&child);
                    }
                } else if constexpr (std::same_as<Item, OpaqueProperty>) {
                    SaturatingAdd(bytes, item.canonical_json.capacity());
                }
            },
            current->value);
    }
    return bytes;
}

} // namespace Uni::GUI::Nodes
