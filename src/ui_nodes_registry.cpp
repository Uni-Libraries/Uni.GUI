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
  return ValidatePropertyValue(value).has_value();
}

[[nodiscard]] bool ValidProperties(const PropertyBag &properties) noexcept {
  return ValidatePropertyBag(properties).has_value();
}

[[nodiscard]] Result<void> NormalizePinDescriptors(std::vector<PinDescriptor>& pins) {
    std::unordered_set<std::string> keys;
    for (auto& pin : pins) {
        if (pin.key.empty() || pin.type.Empty() || !ValidDirection(pin.direction) || !ValidKind(pin.kind) || !ValidCardinality(pin.cardinality)) {
            return std::unexpected(MakeError(ErrorCode::InvalidArgument, "Node descriptor projection contains an invalid pin"));
        }
        if (!keys.insert(pin.key).second) {
            return std::unexpected(MakeError(ErrorCode::DuplicateId, "Node descriptor projection contains duplicate pin semantic keys"));
        }
        if (pin.label.empty())
            pin.label = pin.key;
    }
    return {};
}

[[nodiscard]] Result<std::vector<PinDescriptor>> ResolveDescriptorPinSchema(const NodeTypeDescriptor& descriptor,
                                                                            const PropertyBag& properties) {
    std::vector<PinDescriptor> pins;
    if (descriptor.pin_schema.IsConfigurable()) {
        const auto& configurable = descriptor.pin_schema.Configurable();
        if (!configurable->resolve) {
            return std::unexpected(MakeError(ErrorCode::InvalidArgument,
                                             "Configurable pin schema requires a resolver"));
        }
        try {
            auto resolved = configurable->resolve(properties);
            if (!resolved)
                return std::unexpected(std::move(resolved.error()));
            pins = std::move(*resolved);
        } catch (const std::exception& exception) {
            return std::unexpected(MakeError(ErrorCode::CommandFailed,
                                             std::string{"Node pin schema resolution failed: "} + exception.what()));
        } catch (...) {
            return std::unexpected(MakeError(ErrorCode::CommandFailed,
                                             "Node pin schema resolution failed with an unknown exception"));
        }
    } else {
        pins = descriptor.pin_schema.FixedPins();
    }
    if (auto valid = NormalizePinDescriptors(pins); !valid)
        return std::unexpected(std::move(valid.error()));
    return pins;
}

[[nodiscard]] Result<std::vector<PinDescriptor>>
NormalizeNodeDescriptor(NodeTypeDescriptor &descriptor) {
  if (descriptor.type.Empty() || descriptor.display_name.empty() ||
      descriptor.version == 0 ||
      !ValidProperties(descriptor.default_properties) ||
      !ValidPropertyImpact(descriptor.undeclared_property_impact) ||
      !std::ranges::all_of(descriptor.property_impacts, [](const auto &entry) {
        return entry.first.Root() != nullptr && !entry.first.Root()->empty() && ValidPropertyImpact(entry.second);
      })) {
    return std::unexpected(MakeError(ErrorCode::InvalidArgument,
                                     "Node descriptor requires a stable type, "
                                     "display name, and non-zero version"));
  }
  if (!descriptor.pin_schema.IsConfigurable()) {
      if (auto valid = NormalizePinDescriptors(descriptor.pin_schema.FixedPins()); !valid)
          return std::unexpected(std::move(valid.error()));
  } else {
      const auto& configurable = descriptor.pin_schema.Configurable();
      if (!configurable->resolve) {
          return std::unexpected(MakeError(ErrorCode::InvalidArgument,
                                           "Configurable pin schema requires a resolver"));
      }
      std::set<PropertyPath> dependencies;
      for (const PropertyPath& property : configurable->property_dependencies) {
          if (property.Root() == nullptr || property.Root()->empty()) {
              return std::unexpected(MakeError(ErrorCode::InvalidArgument,
                                               "Pin schema property dependency cannot be empty"));
          }
          if (!dependencies.insert(property).second) {
              return std::unexpected(MakeError(ErrorCode::DuplicateId,
                                               "Pin schema contains duplicate property dependencies"));
          }
      }
  }
  return ResolveDescriptorPinSchema(descriptor, descriptor.default_properties);
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
