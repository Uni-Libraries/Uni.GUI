#include <uni/gui/nodes/io.h>

#include "ui_nodes_internal.h"
#include "ui_nodes_json.h"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_iostream.h>

#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <exception>
#include <limits>
#include <map>
#include <ranges>
#include <set>
#include <span>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <aclapi.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/xattr.h>
#include <unistd.h>
#endif

namespace Uni::GUI::Nodes {
namespace {

constexpr std::string_view FormatName = "uni.gui.nodes";

using Detail::Array;
using Detail::Bool;
using Detail::DumpJson;
using Detail::Fail;
using Detail::Json;
using Detail::JsonFailure;
using Detail::Null;
using Detail::Number;
using Detail::Object;
using Detail::ParseJson;
using Detail::String;
using Detail::ValidUtf8;

[[nodiscard]] const Json::Object& AsObject(const Json& value, const std::string_view where) {
    const auto* result = std::get_if<Json::Object>(&value.value);
    if (result == nullptr)
        Fail(ErrorCode::InvalidFormat, std::string(where) + " must be an object");
    return *result;
}

[[nodiscard]] const Json::Array& AsArray(const Json& value, const std::string_view where) {
    const auto* result = std::get_if<Json::Array>(&value.value);
    if (result == nullptr)
        Fail(ErrorCode::InvalidFormat, std::string(where) + " must be an array");
    return *result;
}

[[nodiscard]] const std::string& AsString(const Json& value, const std::string_view where) {
    const auto* result = std::get_if<std::string>(&value.value);
    if (result == nullptr)
        Fail(ErrorCode::InvalidFormat, std::string(where) + " must be a string");
    return *result;
}

[[nodiscard]] bool AsBool(const Json& value, const std::string_view where) {
    const auto* result = std::get_if<bool>(&value.value);
    if (result == nullptr)
        Fail(ErrorCode::InvalidFormat, std::string(where) + " must be a boolean");
    return *result;
}

[[nodiscard]] const Json::Number& AsNumber(const Json& value, const std::string_view where) {
    const auto* result = std::get_if<Json::Number>(&value.value);
    if (result == nullptr)
        Fail(ErrorCode::InvalidFormat, std::string(where) + " must be a number");
    return *result;
}

[[nodiscard]] const Json& Field(const Json::Object& object, const std::string_view key, const std::string_view where) {
    const auto found = object.find(key);
    if (found == object.end()) {
        Fail(ErrorCode::InvalidFormat, std::string(where) + " is missing field '" + std::string(key) + "'");
    }
    return found->second;
}

void ExpectKeys(const Json::Object& object, const std::initializer_list<std::string_view> keys,
                const std::string_view where) {
    if (object.size() != keys.size() ||
        std::ranges::any_of(keys, [&](const std::string_view key) { return !object.contains(key); })) {
        Fail(ErrorCode::InvalidFormat, std::string(where) + " contains missing or unsupported fields");
    }
}

template<typename Integer>
[[nodiscard]] Integer ParseIntegerText(const std::string_view text, const std::string_view where,
                                       const bool allow_negative) {
    if (text.empty() || (!allow_negative && text.front() == '-') || text.front() == '+' ||
        (text.size() > 1 && text.front() == '0') || (text.size() > 2 && text[0] == '-' && text[1] == '0')) {
        Fail(ErrorCode::InvalidFormat, std::string(where) + " is not a canonical integer");
    }
    Integer result{};
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) {
        Fail(ErrorCode::InvalidFormat, std::string(where) + " is out of range");
    }
    return result;
}

[[nodiscard]] std::uint32_t AsU32(const Json& value, const std::string_view where) {
    return ParseIntegerText<std::uint32_t>(AsNumber(value, where).text, where, false);
}

[[nodiscard]] std::uint64_t AsU64String(const Json& value, const std::string_view where, const bool nonzero = true) {
    const std::uint64_t result = ParseIntegerText<std::uint64_t>(AsString(value, where), where, false);
    if (nonzero && result == 0)
        Fail(ErrorCode::InvalidFormat, std::string(where) + " cannot be zero");
    return result;
}

[[nodiscard]] double AsDouble(const Json& value, const std::string_view where) {
    const auto& text = AsNumber(value, where).text;
    double result = 0.0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || !std::isfinite(result)) {
        Fail(ErrorCode::InvalidFormat, std::string(where) + " is not finite");
    }
    return result;
}

[[nodiscard]] float AsFloat(const Json& value, const std::string_view where) {
    const double number = AsDouble(value, where);
    if (number < -std::numeric_limits<float>::max() || number > std::numeric_limits<float>::max()) {
        Fail(ErrorCode::InvalidFormat, std::string(where) + " is outside float range");
    }
    const float result = static_cast<float>(number);
    if (!std::isfinite(result))
        Fail(ErrorCode::InvalidFormat, std::string(where) + " is outside float range");
    return result;
}

template<typename Id>
[[nodiscard]] Id DecodeId(const Json& value, const std::string_view where) {
    return Id{AsU64String(value, where)};
}

template<typename Id>
[[nodiscard]] Json EncodeId(const Id value) {
    if (!value)
        Fail(ErrorCode::InvalidArgument, "Cannot serialize a zero strong ID");
    return String(std::to_string(value.Value()));
}

[[nodiscard]] Json EncodeVec2(const Vec2 value) { return Array(Json::Array{Number(value.x), Number(value.y)}); }

[[nodiscard]] Vec2 DecodeVec2(const Json& value, const std::string_view where) {
    const auto& array = AsArray(value, where);
    if (array.size() != 2)
        Fail(ErrorCode::InvalidFormat, std::string(where) + " must have two values");
    return {AsFloat(array[0], where), AsFloat(array[1], where)};
}

[[nodiscard]] Json EncodeColor(const std::optional<std::uint32_t> color) { return color ? Number(*color) : Null(); }

[[nodiscard]] std::optional<std::uint32_t> DecodeColor(const Json& value, const std::string_view where) {
    return std::holds_alternative<std::nullptr_t>(value.value) ? std::nullopt
                                                               : std::optional<std::uint32_t>{AsU32(value, where)};
}

[[nodiscard]] std::string_view DirectionName(const PinDirection value) {
    switch (value) {
    case PinDirection::Input:
        return "input";
    case PinDirection::Output:
        return "output";
    }
    Fail(ErrorCode::InvalidArgument, "Pin direction is invalid");
}

[[nodiscard]] PinDirection DecodeDirection(const Json& value) {
    const auto& text = AsString(value, "pin.direction");
    if (text == "input")
        return PinDirection::Input;
    if (text == "output")
        return PinDirection::Output;
    Fail(ErrorCode::InvalidFormat, "pin.direction is invalid");
}

[[nodiscard]] std::string_view KindName(const PinKind value) {
    switch (value) {
    case PinKind::Data:
        return "data";
    case PinKind::Execution:
        return "execution";
    }
    Fail(ErrorCode::InvalidArgument, "Pin kind is invalid");
}

[[nodiscard]] PinKind DecodePinKind(const Json& value) {
    const auto& text = AsString(value, "pin.kind");
    if (text == "data")
        return PinKind::Data;
    if (text == "execution")
        return PinKind::Execution;
    Fail(ErrorCode::InvalidFormat, "pin.kind is invalid");
}

[[nodiscard]] std::string_view CardinalityName(const PinCardinality value) {
    switch (value) {
    case PinCardinality::Single:
        return "single";
    case PinCardinality::Multiple:
        return "multiple";
    }
    Fail(ErrorCode::InvalidArgument, "Pin cardinality is invalid");
}

[[nodiscard]] PinCardinality DecodeCardinality(const Json& value) {
    const auto& text = AsString(value, "pin.cardinality");
    if (text == "single")
        return PinCardinality::Single;
    if (text == "multiple")
        return PinCardinality::Multiple;
    Fail(ErrorCode::InvalidFormat, "pin.cardinality is invalid");
}

[[nodiscard]] std::string_view StorageName(const PinStorage value) {
    switch (value) {
    case PinStorage::Static:
        return "static";
    case PinStorage::Dynamic:
        return "dynamic";
    }
    Fail(ErrorCode::InvalidArgument, "Pin storage is invalid");
}

[[nodiscard]] PinStorage DecodeStorage(const Json& value) {
    const auto& text = AsString(value, "pin.storage");
    if (text == "static")
        return PinStorage::Static;
    if (text == "dynamic")
        return PinStorage::Dynamic;
    Fail(ErrorCode::InvalidFormat, "pin.storage is invalid");
}

[[nodiscard]] std::string_view NodeRoleName(const NodeRole value) {
    switch (value) {
    case NodeRole::Regular: return "regular";
    case NodeRole::Subgraph: return "subgraph";
    case NodeRole::BoundaryInput: return "boundary_input";
    case NodeRole::BoundaryOutput: return "boundary_output";
    case NodeRole::IntergraphInput: return "intergraph_input";
    case NodeRole::IntergraphOutput: return "intergraph_output";
    }
    Fail(ErrorCode::InvalidArgument, "Node role is invalid");
}

[[nodiscard]] NodeRole DecodeNodeRole(const Json& value) {
    const auto& text = AsString(value, "node.role");
    if (text == "regular") return NodeRole::Regular;
    if (text == "subgraph") return NodeRole::Subgraph;
    if (text == "boundary_input") return NodeRole::BoundaryInput;
    if (text == "boundary_output") return NodeRole::BoundaryOutput;
    if (text == "intergraph_input") return NodeRole::IntergraphInput;
    if (text == "intergraph_output") return NodeRole::IntergraphOutput;
    Fail(ErrorCode::InvalidFormat, "node.role is invalid");
}

[[nodiscard]] std::string_view LifetimeName(const GraphLifetime value) {
    switch (value) {
    case GraphLifetime::Reusable: return "reusable";
    case GraphLifetime::Owned: return "owned";
    }
    Fail(ErrorCode::InvalidArgument, "Graph lifetime is invalid");
}

[[nodiscard]] GraphLifetime DecodeLifetime(const Json& value) {
    const auto& text = AsString(value, "graph.lifetime");
    if (text == "reusable") return GraphLifetime::Reusable;
    if (text == "owned") return GraphLifetime::Owned;
    Fail(ErrorCode::InvalidFormat, "graph.lifetime is invalid");
}

[[nodiscard]] Json EncodeGraphInterface(const GraphInterface& interface) {
    Json::Array pins;
    pins.reserve(interface.pins.size());
    for (const auto& pin : interface.pins) {
        Json::Object object;
        object.emplace("boundary_cardinality", String(std::string(CardinalityName(pin.boundary_cardinality))));
        object.emplace("caller_cardinality", String(std::string(CardinalityName(pin.caller_cardinality))));
        object.emplace("direction", String(std::string(DirectionName(pin.direction))));
        object.emplace("key", String(pin.key));
        object.emplace("kind", String(std::string(KindName(pin.kind))));
        object.emplace("label", String(pin.label));
        object.emplace("type", String(pin.type.Value()));
        pins.push_back(Object(std::move(object)));
    }
    Json::Object object;
    object.emplace("pins", Array(std::move(pins)));
    object.emplace("version", Number(interface.version));
    return Object(std::move(object));
}

[[nodiscard]] GraphInterface DecodeGraphInterface(const Json& value) {
    const auto& object = AsObject(value, "graph interface");
    ExpectKeys(object, {"pins", "version"}, "graph interface");
    GraphInterface interface{.version = AsU32(Field(object, "version", "graph interface"), "graph interface.version")};
    for (const auto& encoded : AsArray(Field(object, "pins", "graph interface"), "graph interface.pins")) {
        const auto& pin = AsObject(encoded, "graph interface pin");
        ExpectKeys(
            pin,
            {"boundary_cardinality", "caller_cardinality", "direction", "key", "kind", "label", "type"},
            "graph interface pin");
        interface.pins.push_back(GraphInterfacePin{
            .key = AsString(Field(pin, "key", "graph interface pin"), "graph interface pin.key"),
            .label = AsString(Field(pin, "label", "graph interface pin"), "graph interface pin.label"),
            .type = TypeId{AsString(Field(pin, "type", "graph interface pin"), "graph interface pin.type")},
            .direction = DecodeDirection(Field(pin, "direction", "graph interface pin")),
            .kind = DecodePinKind(Field(pin, "kind", "graph interface pin")),
            .caller_cardinality = DecodeCardinality(Field(pin, "caller_cardinality", "graph interface pin")),
            .boundary_cardinality = DecodeCardinality(Field(pin, "boundary_cardinality", "graph interface pin")),
        });
    }
    return interface;
}

[[nodiscard]] Json EncodeSubgraph(const std::optional<SubgraphReference>& reference) {
    if (!reference) return Null();
    Json::Object target;
    if (const auto* local = std::get_if<DocumentGraphTarget>(&reference->target)) {
        target.emplace("graph", EncodeId(local->graph));
        target.emplace("kind", String("document_graph"));
    } else {
        const auto& asset = std::get<GraphAssetTarget>(reference->target);
        target.emplace("asset", String(asset.asset.Value()));
        target.emplace("interface", EncodeGraphInterface(asset.interface));
        target.emplace("kind", String("graph_asset"));
    }
    Json::Object object;
    object.emplace("ownership", String(
        reference->ownership == SubgraphOwnership::Owned ? "owned" : "referenced"));
    object.emplace("target", Object(std::move(target)));
    return Object(std::move(object));
}

