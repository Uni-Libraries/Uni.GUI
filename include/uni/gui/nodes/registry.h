#pragma once

/// @file
/// @brief Versioned node schemas, converter recipes, and registry-aware graph validation.

#include <uni/gui/export.h>
#include <uni/gui/nodes/graph.h>
#include <uni/gui/nodes/policy.h>

#include <cstdint>
#include <functional>
#include <initializer_list>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Uni::GUI::Nodes {

namespace Detail {
class RegistryAccess;
struct RegistryDependencyRecorder;
struct RegistryOwner;
struct RegistryState;
} // namespace Detail

struct NodeTypeDescriptor;
/// Owns an immutable descriptor; survives catalog mutation, move and destruction.
using NodeTypeDescriptorPtr = std::shared_ptr<const NodeTypeDescriptor>;

/// Descriptor-owned pin in schema order. key must be non-empty and unique on the node, type
/// non-empty. Registration/resolution substitutes the key when the display label is empty.
struct PinDescriptor final {
  std::string key;
  std::string label;
  TypeId type;
  PinDirection direction{PinDirection::Input};
  PinKind kind{PinKind::Data};
  PinCardinality cardinality{PinCardinality::Single};

  bool operator==(const PinDescriptor &) const = default;
};

/// Inspect the borrowed node and pins synchronously; return messages for violations, empty for valid.
using ValidateNodeFn = std::function<std::vector<std::string>(
    const NodeInstance &, std::span<const PinInstance>)>;

/// Prepared node and its pins, not yet inserted. Pin IDs/order must agree with node.pins.
/// prepared_descriptor retains provenance when prepared by the node registry; null is possible
/// for manually constructed or unregistered nodes.
struct NodeCreation final {
  NodeInstance node;
  std::vector<PinInstance> pins;
  NodeTypeDescriptorPtr prepared_descriptor;
};

/// Temporary state for one version step. Preserve node ID and type, keep the pin records and ordered
/// IDs consistent and use allocate_pin_id (zero on exhaustion), remap_links(old,new) or remove_links(pin)
/// when changing endpoints. The context, creation reference and helper functions are invocation-only.
struct NodeMigrationContext final {
  std::uint32_t from_version{0};
  std::uint32_t to_version{0};
  NodeCreation &creation;
  std::function<PinId()> allocate_pin_id;
  std::function<void(PinId, PinId)> remap_links;
  std::function<void(PinId)> remove_links;
};

/// Synchronous migration of one instance; a failed result or exception aborts import as MigrationFailed.
using MigrateNodeFn = std::function<Result<void>(NodeMigrationContext &)>;
/// Pure resolver of descriptor-owned pins for one configuration. Return an ordered schema or an error.
using ResolvePinSchemaFn =
    std::function<Result<std::vector<PinDescriptor>>(const PropertyBag &)>;

/// Declare every property path read by resolve; dependency paths must be unique and rooted at
/// non-empty keys. Resolver output must be deterministic for the bag and immutable captured state.
/// Do not retain arguments or mutate live owners in a callback. Exceptions become CommandFailed.
struct ConfigurablePinSchema final {
  std::vector<PropertyPath> property_dependencies;
  ResolvePinSchemaFn resolve;
};

using ConfigurablePinSchemaPtr = std::shared_ptr<const ConfigurablePinSchema>;

/// Either an ordered fixed schema (empty by default) or a shared configurable schema. FixedPins()
/// borrows the descriptor's fixed vector, not the resolved projection of a configurable schema.
/// Configurable() borrows the handle; copy it to retain ownership.
class NodePinSchema final {
public:
  NodePinSchema() = default;
  NodePinSchema(std::initializer_list<PinDescriptor> pins) : m_fixed{pins} {}
  NodePinSchema(std::vector<PinDescriptor> pins)
      : m_fixed{std::move(pins)} {}
  NodePinSchema(std::vector<PropertyPath> property_dependencies,
                ResolvePinSchemaFn resolve)
      : m_configurable{std::make_shared<const ConfigurablePinSchema>(
            ConfigurablePinSchema{std::move(property_dependencies),
                                  std::move(resolve)})} {}
  NodePinSchema(ConfigurablePinSchemaPtr configurable)
      : m_configurable{std::move(configurable)} {}

