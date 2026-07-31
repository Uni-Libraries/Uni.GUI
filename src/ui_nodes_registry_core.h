#pragma once

namespace Uni::GUI::Nodes {

struct ConversionRecipe::State final {
  ConversionRegistrationToken registration;
  ConversionDescriptor descriptor;
  TypeId node_type;
  std::uint32_t node_version{1};
  std::string display_name;
  PropertyBag default_properties;
  std::vector<PinDescriptor> static_pins;
};

namespace Detail {

struct ConversionKeyHash final {
  [[nodiscard]] std::size_t
  operator()(const ConversionKey &key) const noexcept {
    const std::size_t source = TypeIdHash{}(key.source_type);
    const std::size_t destination = TypeIdHash{}(key.destination_type);
    const std::size_t kind = static_cast<std::size_t>(key.kind);
    const std::size_t combined =
        source ^ (destination + 0x9E3779B9U + (source << 6U) + (source >> 2U));
    return combined ^
           (kind + 0x9E3779B9U + (combined << 6U) + (combined >> 2U));
  }
};

using RegistryDescriptorMap =
    CowAdjacencyMap<TypeId, NodeTypeDescriptorPtr, CowCopyDomain::None>;
using RegistryConversionMap =
    CowAdjacencyMap<ConversionKey, ConversionRecipe, CowCopyDomain::None>;
using RegistryRegistrationMap =
    CowAdjacencyMap<std::uint64_t, ConversionKey, CowCopyDomain::None>;
using RegistryRegistrationSet =
    CowAdjacencyMap<std::uint64_t, std::uint64_t, CowCopyDomain::None>;
using RegistryReverseMap =
    CowAdjacencyMap<TypeId, std::shared_ptr<const RegistryRegistrationSet>,
                    CowCopyDomain::None>;

struct RegistryState final {
  RegistryDescriptorMap descriptors;
  RegistryConversionMap conversions;
  RegistryRegistrationMap registrations;
  RegistryReverseMap registrations_by_node_type;
  std::uint64_t node_revision{0};
  std::uint64_t conversion_revision{0};
  std::uint64_t next_registration{1};
  bool registration_ids_exhausted{false};
  std::uint64_t generation{0};
};

struct RegistryDependencyRecorder final {
  static constexpr std::size_t NodeDependencyCapacity = 64;
  static constexpr std::size_t MissingNodeCapacity = 4'096;

  struct NodeDependency final {
    NodeTypeDescriptorPtr expected;
    std::size_t missing_offset{0};
    std::size_t missing_size{0};
  };

  void RecordNode(const RegistryState &state, const TypeId &type,
                  const NodeTypeDescriptorPtr &expected) noexcept {
    if (sealed || reads_descriptor_root)
      return;
    for (std::size_t index = 0; index < node_dependency_count; ++index) {
      const NodeDependency &dependency = node_dependencies[index];
      if (dependency.expected) {
        if (dependency.expected->type == type)
          return;
      } else if (std::string_view{missing_nodes.data() +
                                      dependency.missing_offset,
                                  dependency.missing_size} == type.Value()) {
        return;
      }
    }
    if (node_dependency_count == NodeDependencyCapacity ||
        (!expected &&
         type.Value().size() > MissingNodeCapacity - missing_node_size)) {
      descriptor_root = state.descriptors;
      reads_descriptor_root = true;
      node_dependency_count = 0;
      missing_node_size = 0;
      return;
    }
    NodeDependency &dependency = node_dependencies[node_dependency_count++];
    if (expected) {
      dependency.expected = expected;
      return;
    }
    dependency.missing_offset = missing_node_size;
    dependency.missing_size = type.Value().size();
    std::ranges::copy(type.Value(), missing_nodes.data() + missing_node_size);
    missing_node_size += type.Value().size();
  }

  std::unordered_map<ConversionKey, ConversionRecipe, ConversionKeyHash>
      conversions;
  RegistryDescriptorMap descriptor_root;
  RegistryConversionMap conversion_root;
  std::shared_ptr<const RegistryState> generation_root;
  std::uint64_t node_revision{0};
  std::uint64_t conversion_revision{0};
  std::uint64_t generation{0};
  bool reads_descriptor_root{false};
  bool reads_conversion_root{false};
  bool reads_node_revision{false};
  bool reads_conversion_revision{false};
  bool reads_generation{false};
  bool sealed{false};

  std::array<NodeDependency, NodeDependencyCapacity> node_dependencies;
  std::size_t node_dependency_count{0};
  std::array<char, MissingNodeCapacity> missing_nodes;
  std::size_t missing_node_size{0};
};

struct RegistryOwner final {
  struct InvocationLease final {
    explicit InvocationLease(std::shared_ptr<RegistryOwner> owner)
        : owner(std::move(owner)) {
      ++this->owner->invocation_depth;
    }
    ~InvocationLease() { --owner->invocation_depth; }

    std::shared_ptr<RegistryOwner> owner;
  };