[[nodiscard]] std::optional<SubgraphReference> DecodeSubgraph(const Json& value) {
    if (std::holds_alternative<std::nullptr_t>(value.value)) return std::nullopt;
    const auto& object = AsObject(value, "node.subgraph");
    ExpectKeys(object, {"ownership", "target"}, "node.subgraph");
    const auto& ownership = AsString(Field(object, "ownership", "node.subgraph"), "node.subgraph.ownership");
    SubgraphReference reference;
    if (ownership == "owned") reference.ownership = SubgraphOwnership::Owned;
    else if (ownership == "referenced") reference.ownership = SubgraphOwnership::Referenced;
    else Fail(ErrorCode::InvalidFormat, "node.subgraph.ownership is invalid");
    const auto& target = AsObject(Field(object, "target", "node.subgraph"), "node.subgraph.target");
    const auto& kind = AsString(Field(target, "kind", "node.subgraph.target"), "node.subgraph.target.kind");
    if (kind == "document_graph") {
        ExpectKeys(target, {"graph", "kind"}, "node.subgraph.target");
        reference.target = DocumentGraphTarget{
            DecodeId<GraphId>(Field(target, "graph", "node.subgraph.target"), "node.subgraph.target.graph")};
    } else if (kind == "graph_asset") {
        ExpectKeys(target, {"asset", "interface", "kind"}, "node.subgraph.target");
        reference.target = GraphAssetTarget{
            .asset = GraphAssetId{AsString(Field(target, "asset", "node.subgraph.target"), "node.subgraph.target.asset")},
            .interface = DecodeGraphInterface(Field(target, "interface", "node.subgraph.target")),
        };
    } else {
        Fail(ErrorCode::InvalidFormat, "node.subgraph.target.kind is invalid");
    }
    return reference;
}

struct DecodeCounts final {
    const GraphIoLimits& limits;
    std::size_t graphs{0};
    std::size_t nodes{0};
    std::size_t pins{0};
    std::size_t links{0};
    std::size_t groups{0};
    std::size_t route_points{0};
    std::size_t properties{0};
};

void Bump(std::size_t& value, const std::size_t limit, const std::string_view name) {
    if (value == limit)
        Fail(ErrorCode::SizeLimitExceeded, std::string(name) + " exceeds its configured limit");
    ++value;
}

[[nodiscard]] bool KnownPropertyKind(const std::string_view kind) noexcept {
    return kind == "bool" || kind == "int64" || kind == "float64" || kind == "string" || kind == "vec2" ||
           kind == "asset";
}

[[nodiscard]] GraphIoLimits OpaqueLimits(const std::string_view json) noexcept {
    GraphIoLimits limits;
    limits.max_bytes = json.size();
    limits.max_string_bytes = json.size();
    limits.max_json_values = json.size() == std::numeric_limits<std::size_t>::max()
        ? json.size()
        : json.size() + 1;
    limits.max_depth = std::min<std::size_t>(
        4096,
        std::max<std::size_t>(128, limits.max_json_values));
    return limits;
}

[[nodiscard]] Json EncodeProperty(const PropertyValue& property) {
    return std::visit(
        [](const auto& value) -> Json {
            using Value = std::remove_cvref_t<decltype(value)>;
            Json::Object object;
            if constexpr (std::same_as<Value, bool>) {
                object.emplace("kind", String("bool"));
                object.emplace("value", Bool(value));
            } else if constexpr (std::same_as<Value, std::int64_t>) {
                object.emplace("kind", String("int64"));
                object.emplace("value", String(std::to_string(value)));
            } else if constexpr (std::same_as<Value, double>) {
                object.emplace("kind", String("float64"));
                object.emplace("value", Number(value));
            } else if constexpr (std::same_as<Value, std::string>) {
                object.emplace("kind", String("string"));
                object.emplace("value", String(value));
            } else if constexpr (std::same_as<Value, Vec2>) {
                object.emplace("kind", String("vec2"));
                object.emplace("value", EncodeVec2(value));
            } else if constexpr (std::same_as<Value, AssetReference>) {
                object.emplace("kind", String("asset"));
                object.emplace("value", String(std::to_string(value.id)));
            } else {
                Json parsed = ParseJson(value.canonical_json, OpaqueLimits(value.canonical_json));
                const auto& opaque = AsObject(parsed, "opaque property");
                const auto& kind = AsString(Field(opaque, "kind", "opaque property"), "opaque property.kind");
                if (KnownPropertyKind(kind)) {
                    Fail(ErrorCode::InvalidArgument, "Opaque property uses a built-in kind");
                }
                return parsed;
            }
            return Object(std::move(object));
        },
        property);
}

[[nodiscard]] PropertyValue DecodeProperty(const Json& value) {
    const auto& object = AsObject(value, "property");
    const auto& kind = AsString(Field(object, "kind", "property"), "property.kind");
    if (!KnownPropertyKind(kind)) {
        std::string canonical = DumpJson(value);
        canonical.pop_back();
        return OpaqueJsonProperty{std::move(canonical)};
    }
    ExpectKeys(object, {"kind", "value"}, "property");
    const Json& encoded = Field(object, "value", "property");
    if (kind == "bool")
        return AsBool(encoded, "property.value");
    if (kind == "int64") {
        return ParseIntegerText<std::int64_t>(AsString(encoded, "property.value"), "property.value", true);
    }
    if (kind == "float64")
        return AsDouble(encoded, "property.value");
    if (kind == "string")
        return AsString(encoded, "property.value");
    if (kind == "vec2")
        return DecodeVec2(encoded, "property.value");
    return AssetReference{AsU64String(encoded, "property.value", false)};
}

[[nodiscard]] Json EncodeProperties(const PropertyBag& properties) {
    Json::Object object;
    for (const auto& [key, value] : properties) {
        if (key.empty() || !ValidUtf8(key))
            Fail(ErrorCode::InvalidArgument, "Property key is invalid");
        object.emplace(key, EncodeProperty(value));
    }
    return Object(std::move(object));
}

[[nodiscard]] PropertyBag DecodeProperties(const Json& value, DecodeCounts& counts) {
    PropertyBag properties;
    for (const auto& [key, property] : AsObject(value, "node.properties")) {
        if (key.empty())
            Fail(ErrorCode::InvalidFormat, "Property key cannot be empty");
        Bump(counts.properties, counts.limits.max_properties, "Property count");
        properties.emplace(key, DecodeProperty(property));
    }
    return properties;
}

[[nodiscard]] Json EncodePin(const PinInstance& pin) {
    Json::Object object;
    object.emplace("cardinality", String(std::string(CardinalityName(pin.cardinality))));
    object.emplace("direction", String(std::string(DirectionName(pin.direction))));
    object.emplace("id", EncodeId(pin.id));
    object.emplace("key", String(pin.key));
    object.emplace("kind", String(std::string(KindName(pin.kind))));
    object.emplace("label", String(pin.label));
    object.emplace("node", EncodeId(pin.node));
    object.emplace("read_only", Bool(pin.read_only));
    object.emplace("storage", String(std::string(StorageName(pin.storage))));
    object.emplace("type", String(pin.type.Value()));
    return Object(std::move(object));
}

[[nodiscard]] PinInstance DecodePin(const Json& value, DecodeCounts& counts) {
    Bump(counts.pins, counts.limits.max_pins, "Pin count");
    const auto& object = AsObject(value, "pin");
    ExpectKeys(object,
               {"cardinality", "direction", "id", "key", "kind", "label", "node", "read_only", "storage", "type"},
               "pin");
    return PinInstance{
        .id = DecodeId<PinId>(Field(object, "id", "pin"), "pin.id"),
        .node = DecodeId<NodeId>(Field(object, "node", "pin"), "pin.node"),
        .key = AsString(Field(object, "key", "pin"), "pin.key"),
        .label = AsString(Field(object, "label", "pin"), "pin.label"),
        .type = TypeId{AsString(Field(object, "type", "pin"), "pin.type")},
        .direction = DecodeDirection(Field(object, "direction", "pin")),
        .kind = DecodePinKind(Field(object, "kind", "pin")),
        .cardinality = DecodeCardinality(Field(object, "cardinality", "pin")),
        .storage = DecodeStorage(Field(object, "storage", "pin")),
        .read_only = AsBool(Field(object, "read_only", "pin"), "pin.read_only"),
    };
}

[[nodiscard]] Json EncodeNodeCreation(const NodeCreation& creation) {
    Json::Array pins;
    pins.reserve(creation.node.pins.size());
    for (const PinId id : creation.node.pins) {
        const auto found = std::ranges::find(creation.pins, id, &PinInstance::id);
        if (found == creation.pins.end())
            Fail(ErrorCode::InvalidGraph, "Node references a missing pin");
        pins.push_back(EncodePin(*found));
    }
    Json::Object object;
    object.emplace("display_name", String(creation.node.display_name));
    object.emplace("id", EncodeId(creation.node.id));
    object.emplace("pins", Array(std::move(pins)));
    object.emplace("properties", EncodeProperties(creation.node.properties));
    object.emplace("read_only", Bool(creation.node.read_only));
    object.emplace("role", String(std::string(NodeRoleName(creation.node.role))));
    object.emplace("subgraph", EncodeSubgraph(creation.node.subgraph));
    object.emplace("type", String(creation.node.type.Value()));
    object.emplace("type_version", Number(creation.node.type_version));
    return Object(std::move(object));
}

[[nodiscard]] NodeCreation DecodeNodeCreation(const Json& value, DecodeCounts& counts) {
    Bump(counts.nodes, counts.limits.max_nodes, "Node count");
    const auto& object = AsObject(value, "node");
    ExpectKeys(object,
               {"display_name", "id", "pins", "properties", "read_only", "role", "subgraph", "type", "type_version"},
               "node");
    NodeCreation creation;
    creation.node.id = DecodeId<NodeId>(Field(object, "id", "node"), "node.id");
    creation.node.type = TypeId{AsString(Field(object, "type", "node"), "node.type")};
    creation.node.type_version = AsU32(Field(object, "type_version", "node"), "node.type_version");
    creation.node.display_name = AsString(Field(object, "display_name", "node"), "node.display_name");
    creation.node.properties = DecodeProperties(Field(object, "properties", "node"), counts);
    creation.node.subgraph = DecodeSubgraph(Field(object, "subgraph", "node"));
    creation.node.read_only = AsBool(Field(object, "read_only", "node"), "node.read_only");
    creation.node.role = DecodeNodeRole(Field(object, "role", "node"));
    for (const Json& pin_value : AsArray(Field(object, "pins", "node"), "node.pins")) {
        PinInstance pin = DecodePin(pin_value, counts);
        creation.node.pins.push_back(pin.id);
        creation.pins.push_back(std::move(pin));
    }
    return creation;
}

[[nodiscard]] Json EncodeLink(const Link& link) {
    Json::Object object;
    object.emplace("id", EncodeId(link.id));
    object.emplace("input", EncodeId(link.input));
    object.emplace("output", EncodeId(link.output));
    object.emplace("read_only", Bool(link.read_only));
    return Object(std::move(object));
}

[[nodiscard]] Link DecodeLink(const Json& value, DecodeCounts& counts) {
    Bump(counts.links, counts.limits.max_links, "Link count");
    const auto& object = AsObject(value, "link");
    ExpectKeys(object, {"id", "input", "output", "read_only"}, "link");
    return Link{
        .id = DecodeId<LinkId>(Field(object, "id", "link"), "link.id"),
        .output = DecodeId<PinId>(Field(object, "output", "link"), "link.output"),
        .input = DecodeId<PinId>(Field(object, "input", "link"), "link.input"),
        .read_only = AsBool(Field(object, "read_only", "link"), "link.read_only"),
    };
}

[[nodiscard]] Json EncodeIntergraphEndpoint(const IntergraphEndpoint& endpoint) {
    Json::Object object;
    object.emplace("graph", EncodeId(endpoint.graph));
    object.emplace("node", EncodeId(endpoint.node));
    object.emplace("pin", EncodeId(endpoint.pin));
    return Object(std::move(object));
}

[[nodiscard]] IntergraphEndpoint DecodeIntergraphEndpoint(const Json& value) {
    const auto& object = AsObject(value, "intergraph endpoint");
    ExpectKeys(object, {"graph", "node", "pin"}, "intergraph endpoint");
    return {
        .graph = DecodeId<GraphId>(Field(object, "graph", "intergraph endpoint"), "intergraph endpoint.graph"),
        .node = DecodeId<NodeId>(Field(object, "node", "intergraph endpoint"), "intergraph endpoint.node"),
        .pin = DecodeId<PinId>(Field(object, "pin", "intergraph endpoint"), "intergraph endpoint.pin"),
    };
}

