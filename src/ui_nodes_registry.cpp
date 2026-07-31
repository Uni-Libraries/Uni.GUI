#include <uni/gui/nodes/registry.h>
#include <uni/gui/nodes/snapshot.h>

#include "ui_nodes_internal.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>
#include <set>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace Uni::GUI::Nodes {
namespace {

[[nodiscard]] Error MakeError(const ErrorCode code, std::string message) {
  return Error{code, std::move(message)};
}

[[nodiscard]] bool ValidDirection(const PinDirection direction) noexcept {
  return direction == PinDirection::Input || direction == PinDirection::Output;
}

[[nodiscard]] bool ValidKind(const PinKind kind) noexcept {
  return kind == PinKind::Data || kind == PinKind::Execution;
}

[[nodiscard]] bool ValidCardinality(const PinCardinality cardinality) noexcept {
  return cardinality == PinCardinality::Single ||
         cardinality == PinCardinality::Multiple;
}

[[nodiscard]] bool ValidStorage(const PinStorage storage) noexcept {
  return storage == PinStorage::Static || storage == PinStorage::Dynamic;
}

[[nodiscard]] bool ValidPropertyImpact(const PropertyImpact impact) noexcept {
  return impact == PropertyImpact::RuntimeOnly ||
         impact == PropertyImpact::Rendering ||
         impact == PropertyImpact::Geometry ||
         impact == PropertyImpact::Topology;
}

[[nodiscard]] bool ValidPropertyValue(const PropertyValue &value) noexcept {
  return std::visit(
      [](const auto &property) noexcept {
        using Value = std::decay_t<decltype(property)>;
        if constexpr (std::is_same_v<Value, double>) {
          return std::isfinite(property);
        } else if constexpr (std::is_same_v<Value, Vec2>) {
          return std::isfinite(property.x) && std::isfinite(property.y);
        } else if constexpr (std::is_same_v<Value, OpaqueJsonProperty>) {
          return Detail::ValidOpaqueJsonProperty(property.canonical_json);
        } else {
          return true;
        }
      },
      value);
}

[[nodiscard]] bool ValidProperties(const PropertyBag &properties) noexcept {
  return std::ranges::all_of(properties, [](const auto &entry) {
    return !entry.first.empty() && ValidPropertyValue(entry.second);
  });
}

[[nodiscard]] Result<void>
NormalizeNodeDescriptor(NodeTypeDescriptor &descriptor) {
  if (descriptor.type.Empty() || descriptor.display_name.empty() ||
      descriptor.version == 0 ||
      !ValidProperties(descriptor.default_properties) ||
      !ValidPropertyImpact(descriptor.undeclared_property_impact) ||
      !std::ranges::all_of(descriptor.property_impacts, [](const auto &entry) {
        return !entry.first.empty() && ValidPropertyImpact(entry.second);
      })) {
    return std::unexpected(MakeError(ErrorCode::InvalidArgument,
                                     "Node descriptor requires a stable type, "
                                     "display name, and non-zero version"));
  }
  std::unordered_set<std::string> keys;
  for (auto &pin : descriptor.static_pins) {
    if (pin.key.empty() || pin.type.Empty() || !ValidDirection(pin.direction) ||
        !ValidKind(pin.kind) || !ValidCardinality(pin.cardinality)) {
      return std::unexpected(
          MakeError(ErrorCode::InvalidArgument,
                    "Node descriptor contains an invalid pin"));
    }
    if (!keys.insert(pin.key).second) {
      return std::unexpected(
          MakeError(ErrorCode::DuplicateId,
                    "Node descriptor contains duplicate pin semantic keys"));
    }
    if (pin.label.empty())
      pin.label = pin.key;
  }
  return {};
}

[[nodiscard]] ConnectionResult
Rejected(std::string reason,
         const ErrorCode error = ErrorCode::IncompatiblePins) {
  return ConnectionResult{ConnectionResult::Status::Rejected, std::move(reason),
                          std::nullopt, error};
}

void AddIssue(std::vector<ValidationIssue> &issues,
              const ValidationSeverity severity, std::string message,
              const GraphId graph, const NodeId node = {}, const PinId pin = {},
              const LinkId link = {}) {
  issues.push_back(ValidationIssue{
      .severity = severity,
      .message = std::move(message),
      .graph = graph,
      .node = node,
      .pin = pin,
      .link = link,
  });
}

} // namespace
} // namespace Uni::GUI::Nodes

#include "ui_nodes_registry_core.h"
#include "ui_nodes_registry_validation.h"
