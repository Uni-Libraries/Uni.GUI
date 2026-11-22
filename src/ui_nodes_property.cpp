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

const PropertyValue* FindProperty(const PropertyBag& properties, const PropertyPath& path) noexcept {
    const std::string* root = path.Root();
    if (root == nullptr || root->empty()) return nullptr;
    const auto found = properties.find(*root);
    if (found == properties.end()) return nullptr;
    const PropertyValue* current = &found->second;
    for (const auto& segment : path.Nested()) {
        if (const auto* key = std::get_if<std::string>(&segment)) {
            const auto* object = current->GetIf<PropertyValue::Object>();
            if (object == nullptr) return nullptr;
            const auto child = object->find(*key);
            if (child == object->end()) return nullptr;
            current = &child->second;
        } else {
            const auto* array = current->GetIf<PropertyValue::Array>();
            const std::size_t index = std::get<std::size_t>(segment);
            if (array == nullptr || index >= array->size()) return nullptr;
            current = &(*array)[index];
        }
    }
    return current;
}

Result<PropertyBag> SetProperty(
    const PropertyBag& properties,
    const PropertyPath& path,
    std::optional<PropertyValue> value) {
    const std::string* root = path.Root();
    if (root == nullptr || root->empty())
        return std::unexpected(MakeError(ErrorCode::InvalidArgument, "Property path requires a non-empty root key"));
    PropertyBag result = properties;
    const auto found = result.find(*root);
    if (path.segments.size() == 1) {
        if (value) result.insert_or_assign(*root, std::move(*value));
        else result.erase(*root);
        return result;
    }
    if (found == result.end())
        return std::unexpected(MakeError(ErrorCode::InvalidArgument, "Property path root does not exist"));
    PropertyValue* current = &found->second;
    auto remaining = path.Nested();
    while (!remaining.empty()) {
        const auto& segment = remaining.front();
        remaining = remaining.subspan(1);
        if (const auto* key = std::get_if<std::string>(&segment)) {
            auto* object = current->GetIf<PropertyValue::Object>();
            if (!object) return std::unexpected(MakeError(ErrorCode::InvalidArgument, "Property path requires an object"));
            if (remaining.empty() && value) {
                object->insert_or_assign(*key, std::move(*value));
                break;
            }
            const auto child = object->find(*key);
            if (child == object->end()) return std::unexpected(MakeError(ErrorCode::InvalidArgument, "Property path does not exist"));
            if (remaining.empty()) object->erase(child);
            else current = &child->second;
        } else {
            auto* array = current->GetIf<PropertyValue::Array>();
            const auto index = std::get<std::size_t>(segment);
            if (!array || index >= array->size())
                return std::unexpected(MakeError(ErrorCode::InvalidArgument, "Property path array index does not exist"));
            current = &(*array)[index];
            if (remaining.empty()) {
                if (!value) return std::unexpected(MakeError(ErrorCode::InvalidArgument, "Replace the containing array to erase elements"));
                *current = std::move(*value);
            }
        }
    }
    return result;
}

namespace {

struct PropertyValidation {
    struct Pending final {
        const PropertyValue* value;
        std::size_t depth;
    };

    const PropertyLimits& limits;
    std::vector<Pending> pending;
    std::size_t value_count = 0;
    std::size_t array_elements = 0;
    std::size_t object_members = 0;
    std::size_t string_bytes = 0;

    Result<void> CheckKey(const std::string& key) {
        if (key.size() > limits.max_string_bytes || !AddWithin(string_bytes, key.size(), limits.max_total_string_bytes))
            return std::unexpected(MakeError(ErrorCode::SizeLimitExceeded, "Property key exceeds its configured limit"));
        if (!Detail::ValidUtf8(key)) return std::unexpected(MakeError(ErrorCode::InvalidArgument, "Property key is not valid UTF-8"));
        return {};
    }

    Result<void> Visit(const PropertyValue& value);
};

Result<void> PropertyValidation::Visit(const PropertyValue& value) {
    pending.push_back({&value, 1});

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
                    if (item.size() > limits.max_values - value_count - pending.size())
                        return std::unexpected(MakeError(ErrorCode::SizeLimitExceeded, "Property value exceeds max_values"));
                    for (auto child = item.rbegin(); child != item.rend(); ++child) {
                        pending.push_back({&*child, current.depth + 1});
                    }
                } else if constexpr (std::same_as<Item, PropertyValue::Object>) {
                    if (!AddWithin(object_members, item.size(), limits.max_object_members)) {
                        return std::unexpected(MakeError(ErrorCode::SizeLimitExceeded, "Property object exceeds max_object_members"));
                    }
                    if (item.size() > limits.max_values - value_count - pending.size())
                        return std::unexpected(MakeError(ErrorCode::SizeLimitExceeded, "Property value exceeds max_values"));
                    for (auto child = item.rbegin(); child != item.rend(); ++child) {
                        if (auto valid_key = CheckKey(child->first); !valid_key) return valid_key;
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

} // namespace

Result<void> ValidatePropertyValue(const PropertyValue& value, const PropertyLimits& limits) {
    return PropertyValidation{limits}.Visit(value);
}

Result<void> ValidatePropertyBag(const PropertyBag& properties, const PropertyLimits& limits) {
    if (properties.size() > limits.max_object_members) {
        return std::unexpected(MakeError(ErrorCode::SizeLimitExceeded, "Property bag exceeds max_object_members"));
    }
    PropertyValidation validation{limits};
    for (const auto& [key, value] : properties) {
        if (key.empty()) return std::unexpected(MakeError(ErrorCode::InvalidArgument, "Property key is empty"));
        if (auto valid = validation.CheckKey(key); !valid) return valid;
        if (auto valid = validation.Visit(value); !valid) return valid;
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