[[nodiscard]] Json EncodeIntergraphLink(const IntergraphLink& link) {
    Json::Object object;
    object.emplace("destination", EncodeIntergraphEndpoint(link.destination));
    object.emplace("id", EncodeId(link.id));
    object.emplace("read_only", Bool(link.read_only));
    object.emplace("source", EncodeIntergraphEndpoint(link.source));
    return Object(std::move(object));
}

[[nodiscard]] IntergraphLink DecodeIntergraphLink(const Json& value) {
    const auto& object = AsObject(value, "intergraph link");
    ExpectKeys(object, {"destination", "id", "read_only", "source"}, "intergraph link");
    return {
        .id = DecodeId<IntergraphLinkId>(Field(object, "id", "intergraph link"), "intergraph link.id"),
        .source = DecodeIntergraphEndpoint(Field(object, "source", "intergraph link")),
        .destination = DecodeIntergraphEndpoint(Field(object, "destination", "intergraph link")),
        .read_only = AsBool(Field(object, "read_only", "intergraph link"), "intergraph link.read_only"),
    };
}

[[nodiscard]] Json EncodeNodePresentation(const NodePresentation& value) {
    Json::Object object;
    object.emplace("collapsed", Bool(value.collapsed));
    object.emplace("color", EncodeColor(value.color));
    object.emplace("locked", Bool(value.locked));
    object.emplace("position", EncodeVec2(value.position));
    object.emplace("size", EncodeVec2(value.size));
    object.emplace("z_order", String(std::to_string(value.z_order)));
    return Object(std::move(object));
}

[[nodiscard]] NodePresentation DecodeNodePresentation(const Json& value) {
    const auto& object = AsObject(value, "node presentation");
    ExpectKeys(object, {"collapsed", "color", "locked", "position", "size", "z_order"}, "node presentation");
    return NodePresentation{
        .position = DecodeVec2(Field(object, "position", "node presentation"), "node presentation.position"),
        .size = DecodeVec2(Field(object, "size", "node presentation"), "node presentation.size"),
        .collapsed = AsBool(Field(object, "collapsed", "node presentation"), "node presentation.collapsed"),
        .z_order = AsU64String(Field(object, "z_order", "node presentation"), "node presentation.z_order", false),
        .color = DecodeColor(Field(object, "color", "node presentation"), "node presentation.color"),
        .locked = AsBool(Field(object, "locked", "node presentation"), "node presentation.locked"),
    };
}

[[nodiscard]] Json EncodeLinkPresentation(const LinkPresentation& value) {
    Json::Array points;
    points.reserve(value.Route().size());
    for (const auto& point : value.Route()) {
        Json::Object object;
        object.emplace("id", EncodeId(point.id));
        object.emplace("position", EncodeVec2(point.position));
        points.push_back(Object(std::move(object)));
    }
    Json::Object object;
    object.emplace("color", EncodeColor(value.Style().color));
    object.emplace("locked", Bool(value.Style().locked));
    object.emplace("route_points", Array(std::move(points)));
    object.emplace("router", String(value.Style().router.Value()));
    return Object(std::move(object));
}

[[nodiscard]] LinkPresentation DecodeLinkPresentation(const Json& value, DecodeCounts& counts) {
    const auto& object = AsObject(value, "link presentation");
    ExpectKeys(object, {"color", "locked", "route_points", "router"}, "link presentation");
    LinkStyle style{
        .router = TypeId{AsString(Field(object, "router", "link presentation"), "link presentation.router")},
        .color = DecodeColor(Field(object, "color", "link presentation"), "link presentation.color"),
        .locked = AsBool(Field(object, "locked", "link presentation"), "link presentation.locked"),
    };
    std::vector<RoutePoint> route;
    for (const Json& encoded :
         AsArray(Field(object, "route_points", "link presentation"), "link presentation.route_points")) {
        Bump(counts.route_points, counts.limits.max_route_points, "Route point count");
        const auto& point = AsObject(encoded, "route point");
        ExpectKeys(point, {"id", "position"}, "route point");
        route.push_back(RoutePoint{
            .id = DecodeId<RoutePointId>(Field(point, "id", "route point"), "route point.id"),
            .position = DecodeVec2(Field(point, "position", "route point"), "route point.position"),
        });
    }
    return LinkPresentation{std::move(style), PersistentRoutePointSequence{std::move(route)}};
}

[[nodiscard]] std::string_view GroupKindName(const GroupKind value) {
    switch (value) {
    case GroupKind::Group:
        return "group";
    case GroupKind::Comment:
        return "comment";
    }
    Fail(ErrorCode::InvalidArgument, "Group kind is invalid");
}

[[nodiscard]] GroupKind DecodeGroupKind(const Json& value) {
    const auto& text = AsString(value, "group.kind");
    if (text == "group")
        return GroupKind::Group;
    if (text == "comment")
        return GroupKind::Comment;
    Fail(ErrorCode::InvalidFormat, "group.kind is invalid");
}

[[nodiscard]] Json EncodeGroup(const GroupPresentation& group) {
    if (!group.style) Fail(ErrorCode::InvalidGraph, "Group style is null");
    Json::Array members;
    members.reserve(group.members.size());
    for (const NodeId member : group.members)
        members.push_back(EncodeId(member));
    Json::Object object;
    object.emplace("body", String(group.style->body));
    object.emplace("collapsed", Bool(group.geometry.collapsed));
    object.emplace("color", Number(group.style->color));
    object.emplace("graph", EncodeId(group.graph));
    object.emplace("id", EncodeId(group.id));
    object.emplace("kind", String(std::string(GroupKindName(group.style->kind))));
    object.emplace("locked", Bool(group.protection.locked));
    object.emplace("members", Array(std::move(members)));
    object.emplace("position", EncodeVec2(group.geometry.position));
    object.emplace("size", EncodeVec2(group.geometry.size));
    object.emplace("title", String(group.style->title));
    object.emplace("z_order", String(std::to_string(group.geometry.z_order)));
    return Object(std::move(object));
}

[[nodiscard]] GroupPresentation DecodeGroup(const Json& value, DecodeCounts& counts) {
    Bump(counts.groups, counts.limits.max_groups, "Group count");
    const auto& object = AsObject(value, "group");
    ExpectKeys(object,
               {"body", "collapsed", "color", "graph", "id", "kind", "locked", "members", "position", "size", "title", "z_order"},
               "group");
    GroupPresentation group{
        .id = DecodeId<GroupId>(Field(object, "id", "group"), "group.id"),
        .graph = DecodeId<GraphId>(Field(object, "graph", "group"), "group.graph"),
        .geometry = GroupGeometry{
            .position = DecodeVec2(Field(object, "position", "group"), "group.position"),
            .size = DecodeVec2(Field(object, "size", "group"), "group.size"),
            .collapsed = AsBool(Field(object, "collapsed", "group"), "group.collapsed"),
            .z_order = AsU64String(Field(object, "z_order", "group"), "group.z_order", false),
        },
        .style = MakeGroupStyle(GroupStyle{
            .title = AsString(Field(object, "title", "group"), "group.title"),
            .body = AsString(Field(object, "body", "group"), "group.body"),
            .color = AsU32(Field(object, "color", "group"), "group.color"),
            .kind = DecodeGroupKind(Field(object, "kind", "group")),
        }),
        .protection = GroupProtection{
            .locked = AsBool(Field(object, "locked", "group"), "group.locked"),
        },
    };
    for (const Json& member : AsArray(Field(object, "members", "group"), "group.members")) {
        if (!group.members.Insert(DecodeId<NodeId>(member, "group.member"))) {
            Fail(ErrorCode::DuplicateId, "Group contains a duplicate member");
        }
    }
    return group;
}

template<typename Map>
[[nodiscard]] std::vector<typename Map::key_type> SortedIds(const Map& values) {
    std::vector<typename Map::key_type> ids;
    ids.reserve(values.size());
    for (const auto& [id, value] : values) {
        (void)value;
        ids.push_back(id);
    }
    std::ranges::sort(ids, [](const auto first, const auto second) { return first.Value() < second.Value(); });
    return ids;
}

[[nodiscard]] Json EncodeGraph(const Graph& graph) {
    Json::Array nodes;
    for (const NodeId node_id : SortedIds(graph.nodes)) {
        const NodeInstance& node = graph.nodes.at(node_id);
        NodeCreation creation{.node = node};
        creation.pins.reserve(node.pins.size());
        for (const PinId pin_id : node.pins) {
            const auto pin = graph.pins.find(pin_id);
            if (pin == graph.pins.end())
                Fail(ErrorCode::InvalidGraph, "Node references a missing pin");
            creation.pins.push_back(pin->second);
        }
        nodes.push_back(EncodeNodeCreation(creation));
    }
    Json::Array links;
    for (const LinkId link : SortedIds(graph.links))
        links.push_back(EncodeLink(graph.links.at(link)));
    Json::Object object;
    object.emplace("display_name", String(graph.display_name));
    object.emplace("id", EncodeId(graph.id));
    object.emplace("interface", EncodeGraphInterface(graph.interface));
    object.emplace("lifetime", String(std::string(LifetimeName(graph.lifetime))));
    object.emplace("links", Array(std::move(links)));
    object.emplace("nodes", Array(std::move(nodes)));
    object.emplace("read_only", Bool(graph.read_only));
    return Object(std::move(object));
}

[[nodiscard]] Graph DecodeGraph(const Json& value, DecodeCounts& counts) {
    Bump(counts.graphs, counts.limits.max_graphs, "Graph count");
    const auto& object = AsObject(value, "graph");
    ExpectKeys(object, {"display_name", "id", "interface", "lifetime", "links", "nodes", "read_only"}, "graph");
    Graph graph{
        .id = DecodeId<GraphId>(Field(object, "id", "graph"), "graph.id"),
        .display_name = AsString(Field(object, "display_name", "graph"), "graph.display_name"),
        .lifetime = DecodeLifetime(Field(object, "lifetime", "graph")),
        .interface = DecodeGraphInterface(Field(object, "interface", "graph")),
        .read_only = AsBool(Field(object, "read_only", "graph"), "graph.read_only"),
    };
    for (const Json& node_value : AsArray(Field(object, "nodes", "graph"), "graph.nodes")) {
        NodeCreation creation = DecodeNodeCreation(node_value, counts);
        for (auto& pin : creation.pins) {
            if (!graph.pins.emplace(pin.id, std::move(pin)).second) {
                Fail(ErrorCode::DuplicateId, "Graph contains duplicate pin IDs");
            }
        }
        if (!graph.nodes.emplace(creation.node.id, std::move(creation.node)).second) {
            Fail(ErrorCode::DuplicateId, "Graph contains duplicate node IDs");
        }
    }
    for (const Json& link_value : AsArray(Field(object, "links", "graph"), "graph.links")) {
        Link link = DecodeLink(link_value, counts);
        if (!graph.links.emplace(link.id, std::move(link)).second) {
            Fail(ErrorCode::DuplicateId, "Graph contains duplicate link IDs");
        }
    }
    return graph;
}

[[nodiscard]] Json EncodePresentation(const GraphPresentation& presentation) {
    Json::Array nodes;
    for (const NodeId id : SortedIds(presentation.Nodes())) {
        Json::Object item;
        item.emplace("id", EncodeId(id));
        item.emplace("value", EncodeNodePresentation(presentation.Nodes().at(id)));
        nodes.push_back(Object(std::move(item)));
    }
    Json::Array links;
    for (const LinkId id : SortedIds(presentation.Links())) {
        Json::Object item;
        item.emplace("id", EncodeId(id));
        item.emplace("value", EncodeLinkPresentation(presentation.Links().at(id)));
        links.push_back(Object(std::move(item)));
    }
    Json::Array groups;
    for (const GroupId id : SortedIds(presentation.Groups()))
        groups.push_back(EncodeGroup(presentation.Groups().at(id)));
    Json::Object object;
    object.emplace("groups", Array(std::move(groups)));
    object.emplace("links", Array(std::move(links)));
    object.emplace("nodes", Array(std::move(nodes)));
    return Object(std::move(object));
}

void DecodePresentation(const Json& value, DecodeCounts& counts, GraphArchive& archive) {
    const auto& object = AsObject(value, "presentation");
    ExpectKeys(object, {"groups", "links", "nodes"}, "presentation");
    const auto& node_values = AsArray(Field(object, "nodes", "presentation"), "presentation.nodes");
    const auto& link_values = AsArray(Field(object, "links", "presentation"), "presentation.links");
    if (node_values.size() > counts.limits.max_nodes || link_values.size() > counts.limits.max_links) {
        Fail(ErrorCode::SizeLimitExceeded, "Presentation entry count exceeds its configured limit");
    }
    for (const Json& encoded : node_values) {
        const auto& item = AsObject(encoded, "node presentation entry");
        ExpectKeys(item, {"id", "value"}, "node presentation entry");
        const NodeId id = DecodeId<NodeId>(Field(item, "id", "node presentation entry"), "node presentation entry.id");
        if (!archive.nodes.emplace(id, DecodeNodePresentation(Field(item, "value", "node presentation entry")))
                 .second) {
            Fail(ErrorCode::DuplicateId, "Presentation contains duplicate node IDs");
        }
    }
    for (const Json& encoded : link_values) {
        const auto& item = AsObject(encoded, "link presentation entry");
        ExpectKeys(item, {"id", "value"}, "link presentation entry");
        const LinkId id = DecodeId<LinkId>(Field(item, "id", "link presentation entry"), "link presentation entry.id");
        if (!archive.links.emplace(id, DecodeLinkPresentation(Field(item, "value", "link presentation entry"), counts))
                 .second) {
            Fail(ErrorCode::DuplicateId, "Presentation contains duplicate link IDs");
        }
    }
    for (const Json& encoded : AsArray(Field(object, "groups", "presentation"), "presentation.groups")) {
        GroupPresentation group = DecodeGroup(encoded, counts);
        if (!archive.groups.emplace(group.id, std::move(group)).second) {
            Fail(ErrorCode::DuplicateId, "Presentation contains duplicate group IDs");
        }
    }
}