  [[nodiscard]] bool IsConfigurable() const noexcept {
    return static_cast<bool>(m_configurable);
  }
  [[nodiscard]] const std::vector<PinDescriptor> &FixedPins() const noexcept {
    return m_fixed;
  }
  [[nodiscard]] std::vector<PinDescriptor> &FixedPins() noexcept {
    return m_fixed;
  }
  [[nodiscard]] const ConfigurablePinSchemaPtr &Configurable() const noexcept {
    return m_configurable;
  }

  bool operator==(const NodePinSchema &) const = default;

private:
  std::vector<PinDescriptor> m_fixed;
  ConfigurablePinSchemaPtr m_configurable;
};

/// Optional application callbacks. A shared immutable behavior handle is part of descriptor identity;
/// changing callback configuration requires publishing a new descriptor, not mutating captures.
struct NodeBehavior final {
  MigrateNodeFn migrate;
  ValidateNodeFn validate;
};

using NodeBehaviorPtr = std::shared_ptr<const NodeBehavior>;

/// Additional directional compatibility rule, called only after equality/wildcard checks fail;
/// receives output first. Return true for a direct wire, not a request to insert a converter.
using TypeCompatibilityFn = std::function<bool(
    const TypeId &output, const TypeId &input, PinKind kind)>;

/// RuntimeOnly and Rendering invalidate semantic values; Geometry additionally invalidates layout,
/// Topology additionally invalidates connectivity. Schema changes can affect these domains independently.
enum class PropertyImpact : std::uint8_t {
  RuntimeOnly,
  Rendering,
  Geometry,
  Topology,
};

/// Semantic schema, independent of node UI. type and display_name are required, version is nonzero,
/// defaults must form a valid property bag. The longest declared path prefix determines property
/// impact. Pin schema dependencies are separate. Increment version and supply a migration when the
/// meaning of persisted properties or descriptor-owned pins changes incompatibly.
struct NodeTypeDescriptor final {
  TypeId type;
  std::string display_name;
  std::string category;
  std::uint32_t version{1};
  NodePinSchema pin_schema;
  PropertyBag default_properties;
  std::map<PropertyPath, PropertyImpact> property_impacts;
  PropertyImpact undeclared_property_impact{PropertyImpact::Geometry};
  NodeBehaviorPtr behavior;

  bool operator==(const NodeTypeDescriptor &) const = default;
};

/// Empty display_name inherits the descriptor name. Overrides replace whole top-level properties
/// before schema resolution; this is not a recursive merge of nested objects.
struct NodeInstantiationOptions final {
  std::string display_name;
  PropertyBag property_overrides;
};

/// Directed source-to-destination conversion within one pin kind. Registered endpoints must be
/// distinct concrete types (non-empty, not the wildcard "*").
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

/// One explicit converter node. input_pin and output_pin are distinct semantic keys of its default
/// schema, with matching direction, source/destination type and kind; node_type must be registered.
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

/// Opaque, catalog-specific registration ID, stable across recipe replacement. A truthy token was
/// issued; this does not prove it is still registered. Destruction does not unregister the conversion.
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

/// Copyable immutable converter factory captured at lookup. Equality compares exact recipe identity,
/// not just key or token. A retained recipe survives replacement; use ValidateRecipe() on the
/// destination RegistrySnapshot before applying a prepared insertion.
class UNI_GUI_EXPORT ConversionRecipe final {
public:
  ConversionRecipe() = default;
  ConversionRecipe(const ConversionRecipe &) noexcept = default;
  ConversionRecipe &operator=(const ConversionRecipe &) noexcept = default;
  ConversionRecipe(ConversionRecipe &&) noexcept = default;
  ConversionRecipe &operator=(ConversionRecipe &&) noexcept = default;
  ~ConversionRecipe() = default;

  [[nodiscard]] explicit operator bool() const noexcept;
  /// Borrow captured metadata, or an empty descriptor for a default/moved-from recipe.
  [[nodiscard]] const ConversionDescriptor &Descriptor() const noexcept;
  /// Return the catalog-bound token, empty for an empty recipe.
  [[nodiscard]] ConversionRegistrationToken Registration() const noexcept;
  /// Reserve IDs in document and build a detached converter from captured defaults. Does not consult
  /// the live registry or insert a node. TypeNotFound for empty recipe, InvalidArgument on ID exhaustion.
  [[nodiscard]] Result<NodeCreation> Instantiate(GraphDocument &document) const;
  /// Check the supplied node, properties and ordered static pins against these recipe defaults.
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

/// Admission result, not a graph mutation. Allowed means a direct link; RequiresConversion supplies
/// an owning recipe; Rejected carries the diagnostic reason and error code (inspect status first).
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

/// Pins may be supplied in either order. A nonzero replacing names a local link to reconnect,
/// retaining at least one endpoint; its own incidence is excluded from cardinality checks.
struct ConnectionRequest final {
  GraphId graph;
  PinId first;
  PinId second;
  LinkId replacing;
};

/// Read one immutable combined catalog generation. A snapshot from RegistryCatalog::Snapshot() owns
/// its state without blocking catalog publication; command-provided snapshots additionally track
/// consulted dependencies for deferred authorization. A default snapshot contains no registrations.
class UNI_GUI_EXPORT RegistrySnapshot final {
public:
  RegistrySnapshot() = default;