  std::shared_ptr<const RegistryState> state{
      std::make_shared<const RegistryState>()};
  std::size_t invocation_depth{0};
  std::shared_ptr<const void> identity{std::make_shared<const std::uint8_t>(0)};
};

} // namespace Detail

struct RegistryCatalog::Impl final {
  std::shared_ptr<Detail::RegistryOwner> owner{
      std::make_shared<Detail::RegistryOwner>()};
};

RegistrySnapshot::RegistrySnapshot(
    std::shared_ptr<const Detail::RegistryState> state,
    std::shared_ptr<const void> catalog_identity,
    std::shared_ptr<Detail::RegistryDependencyRecorder> dependencies)
    : m_state(std::move(state)),
      m_catalog_identity(std::move(catalog_identity)),
      m_dependencies(std::move(dependencies)) {}

NodeTypeDescriptorPtr
RegistrySnapshot::Find(const TypeId &type) const noexcept {
  NodeTypeDescriptorPtr result;
  if (m_state != nullptr) {
    if (const auto *found = m_state->descriptors.Find(type))
      result = *found;
  }
  if (m_dependencies != nullptr)
    m_dependencies->RecordNode(*m_state, type, result);
  return result;
}

PropertyImpact RegistrySnapshot::ResolvePropertyImpact(
    const TypeId &type, const std::string_view key) const noexcept {
  const auto descriptor = Find(type);
  if (!descriptor)
    return PropertyImpact::Geometry;
  const auto found = std::ranges::find_if(
      descriptor->property_impacts,
      [&](const auto &entry) { return entry.first == key; });
  return found != descriptor->property_impacts.end()
             ? found->second
             : descriptor->undeclared_property_impact;
}

std::vector<NodeTypeDescriptorPtr> RegistrySnapshot::Descriptors() const {
  std::vector<NodeTypeDescriptorPtr> result;
  if (m_state == nullptr)
    return result;
  if (m_dependencies != nullptr && !m_dependencies->sealed) {
    m_dependencies->descriptor_root = m_state->descriptors;
    m_dependencies->reads_descriptor_root = true;
  }
  result.reserve(m_state->descriptors.size());
  for (const auto &descriptor : m_state->descriptors)
    result.push_back(descriptor);
  return result;
}

std::uint64_t RegistrySnapshot::NodeRevision() const noexcept {
  if (m_state != nullptr && m_dependencies != nullptr &&
      !m_dependencies->sealed) {
    m_dependencies->descriptor_root = m_state->descriptors;
    m_dependencies->node_revision = m_state->node_revision;
    m_dependencies->reads_node_revision = true;
  }
  return m_state != nullptr ? m_state->node_revision : 0;
}

std::uint64_t RegistrySnapshot::ConversionRevision() const noexcept {
  if (m_state != nullptr && m_dependencies != nullptr &&
      !m_dependencies->sealed) {
    m_dependencies->conversion_root = m_state->conversions;
    m_dependencies->conversion_revision = m_state->conversion_revision;
    m_dependencies->reads_conversion_revision = true;
  }
  return m_state != nullptr ? m_state->conversion_revision : 0;
}

std::uint64_t RegistrySnapshot::Generation() const noexcept {
  if (m_state != nullptr && m_dependencies != nullptr &&
      !m_dependencies->sealed) {
    m_dependencies->generation_root = m_state;
    m_dependencies->generation = m_state->generation;
    m_dependencies->reads_generation = true;
  }
  return m_state != nullptr ? m_state->generation : 0;
}

Result<NodeCreation>
RegistrySnapshot::Instantiate(GraphDocument &document, const TypeId &type,
                              const std::string_view display_name) const {
  const auto descriptor = Find(type);
  if (!descriptor) {
    return std::unexpected(
        MakeError(ErrorCode::TypeNotFound, "Node type is not registered"));
  }

  NodeCreation creation;
  creation.node.id = document.AllocateNodeId();
  if (!creation.node.id) {
    return std::unexpected(
        MakeError(ErrorCode::InvalidArgument, "Node ID space is exhausted"));
  }
  creation.node.type = descriptor->type;
  creation.node.type_version = descriptor->version;
  creation.node.display_name = display_name.empty() ? descriptor->display_name
                                                    : std::string(display_name);
  creation.node.properties = descriptor->default_properties;
  creation.prepared_descriptor = descriptor;
  creation.pins.reserve(descriptor->static_pins.size());
  creation.node.pins.reserve(descriptor->static_pins.size());
  for (const auto &pin : descriptor->static_pins) {
    const PinId id = document.AllocatePinId();
    if (!id) {
      return std::unexpected(
          MakeError(ErrorCode::InvalidArgument, "Pin ID space is exhausted"));
    }
    creation.node.pins.push_back(id);
    creation.pins.push_back(PinInstance{
        .id = id,
        .node = creation.node.id,
        .key = pin.key,
        .label = pin.label,
        .type = pin.type,
        .direction = pin.direction,
        .kind = pin.kind,
        .cardinality = pin.cardinality,
        .storage = PinStorage::Static,
    });
  }
  return creation;
}

ConnectionResult RegistrySnapshot::Check(const TypeId &output,
                                         const TypeId &input,
                                         const PinKind kind) const {
  if (output.Empty() || input.Empty())
    return Rejected("Pins must have registered value types");
  if (!ValidKind(kind))
    return Rejected("Pin kind is invalid");
  if (output == input || output.Value() == "*" || input.Value() == "*") {
    return ConnectionResult{ConnectionResult::Status::Allowed,
                            {},
                            std::nullopt,
                            ErrorCode::IncompatiblePins};
  }

  const ConversionKey key{output, input, kind};
  ConversionRecipe recipe;
  if (m_state != nullptr) {
    if (const auto *found = m_state->conversions.Find(key))
      recipe = *found;
  }
  if (m_dependencies != nullptr && !m_dependencies->sealed)
    m_dependencies->conversions.insert_or_assign(key, recipe);
  if (recipe) {
    return ConnectionResult{ConnectionResult::Status::RequiresConversion,
                            "Connection requires conversion from '" +
                                output.Value() + "' to '" + input.Value() + "'",
                            recipe, ErrorCode::IncompatiblePins};
  }
  return Rejected("Cannot connect '" + output.Value() + "' to '" +
                  input.Value() + "'");
}

Result<void>
RegistrySnapshot::ValidateRecipe(const ConversionRecipe &recipe) const {
  if (!recipe) {
    return std::unexpected(
        MakeError(ErrorCode::InvalidArgument, "Conversion recipe is empty"));
  }
  if (recipe.m_state->registration.m_catalog_identity != m_catalog_identity) {
    return std::unexpected(
        MakeError(ErrorCode::RegistryMismatch,
                  "Conversion recipe belongs to a different registry catalog"));
  }
  ConversionRecipe current;
  if (m_state != nullptr) {
    if (const auto *found = m_state->conversions.Find(recipe.Descriptor().key))
      current = *found;
  }
  if (m_dependencies != nullptr && !m_dependencies->sealed) {
    m_dependencies->conversions.insert_or_assign(recipe.Descriptor().key,
                                                 current);
  }
  if (!current) {
    return std::unexpected(MakeError(
        ErrorCode::TypeNotFound, "Conversion recipe is no longer registered"));
  }
  if (current != recipe) {
    return std::unexpected(
        MakeError(ErrorCode::RevisionConflict,
                  "Conversion recipe was replaced by a newer catalog record"));
  }
  return {};
}

ConversionRegistrationToken::ConversionRegistrationToken(
    std::shared_ptr<const void> catalog_identity,
    const std::uint64_t registration_id) noexcept
    : m_catalog_identity(std::move(catalog_identity)),
      m_registration_id(registration_id) {}

ConversionRegistrationToken::operator bool() const noexcept {
  return static_cast<bool>(m_catalog_identity) && m_registration_id != 0;
}

ConversionRecipe::ConversionRecipe(std::shared_ptr<const State> state) noexcept
    : m_state(std::move(state)) {}

ConversionRecipe::operator bool() const noexcept {
  return static_cast<bool>(m_state);
}

const ConversionDescriptor &ConversionRecipe::Descriptor() const noexcept {
  static const ConversionDescriptor empty;
  return m_state ? m_state->descriptor : empty;
}

ConversionRegistrationToken ConversionRecipe::Registration() const noexcept {
  return m_state ? m_state->registration : ConversionRegistrationToken{};
}

bool ConversionRecipe::operator==(
    const ConversionRecipe &other) const noexcept {
  return m_state == other.m_state;
}

Result<NodeCreation>
ConversionRecipe::Instantiate(GraphDocument &document) const {
  if (!m_state) {
    return std::unexpected(
        MakeError(ErrorCode::TypeNotFound, "Conversion recipe is empty"));
  }
  NodeCreation creation;
  creation.node.id = document.AllocateNodeId();
  if (!creation.node.id) {
    return std::unexpected(
        MakeError(ErrorCode::InvalidArgument, "Node ID space is exhausted"));
  }
  creation.node.type = m_state->node_type;
  creation.node.type_version = m_state->node_version;
  creation.node.display_name = m_state->display_name;
  creation.node.properties = m_state->default_properties;
  creation.node.pins.reserve(m_state->static_pins.size());
  creation.pins.reserve(m_state->static_pins.size());
  for (const auto &pin : m_state->static_pins) {
    const PinId id = document.AllocatePinId();
    if (!id) {
      return std::unexpected(
          MakeError(ErrorCode::InvalidArgument, "Pin ID space is exhausted"));
    }
    creation.node.pins.push_back(id);
    creation.pins.push_back(PinInstance{
        .id = id,
        .node = creation.node.id,
        .key = pin.key,
        .label = pin.label,
        .type = pin.type,
        .direction = pin.direction,
        .kind = pin.kind,
        .cardinality = pin.cardinality,
        .storage = PinStorage::Static,
    });
  }
  return creation;
}

bool ConversionRecipe::Matches(const NodeCreation &creation) const noexcept {
  if (!m_state || !creation.node.id ||
      creation.node.type != m_state->node_type ||
      creation.node.type_version != m_state->node_version ||
      creation.node.display_name != m_state->display_name ||
      creation.node.properties != m_state->default_properties ||
      creation.node.subgraph || creation.node.read_only ||
      creation.node.role != NodeRole::Regular ||
      creation.node.pins.size() != m_state->static_pins.size() ||
      creation.pins.size() != m_state->static_pins.size()) {
    return false;
  }
  for (std::size_t index = 0; index < m_state->static_pins.size(); ++index) {
    const PinDescriptor &expected = m_state->static_pins[index];
    const PinInstance &actual = creation.pins[index];
    if (!actual.id || creation.node.pins[index] != actual.id ||
        actual.node != creation.node.id || actual.key != expected.key ||
        actual.label != expected.label || actual.type != expected.type ||
        actual.direction != expected.direction ||
        actual.kind != expected.kind ||
        actual.cardinality != expected.cardinality ||
        actual.storage != PinStorage::Static || actual.read_only) {
      return false;
    }
  }
  return true;
}

RegistryCatalog::RegistryCatalog() : m_impl(std::make_shared<Impl>()) {}

RegistryCatalog::~RegistryCatalog() = default;

RegistryCatalog::RegistryCatalog(RegistryCatalog &&other)
    : m_impl(std::move(other.m_impl)) {
  other.m_impl = std::make_shared<Impl>();
}

RegistryCatalog &RegistryCatalog::operator=(RegistryCatalog &&other) {
  if (this != &other) {
    auto replacement = std::make_shared<Impl>();
    m_impl = std::move(other.m_impl);
    other.m_impl = std::move(replacement);
  }
  return *this;
}

RegistrySnapshot RegistryCatalog::Snapshot() const {
  return RegistrySnapshot{m_impl->owner->state, m_impl->owner->identity,
                          nullptr};
}

NodeTypeDescriptorPtr RegistryCatalog::Find(const TypeId &type) const noexcept {
  if (const auto *found = m_impl->owner->state->descriptors.Find(type))
    return *found;
  return {};
}

PropertyImpact RegistryCatalog::ResolvePropertyImpact(
    const TypeId &type, const std::string_view key) const noexcept {
  return Snapshot().ResolvePropertyImpact(type, key);
}

Result<NodeCreation>
RegistryCatalog::Instantiate(GraphDocument &document, const TypeId &type,
                             const std::string_view display_name) const {
  return Snapshot().Instantiate(document, type, display_name);
}

ConnectionResult RegistryCatalog::Check(const TypeId &output,
                                        const TypeId &input,
                                        const PinKind kind) const {
  return Snapshot().Check(output, input, kind);
}

std::uint64_t RegistryCatalog::NodeRevision() const noexcept {
  return m_impl->owner->state->node_revision;
}

std::uint64_t RegistryCatalog::ConversionRevision() const noexcept {
  return m_impl->owner->state->conversion_revision;
}

std::uint64_t RegistryCatalog::Generation() const noexcept {
  return m_impl->owner->state->generation;
}

Result<RegistryUpdate> RegistryCatalog::BeginUpdate() {
  if (m_impl->owner->invocation_depth != 0) {
    return std::unexpected(MakeError(ErrorCode::CommandFailed,
                                     "Registry update is unavailable while "
                                     "registry-backed callbacks are running"));
  }
  return RegistryUpdate{m_impl->owner, m_impl->owner->state};
}

Result<void> RegistryCatalog::RegisterNodeType(NodeTypeDescriptor descriptor) {
  auto update = BeginUpdate();
  if (!update)
    return std::unexpected(std::move(update.error()));
  if (auto staged = update->RegisterNodeType(std::move(descriptor)); !staged)
    return staged;
  auto committed = update->Commit();
  return committed ? Result<void>{}
                   : std::unexpected(std::move(committed.error()));
}

Result<void> RegistryCatalog::ReplaceNodeType(NodeTypeDescriptor descriptor) {
  auto update = BeginUpdate();
  if (!update)
    return std::unexpected(std::move(update.error()));
  if (auto staged = update->ReplaceNodeType(std::move(descriptor)); !staged)
    return staged;
  auto committed = update->Commit();
  return committed ? Result<void>{}
                   : std::unexpected(std::move(committed.error()));
}

Result<bool> RegistryCatalog::UnregisterNodeType(const TypeId &type) {
  if (!m_impl->owner->state->descriptors.contains(type))
    return false;
  auto update = BeginUpdate();
  if (!update)
    return std::unexpected(std::move(update.error()));
  if (auto staged = update->UnregisterNodeType(type); !staged) {
    return std::unexpected(std::move(staged.error()));
  }
  auto committed = update->Commit();
  if (!committed)
    return std::unexpected(std::move(committed.error()));
  return true;
}

Result<ConversionRegistrationToken>
RegistryCatalog::RegisterConversion(ConversionDescriptor descriptor) {
  auto update = BeginUpdate();
  if (!update)
    return std::unexpected(std::move(update.error()));
  if (auto staged = update->RegisterConversion(std::move(descriptor));
      !staged) {
    return std::unexpected(std::move(staged.error()));
  }
  auto committed = update->Commit();
  if (!committed)
    return std::unexpected(std::move(committed.error()));
  return committed->registrations.front();
}

Result<bool> RegistryCatalog::UnregisterConversion(
    const ConversionRegistrationToken registration) {
  if (!registration ||
      registration.m_catalog_identity != m_impl->owner->identity) {
    return std::unexpected(MakeError(
        ErrorCode::RegistryMismatch,
        "Conversion registration belongs to a different registry catalog"));
  }
  if (!m_impl->owner->state->registrations.contains(
          registration.m_registration_id))
    return false;
  auto update = BeginUpdate();
  if (!update)
    return std::unexpected(std::move(update.error()));
  if (auto staged = update->UnregisterConversion(registration); !staged) {
    return std::unexpected(std::move(staged.error()));
  }
  auto committed = update->Commit();
  if (!committed)
    return std::unexpected(std::move(committed.error()));
  return true;
}

Result<void> RegistryCatalog::ReplaceConversion(
    const ConversionRegistrationToken registration,
    ConversionDescriptor descriptor) {
  auto update = BeginUpdate();
  if (!update)
    return std::unexpected(std::move(update.error()));
  if (auto staged =
          update->ReplaceConversion(registration, std::move(descriptor));
      !staged)
    return staged;
  auto committed = update->Commit();
  return committed ? Result<void>{}
                   : std::unexpected(std::move(committed.error()));
}

bool RegistryCatalog::HasConversionsForNodeType(
    const TypeId &node_type) const noexcept {
  const auto *registrations =
      m_impl->owner->state->registrations_by_node_type.Find(node_type);
  return registrations != nullptr && *registrations != nullptr &&
         !(*registrations)->empty();
}

std::vector<ConversionRegistrationToken>
RegistryCatalog::RegistrationsForNodeType(const TypeId &node_type) const {
  std::vector<ConversionRegistrationToken> result;
  const auto *registrations =
      m_impl->owner->state->registrations_by_node_type.Find(node_type);
  if (registrations == nullptr || *registrations == nullptr)
    return result;
  result.reserve((*registrations)->size());
  for (const std::uint64_t id : **registrations) {
    result.push_back(ConversionRegistrationToken{m_impl->owner->identity, id});
  }
  return result;
}

struct RegistryUpdate::Impl final {
  enum class NodeOperationKind { Register, Replace, Unregister };
  enum class ConversionOperationKind { Register, Replace, Unregister };