[[nodiscard]] Json EncodeDocumentEnvelope(const GraphDocument& document, const GraphPresentation& presentation) {
    auto graphs = document.Graphs();
    std::ranges::sort(graphs, {}, [](const auto& value) { return value.get().id.Value(); });
    Json::Array encoded_graphs;
    encoded_graphs.reserve(graphs.size());
    for (const auto& graph : graphs)
        encoded_graphs.push_back(EncodeGraph(graph.get()));
    Json::Array intergraph_links;
    for (const IntergraphLinkId id : SortedIds(document.IntergraphLinks())) {
        intergraph_links.push_back(EncodeIntergraphLink(document.IntergraphLinks().at(id)));
    }

    Json::Object payload;
    payload.emplace("graphs", Array(std::move(encoded_graphs)));
    payload.emplace("intergraph_links", Array(std::move(intergraph_links)));
    payload.emplace("presentation", EncodePresentation(presentation));
    payload.emplace("root_graph", EncodeId(document.RootGraph()));

    Json::Object envelope;
    envelope.emplace("format", String(std::string(FormatName)));
    envelope.emplace("format_version", Number(GraphJsonFormatVersion));
    envelope.emplace("kind", String("document"));
    envelope.emplace("payload", Object(std::move(payload)));
    envelope.emplace("schema_version", Number(document.SchemaVersion()));
    return Object(std::move(envelope));
}

void ValidateEnvelope(const Json::Object& envelope, const std::string_view kind, const bool document) {
    if (document)
        ExpectKeys(envelope, {"format", "format_version", "kind", "payload", "schema_version"}, "JSON envelope");
    else
        ExpectKeys(envelope, {"format", "format_version", "kind", "payload"}, "JSON envelope");
    if (AsString(Field(envelope, "format", "JSON envelope"), "JSON envelope.format") != FormatName) {
        Fail(ErrorCode::InvalidFormat, "JSON envelope format is not uni.gui.nodes");
    }
    if (AsU32(Field(envelope, "format_version", "JSON envelope"), "JSON envelope.format_version") !=
        GraphJsonFormatVersion) {
        Fail(ErrorCode::UnsupportedVersion, "JSON envelope version is not supported");
    }
    if (AsString(Field(envelope, "kind", "JSON envelope"), "JSON envelope.kind") != kind) {
        Fail(ErrorCode::InvalidFormat, "JSON envelope has the wrong kind");
    }
}

[[nodiscard]] GraphArchive DecodeDocumentArchive(const Json& root, DecodeCounts& counts) {
    const auto& envelope = AsObject(root, "JSON envelope");
    ValidateEnvelope(envelope, "document", true);
    GraphArchive archive;
    archive.schema_version = AsU32(Field(envelope, "schema_version", "JSON envelope"), "JSON envelope.schema_version");
    if (archive.schema_version == 0)
        Fail(ErrorCode::InvalidFormat, "Document schema version cannot be zero");
    const auto& payload = AsObject(Field(envelope, "payload", "JSON envelope"), "document payload");
    ExpectKeys(payload, {"graphs", "intergraph_links", "presentation", "root_graph"}, "document payload");
    archive.root_graph = DecodeId<GraphId>(Field(payload, "root_graph", "document payload"), "document root_graph");
    for (const Json& graph : AsArray(Field(payload, "graphs", "document payload"), "document graphs")) {
        archive.graphs.push_back(DecodeGraph(graph, counts));
    }
    for (const Json& link : AsArray(
             Field(payload, "intergraph_links", "document payload"),
             "document intergraph_links")) {
        Bump(counts.links, counts.limits.max_links, "Link count");
        archive.intergraph_links.push_back(DecodeIntergraphLink(link));
    }
    DecodePresentation(Field(payload, "presentation", "document payload"), counts, archive);
    return archive;
}

[[nodiscard]] Json EncodeGraphAssetEnvelope(const GraphAsset& asset) {
    auto graphs = asset.document.Graphs();
    std::ranges::sort(graphs, {}, [](const auto& value) { return value.get().id.Value(); });
    Json::Array encoded_graphs;
    for (const auto& graph : graphs) encoded_graphs.push_back(EncodeGraph(graph.get()));
    Json::Array intergraph_links;
    for (const IntergraphLinkId id : SortedIds(asset.document.IntergraphLinks())) {
        intergraph_links.push_back(EncodeIntergraphLink(asset.document.IntergraphLinks().at(id)));
    }
    Json::Object payload;
    payload.emplace("asset_id", String(asset.id.Value()));
    payload.emplace("graphs", Array(std::move(encoded_graphs)));
    payload.emplace("intergraph_links", Array(std::move(intergraph_links)));
    payload.emplace("presentation", EncodePresentation(asset.presentation));
    payload.emplace("root_graph", EncodeId(asset.document.RootGraph()));
    Json::Object envelope;
    envelope.emplace("format", String(std::string(FormatName)));
    envelope.emplace("format_version", Number(GraphJsonFormatVersion));
    envelope.emplace("kind", String("graph_asset"));
    envelope.emplace("payload", Object(std::move(payload)));
    envelope.emplace("schema_version", Number(asset.document.SchemaVersion()));
    return Object(std::move(envelope));
}

struct DecodedGraphAsset final {
    GraphAssetId id;
    GraphArchive archive;
};

[[nodiscard]] DecodedGraphAsset DecodeGraphAssetArchive(const Json& root, DecodeCounts& counts) {
    const auto& envelope = AsObject(root, "JSON envelope");
    ValidateEnvelope(envelope, "graph_asset", true);
    DecodedGraphAsset decoded;
    decoded.archive.schema_version = AsU32(
        Field(envelope, "schema_version", "JSON envelope"),
        "JSON envelope.schema_version");
    const auto& payload = AsObject(Field(envelope, "payload", "JSON envelope"), "graph asset payload");
    ExpectKeys(
        payload,
        {"asset_id", "graphs", "intergraph_links", "presentation", "root_graph"},
        "graph asset payload");
    decoded.id = GraphAssetId{
        AsString(Field(payload, "asset_id", "graph asset payload"), "graph asset payload.asset_id")};
    decoded.archive.root_graph = DecodeId<GraphId>(
        Field(payload, "root_graph", "graph asset payload"),
        "graph asset payload.root_graph");
    for (const Json& graph : AsArray(Field(payload, "graphs", "graph asset payload"), "graph asset graphs")) {
        decoded.archive.graphs.push_back(DecodeGraph(graph, counts));
    }
    for (const Json& link : AsArray(
             Field(payload, "intergraph_links", "graph asset payload"),
             "graph asset intergraph_links")) {
        Bump(counts.links, counts.limits.max_links, "Link count");
        decoded.archive.intergraph_links.push_back(DecodeIntergraphLink(link));
    }
    DecodePresentation(Field(payload, "presentation", "graph asset payload"), counts, decoded.archive);
    return decoded;
}

[[nodiscard]] Json EncodeFragmentEnvelope(const GraphFragment& fragment) {
    Json::Array nodes;
    auto ordered_nodes = fragment.nodes;
    std::ranges::sort(ordered_nodes, {}, [](const GraphFragmentNode& node) { return node.creation.node.id.Value(); });
    for (const auto& node : ordered_nodes) {
        Json::Object item;
        item.emplace("node", EncodeNodeCreation(node.creation));
        item.emplace("presentation", EncodeNodePresentation(node.presentation));
        nodes.push_back(Object(std::move(item)));
    }

    Json::Array links;
    auto ordered_links = fragment.links;
    std::ranges::sort(ordered_links, {}, [](const GraphFragmentLink& link) { return link.link.id.Value(); });
    for (const auto& link : ordered_links) {
        Json::Object item;
        item.emplace("link", EncodeLink(link.link));
        item.emplace("presentation", link.presentation ? EncodeLinkPresentation(*link.presentation) : Null());
        links.push_back(Object(std::move(item)));
    }

    Json::Array groups;
    auto ordered_groups = fragment.groups;
    std::ranges::sort(ordered_groups, {}, &GroupPresentation::id);
    for (const auto& group : ordered_groups)
        groups.push_back(EncodeGroup(group));

    Json::Array owned_graphs;
    auto ordered_owned = fragment.owned_graphs;
    std::ranges::sort(ordered_owned, {}, [](const GraphFragment::OwnedGraph& graph) {
        return graph.graph.id.Value();
    });
    for (const auto& owned : ordered_owned) {
        Json::Array node_states;
        for (const NodeId id : SortedIds(owned.nodes)) {
            Json::Object item;
            item.emplace("id", EncodeId(id));
            item.emplace("value", EncodeNodePresentation(owned.nodes.at(id)));
            node_states.push_back(Object(std::move(item)));
        }
        Json::Array link_states;
        for (const LinkId id : SortedIds(owned.links)) {
            Json::Object item;
            item.emplace("id", EncodeId(id));
            item.emplace("value", EncodeLinkPresentation(owned.links.at(id)));
            link_states.push_back(Object(std::move(item)));
        }
        auto owned_groups_sorted = owned.groups;
        std::ranges::sort(owned_groups_sorted, {}, &GroupPresentation::id);
        Json::Array owned_groups_encoded;
        for (const auto& group : owned_groups_sorted) owned_groups_encoded.push_back(EncodeGroup(group));
        Json::Object item;
        item.emplace("graph", EncodeGraph(owned.graph));
        item.emplace("groups", Array(std::move(owned_groups_encoded)));
        item.emplace("links", Array(std::move(link_states)));
        item.emplace("nodes", Array(std::move(node_states)));
        owned_graphs.push_back(Object(std::move(item)));
    }

    Json::Array intergraph_links;
    auto ordered_intergraph = fragment.intergraph_links;
    std::ranges::sort(ordered_intergraph, {}, [](const IntergraphLink& link) { return link.id.Value(); });
    for (const auto& link : ordered_intergraph) intergraph_links.push_back(EncodeIntergraphLink(link));

    Json::Object payload;
    payload.emplace("groups", Array(std::move(groups)));
    payload.emplace("intergraph_links", Array(std::move(intergraph_links)));
    payload.emplace("links", Array(std::move(links)));
    payload.emplace("nodes", Array(std::move(nodes)));
    payload.emplace("origin", EncodeVec2(fragment.origin));
    payload.emplace("owned_graphs", Array(std::move(owned_graphs)));

    Json::Object envelope;
    envelope.emplace("format", String(std::string(FormatName)));
    envelope.emplace("format_version", Number(GraphJsonFormatVersion));
    envelope.emplace("kind", String("fragment"));
    envelope.emplace("payload", Object(std::move(payload)));
    return Object(std::move(envelope));
}