  /// Return an owning descriptor handle, or null for an unknown type.
  [[nodiscard]] NodeTypeDescriptorPtr Find(const TypeId &type) const noexcept;
  /// Most specific prefix match, otherwise the descriptor's fallback; Geometry if type is unknown.
  [[nodiscard]] PropertyImpact
  ResolvePropertyImpact(const TypeId &type,
                        const PropertyPath& path) const noexcept;
  /// Resolve the supplied bag, normalize labels and validate pins. Returns TypeNotFound for a miss,
  /// resolver errors on failure, or a new ordered vector (not borrowed registry storage).
  [[nodiscard]] Result<std::vector<PinDescriptor>>
  ResolvePinSchema(const TypeId &type, const PropertyBag &properties) const;
  /// Borrow the materialized default schema without invoking a resolver. Empty for unknown or pinless
  /// types. Keep a copy of this generation alive for as long as the span is used.
  [[nodiscard]] std::span<const PinDescriptor>
  DefaultPinSchema(const TypeId &type) const noexcept;
  /// True when either the edited path or a declared dependency is a prefix of the other.
  [[nodiscard]] bool PinSchemaDependsOn(const TypeId &type,
                                        const PropertyPath& property) const noexcept;
  /// Enumerate owning handles in type-key order, not registration order.
  [[nodiscard]] std::vector<NodeTypeDescriptorPtr> Descriptors() const;
  /// Prepare an uninserted node with defaults, overrides, descriptor version and resolved static pins.
  /// Reserves node/pin IDs in document; errors (including schema/ID exhaustion) need not undo reservations.
  [[nodiscard]] Result<NodeCreation>
  Instantiate(GraphDocument &document, const TypeId &type,
              NodeInstantiationOptions options = {}) const;
  /// Test type-level compatibility only. Equal types or a wildcard on either end allow a direct link;
  /// then try the compatibility callback, exact recipe, or a unique compatible recipe when a callback
  /// exists. Invalid/empty types or kind, callback exceptions and ambiguous conversions reject.
  [[nodiscard]] ConnectionResult Check(const TypeId &output,
                                       const TypeId &input, PinKind kind) const;
  /// Check exact recipe provenance in this generation: InvalidArgument if empty, RegistryMismatch
  /// if foreign, TypeNotFound if absent, RevisionConflict if replaced.
  [[nodiscard]] Result<void>
  ValidateRecipe(const ConversionRecipe &recipe) const;
  /// @name Revisions of this capture
  /// Node and conversion counters track their domains; Generation identifies a combined publication
  /// within one catalog. A default snapshot returns zeros. Values are not comparable across catalogs.
  /// @{
  [[nodiscard]] std::uint64_t NodeRevision() const noexcept;
  [[nodiscard]] std::uint64_t ConversionRevision() const noexcept;
  [[nodiscard]] std::uint64_t Generation() const noexcept;

  /// @}

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

/// Work performed by a commit: persistent-tree copies, processed/no-op records, recipes built and
/// number of combined generations published (zero for an unchanged catalog).
struct RegistryUpdateStatistics final {
  std::uint64_t path_copies{0};
  std::uint64_t touched_records{0};
  std::uint64_t no_op_records{0};
  std::uint64_t recipes_built{0};
  std::uint64_t published_generations{0};
};

/// Published counters and tokens for newly registered conversions. Replacements retain their token.
struct RegistryUpdateResult final {
  std::uint64_t generation{0};
  std::uint64_t node_revision{0};
  std::uint64_t conversion_revision{0};
  std::vector<ConversionRegistrationToken> registrations;
  RegistryUpdateStatistics statistics;
};

/// Move-only batch created by BeginUpdate(). Staging does not publish. A staging error poisons the
/// batch, destruction discards it, and Commit() consumes it even on failure. Stage coordinated node
/// and conversion changes together so validation sees their final consistent set.
class UNI_GUI_EXPORT RegistryUpdate final {
public:
  ~RegistryUpdate();
  RegistryUpdate(RegistryUpdate &&other) noexcept;
  RegistryUpdate &operator=(RegistryUpdate &&other) noexcept;
  RegistryUpdate(const RegistryUpdate &) = delete;
  RegistryUpdate &operator=(const RegistryUpdate &) = delete;

