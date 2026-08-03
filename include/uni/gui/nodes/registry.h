#pragma once

#include <uni/gui/export.h>
#include <uni/gui/nodes/graph.h>
#include <uni/gui/nodes/policy.h>

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Uni::GUI::Nodes {

namespace Detail {
class RegistryAccess;
struct RegistryDependencyRecorder;
struct RegistryOwner;
struct RegistryState;
} // namespace Detail

struct NodeTypeDescriptor;
using NodeTypeDescriptorPtr = std::shared_ptr<const NodeTypeDescriptor>;

struct PinDescriptor final {
  std::string key;
  std::string label;
  TypeId type;
  PinDirection direction{PinDirection::Input};
  PinKind kind{PinKind::Data};
  PinCardinality cardinality{PinCardinality::Single};

  bool operator==(const PinDescriptor &) const = default;
};

using ValidateNodeFn = std::function<std::vector<std::string>(
    const NodeInstance &, std::span<const PinInstance>)>;

struct NodeCreation final {
  NodeInstance node;
  std::vector<PinInstance> pins;
  NodeTypeDescriptorPtr prepared_descriptor;
};

struct NodeMigrationContext final {
  std::uint32_t from_version{0};
  std::uint32_t to_version{0};
  NodeCreation &creation;
  std::function<PinId()> allocate_pin_id;
  std::function<void(PinId, PinId)> remap_links;
  std::function<void(PinId)> remove_links;
};

using MigrateNodeFn = std::function<Result<void>(NodeMigrationContext &)>;

struct NodeBehavior final {
  MigrateNodeFn migrate;
  ValidateNodeFn validate;
};

using NodeBehaviorPtr = std::shared_ptr<const NodeBehavior>;

using TypeCompatibilityFn = std::function<bool(
    const TypeId &output, const TypeId &input, PinKind kind)>;

enum class PropertyImpact : std::uint8_t {
  RuntimeOnly,
  Rendering,
  Geometry,
  Topology,
};

struct NodeTypeDescriptor final {
  TypeId type;
  std::string display_name;
  std::string category;
  std::uint32_t version{1};
  std::vector<PinDescriptor> static_pins;
  PropertyBag default_properties;
  std::map<std::string, PropertyImpact> property_impacts;
  PropertyImpact undeclared_property_impact{PropertyImpact::Geometry};
  NodeBehaviorPtr behavior;

  bool operator==(const NodeTypeDescriptor &) const = default;
};

struct ConversionKey final {
  TypeId source_type;
  TypeId destination_type;
  PinKind kind{PinKind::Data};

  bool operator==(const ConversionKey &) const = default;
  [[nodiscard]] bool operator<(const ConversionKey &other) const noexcept {
    if (source_type != other.source_type)
      return source_type < other.source_type;
    if (destination_type != other.destination_type)
      return destination_type < other.destination_type;
    return kind < other.kind;
  }
};

struct ConversionDescriptor final {
  ConversionKey key;
  TypeId node_type;
  std::string input_pin;
  std::string output_pin;

  bool operator==(const ConversionDescriptor &) const = default;
};

class RegistryCatalog;
class RegistrySnapshot;
class RegistryUpdate;

class UNI_GUI_EXPORT ConversionRegistrationToken final {
public:
  ConversionRegistrationToken() = default;
  [[nodiscard]] explicit operator bool() const noexcept;
  bool operator==(const ConversionRegistrationToken &) const = default;

private:
  ConversionRegistrationToken(std::shared_ptr<const void> catalog_identity,
                              std::uint64_t registration_id) noexcept;

  std::shared_ptr<const void> m_catalog_identity;
  std::uint64_t m_registration_id{0};

  friend class ConversionRecipe;
  friend class RegistryCatalog;
  friend class RegistrySnapshot;
  friend class RegistryUpdate;
};

class UNI_GUI_EXPORT ConversionRecipe final {
public:
  ConversionRecipe() = default;
  ConversionRecipe(const ConversionRecipe &) noexcept = default;
  ConversionRecipe &operator=(const ConversionRecipe &) noexcept = default;
  ConversionRecipe(ConversionRecipe &&) noexcept = default;
  ConversionRecipe &operator=(ConversionRecipe &&) noexcept = default;
  ~ConversionRecipe() = default;

  [[nodiscard]] explicit operator bool() const noexcept;
  [[nodiscard]] const ConversionDescriptor &Descriptor() const noexcept;
  [[nodiscard]] ConversionRegistrationToken Registration() const noexcept;
  [[nodiscard]] Result<NodeCreation> Instantiate(GraphDocument &document) const;
  [[nodiscard]] bool Matches(const NodeCreation &creation) const noexcept;
  [[nodiscard]] bool operator==(const ConversionRecipe &other) const noexcept;

private:
  struct State;
  explicit ConversionRecipe(std::shared_ptr<const State> state) noexcept;

  std::shared_ptr<const State> m_state;

  friend class RegistryCatalog;
  friend class RegistrySnapshot;
  friend class RegistryUpdate;
};

struct ConnectionResult final {
  enum class Status {
    Allowed,
    RequiresConversion,
    Rejected,
  };

  Status status{Status::Rejected};
  std::string reason;
  std::optional<ConversionRecipe> recipe;
  ErrorCode error{ErrorCode::IncompatiblePins};
};

struct ConnectionRequest final {
  GraphId graph;
  PinId first;
  PinId second;
  LinkId replacing;
};

class UNI_GUI_EXPORT RegistrySnapshot final {
public:
  RegistrySnapshot() = default;

  [[nodiscard]] NodeTypeDescriptorPtr Find(const TypeId &type) const noexcept;
  [[nodiscard]] PropertyImpact
  ResolvePropertyImpact(const TypeId &type,
                        std::string_view key) const noexcept;
  [[nodiscard]] std::vector<NodeTypeDescriptorPtr> Descriptors() const;
  [[nodiscard]] Result<NodeCreation>
  Instantiate(GraphDocument &document, const TypeId &type,
              std::string_view display_name = {}) const;
  [[nodiscard]] ConnectionResult Check(const TypeId &output,
                                       const TypeId &input, PinKind kind) const;
  [[nodiscard]] Result<void>
  ValidateRecipe(const ConversionRecipe &recipe) const;
  [[nodiscard]] std::uint64_t NodeRevision() const noexcept;
  [[nodiscard]] std::uint64_t ConversionRevision() const noexcept;
  [[nodiscard]] std::uint64_t Generation() const noexcept;

private:
  RegistrySnapshot(
      std::shared_ptr<const Detail::RegistryState> state,
      std::shared_ptr<const void> catalog_identity,
      std::shared_ptr<Detail::RegistryDependencyRecorder> dependencies);

  std::shared_ptr<const Detail::RegistryState> m_state;
  std::shared_ptr<const void> m_catalog_identity;
  std::shared_ptr<Detail::RegistryDependencyRecorder> m_dependencies;

  friend class RegistryCatalog;
  friend class Detail::RegistryAccess;
};

struct RegistryUpdateStatistics final {
  std::uint64_t path_copies{0};
  std::uint64_t touched_records{0};
  std::uint64_t no_op_records{0};
  std::uint64_t recipes_built{0};
  std::uint64_t published_generations{0};
};

struct RegistryUpdateResult final {
  std::uint64_t generation{0};
  std::uint64_t node_revision{0};
  std::uint64_t conversion_revision{0};
  std::vector<ConversionRegistrationToken> registrations;
  RegistryUpdateStatistics statistics;
};

class UNI_GUI_EXPORT RegistryUpdate final {
public:
  ~RegistryUpdate();
  RegistryUpdate(RegistryUpdate &&other) noexcept;
  RegistryUpdate &operator=(RegistryUpdate &&other) noexcept;
  RegistryUpdate(const RegistryUpdate &) = delete;
  RegistryUpdate &operator=(const RegistryUpdate &) = delete;

  [[nodiscard]] Result<void> RegisterNodeType(NodeTypeDescriptor descriptor);
  [[nodiscard]] Result<void> ReplaceNodeType(NodeTypeDescriptor descriptor);
  [[nodiscard]] Result<void> UnregisterNodeType(TypeId type);
  [[nodiscard]] Result<void>
  RegisterConversion(ConversionDescriptor descriptor);
  [[nodiscard]] Result<void>
  ReplaceConversion(ConversionRegistrationToken registration,
                    ConversionDescriptor descriptor);
  [[nodiscard]] Result<void>
  UnregisterConversion(ConversionRegistrationToken registration);
  [[nodiscard]] Result<void>
  SetTypeCompatibility(TypeCompatibilityFn compatibility);
  [[nodiscard]] Result<void> ClearTypeCompatibility();
  [[nodiscard]] Result<RegistryUpdateResult> Commit();

private:
  RegistryUpdate(std::shared_ptr<Detail::RegistryOwner> owner,
                 std::shared_ptr<const Detail::RegistryState> base);

  struct Impl;
  std::unique_ptr<Impl> m_impl;

  friend class RegistryCatalog;
};

class UNI_GUI_EXPORT RegistryCatalog final {
public:
  RegistryCatalog();
  ~RegistryCatalog();
  RegistryCatalog(RegistryCatalog &&other);
  RegistryCatalog &operator=(RegistryCatalog &&other);
  RegistryCatalog(const RegistryCatalog &) = delete;
  RegistryCatalog &operator=(const RegistryCatalog &) = delete;

  [[nodiscard]] Result<void> RegisterNodeType(NodeTypeDescriptor descriptor);
  [[nodiscard]] Result<void> ReplaceNodeType(NodeTypeDescriptor descriptor);
  [[nodiscard]] Result<void>
  SetTypeCompatibility(TypeCompatibilityFn compatibility);
  [[nodiscard]] Result<void> ClearTypeCompatibility();
  [[nodiscard]] Result<bool> UnregisterNodeType(const TypeId &type);
  [[nodiscard]] Result<ConversionRegistrationToken>
  RegisterConversion(ConversionDescriptor descriptor);
  [[nodiscard]] Result<bool>
  UnregisterConversion(ConversionRegistrationToken registration);
  [[nodiscard]] Result<void>
  ReplaceConversion(ConversionRegistrationToken registration,
                    ConversionDescriptor descriptor);
  [[nodiscard]] bool
  HasConversionsForNodeType(const TypeId &node_type) const noexcept;
  [[nodiscard]] std::vector<ConversionRegistrationToken>
  RegistrationsForNodeType(const TypeId &node_type) const;

  [[nodiscard]] NodeTypeDescriptorPtr Find(const TypeId &type) const noexcept;
  [[nodiscard]] PropertyImpact
  ResolvePropertyImpact(const TypeId &type,
                        std::string_view key) const noexcept;
  [[nodiscard]] Result<NodeCreation>
  Instantiate(GraphDocument &document, const TypeId &type,
              std::string_view display_name = {}) const;
  [[nodiscard]] ConnectionResult Check(const TypeId &output,
                                       const TypeId &input, PinKind kind) const;
  [[nodiscard]] RegistrySnapshot Snapshot() const;
  [[nodiscard]] std::uint64_t NodeRevision() const noexcept;
  [[nodiscard]] std::uint64_t ConversionRevision() const noexcept;
  [[nodiscard]] std::uint64_t Generation() const noexcept;
  [[nodiscard]] Result<RegistryUpdate> BeginUpdate();

private:
  struct Impl;
  std::shared_ptr<Impl> m_impl;

  friend class Detail::RegistryAccess;
};

enum class ValidationSeverity {
  Warning,
  Error,
};

struct ValidationIssue final {
  ValidationSeverity severity{ValidationSeverity::Error};
  std::string message;
  GraphId graph;
  NodeId node;
  PinId pin;
  LinkId link;
  IntergraphLinkId intergraph_link;
};

[[nodiscard]] UNI_GUI_EXPORT ConnectionResult ValidateConnection(
    const GraphDocument &document, const GraphPresentation &presentation,
    const ConnectionRequest &request, const RegistryCatalog &registry,
    const GraphPolicy &policy = {},
    std::span<const PinInstance> pending_pins = {},
    std::span<const NodeInstance> pending_nodes = {});

[[nodiscard]] UNI_GUI_EXPORT std::vector<ValidationIssue>
ValidateGraph(const GraphDocument &document, GraphId graph,
              const RegistryCatalog &registry);

} // namespace Uni::GUI::Nodes