[[nodiscard]] GraphFragment DecodeFragment(const Json& root, DecodeCounts& counts) {
    const auto& envelope = AsObject(root, "JSON envelope");
    ValidateEnvelope(envelope, "fragment", false);
    const auto& payload = AsObject(Field(envelope, "payload", "JSON envelope"), "fragment payload");
    ExpectKeys(payload, {"groups", "intergraph_links", "links", "nodes", "origin", "owned_graphs"}, "fragment payload");
    GraphFragment fragment{.origin = DecodeVec2(Field(payload, "origin", "fragment payload"), "fragment origin")};
    for (const Json& encoded : AsArray(Field(payload, "nodes", "fragment payload"), "fragment nodes")) {
        const auto& item = AsObject(encoded, "fragment node");
        ExpectKeys(item, {"node", "presentation"}, "fragment node");
        fragment.nodes.push_back(GraphFragmentNode{
            .creation = DecodeNodeCreation(Field(item, "node", "fragment node"), counts),
            .presentation = DecodeNodePresentation(Field(item, "presentation", "fragment node")),
        });
    }
    for (const Json& encoded : AsArray(Field(payload, "links", "fragment payload"), "fragment links")) {
        const auto& item = AsObject(encoded, "fragment link");
        ExpectKeys(item, {"link", "presentation"}, "fragment link");
        const Json& presentation = Field(item, "presentation", "fragment link");
        fragment.links.push_back(GraphFragmentLink{
            .link = DecodeLink(Field(item, "link", "fragment link"), counts),
            .presentation = std::holds_alternative<std::nullptr_t>(presentation.value)
                                ? std::nullopt
                                : std::optional<LinkPresentation>{DecodeLinkPresentation(presentation, counts)},
        });
    }
    for (const Json& encoded : AsArray(Field(payload, "groups", "fragment payload"), "fragment groups")) {
        fragment.groups.push_back(DecodeGroup(encoded, counts));
    }
    for (const Json& encoded : AsArray(
             Field(payload, "owned_graphs", "fragment payload"),
             "fragment owned_graphs")) {
        const auto& object = AsObject(encoded, "fragment owned graph");
        ExpectKeys(object, {"graph", "groups", "links", "nodes"}, "fragment owned graph");
        GraphFragment::OwnedGraph owned{.graph = DecodeGraph(Field(object, "graph", "fragment owned graph"), counts)};
        for (const Json& state : AsArray(Field(object, "nodes", "fragment owned graph"), "owned node states")) {
            const auto& item = AsObject(state, "owned node state");
            ExpectKeys(item, {"id", "value"}, "owned node state");
            const NodeId id = DecodeId<NodeId>(Field(item, "id", "owned node state"), "owned node state.id");
            if (!owned.nodes.emplace(id, DecodeNodePresentation(Field(item, "value", "owned node state"))).second) {
                Fail(ErrorCode::DuplicateId, "Owned graph contains duplicate node presentation IDs");
            }
        }
        for (const Json& state : AsArray(Field(object, "links", "fragment owned graph"), "owned link states")) {
            const auto& item = AsObject(state, "owned link state");
            ExpectKeys(item, {"id", "value"}, "owned link state");
            const LinkId id = DecodeId<LinkId>(Field(item, "id", "owned link state"), "owned link state.id");
            if (!owned.links.emplace(id, DecodeLinkPresentation(Field(item, "value", "owned link state"), counts)).second) {
                Fail(ErrorCode::DuplicateId, "Owned graph contains duplicate link presentation IDs");
            }
        }
        for (const Json& group : AsArray(Field(object, "groups", "fragment owned graph"), "owned groups")) {
            owned.groups.push_back(DecodeGroup(group, counts));
        }
        fragment.owned_graphs.push_back(std::move(owned));
    }
    for (const Json& encoded : AsArray(
             Field(payload, "intergraph_links", "fragment payload"),
             "fragment intergraph_links")) {
        Bump(counts.links, counts.limits.max_links, "Link count");
        fragment.intergraph_links.push_back(DecodeIntergraphLink(encoded));
    }
    return fragment;
}

[[nodiscard]] bool Finite(const Vec2 value) noexcept { return std::isfinite(value.x) && std::isfinite(value.y); }

[[nodiscard]] bool ValidProperty(const PropertyValue& property) noexcept {
    return std::visit(
        [](const auto& value) noexcept {
            using Value = std::remove_cvref_t<decltype(value)>;
            if constexpr (std::same_as<Value, double>)
                return std::isfinite(value);
            if constexpr (std::same_as<Value, Vec2>)
                return Finite(value);
            if constexpr (std::same_as<Value, OpaqueJsonProperty>) {
                return Detail::ValidOpaqueJsonProperty(value.canonical_json);
            }
            return true;
        },
        property);
}

void ValidateNodeDescriptor(const NodeCreation& creation, const RegistrySnapshot& registry, const NodeTypeDescriptor& descriptor) {
    auto resolved = registry.ResolvePinSchema(descriptor.type, creation.node.properties);
    if (!resolved) {
        Fail(ErrorCode::MigrationFailed, "Migrated node pin schema resolution failed: " + resolved.error().message);
    }
    std::vector<const PinInstance*> static_pins;
    for (const auto& pin : creation.pins) {
        if (pin.storage == PinStorage::Static)
            static_pins.push_back(&pin);
    }
    if (static_pins.size() != resolved->size()) {
        Fail(ErrorCode::MigrationFailed, "Migrated node static pin count does not match its descriptor");
    }
    for (std::size_t index = 0; index < static_pins.size(); ++index) {
        const auto& actual = *static_pins[index];
        const auto& expected = (*resolved)[index];
        if (actual.key != expected.key || actual.label != expected.label || actual.type != expected.type || actual.direction != expected.direction ||
            actual.kind != expected.kind || actual.cardinality != expected.cardinality) {
            Fail(ErrorCode::MigrationFailed, "Migrated node static pins do not match their descriptor");
        }
    }
    if (descriptor.behavior && descriptor.behavior->validate) {
        const ValidateNodeFn validate = descriptor.behavior->validate;
        try {
            const auto messages = validate(creation.node, creation.pins);
            if (!messages.empty()) {
                Fail(ErrorCode::MigrationFailed,
                     messages.front().empty() ? "Node validation failed" : messages.front());
            }
        } catch (const JsonFailure&) {
            throw;
        } catch (const std::exception& exception) {
            Fail(ErrorCode::MigrationFailed, std::string("Node validator failed: ") + exception.what());
        } catch (...) {
            Fail(ErrorCode::MigrationFailed, "Node validator failed with an unknown exception");
        }
    }
}

void ValidateMigrationCreation(const NodeCreation& creation) {
    if (!creation.node.id || creation.node.type.Empty() || creation.node.type_version == 0 ||
        creation.node.pins.size() != creation.pins.size() ||
        !std::ranges::all_of(creation.node.properties, [](const auto& property) {
            return !property.first.empty() && ValidProperty(property.second);
        })) {
        Fail(ErrorCode::InvalidGraph, "Node migration input is structurally invalid");
    }
    std::unordered_set<PinId, IdHash> ids;
    std::unordered_set<std::string> keys;
    for (std::size_t index = 0; index < creation.pins.size(); ++index) {
        const auto& pin = creation.pins[index];
        const bool direction = pin.direction == PinDirection::Input || pin.direction == PinDirection::Output;
        const bool kind = pin.kind == PinKind::Data || pin.kind == PinKind::Execution;
        const bool cardinality =
            pin.cardinality == PinCardinality::Single || pin.cardinality == PinCardinality::Multiple;
        const bool storage = pin.storage == PinStorage::Static || pin.storage == PinStorage::Dynamic;
        if (!pin.id || pin.node != creation.node.id || creation.node.pins[index] != pin.id || pin.key.empty() ||
            pin.type.Empty() || !direction || !kind || !cardinality || !storage || !ids.insert(pin.id).second ||
            !keys.insert(pin.key).second) {
            Fail(ErrorCode::InvalidGraph, "Node migration input contains invalid pins");
        }
    }
}

template<typename Replace>
void MigrateCreation(NodeCreation creation, const RegistrySnapshot& registry,
                       const std::function<PinId()>& allocate_pin_id,
                      const std::function<void(PinId, PinId)>& remap_links,
                      const std::function<void(PinId)>& remove_links,
                      std::vector<GraphIoWarning>* warnings, const GraphId graph, Replace&& replace) {
    ValidateMigrationCreation(creation);
    const auto descriptor = registry.Find(creation.node.type);
    if (!descriptor) {
        if (warnings != nullptr)
            warnings->push_back({
                .message = "Node type '" + creation.node.type.Value() + "' is not registered; data was preserved",
                .graph = graph,
                .node = creation.node.id,
            });
        replace(std::move(creation));
        return;
    }
    if (creation.node.type_version > descriptor->version) {
        if (warnings != nullptr)
            warnings->push_back({
                .message = "Node was created by a newer descriptor; data was preserved",
                .graph = graph,
                .node = creation.node.id,
            });
        replace(std::move(creation));
        return;
    }

    const NodeId original_id = creation.node.id;
    const TypeId original_type = creation.node.type;
    while (creation.node.type_version < descriptor->version) {
        if (!descriptor->behavior || !descriptor->behavior->migrate) {
            Fail(ErrorCode::MigrationMissing, "Node type '" + original_type.Value() +
                                                  "' has no migration from version " +
                                                  std::to_string(creation.node.type_version));
        }
        const std::uint32_t from = creation.node.type_version;
        NodeMigrationContext context{
            .from_version = from,
            .to_version = from + 1,
            .creation = creation,
            .allocate_pin_id = allocate_pin_id,
            .remap_links = remap_links,
            .remove_links = remove_links,
        };
        const MigrateNodeFn migrate = descriptor->behavior->migrate;
        try {
            auto migrated = migrate(context);
            if (!migrated)
                Fail(ErrorCode::MigrationFailed, migrated.error().message);
        } catch (const JsonFailure&) {
            throw;
        } catch (const std::exception& exception) {
            Fail(ErrorCode::MigrationFailed, std::string("Node migration failed: ") + exception.what());
        } catch (...) {
            Fail(ErrorCode::MigrationFailed, "Node migration failed with an unknown exception");
        }
        if (creation.node.id != original_id || creation.node.type != original_type) {
            Fail(ErrorCode::MigrationFailed, "Node migration changed immutable node identity");
        }
        creation.node.type_version = from + 1;
        ValidateMigrationCreation(creation);
    }
    ValidateNodeDescriptor(creation, registry, *descriptor);
    replace(std::move(creation));
}

[[nodiscard]] std::function<PinId()> MakePinAllocator(const std::uint64_t maximum) {
    auto next = std::make_shared<std::uint64_t>(maximum);
    return [next]() mutable -> PinId {
        if (*next == std::numeric_limits<std::uint64_t>::max())
            return {};
        return PinId{++*next};
    };
}

void MigrateArchiveNodes(
    GraphArchive& archive,
    const RegistrySnapshot& registry,
    std::vector<GraphIoWarning>& warnings) {
    std::uint64_t maximum_pin = 0;
    for (const auto& graph : archive.graphs) {
        for (const auto& [pin_id, pin] : graph.pins) {
            (void)pin;
            maximum_pin = std::max(maximum_pin, pin_id.Value());
        }
    }
    const auto allocate = MakePinAllocator(maximum_pin);
    std::ranges::sort(archive.graphs, {}, &Graph::id);
    for (auto& graph : archive.graphs) {
        const auto node_ids = SortedIds(graph.nodes);
        for (const NodeId node_id : node_ids) {
            NodeCreation creation{.node = graph.nodes.at(node_id)};
            for (const PinId pin_id : creation.node.pins) {
                const auto pin = graph.pins.find(pin_id);
                if (pin == graph.pins.end())
                    Fail(ErrorCode::InvalidGraph, "Node migration input has a missing pin");
                creation.pins.push_back(pin->second);
            }
            const std::vector<PinId> old_pins = creation.node.pins;
            const auto remap_links = [&](const PinId from, const PinId to) {
                std::vector<LinkId> matching;
                for (const auto& [link_id, link] : graph.links) {
                    if (link.output == from || link.input == from) matching.push_back(link_id);
                }
                for (const LinkId link_id : matching) {
                    Link link = graph.links.at(link_id);
                    if (link.output == from) link.output = to;
                    if (link.input == from) link.input = to;
                    graph.links.insert_or_assign(link_id, std::move(link));
                }
                for (auto& link : archive.intergraph_links) {
                    if (link.source.pin == from) link.source.pin = to;
                    if (link.destination.pin == from) link.destination.pin = to;
                }
            };
            const auto remove_links = [&](const PinId pin) {
                std::vector<LinkId> removed;
                for (const auto& [link_id, link] : graph.links) {
                    if (link.output == pin || link.input == pin) removed.push_back(link_id);
                }
                for (const LinkId link : removed) {
                    graph.links.erase(link);
                    archive.links.erase(link);
                }
                std::erase_if(archive.intergraph_links, [pin](const IntergraphLink& link) {
                    return link.source.pin == pin || link.destination.pin == pin;
                });
            };
            MigrateCreation(std::move(creation), registry, allocate, remap_links, remove_links,
                            &warnings, graph.id, [&](NodeCreation migrated) {
                for (const PinId pin : old_pins)
                    graph.pins.erase(pin);
                graph.nodes.insert_or_assign(node_id, migrated.node);
                for (auto& pin : migrated.pins) {
                    if (!graph.pins.emplace(pin.id, std::move(pin)).second) {
                        Fail(ErrorCode::DuplicateId, "Node migration produced a duplicate pin ID");
                    }
                }
            });
        }
    }
}

void WarnExternalGraphAssets(const GraphArchive& archive, std::vector<GraphIoWarning>& warnings) {
    for (const auto& graph : archive.graphs) {
        for (const auto& [node_id, node] : graph.nodes) {
            if (!node.subgraph) continue;
            if (const auto* asset = std::get_if<GraphAssetTarget>(&node.subgraph->target)) {
                warnings.push_back(GraphIoWarning{
                    .message = "Graph asset '" + asset->asset.Value() + "' requires application resolution",
                    .graph = graph.id,
                    .node = node_id,
                });
            }
        }
    }
}