  /// @name Stage descriptor changes
  /// Register needs a new type, replace/unregister an existing one. Default schemas are normalized
  /// while staging. Duplicate or missing registrations can still fail at commit. A removed type
  /// must not remain in use as a converter after the batch.
  /// @{
  [[nodiscard]] Result<void> RegisterNodeType(NodeTypeDescriptor descriptor);
  [[nodiscard]] Result<void> ReplaceNodeType(NodeTypeDescriptor descriptor);
  [[nodiscard]] Result<void> UnregisterNodeType(TypeId type);
  /// @}
  /// @name Stage conversion changes
  /// Supply a new descriptor for registration; replacing/removing requires the original token of
  /// this catalog. Keys must be unique in the final publication. Tokens for new records arrive at commit.
  /// @{
  [[nodiscard]] Result<void>
  RegisterConversion(ConversionDescriptor descriptor);
  [[nodiscard]] Result<void>
  ReplaceConversion(ConversionRegistrationToken registration,
                    ConversionDescriptor descriptor);
  [[nodiscard]] Result<void>
  UnregisterConversion(ConversionRegistrationToken registration);
  /// @}
  /// @name Stage the additional direct-compatibility rule
  /// An empty callable and ClearTypeCompatibility() both restore built-in rules plus registered recipes.
  /// @{
  [[nodiscard]] Result<void>
  SetTypeCompatibility(TypeCompatibilityFn compatibility);
  [[nodiscard]] Result<void> ClearTypeCompatibility();
  /// @}
  /// Publish all validated changes at once. RevisionConflict means another publication superseded
  /// the base; CommandFailed means inactive update or active catalog invocation. Validation and
  /// generation overflow propagate without partially publishing. A no-op retains the counters.
  [[nodiscard]] Result<RegistryUpdateResult> Commit();

private:
  RegistryUpdate(std::shared_ptr<Detail::RegistryOwner> owner,
                 std::shared_ptr<const Detail::RegistryState> base);

  struct Impl;
  std::unique_ptr<Impl> m_impl;

  friend class RegistryCatalog;
};

/// UI-thread owner of semantic descriptors, compatibility rules and converter registrations; not
/// serialized with graph data. Effective mutations publish a generation; mutating the logical catalog
/// during its guarded callbacks is rejected. Moving transfers catalog identity and leaves a fresh empty owner.
class UNI_GUI_EXPORT RegistryCatalog final {
public:
  RegistryCatalog();
  ~RegistryCatalog();
  RegistryCatalog(RegistryCatalog &&other);
  RegistryCatalog &operator=(RegistryCatalog &&other);
  RegistryCatalog(const RegistryCatalog &) = delete;
  RegistryCatalog &operator=(const RegistryCatalog &) = delete;

  /// @name Single-publication descriptor and compatibility edits
  /// Register requires a new type (DuplicateId on collision), replace an existing one (TypeNotFound
  /// on miss). Replacements must leave converter recipes valid. An empty compatibility rule clears
  /// the extension; built-in equality/wildcard checks remain. Unregister returns false if absent,
  /// TypeInUse if a conversion still requires the node type. Existing instances are not migrated.
  /// @{
  [[nodiscard]] Result<void> RegisterNodeType(NodeTypeDescriptor descriptor);
  [[nodiscard]] Result<void> ReplaceNodeType(NodeTypeDescriptor descriptor);
  [[nodiscard]] Result<void>
  SetTypeCompatibility(TypeCompatibilityFn compatibility);
  [[nodiscard]] Result<void> ClearTypeCompatibility();
  [[nodiscard]] Result<bool> UnregisterNodeType(const TypeId &type);
  /// @}
  /// @name Single-publication converter edits
  /// Register a unique key and receive its owning identity token. Replace retains the token and may
  /// change the key. Unregister returns false for a removed token of this catalog; empty or foreign
  /// tokens give RegistryMismatch. Removing/replacing a recipe does not rewrite existing graph nodes.
  /// @{
  [[nodiscard]] Result<ConversionRegistrationToken>
  RegisterConversion(ConversionDescriptor descriptor);
  [[nodiscard]] Result<bool>
  UnregisterConversion(ConversionRegistrationToken registration);
  [[nodiscard]] Result<void>
  ReplaceConversion(ConversionRegistrationToken registration,
                    ConversionDescriptor descriptor);
  /// @}
  /// Test whether any registered recipe instantiates node_type (not whether its pins use that type).
  [[nodiscard]] bool
  HasConversionsForNodeType(const TypeId &node_type) const noexcept;
  /// Return independent tokens for recipes using this converter node type; empty on miss.
  [[nodiscard]] std::vector<ConversionRegistrationToken>
  RegistrationsForNodeType(const TypeId &node_type) const;