  struct NodeOperation final {
    NodeOperationKind kind{NodeOperationKind::Register};
    NodeTypeDescriptor descriptor;
    TypeId type;
  };

  struct ConversionOperation final {
    ConversionOperationKind kind{ConversionOperationKind::Register};
    ConversionRegistrationToken registration;
    ConversionDescriptor descriptor;
  };

  std::shared_ptr<Detail::RegistryOwner> owner;
  std::shared_ptr<const Detail::RegistryState> base;
  std::vector<NodeOperation> node_operations;
  std::vector<ConversionOperation> conversion_operations;
  std::optional<Error> error;
  bool finished{false};
};

RegistryUpdate::RegistryUpdate(
    std::shared_ptr<Detail::RegistryOwner> owner,
    std::shared_ptr<const Detail::RegistryState> base)
    : m_impl(std::make_unique<Impl>(
          Impl{.owner = std::move(owner), .base = std::move(base)})) {}

RegistryUpdate::~RegistryUpdate() = default;
RegistryUpdate::RegistryUpdate(RegistryUpdate &&other) noexcept = default;
RegistryUpdate &
RegistryUpdate::operator=(RegistryUpdate &&other) noexcept = default;

namespace {

template <typename UpdateImpl>
[[nodiscard]] Result<void> StageRegistryOperation(UpdateImpl *impl) {
  if (impl == nullptr || impl->finished) {
    return std::unexpected(MakeError(ErrorCode::CommandFailed,
                                     "Registry update is no longer active"));
  }
  if (impl->error)
    return std::unexpected(*impl->error);
  return {};
}

template <typename UpdateImpl>
void PoisonRegistryUpdate(UpdateImpl &impl, Error error) {
  if (!impl.error)
    impl.error = std::move(error);
}

} // namespace

Result<void> RegistryUpdate::RegisterNodeType(NodeTypeDescriptor descriptor) {
  if (auto active = StageRegistryOperation(m_impl.get()); !active)
    return active;
  if (auto valid = NormalizeNodeDescriptor(descriptor); !valid) {
    PoisonRegistryUpdate(*m_impl, valid.error());
    return valid;
  }
  m_impl->node_operations.push_back(Impl::NodeOperation{
      .kind = Impl::NodeOperationKind::Register,
      .descriptor = std::move(descriptor),
  });
  return {};
}

Result<void> RegistryUpdate::ReplaceNodeType(NodeTypeDescriptor descriptor) {
  if (auto active = StageRegistryOperation(m_impl.get()); !active)
    return active;
  if (auto valid = NormalizeNodeDescriptor(descriptor); !valid) {
    PoisonRegistryUpdate(*m_impl, valid.error());
    return valid;
  }
  m_impl->node_operations.push_back(Impl::NodeOperation{
      .kind = Impl::NodeOperationKind::Replace,
      .descriptor = std::move(descriptor),
  });
  return {};
}

Result<void> RegistryUpdate::UnregisterNodeType(TypeId type) {
  if (auto active = StageRegistryOperation(m_impl.get()); !active)
    return active;
  if (type.Empty()) {
    Error error = MakeError(ErrorCode::InvalidArgument, "Node type is empty");
    PoisonRegistryUpdate(*m_impl, error);
    return std::unexpected(std::move(error));
  }
  m_impl->node_operations.push_back(Impl::NodeOperation{
      .kind = Impl::NodeOperationKind::Unregister,
      .type = std::move(type),
  });
  return {};
}

Result<void>
RegistryUpdate::RegisterConversion(ConversionDescriptor descriptor) {
  if (auto active = StageRegistryOperation(m_impl.get()); !active)
    return active;
  m_impl->conversion_operations.push_back(Impl::ConversionOperation{
      .kind = Impl::ConversionOperationKind::Register,
      .descriptor = std::move(descriptor),
  });
  return {};
}

Result<void>
RegistryUpdate::ReplaceConversion(ConversionRegistrationToken registration,
                                  ConversionDescriptor descriptor) {
  if (auto active = StageRegistryOperation(m_impl.get()); !active)
    return active;
  if (!registration ||
      registration.m_catalog_identity != m_impl->owner->identity) {
    Error error = MakeError(
        ErrorCode::RegistryMismatch,
        "Conversion registration belongs to a different registry catalog");
    PoisonRegistryUpdate(*m_impl, error);
    return std::unexpected(std::move(error));
  }
  m_impl->conversion_operations.push_back(Impl::ConversionOperation{
      .kind = Impl::ConversionOperationKind::Replace,
      .registration = std::move(registration),
      .descriptor = std::move(descriptor),
  });
  return {};
}

Result<void>
RegistryUpdate::UnregisterConversion(ConversionRegistrationToken registration) {
  if (auto active = StageRegistryOperation(m_impl.get()); !active)
    return active;
  if (!registration ||
      registration.m_catalog_identity != m_impl->owner->identity) {
    Error error = MakeError(
        ErrorCode::RegistryMismatch,
        "Conversion registration belongs to a different registry catalog");
    PoisonRegistryUpdate(*m_impl, error);
    return std::unexpected(std::move(error));
  }
  m_impl->conversion_operations.push_back(Impl::ConversionOperation{
      .kind = Impl::ConversionOperationKind::Unregister,
      .registration = std::move(registration),
  });
  return {};
}

Result<RegistryUpdateResult> RegistryUpdate::Commit() {
  if (m_impl == nullptr || m_impl->finished) {
    return std::unexpected(MakeError(ErrorCode::CommandFailed,
                                     "Registry update is no longer active"));
  }
  m_impl->finished = true;
  if (m_impl->error)
    return std::unexpected(std::move(*m_impl->error));
  if (m_impl->owner->invocation_depth != 0) {
    return std::unexpected(MakeError(ErrorCode::CommandFailed,
                                     "Registry cannot be mutated while "
                                     "registry-backed callbacks are running"));
  }
  if (m_impl->owner->state != m_impl->base) {
    return std::unexpected(
        MakeError(ErrorCode::RevisionConflict,
                  "Registry generation changed while an update was staged"));
  }

  RegistryUpdateResult result{
      .generation = m_impl->base->generation,
      .node_revision = m_impl->base->node_revision,
      .conversion_revision = m_impl->base->conversion_revision,
  };
  if (m_impl->node_operations.empty() && m_impl->conversion_operations.empty())
    return result;

  Detail::RegistryState candidate = *m_impl->base;
  std::unordered_set<TypeId, TypeIdHash> touched_node_types;
  std::unordered_set<std::uint64_t> touched_registrations;

  const auto record_copies = [&](const std::uint64_t copies) {
    result.statistics.path_copies += copies;
  };
  const auto assign_descriptor = [&](const TypeId &type,
                                     NodeTypeDescriptorPtr descriptor) {
    std::uint64_t copies = 0;
    candidate.descriptors.insert_or_assign(type, std::move(descriptor),
                                           &copies);
    record_copies(copies);
  };
  const auto erase_descriptor = [&](const TypeId &type) {
    std::uint64_t copies = 0;
    candidate.descriptors.erase(type, &copies);
    record_copies(copies);
  };
  const auto assign_conversion = [&](const ConversionKey &key,
                                     ConversionRecipe recipe) {
    std::uint64_t copies = 0;
    candidate.conversions.insert_or_assign(key, std::move(recipe), &copies);
    record_copies(copies);
  };
  const auto erase_conversion = [&](const ConversionKey &key) {
    std::uint64_t copies = 0;
    candidate.conversions.erase(key, &copies);
    record_copies(copies);
  };
  const auto assign_registration = [&](const std::uint64_t id,
                                       ConversionKey key) {
    std::uint64_t copies = 0;
    candidate.registrations.insert_or_assign(id, std::move(key), &copies);
    record_copies(copies);
  };
  const auto erase_registration = [&](const std::uint64_t id) {
    std::uint64_t copies = 0;
    candidate.registrations.erase(id, &copies);
    record_copies(copies);
  };
  const auto add_reverse = [&](const TypeId &type, const std::uint64_t id) {
    Detail::RegistryRegistrationSet registrations;
    if (const auto *current = candidate.registrations_by_node_type.Find(type))
      registrations = **current;
    std::uint64_t copies = 0;
    registrations.insert_or_assign(id, id, &copies);
    record_copies(copies);
    copies = 0;
    candidate.registrations_by_node_type.insert_or_assign(
        type,
        std::make_shared<const Detail::RegistryRegistrationSet>(
            std::move(registrations)),
        &copies);
    record_copies(copies);
  };
  const auto remove_reverse = [&](const TypeId &type, const std::uint64_t id) {
    const auto *current = candidate.registrations_by_node_type.Find(type);
    if (current == nullptr)
      return;
    Detail::RegistryRegistrationSet registrations = **current;
    std::uint64_t copies = 0;
    registrations.erase(id, &copies);
    record_copies(copies);
    copies = 0;
    if (registrations.empty()) {
      candidate.registrations_by_node_type.erase(type, &copies);
    } else {
      candidate.registrations_by_node_type.insert_or_assign(
          type,
          std::make_shared<const Detail::RegistryRegistrationSet>(
              std::move(registrations)),
          &copies);
    }
    record_copies(copies);
  };

  const auto build_recipe =
      [&](ConversionDescriptor descriptor,
          const ConversionRegistrationToken token) -> Result<ConversionRecipe> {
    if (descriptor.key.source_type.Empty() ||
        descriptor.key.destination_type.Empty() ||
        !ValidKind(descriptor.key.kind) || descriptor.node_type.Empty() ||
        descriptor.input_pin.empty() || descriptor.output_pin.empty() ||
        descriptor.key.source_type == descriptor.key.destination_type ||
        descriptor.key.source_type.Value() == "*" ||
        descriptor.key.destination_type.Value() == "*") {
      return std::unexpected(
          MakeError(ErrorCode::InvalidArgument,
                    "Conversion requires distinct concrete types, a pin kind, "
                    "a node type, and semantic pin keys"));
    }
    const auto *node = candidate.descriptors.Find(descriptor.node_type);
    if (node == nullptr) {
      return std::unexpected(MakeError(
          ErrorCode::TypeNotFound, "Conversion node type is not registered"));
    }
    const auto input = std::ranges::find(
        (*node)->static_pins, descriptor.input_pin, &PinDescriptor::key);
    const auto output = std::ranges::find(
        (*node)->static_pins, descriptor.output_pin, &PinDescriptor::key);
    if (input == (*node)->static_pins.end() ||
        output == (*node)->static_pins.end() || input == output ||
        input->direction != PinDirection::Input ||
        output->direction != PinDirection::Output ||
        input->type != descriptor.key.source_type ||
        output->type != descriptor.key.destination_type ||
        input->kind != descriptor.key.kind ||
        output->kind != descriptor.key.kind) {
      return std::unexpected(
          MakeError(ErrorCode::InvalidArgument,
                    "Conversion semantic pins do not match its key"));
    }
    ++result.statistics.recipes_built;
    return ConversionRecipe{
        std::make_shared<const ConversionRecipe::State>(ConversionRecipe::State{
            .registration = token,
            .descriptor = std::move(descriptor),
            .node_type = (*node)->type,
            .node_version = (*node)->version,
            .display_name = (*node)->display_name,
            .default_properties = (*node)->default_properties,
            .static_pins = (*node)->static_pins,
        })};
  };

  for (auto &operation : m_impl->node_operations) {
    ++result.statistics.touched_records;
    const TypeId type = operation.kind == Impl::NodeOperationKind::Unregister
                            ? operation.type
                            : operation.descriptor.type;
    touched_node_types.insert(type);
    const auto *current = candidate.descriptors.Find(type);
    if (operation.kind == Impl::NodeOperationKind::Register) {
      if (current != nullptr) {
        return std::unexpected(MakeError(ErrorCode::DuplicateId,
                                         "Node type is already registered"));
      }
      assign_descriptor(type, std::make_shared<const NodeTypeDescriptor>(
                                  std::move(operation.descriptor)));
    } else if (operation.kind == Impl::NodeOperationKind::Replace) {
      if (current == nullptr) {
        return std::unexpected(
            MakeError(ErrorCode::TypeNotFound, "Node type is not registered"));
      }
      if (**current == operation.descriptor) {
        ++result.statistics.no_op_records;
        continue;
      }
      assign_descriptor(type, std::make_shared<const NodeTypeDescriptor>(
                                  std::move(operation.descriptor)));
    } else {
      if (current == nullptr) {
        return std::unexpected(
            MakeError(ErrorCode::TypeNotFound, "Node type is not registered"));
      }
      erase_descriptor(type);
    }
  }

  struct FinalConversionPlan final {
    bool unregister{false};
    ConversionKey key;
  };
  std::unordered_map<std::uint64_t, FinalConversionPlan> final_conversion_plans;
  for (const auto &operation : m_impl->conversion_operations) {
    if (operation.kind == Impl::ConversionOperationKind::Register)
      continue;
    final_conversion_plans.insert_or_assign(
        operation.registration.m_registration_id,
        FinalConversionPlan{
            .unregister =
                operation.kind == Impl::ConversionOperationKind::Unregister,
            .key = operation.descriptor.key,
        });
  }

  std::unordered_map<std::uint64_t, ConversionRecipe> displaced_recipes;
  for (std::size_t operation_index = 0;
       operation_index < m_impl->conversion_operations.size();
       ++operation_index) {
    auto &operation = m_impl->conversion_operations[operation_index];
    ++result.statistics.touched_records;
    if (operation.kind == Impl::ConversionOperationKind::Register) {
      if (candidate.conversions.contains(operation.descriptor.key)) {
        return std::unexpected(MakeError(ErrorCode::DuplicateId,
                                         "Conversion is already registered"));
      }
      if (candidate.registration_ids_exhausted) {
        return std::unexpected(
            MakeError(ErrorCode::GenerationOverflow,
                      "Conversion registration ID space is exhausted"));
      }
      const std::uint64_t id = candidate.next_registration;
      const ConversionRegistrationToken token{m_impl->owner->identity, id};
      auto recipe = build_recipe(std::move(operation.descriptor), token);
      if (!recipe)
        return std::unexpected(std::move(recipe.error()));
      const ConversionKey key = recipe->Descriptor().key;
      const TypeId node_type = recipe->Descriptor().node_type;
      assign_conversion(key, std::move(*recipe));
      assign_registration(id, key);
      add_reverse(node_type, id);
      touched_registrations.insert(id);
      result.registrations.push_back(token);
      if (id == std::numeric_limits<std::uint64_t>::max()) {
        candidate.registration_ids_exhausted = true;
      } else {
        ++candidate.next_registration;
      }
      continue;
    }

    const std::uint64_t id = operation.registration.m_registration_id;
    const auto *old_key = candidate.registrations.Find(id);
    if (old_key == nullptr) {
      return std::unexpected(MakeError(
          ErrorCode::TypeNotFound, "Conversion registration does not exist"));
    }
    ConversionRecipe old_recipe;
    bool is_displaced = false;
    if (const auto displaced = displaced_recipes.find(id);
        displaced != displaced_recipes.end()) {
      old_recipe = displaced->second;
      is_displaced = true;
    } else if (const auto *current = candidate.conversions.Find(*old_key);
               current != nullptr &&
               current->Registration().m_registration_id == id) {
      old_recipe = *current;
    }
    if (!old_recipe) {
      return std::unexpected(MakeError(ErrorCode::RevisionConflict,
                                       "Conversion staging lost its recipe"));
    }
    const TypeId old_node_type = old_recipe.Descriptor().node_type;
    touched_registrations.insert(id);
    if (operation.kind == Impl::ConversionOperationKind::Unregister) {
      if (const auto *current = candidate.conversions.Find(*old_key);
          current != nullptr &&
          current->Registration().m_registration_id == id) {
        erase_conversion(*old_key);
      }
      erase_registration(id);
      remove_reverse(old_node_type, id);
      displaced_recipes.erase(id);
      continue;
    }
    if (old_recipe.Descriptor() == operation.descriptor) {
      ++result.statistics.no_op_records;
      continue;
    }
    if (operation.descriptor.key != *old_key) {
      if (const auto *collision =
              candidate.conversions.Find(operation.descriptor.key)) {
        const std::uint64_t collision_id =
            collision->Registration().m_registration_id;
        const auto collision_plan = final_conversion_plans.find(collision_id);
        const bool collision_moves =
            collision_plan != final_conversion_plans.end() &&
            (collision_plan->second.unregister ||
             collision_plan->second.key != operation.descriptor.key);
        if (!collision_moves) {
          return std::unexpected(MakeError(ErrorCode::DuplicateId,
                                           "Conversion is already registered"));
        }
        displaced_recipes.insert_or_assign(collision_id, *collision);
      }
    }
    auto recipe =
        build_recipe(std::move(operation.descriptor), operation.registration);
    if (!recipe)
      return std::unexpected(std::move(recipe.error()));
    const ConversionKey new_key = recipe->Descriptor().key;
    const TypeId new_node_type = recipe->Descriptor().node_type;
    if (is_displaced && new_key == *old_key) {
      displaced_recipes.insert_or_assign(id, std::move(*recipe));
      if (new_node_type != old_node_type) {
        remove_reverse(old_node_type, id);
        add_reverse(new_node_type, id);
      }
      continue;
    }
    if (new_key != *old_key) {
      if (const auto *current = candidate.conversions.Find(*old_key);
          current != nullptr &&
          current->Registration().m_registration_id == id) {
        erase_conversion(*old_key);
      }
    }
    assign_conversion(new_key, std::move(*recipe));
    if (new_key != *old_key)
      assign_registration(id, new_key);
    if (new_node_type != old_node_type) {
      remove_reverse(old_node_type, id);
      add_reverse(new_node_type, id);
    }
    displaced_recipes.erase(id);
  }

  const auto recipe_matches_node = [](const ConversionRecipe &recipe,
                                      const NodeTypeDescriptor &node) {
    return recipe.m_state->node_type == node.type &&
           recipe.m_state->node_version == node.version &&
           recipe.m_state->display_name == node.display_name &&
           recipe.m_state->default_properties == node.default_properties &&
           recipe.m_state->static_pins == node.static_pins;
  };
  for (const TypeId &type : touched_node_types) {
    const auto *registrations = candidate.registrations_by_node_type.Find(type);
    if (registrations == nullptr)
      continue;
    const auto *node = candidate.descriptors.Find(type);
    if (node == nullptr) {
      return std::unexpected(MakeError(
          ErrorCode::TypeInUse,
          "Cannot unregister a node type while conversions reference it"));
    }
    for (const std::uint64_t id : **registrations) {
      const ConversionKey key = *candidate.registrations.Find(id);
      const ConversionRecipe current = *candidate.conversions.Find(key);
      touched_registrations.insert(id);
      if (recipe_matches_node(current, **node))
        continue;
      auto recipe = build_recipe(
          current.Descriptor(),
          ConversionRegistrationToken{m_impl->owner->identity, id});
      if (!recipe)
        return std::unexpected(std::move(recipe.error()));
      assign_conversion(key, std::move(*recipe));
    }
  }

  bool node_changed = false;
  for (const TypeId &type : touched_node_types) {
    const auto *before = m_impl->base->descriptors.Find(type);
    const auto *after = candidate.descriptors.Find(type);
    if (before != nullptr && after != nullptr && **before == **after) {
      if (*before != *after)
        assign_descriptor(type, *before);
      continue;
    }
    if (before == nullptr && after == nullptr)
      continue;
    node_changed = true;
  }

  const auto same_recipe_value = [](const ConversionRecipe &left,
                                    const ConversionRecipe &right) {
    return left.Descriptor() == right.Descriptor() &&
           left.m_state->node_type == right.m_state->node_type &&
           left.m_state->node_version == right.m_state->node_version &&
           left.m_state->display_name == right.m_state->display_name &&
           left.m_state->default_properties ==
               right.m_state->default_properties &&
           left.m_state->static_pins == right.m_state->static_pins;
  };
  bool conversion_changed = false;
  for (const std::uint64_t id : touched_registrations) {
    const auto *before_key = m_impl->base->registrations.Find(id);
    const auto *after_key = candidate.registrations.Find(id);
    if (before_key == nullptr || after_key == nullptr ||
        *before_key != *after_key) {
      if (before_key != nullptr || after_key != nullptr)
        conversion_changed = true;
      continue;
    }
    const ConversionRecipe before =
        *m_impl->base->conversions.Find(*before_key);
    const ConversionRecipe after = *candidate.conversions.Find(*after_key);
    if (same_recipe_value(before, after)) {
      if (before != after)
        assign_conversion(*after_key, before);
    } else {
      conversion_changed = true;
    }
  }

  if (!node_changed)
    candidate.descriptors = m_impl->base->descriptors;
  if (!conversion_changed) {
    candidate.conversions = m_impl->base->conversions;
    candidate.registrations = m_impl->base->registrations;
    candidate.registrations_by_node_type =
        m_impl->base->registrations_by_node_type;
    candidate.next_registration = m_impl->base->next_registration;
    candidate.registration_ids_exhausted =
        m_impl->base->registration_ids_exhausted;
  }

  if (!node_changed && !conversion_changed) {
    if (result.statistics.touched_records != 0)
      ++result.statistics.no_op_records;
    return result;
  }

  auto next_generation = Detail::NextRegistryRevision(m_impl->base->generation,
                                                      "Registry generation");
  if (!next_generation)
    return std::unexpected(std::move(next_generation.error()));
  if (node_changed) {
    auto revision = Detail::NextRegistryRevision(m_impl->base->node_revision,
                                                 "Node registry");
    if (!revision)
      return std::unexpected(std::move(revision.error()));
    candidate.node_revision = *revision;
  }
  if (conversion_changed) {
    auto revision = Detail::NextRegistryRevision(
        m_impl->base->conversion_revision, "Conversion registry");
    if (!revision)
      return std::unexpected(std::move(revision.error()));
    candidate.conversion_revision = *revision;
  }
  candidate.generation = *next_generation;
  auto state =
      std::make_shared<const Detail::RegistryState>(std::move(candidate));
  m_impl->owner->state = state;
  result.generation = state->generation;
  result.node_revision = state->node_revision;
  result.conversion_revision = state->conversion_revision;
  result.statistics.published_generations = 1;
  return result;
}

Detail::RegistryInvocation
Detail::RegistryAccess::Invoke(const RegistryCatalog &registry) {
  return Invoke(RegistryInvocationSource{registry.m_impl->owner});
}

Detail::RegistryInvocation
Detail::RegistryAccess::Invoke(const RegistryInvocationSource &source) {
  auto lease =
      std::make_shared<const RegistryOwner::InvocationLease>(source.m_owner);
  return RegistryInvocation{
      RegistrySnapshot{source.m_owner->state, source.m_owner->identity,
                       std::make_shared<RegistryDependencyRecorder>()},
      source, std::move(lease)};
}

bool Detail::RegistryAccess::DependenciesCurrent(
    const RegistryInvocationSource &source,
    const RegistrySnapshot &snapshot) noexcept {
  if (!source.m_owner ||
      snapshot.m_catalog_identity != source.m_owner->identity ||
      snapshot.m_dependencies == nullptr) {
    return false;
  }
  const auto &current = *source.m_owner->state;
  for (std::size_t index = 0;
       index < snapshot.m_dependencies->node_dependency_count; ++index) {
    const auto &dependency = snapshot.m_dependencies->node_dependencies[index];
    if (dependency.expected) {
      const auto *found =
          current.descriptors.Find(dependency.expected->type);
      if (found == nullptr || *found != dependency.expected)
        return false;
      continue;
    }
    const std::string_view missing{
        snapshot.m_dependencies->missing_nodes.data() +
            dependency.missing_offset,
        dependency.missing_size};
    if (current.descriptors.FindEquivalent(
            missing,
            [](const std::string_view lookup, const TypeId &candidate) {
              return lookup.compare(candidate.Value());
            }) != nullptr) {
      return false;
    }
  }
  for (const auto &[key, expected] : snapshot.m_dependencies->conversions) {
    const auto *found = current.conversions.Find(key);
    const ConversionRecipe actual =
        found != nullptr ? *found : ConversionRecipe{};
    if (actual != expected)
      return false;
  }
  if (snapshot.m_dependencies->reads_descriptor_root &&
      !current.descriptors.SharesStorageWith(
          snapshot.m_dependencies->descriptor_root)) {
    return false;
  }
  if (snapshot.m_dependencies->reads_node_revision &&
      (current.node_revision != snapshot.m_dependencies->node_revision ||
       !current.descriptors.SharesStorageWith(
           snapshot.m_dependencies->descriptor_root))) {
    return false;
  }
  if (snapshot.m_dependencies->reads_conversion_revision &&
      (current.conversion_revision !=
           snapshot.m_dependencies->conversion_revision ||
       !current.conversions.SharesStorageWith(
           snapshot.m_dependencies->conversion_root))) {
    return false;
  }
  return !snapshot.m_dependencies->reads_generation ||
         (current.generation == snapshot.m_dependencies->generation &&
          source.m_owner->state == snapshot.m_dependencies->generation_root);
}

void Detail::RegistryAccess::SealDependencies(
    const RegistrySnapshot &snapshot) noexcept {
  if (snapshot.m_dependencies != nullptr)
    snapshot.m_dependencies->sealed = true;
}

bool Detail::RegistryAccess::SameCatalog(
    const RegistryInvocationSource &left,
    const RegistryInvocationSource &right) noexcept {
  return left.m_owner && right.m_owner &&
         left.m_owner->identity == right.m_owner->identity;
}

void Detail::RegistryAccess::SetRevisionsForTesting(
    RegistryCatalog &registry, const std::uint64_t node_revision,
    const std::uint64_t conversion_revision, const std::uint64_t generation,
    const std::uint64_t next_registration,
    const bool registration_ids_exhausted) {
  auto state = std::make_shared<RegistryState>(*registry.m_impl->owner->state);
  state->node_revision = node_revision;
  state->conversion_revision = conversion_revision;
  state->generation = generation;
  state->next_registration = next_registration;
  state->registration_ids_exhausted = registration_ids_exhausted;
  registry.m_impl->owner->state = std::move(state);
}

} // namespace Uni::GUI::Nodes