void MigrateFragmentNodes(GraphFragment& fragment, const RegistrySnapshot& registry) {
    std::uint64_t maximum_pin = 0;
    for (const auto& node : fragment.nodes) {
        for (const auto& pin : node.creation.pins)
            maximum_pin = std::max(maximum_pin, pin.id.Value());
    }
    for (const auto& owned : fragment.owned_graphs) {
        for (const auto& [pin_id, pin] : owned.graph.pins) {
            (void)pin;
            maximum_pin = std::max(maximum_pin, pin_id.Value());
        }
    }
    const auto allocate = MakePinAllocator(maximum_pin);
    std::ranges::sort(fragment.nodes, {}, [](const GraphFragmentNode& node) { return node.creation.node.id.Value(); });
    for (auto& node : fragment.nodes) {
        const auto remap_links = [&](const PinId from, const PinId to) {
            for (auto& link : fragment.links) {
                if (link.link.output == from) link.link.output = to;
                if (link.link.input == from) link.link.input = to;
            }
        };
        const auto remove_links = [&](const PinId pin) {
            std::erase_if(fragment.links, [&](const GraphFragmentLink& link) {
                return link.link.output == pin || link.link.input == pin;
            });
        };
        MigrateCreation(node.creation, registry, allocate, remap_links, remove_links, nullptr, {},
                        [&](NodeCreation migrated) { node.creation = std::move(migrated); });
    }
    for (auto& owned : fragment.owned_graphs) {
        const auto node_ids = SortedIds(owned.graph.nodes);
        for (const NodeId node_id : node_ids) {
            NodeCreation creation{.node = owned.graph.nodes.at(node_id)};
            for (const PinId pin_id : creation.node.pins) {
                creation.pins.push_back(owned.graph.pins.at(pin_id));
            }
            const auto old_pins = creation.node.pins;
            const auto remap_links = [&](const PinId from, const PinId to) {
                std::vector<LinkId> matching;
                for (const auto& [link_id, link] : owned.graph.links) {
                    if (link.output == from || link.input == from) matching.push_back(link_id);
                }
                for (const LinkId link_id : matching) {
                    Link link = owned.graph.links.at(link_id);
                    if (link.output == from) link.output = to;
                    if (link.input == from) link.input = to;
                    owned.graph.links.insert_or_assign(link_id, std::move(link));
                }
                for (auto& link : fragment.intergraph_links) {
                    if (link.source.pin == from) link.source.pin = to;
                    if (link.destination.pin == from) link.destination.pin = to;
                }
            };
            const auto remove_links = [&](const PinId pin) {
                std::vector<LinkId> removed;
                for (const auto& [link_id, link] : owned.graph.links) {
                    if (link.output == pin || link.input == pin) removed.push_back(link_id);
                }
                for (const LinkId link : removed) {
                    owned.graph.links.erase(link);
                    owned.links.erase(link);
                }
                std::erase_if(fragment.intergraph_links, [pin](const IntergraphLink& link) {
                    return link.source.pin == pin || link.destination.pin == pin;
                });
            };
            MigrateCreation(creation, registry, allocate, remap_links, remove_links, nullptr, owned.graph.id,
                            [&](NodeCreation migrated) {
                for (const PinId pin : old_pins) owned.graph.pins.erase(pin);
                owned.graph.nodes.insert_or_assign(node_id, migrated.node);
                for (auto& pin : migrated.pins) owned.graph.pins.emplace(pin.id, std::move(pin));
            });
        }
    }
}

void ValidatePortableSubgraphs(const GraphFragment& fragment) {
    std::unordered_set<GraphId, IdHash> owned_graphs;
    for (const auto& owned : fragment.owned_graphs) {
        if (!owned_graphs.insert(owned.graph.id).second) {
            Fail(ErrorCode::DuplicateId, "Fragment contains duplicate owned graph IDs");
        }
    }
    const auto validate = [&](const NodeInstance& node) {
        if (!node.subgraph) return;
        if (const auto* local = std::get_if<DocumentGraphTarget>(&node.subgraph->target)) {
            if (node.subgraph->ownership != SubgraphOwnership::Owned || !owned_graphs.contains(local->graph)) {
                Fail(
                    ErrorCode::InvalidGraph,
                    "Serialized fragments cannot contain referenced document-local graphs");
            }
        } else {
            const auto& asset = std::get<GraphAssetTarget>(node.subgraph->target);
            if (node.subgraph->ownership != SubgraphOwnership::Referenced || asset.asset.Empty()) {
                Fail(ErrorCode::InvalidGraph, "Serialized fragment contains an invalid graph asset reference");
            }
        }
    };
    for (const auto& node : fragment.nodes) validate(node.creation.node);
    for (const auto& owned : fragment.owned_graphs) {
        for (const auto& [node_id, node] : owned.graph.nodes) {
            (void)node_id;
            validate(node);
        }
    }
}

void ValidateFragment(const GraphFragment& fragment) {
    ValidatePortableSubgraphs(fragment);
    if ((!Finite(fragment.origin)) || (fragment.nodes.empty() && fragment.groups.empty())) {
        Fail(ErrorCode::InvalidGraph, "Graph fragment is empty or has an invalid origin");
    }
    std::unordered_set<NodeId, IdHash> nodes;
    std::unordered_set<PinId, IdHash> pins;
    std::unordered_map<PinId, const PinInstance*, IdHash> pins_by_id;
    for (const auto& item : fragment.nodes) {
        const auto& node = item.creation.node;
        if (!node.id || !nodes.insert(node.id).second || node.type.Empty() || node.type_version == 0 ||
            node.pins.size() != item.creation.pins.size() || !Finite(item.presentation.position) ||
            !Finite(item.presentation.size) || item.presentation.size.x < 0.0f || item.presentation.size.y < 0.0f ||
            !std::ranges::all_of(
                node.properties,
                [](const auto& property) { return !property.first.empty() && ValidProperty(property.second); })) {
            Fail(ErrorCode::InvalidGraph, "Graph fragment contains an invalid node");
        }
        std::unordered_set<std::string> keys;
        for (std::size_t index = 0; index < item.creation.pins.size(); ++index) {
            const auto& pin = item.creation.pins[index];
            if (!pin.id || pin.node != node.id || node.pins[index] != pin.id || pin.key.empty() || pin.type.Empty() ||
                !pins.insert(pin.id).second || !keys.insert(pin.key).second) {
                Fail(ErrorCode::InvalidGraph, "Graph fragment contains an invalid pin");
            }
            pins_by_id.emplace(pin.id, &pin);
        }
    }

    std::unordered_set<LinkId, IdHash> links;
    std::set<std::pair<std::uint64_t, std::uint64_t>> endpoint_pairs;
    std::unordered_map<PinId, std::size_t, IdHash> connection_counts;
    std::unordered_set<RoutePointId, IdHash> route_points;
    for (const auto& item : fragment.links) {
        const auto output = pins_by_id.find(item.link.output);
        const auto input = pins_by_id.find(item.link.input);
        if (!item.link.id || !links.insert(item.link.id).second || output == pins_by_id.end() ||
            input == pins_by_id.end() || output->second->direction != PinDirection::Output ||
            input->second->direction != PinDirection::Input || output->second->kind != input->second->kind) {
            Fail(ErrorCode::InvalidGraph, "Graph fragment contains an invalid link");
        }
        const auto pair = std::pair{item.link.output.Value(), item.link.input.Value()};
        if (!endpoint_pairs.insert(pair).second ||
            (output->second->cardinality == PinCardinality::Single && connection_counts[item.link.output] != 0) ||
            (input->second->cardinality == PinCardinality::Single && connection_counts[item.link.input] != 0)) {
            Fail(ErrorCode::InvalidGraph, "Graph fragment contains duplicate links or exceeds cardinality");
        }
        ++connection_counts[item.link.output];
        ++connection_counts[item.link.input];
        if (item.presentation) {
            for (const auto& point : item.presentation->Route()) {
                if (!point.id || !Finite(point.position) || !route_points.insert(point.id).second) {
                    Fail(ErrorCode::InvalidGraph, "Graph fragment contains an invalid route point");
                }
            }
        }
    }

    std::unordered_set<GroupId, IdHash> groups;
    for (const auto& group : fragment.groups) {
        std::unordered_set<NodeId, IdHash> members;
        if (!group.id || !group.graph || !group.style || !groups.insert(group.id).second ||
            !Finite(group.geometry.position) || !Finite(group.geometry.size) ||
            group.geometry.size.x < 0.0f || group.geometry.size.y < 0.0f ||
            (group.style->kind != GroupKind::Group && group.style->kind != GroupKind::Comment) ||
            !std::ranges::all_of(
                group.members,
                [&](const NodeId member) { return nodes.contains(member) && members.insert(member).second; })) {
            Fail(ErrorCode::InvalidGraph, "Graph fragment contains an invalid group");
        }
    }
    std::unordered_set<GraphId, IdHash> owned_graphs;
    for (const auto& owned : fragment.owned_graphs) {
        if (!owned.graph.id || owned.graph.lifetime != GraphLifetime::Owned ||
            !owned_graphs.insert(owned.graph.id).second) {
            Fail(ErrorCode::InvalidGraph, "Fragment contains an invalid owned graph");
        }
        std::unordered_set<PinId, IdHash> referenced_pins;
        for (const auto& [node_id, node] : owned.graph.nodes) {
            if (!node_id || node.id != node_id || !nodes.insert(node_id).second || node.type.Empty() ||
                node.type_version == 0 ||
                !std::ranges::all_of(node.properties, [](const auto& property) {
                    return !property.first.empty() && ValidProperty(property.second);
                })) {
                Fail(ErrorCode::InvalidGraph, "Owned graph contains an invalid node");
            }
            std::unordered_set<std::string> keys;
            std::unordered_set<PinId, IdHash> node_pins;
            for (const PinId pin_id : node.pins) {
                const auto pin = owned.graph.pins.find(pin_id);
                if (!node_pins.insert(pin_id).second || pin == owned.graph.pins.end() ||
                    pin->second.node != node_id || !keys.insert(pin->second.key).second) {
                    Fail(ErrorCode::InvalidGraph, "Owned graph node pin references are inconsistent");
                }
                referenced_pins.insert(pin_id);
            }
        }
        for (const auto& [pin_id, pin] : owned.graph.pins) {
            if (!pin_id || pin.id != pin_id || !pin.node || pin.key.empty() || pin.type.Empty() ||
                !owned.graph.nodes.contains(pin.node) || !referenced_pins.contains(pin_id) ||
                !pins.insert(pin_id).second) {
                Fail(ErrorCode::InvalidGraph, "Owned graph contains an invalid pin");
            }
        }
        std::set<std::pair<PinId, PinId>> owned_endpoints;
        std::unordered_map<PinId, std::size_t, IdHash> owned_connection_counts;
        for (const auto& [link_id, link] : owned.graph.links) {
            const auto output = owned.graph.pins.find(link.output);
            const auto input = owned.graph.pins.find(link.input);
            if (!link_id || link.id != link_id || !links.insert(link_id).second ||
                output == owned.graph.pins.end() || input == owned.graph.pins.end() ||
                output->second.direction != PinDirection::Output ||
                input->second.direction != PinDirection::Input ||
                output->second.kind != input->second.kind ||
                !owned_endpoints.insert({link.output, link.input}).second) {
                Fail(ErrorCode::InvalidGraph, "Owned graph contains an invalid link");
            }
            ++owned_connection_counts[link.output];
            ++owned_connection_counts[link.input];
        }
        for (const auto& [pin_id, count] : owned_connection_counts) {
            if (owned.graph.pins.at(pin_id).cardinality == PinCardinality::Single && count > 1) {
                Fail(ErrorCode::InvalidGraph, "Owned graph exceeds pin cardinality");
            }
        }
        for (const auto& [node_id, state] : owned.nodes) {
            if (!owned.graph.nodes.contains(node_id) || !Finite(state.position) || !Finite(state.size)) {
                Fail(ErrorCode::InvalidGraph, "Owned graph presentation references a missing node");
            }
        }
        for (const auto& [link_id, state] : owned.links) {
            if (!owned.graph.links.contains(link_id)) {
                Fail(ErrorCode::InvalidGraph, "Owned graph presentation references a missing link");
            }
            for (const auto& point : state.Route()) {
                if (!point.id || !Finite(point.position) || !route_points.insert(point.id).second) {
                    Fail(ErrorCode::InvalidGraph, "Owned graph contains an invalid route point");
                }
            }
        }
        for (const auto& group : owned.groups) {
            std::unordered_set<NodeId, IdHash> members;
            if (!group.id || group.graph != owned.graph.id || !group.style ||
                !Finite(group.geometry.position) || !Finite(group.geometry.size) ||
                group.geometry.size.x < 0.0f || group.geometry.size.y < 0.0f ||
                (group.style->kind != GroupKind::Group && group.style->kind != GroupKind::Comment) ||
                !groups.insert(group.id).second ||
                !std::ranges::all_of(group.members, [&](const NodeId member) {
                    return owned.graph.nodes.contains(member) && members.insert(member).second;
                })) {
                Fail(ErrorCode::InvalidGraph, "Owned graph group has the wrong graph ID");
            }
        }
    }
    std::unordered_set<IntergraphLinkId, IdHash> intergraph_links;
    for (const auto& link : fragment.intergraph_links) {
        if (!link.id || !intergraph_links.insert(link.id).second ||
            !owned_graphs.contains(link.source.graph) || !owned_graphs.contains(link.destination.graph)) {
            Fail(ErrorCode::InvalidGraph, "Fragment contains an invalid intergraph link");
        }
    }
}