  /// @name Query the current publication
  /// Same lookup, property impact, schema, instantiation and compatibility rules as RegistrySnapshot.
  /// Find returns an owning handle. In contrast DefaultPinSchema returns a borrowed span: reacquire
  /// after publication or use a retained snapshot. Instantiate reserves IDs but does not insert.
  /// @{
  [[nodiscard]] NodeTypeDescriptorPtr Find(const TypeId &type) const noexcept;
  [[nodiscard]] PropertyImpact
  ResolvePropertyImpact(const TypeId &type,
                        const PropertyPath& path) const noexcept;
  [[nodiscard]] Result<std::vector<PinDescriptor>>
  ResolvePinSchema(const TypeId &type, const PropertyBag &properties) const;
  [[nodiscard]] std::span<const PinDescriptor>
  DefaultPinSchema(const TypeId &type) const noexcept;
  [[nodiscard]] bool PinSchemaDependsOn(const TypeId &type,
                                        const PropertyPath& property) const noexcept;
  [[nodiscard]] Result<NodeCreation>
  Instantiate(GraphDocument &document, const TypeId &type,
              NodeInstantiationOptions options = {}) const;
  [[nodiscard]] ConnectionResult Check(const TypeId &output,
                                       const TypeId &input, PinKind kind) const;
  /// @}
  /// Pin the current combined generation without taking a mutation lock.
  [[nodiscard]] RegistrySnapshot Snapshot() const;
  /// @name Publication counters
  /// Node/conversion revisions distinguish domain changes; Generation advances per changed batch.
  /// @{
  [[nodiscard]] std::uint64_t NodeRevision() const noexcept;
  [[nodiscard]] std::uint64_t ConversionRevision() const noexcept;
  [[nodiscard]] std::uint64_t Generation() const noexcept;
  /// @}
  /// Start an optimistic batch based on the current generation; fails during a guarded invocation.
  [[nodiscard]] Result<RegistryUpdate> BeginUpdate();

private:
  struct Impl;
  std::shared_ptr<Impl> m_impl;

  friend class Detail::RegistryAccess;
};

/// Warnings describe retained but potentially unresolved data; errors indicate a failed invariant.
enum class ValidationSeverity {
  Warning,
  Error,
};

/// Owning diagnostic with affected IDs, zero for non-applicable locations.
struct ValidationIssue final {
  ValidationSeverity severity{ValidationSeverity::Error};
  std::string message;
  GraphId graph;
  NodeId node;
  PinId pin;
  LinkId link;
  IntergraphLinkId intergraph_link;
};

/// Preview admission without creating a link. Uses graph and presentation protection, endpoint
/// direction/kind, duplicate pair, cardinality, type/recipe lookup and optional leaf policy. A
/// denial rejects; preview is not batch authorization. pending_pins/pending_nodes supply borrowed
/// fallback endpoints of a not-yet-inserted creation (existing records take precedence).
[[nodiscard]] UNI_GUI_EXPORT ConnectionResult ValidateConnection(
    const GraphDocument &document, const GraphPresentation &presentation,
    const ConnectionRequest &request, const RegistryCatalog &registry,
    const GraphPolicy &policy = {},
    std::span<const PinInstance> pending_pins = {},
    std::span<const NodeInstance> pending_nodes = {});

/// Collect diagnostics for graph using captured document and registry state: includes a document
/// structure audit, schema/instance and link checks and application validators. Missing graph is
/// an error issue; unknown or newer node types can produce warnings. Does not resolve external assets.
[[nodiscard]] UNI_GUI_EXPORT std::vector<ValidationIssue>
ValidateGraph(const GraphDocument &document, GraphId graph,
              const RegistryCatalog &registry);

} // namespace Uni::GUI::Nodes