[[nodiscard]] std::string SdlError(const std::string_view prefix) {
    const char* error = SDL_GetError();
    return std::string(prefix) + (error != nullptr && *error != '\0' ? ": " + std::string(error) : std::string{});
}

#ifdef _WIN32
[[nodiscard]] Result<std::wstring> WidePath(const std::string_view path) {
    if (path.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return std::unexpected(Error{ErrorCode::InvalidArgument, "File path is too long"});
    }
    const int size = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        path.data(),
        static_cast<int>(path.size()),
        nullptr,
        0);
    if (size <= 0) {
        return std::unexpected(Error{ErrorCode::InvalidArgument, "File path is not valid UTF-8"});
    }
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    if (MultiByteToWideChar(
            CP_UTF8,
            MB_ERR_INVALID_CHARS,
            path.data(),
            static_cast<int>(path.size()),
            result.data(),
            size) != size) {
        return std::unexpected(Error{ErrorCode::InvalidArgument, "Failed to convert the file path"});
    }
    return result;
}
#endif

#ifndef _WIN32
[[nodiscard]] Result<void> CopyExtendedAttributes(const int source, const int destination) {
#ifdef __APPLE__
    const ssize_t names_size = ::flistxattr(source, nullptr, 0, 0);
#else
    const ssize_t names_size = ::flistxattr(source, nullptr, 0);
#endif
    if (names_size < 0) {
        if (errno == ENOTSUP) return {};
        return std::unexpected(Error{ErrorCode::IoWrite, "Failed to inspect graph file extended attributes"});
    }
    if (names_size == 0) return {};
    std::vector<char> names(static_cast<std::size_t>(names_size));
#ifdef __APPLE__
    const ssize_t read_names = ::flistxattr(source, names.data(), names.size(), 0);
#else
    const ssize_t read_names = ::flistxattr(source, names.data(), names.size());
#endif
    if (read_names != names_size) {
        return std::unexpected(Error{ErrorCode::IoWrite, "Failed to read graph file extended attribute names"});
    }
    for (std::size_t offset = 0; offset < names.size();) {
        const char* name = names.data() + offset;
        const auto terminator = std::find(names.begin() + static_cast<std::ptrdiff_t>(offset), names.end(), '\0');
        if (terminator == names.end()) {
            return std::unexpected(Error{ErrorCode::IoWrite, "Graph file extended attributes are malformed"});
        }
        const std::size_t length = static_cast<std::size_t>(terminator - names.begin()) - offset;
        if (length == 0) {
            return std::unexpected(Error{ErrorCode::IoWrite, "Graph file extended attributes are malformed"});
        }
#ifndef __APPLE__
        if (std::string_view{name, length} == "security.selinux") {
            offset += length + 1;
            continue;
        }
#endif
#ifdef __APPLE__
        const ssize_t value_size = ::fgetxattr(source, name, nullptr, 0, 0, 0);
#else
        const ssize_t value_size = ::fgetxattr(source, name, nullptr, 0);
#endif
        if (value_size < 0) {
            return std::unexpected(Error{ErrorCode::IoWrite, "Failed to inspect graph file extended attribute"});
        }
        std::vector<char> value(static_cast<std::size_t>(value_size));
#ifdef __APPLE__
        const ssize_t read_value = ::fgetxattr(source, name, value.data(), value.size(), 0, 0);
        const int set_result = read_value == value_size
            ? ::fsetxattr(destination, name, value.data(), value.size(), 0, 0)
            : -1;
#else
        const ssize_t read_value = ::fgetxattr(source, name, value.data(), value.size());
        const int set_result = read_value == value_size
            ? ::fsetxattr(destination, name, value.data(), value.size(), 0)
            : -1;
#endif
        if (set_result != 0) {
            return std::unexpected(Error{ErrorCode::IoWrite, "Failed to preserve graph file extended attribute"});
        }
        offset += length + 1;
    }
    return {};
}
#endif

[[nodiscard]] Result<void> WriteTemporaryFile(
    const std::string_view destination,
    const std::string_view temporary,
    const std::string_view bytes) {
#ifdef _WIN32
    auto destination_path = WidePath(destination);
    auto temporary_path = WidePath(temporary);
    if (!destination_path) return std::unexpected(destination_path.error());
    if (!temporary_path) return std::unexpected(temporary_path.error());

    DWORD attributes = GetFileAttributesW(destination_path->c_str());
    PSECURITY_DESCRIPTOR security_descriptor = nullptr;
    PACL dacl = nullptr;
    SECURITY_ATTRIBUTES security_attributes{.nLength = sizeof(SECURITY_ATTRIBUTES)};
    SECURITY_ATTRIBUTES* security = nullptr;
    if (attributes != INVALID_FILE_ATTRIBUTES) {
        if ((attributes & FILE_ATTRIBUTE_DIRECTORY) != 0 ||
            GetNamedSecurityInfoW(destination_path->data(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION,
                                  nullptr, nullptr, &dacl, nullptr, &security_descriptor) != ERROR_SUCCESS) {
            return std::unexpected(Error{ErrorCode::IoWrite, "Failed to preserve existing graph file ACLs"});
        }
        security_attributes.lpSecurityDescriptor = security_descriptor;
        security = &security_attributes;
    } else {
        const DWORD inspect_error = GetLastError();
        if (inspect_error != ERROR_FILE_NOT_FOUND && inspect_error != ERROR_PATH_NOT_FOUND) {
            return std::unexpected(Error{ErrorCode::IoWrite, "Failed to inspect existing graph file metadata"});
        }
        attributes = FILE_ATTRIBUTE_NORMAL;
    }
    HANDLE handle = CreateFileW(temporary_path->c_str(), GENERIC_WRITE, 0, security, CREATE_NEW,
                                FILE_ATTRIBUTE_NORMAL, nullptr);
    if (security_descriptor != nullptr) LocalFree(security_descriptor);
    if (handle == INVALID_HANDLE_VALUE) {
        return std::unexpected(Error{ErrorCode::IoWrite, "Failed to create a secure temporary graph file"});
    }
    std::size_t offset = 0;
    bool written = true;
    while (offset < bytes.size()) {
        const DWORD chunk = static_cast<DWORD>(std::min<std::size_t>(
            bytes.size() - offset,
            std::numeric_limits<DWORD>::max()));
        DWORD count = 0;
        if (!WriteFile(handle, bytes.data() + offset, chunk, &count, nullptr) || count != chunk) {
            written = false;
            break;
        }
        offset += count;
    }
    const bool synchronized = written && FlushFileBuffers(handle) != 0;
    const bool closed = CloseHandle(handle) != 0;
    const bool attributed = attributes == FILE_ATTRIBUTE_NORMAL ||
        SetFileAttributesW(temporary_path->c_str(), attributes) != 0;
    if (!written || !synchronized || !closed || !attributed) {
        (void)DeleteFileW(temporary_path->c_str());
        return std::unexpected(Error{ErrorCode::IoWrite, "Failed to write the temporary graph file"});
    }
    return {};
#else
    const std::string destination_path(destination);
    const std::string temporary_path(temporary);
    int source = -1;
    int source_flags = O_RDONLY;
#ifdef O_CLOEXEC
    source_flags |= O_CLOEXEC;
#endif
#ifdef O_NOFOLLOW
    source_flags |= O_NOFOLLOW;
#endif
    source = ::open(destination_path.c_str(), source_flags);
    mode_t permissions = S_IRUSR | S_IWUSR;
    if (source >= 0) {
        struct stat status {};
        if (::fstat(source, &status) != 0 || !S_ISREG(status.st_mode)) {
            (void)::close(source);
            return std::unexpected(Error{ErrorCode::IoWrite, "Existing graph path is not a regular file"});
        }
        permissions = status.st_mode & 07777;
    } else if (errno != ENOENT) {
        return std::unexpected(Error{ErrorCode::IoWrite, "Failed to inspect existing graph file metadata"});
    }
    int flags = O_WRONLY | O_CREAT | O_EXCL;
#ifdef O_CLOEXEC
    flags |= O_CLOEXEC;
#endif
#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif
    const int descriptor = ::open(temporary_path.c_str(), flags, permissions | S_IWUSR);
    if (descriptor < 0) {
        if (source >= 0) (void)::close(source);
        return std::unexpected(Error{ErrorCode::IoWrite, "Failed to create a secure temporary graph file"});
    }
    if (source >= 0) {
        if (auto copied = CopyExtendedAttributes(source, descriptor); !copied) {
            (void)::close(source);
            (void)::close(descriptor);
            (void)::unlink(temporary_path.c_str());
            return copied;
        }
        (void)::close(source);
    }
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const std::size_t chunk = std::min<std::size_t>(
            bytes.size() - offset,
            static_cast<std::size_t>(std::numeric_limits<ssize_t>::max()));
        const ssize_t count = ::write(descriptor, bytes.data() + offset, chunk);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) {
            (void)::close(descriptor);
            (void)::unlink(temporary_path.c_str());
            return std::unexpected(Error{ErrorCode::IoWrite, "Failed to write the temporary graph file"});
        }
        offset += static_cast<std::size_t>(count);
    }
    const bool metadata_set = ::fchmod(descriptor, permissions) == 0;
    const bool synchronized = metadata_set && ::fsync(descriptor) == 0;
    const bool closed = ::close(descriptor) == 0;
    if (!metadata_set || !synchronized || !closed) {
        (void)::unlink(temporary_path.c_str());
        return std::unexpected(Error{ErrorCode::IoWrite, "Failed to finalize the temporary graph file"});
    }
    return {};
#endif
}

[[nodiscard]] Result<void> ReplaceTemporaryFile(
    const std::string_view temporary,
    const std::string_view destination) {
#ifdef _WIN32
    auto temporary_path = WidePath(temporary);
    auto destination_path = WidePath(destination);
    if (!temporary_path) return std::unexpected(temporary_path.error());
    if (!destination_path) return std::unexpected(destination_path.error());
    if (!MoveFileExW(
            temporary_path->c_str(),
            destination_path->c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        return std::unexpected(Error{ErrorCode::IoWrite, "Failed to atomically replace graph file"});
    }
#else
    const std::string temporary_path(temporary);
    const std::string destination_path(destination);
    if (::rename(temporary_path.c_str(), destination_path.c_str()) != 0) {
        return std::unexpected(Error{ErrorCode::IoWrite, "Failed to atomically replace graph file"});
    }
    const std::size_t separator = destination.find_last_of('/');
    const std::string directory = separator == std::string_view::npos
        ? "."
        : separator == 0 ? "/" : std::string(destination.substr(0, separator));
    int flags = O_RDONLY;
#ifdef O_CLOEXEC
    flags |= O_CLOEXEC;
#endif
#ifdef O_DIRECTORY
    flags |= O_DIRECTORY;
#endif
    const int descriptor = ::open(directory.c_str(), flags);
    if (descriptor < 0) {
        return std::unexpected(Error{ErrorCode::IoWrite, "Failed to open graph file directory for synchronization"});
    }
    const bool synchronized = ::fsync(descriptor) == 0;
    const bool closed = ::close(descriptor) == 0;
    if (!synchronized || !closed) {
        return std::unexpected(Error{ErrorCode::IoWrite, "Failed to synchronize the graph file directory"});
    }
#endif
    return {};
}

[[nodiscard]] Result<void> WriteAtomic(const std::string_view path, const std::string_view bytes) {
    if (path.empty() || path.find('\0') != std::string_view::npos)
        return std::unexpected(Error{ErrorCode::InvalidArgument, "File path cannot be empty or contain NUL"});
    static std::atomic<std::uint64_t> counter{0};
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::string destination(path);
    const std::string temporary = destination + ".tmp." + std::to_string(stamp) + "." +
                                   std::to_string(counter.fetch_add(1, std::memory_order_relaxed));
    if (auto written = WriteTemporaryFile(destination, temporary, bytes); !written) {
        return written;
    }
    if (auto replaced = ReplaceTemporaryFile(temporary, destination); !replaced) {
        (void)SDL_RemovePath(temporary.c_str());
        return replaced;
    }
    return {};
}

[[nodiscard]] Result<std::string> ReadFile(const std::string_view path, const GraphIoLimits& limits) {
    if (path.empty() || path.find('\0') != std::string_view::npos)
        return std::unexpected(Error{ErrorCode::InvalidArgument, "File path cannot be empty or contain NUL"});
    const std::string native(path);
    SDL_IOStream* stream = SDL_IOFromFile(native.c_str(), "rb");
    if (stream == nullptr)
        return std::unexpected(Error{ErrorCode::IoRead, SdlError("Failed to open graph file")});
    const Sint64 size = SDL_GetIOSize(stream);
    if (size < 0) {
        (void)SDL_CloseIO(stream);
        return std::unexpected(Error{ErrorCode::IoRead, SdlError("Failed to inspect graph file")});
    }
    if (static_cast<std::uint64_t>(size) > limits.max_bytes) {
        (void)SDL_CloseIO(stream);
        return std::unexpected(Error{ErrorCode::SizeLimitExceeded, "Graph file exceeds max_bytes"});
    }
    std::string bytes(static_cast<std::size_t>(size), '\0');
    const bool read = bytes.empty() || SDL_ReadIO(stream, bytes.data(), bytes.size()) == bytes.size();
    const bool closed = SDL_CloseIO(stream);
    if (!read || !closed)
        return std::unexpected(Error{ErrorCode::IoRead, SdlError("Failed to read graph file")});
    return bytes;
}

} // namespace

namespace Detail {

bool ValidOpaqueJsonProperty(const std::string_view json) noexcept {
    try {
        Json parsed = ParseJson(json, OpaqueLimits(json));
        const auto& object = AsObject(parsed, "opaque property");
        const auto& kind = AsString(Field(object, "kind", "opaque property"), "opaque property.kind");
        if (KnownPropertyKind(kind))
            return false;
        std::string canonical = DumpJson(parsed);
        canonical.pop_back();
        return canonical == json;
    } catch (...) {
        return false;
    }
}

struct GraphIoAccess final {
    [[nodiscard]] static Result<void> Import(GraphDocument& document, GraphArchive& archive) {
        return document.Import(
            archive.schema_version,
            archive.root_graph,
            std::move(archive.graphs),
            std::move(archive.intergraph_links));
    }

    [[nodiscard]] static Result<void> Import(GraphPresentation& presentation, const GraphDocument& document,
                                             GraphArchive& archive) {
        return presentation.Import(document, std::move(archive.nodes), std::move(archive.links),
                                   std::move(archive.groups));
    }
};

} // namespace Detail

struct DocumentMigrationRegistry::Impl final {
    std::uint32_t target_version{1};
    std::map<std::uint32_t, DocumentMigrationFn> migrations;
    std::size_t invocation_depth{0};
};

DocumentMigrationRegistry::DocumentMigrationRegistry(const std::uint32_t target_version)
    : m_impl(std::make_shared<Impl>()) {
    m_impl->target_version = target_version == 0 ? 1 : target_version;
}

DocumentMigrationRegistry::~DocumentMigrationRegistry() = default;

DocumentMigrationRegistry::DocumentMigrationRegistry(DocumentMigrationRegistry&& other)
    : m_impl(std::move(other.m_impl)) {
    other.m_impl = std::make_shared<Impl>();
}

DocumentMigrationRegistry& DocumentMigrationRegistry::operator=(DocumentMigrationRegistry&& other) {
    if (this != &other) {
        auto replacement = std::make_shared<Impl>();
        m_impl = std::move(other.m_impl);
        other.m_impl = std::move(replacement);
    }
    return *this;
}

std::uint32_t DocumentMigrationRegistry::TargetVersion() const noexcept { return m_impl->target_version; }

Result<void> DocumentMigrationRegistry::Register(const std::uint32_t from_version, DocumentMigrationFn migration) {
    if (m_impl->invocation_depth != 0) {
        return std::unexpected(Error{
            ErrorCode::CommandFailed,
            "Document migration registry cannot be mutated while migrations are running"});
    }
    if (from_version == 0 || !migration) {
        return std::unexpected(
            Error{ErrorCode::InvalidArgument, "Document migration requires a source version and callback"});
    }
    if (!m_impl->migrations.emplace(from_version, std::move(migration)).second) {
        return std::unexpected(Error{ErrorCode::DuplicateId, "Document migration step is already registered"});
    }
    return {};
}

Result<void> DocumentMigrationRegistry::Migrate(GraphArchive& archive) const {
    const std::shared_ptr<Impl> impl = m_impl;
    struct InvocationGuard final {
        std::shared_ptr<Impl> impl;
        ~InvocationGuard() { --impl->invocation_depth; }
    };
    ++impl->invocation_depth;
    const InvocationGuard invocation{impl};
    if (archive.schema_version == 0) {
        return std::unexpected(Error{ErrorCode::InvalidFormat, "Document schema version cannot be zero"});
    }
    if (archive.schema_version > impl->target_version) {
        return std::unexpected(
            Error{ErrorCode::UnsupportedVersion, "Document schema is newer than the migration target"});
    }
    while (archive.schema_version < impl->target_version) {
        const auto step = impl->migrations.find(archive.schema_version);
        if (step == impl->migrations.end()) {
            return std::unexpected(Error{
                ErrorCode::MigrationMissing,
                "Missing document migration from version " + std::to_string(archive.schema_version),
            });
        }
        const std::uint32_t from = archive.schema_version;
        DocumentMigrationContext context{.from_version = from, .to_version = from + 1, .archive = archive};
        const DocumentMigrationFn migrate = step->second;
        try {
            auto migrated = migrate(context);
            if (!migrated) {
                return std::unexpected(Error{ErrorCode::MigrationFailed, migrated.error().message});
            }
        } catch (const std::exception& exception) {
            return std::unexpected(
                Error{ErrorCode::MigrationFailed, std::string("Document migration failed: ") + exception.what()});
        } catch (...) {
            return std::unexpected(
                Error{ErrorCode::MigrationFailed, "Document migration failed with an unknown exception"});
        }
        archive.schema_version = from + 1;
    }
    return {};
}

Result<std::string> SerializeGraphDocumentJson(const GraphDocument& document, const GraphPresentation& presentation,
                                               const GraphIoLimits& limits) {
    try {
        std::string output = DumpJson(EncodeDocumentEnvelope(document, presentation));
        DecodeCounts counts{limits};
        (void)DecodeDocumentArchive(ParseJson(output, limits), counts);
        return output;
    } catch (const JsonFailure& failure) {
        return std::unexpected(failure.error);
    }
}

Result<LoadedGraphDocument> DeserializeGraphDocumentJson(const std::string_view json, const RegistryCatalog& registry,
                                                         const DocumentMigrationRegistry* migrations,
                                                         const GraphIoLimits& limits) {
    try {
        const auto invocation = Detail::RegistryAccess::Invoke(registry);
        const RegistrySnapshot& snapshot = invocation.Snapshot();
        DecodeCounts counts{limits};
        GraphArchive archive = DecodeDocumentArchive(ParseJson(json, limits), counts);
        if (migrations != nullptr) {
            auto migrated = migrations->Migrate(archive);
            if (!migrated)
                return std::unexpected(migrated.error());
        } else if (archive.schema_version != 1) {
            return std::unexpected(Error{
                ErrorCode::UnsupportedVersion,
                "Document schema requires an explicit migration registry",
            });
        }
        LoadedGraphDocument loaded;
        MigrateArchiveNodes(archive, snapshot, loaded.warnings);
        WarnExternalGraphAssets(archive, loaded.warnings);
        if (auto imported = Detail::GraphIoAccess::Import(loaded.document, archive); !imported) {
            return std::unexpected(imported.error());
        }
        if (auto imported = Detail::GraphIoAccess::Import(loaded.presentation, loaded.document, archive); !imported) {
            return std::unexpected(imported.error());
        }
        if (auto validated = SerializeGraphDocumentJson(loaded.document, loaded.presentation, limits); !validated) {
            return std::unexpected(validated.error());
        }
        return loaded;
    } catch (const JsonFailure& failure) {
        return std::unexpected(failure.error);
    }
}

Result<void> SaveGraphDocumentJson(const std::string_view path, const GraphDocument& document,
                                   const GraphPresentation& presentation, const GraphIoLimits& limits) {
    auto serialized = SerializeGraphDocumentJson(document, presentation, limits);
    if (!serialized)
        return std::unexpected(serialized.error());
    return WriteAtomic(path, *serialized);
}

Result<LoadedGraphDocument> LoadGraphDocumentJson(const std::string_view path, const RegistryCatalog& registry,
                                                  const DocumentMigrationRegistry* migrations,
                                                  const GraphIoLimits& limits) {
    auto bytes = ReadFile(path, limits);
    if (!bytes)
        return std::unexpected(bytes.error());
    return DeserializeGraphDocumentJson(*bytes, registry, migrations, limits);
}

Result<std::string> SerializeGraphFragmentJson(const GraphFragment& fragment, const GraphIoLimits& limits) {
    try {
        ValidateFragment(fragment);
        std::string output = DumpJson(EncodeFragmentEnvelope(fragment));
        DecodeCounts counts{limits};
        (void)DecodeFragment(ParseJson(output, limits), counts);
        return output;
    } catch (const JsonFailure& failure) {
        return std::unexpected(failure.error);
    }
}

Result<GraphFragment> DeserializeGraphFragmentJson(const std::string_view json, const RegistryCatalog& registry,
                                                   const GraphIoLimits& limits) {
    try {
        const auto invocation = Detail::RegistryAccess::Invoke(registry);
        const RegistrySnapshot& snapshot = invocation.Snapshot();
        DecodeCounts counts{limits};
        GraphFragment fragment = DecodeFragment(ParseJson(json, limits), counts);
        ValidateFragment(fragment);
        MigrateFragmentNodes(fragment, snapshot);
        ValidateFragment(fragment);
        if (auto validated = SerializeGraphFragmentJson(fragment, limits); !validated) {
            return std::unexpected(validated.error());
        }
        return fragment;
    } catch (const JsonFailure& failure) {
        return std::unexpected(failure.error);
    } catch (const std::exception& exception) {
        return std::unexpected(Error{
            ErrorCode::InvalidGraph,
            std::string{"Graph fragment processing failed: "} + exception.what(),
        });
    }
}

Result<std::string> SerializeGraphAssetJson(const GraphAsset& asset, const GraphIoLimits& limits) {
    try {
        if (asset.id.Empty() || !ValidUtf8(asset.id.Value())) {
            return std::unexpected(Error{ErrorCode::InvalidArgument, "Graph asset metadata is invalid"});
        }
        if (auto valid = asset.document.ValidateStructure(); !valid) {
            return std::unexpected(valid.error());
        }
        if (auto valid = ValidateGraphPresentation(asset.document, asset.presentation); !valid) {
            return std::unexpected(valid.error());
        }
        std::string output = DumpJson(EncodeGraphAssetEnvelope(asset));
        DecodeCounts counts{limits};
        (void)DecodeGraphAssetArchive(ParseJson(output, limits), counts);
        return output;
    } catch (const JsonFailure& failure) {
        return std::unexpected(failure.error);
    }
}

Result<LoadedGraphAsset> DeserializeGraphAssetJson(
    const std::string_view json,
    const RegistryCatalog& registry,
    const DocumentMigrationRegistry* migrations,
    const GraphIoLimits& limits) {
    try {
        const auto invocation = Detail::RegistryAccess::Invoke(registry);
        const RegistrySnapshot& snapshot = invocation.Snapshot();
        DecodeCounts counts{limits};
        DecodedGraphAsset decoded = DecodeGraphAssetArchive(ParseJson(json, limits), counts);
        if (decoded.id.Empty()) {
            return std::unexpected(Error{ErrorCode::InvalidFormat, "Graph asset metadata is invalid"});
        }
        if (migrations != nullptr) {
            auto migrated = migrations->Migrate(decoded.archive);
            if (!migrated) return std::unexpected(migrated.error());
        } else if (decoded.archive.schema_version != 1) {
            return std::unexpected(Error{
                ErrorCode::UnsupportedVersion,
                "Graph asset schema requires an explicit migration registry",
            });
        }
        LoadedGraphAsset loaded;
        loaded.asset.id = decoded.id;
        MigrateArchiveNodes(decoded.archive, snapshot, loaded.warnings);
        WarnExternalGraphAssets(decoded.archive, loaded.warnings);
        if (auto imported = Detail::GraphIoAccess::Import(loaded.asset.document, decoded.archive); !imported) {
            return std::unexpected(imported.error());
        }
        if (auto imported = Detail::GraphIoAccess::Import(
                loaded.asset.presentation, loaded.asset.document, decoded.archive); !imported) {
            return std::unexpected(imported.error());
        }
        if (auto validated = SerializeGraphAssetJson(loaded.asset, limits); !validated) {
            return std::unexpected(validated.error());
        }
        return loaded;
    } catch (const JsonFailure& failure) {
        return std::unexpected(failure.error);
    }
}

Result<void> SaveGraphAssetJson(
    const std::string_view path,
    const GraphAsset& asset,
    const GraphIoLimits& limits) {
    auto serialized = SerializeGraphAssetJson(asset, limits);
    if (!serialized) return std::unexpected(serialized.error());
    return WriteAtomic(path, *serialized);
}

Result<LoadedGraphAsset> LoadGraphAssetJson(
    const std::string_view path,
    const RegistryCatalog& registry,
    const DocumentMigrationRegistry* migrations,
    const GraphIoLimits& limits) {
    auto bytes = ReadFile(path, limits);
    if (!bytes) return std::unexpected(bytes.error());
    return DeserializeGraphAssetJson(*bytes, registry, migrations, limits);
}

} // namespace Uni::GUI::Nodes
