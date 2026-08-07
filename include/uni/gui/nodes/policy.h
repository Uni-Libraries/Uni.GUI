#pragma once

#include <uni/gui/export.h>
#include <uni/gui/nodes/presentation.h>

#include <any>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace Uni::GUI::Nodes {

class Command;

enum class OperationKind {
    SetSchemaVersion,
    SetRootGraph,
    AddGraph,
    RemoveGraph,
    AddNode,
    DeleteElements,
    Connect,
    AddDynamicPin,
    RemoveDynamicPin,
    UpdateDynamicPin,
    ReorderDynamicPins,
    SetNodeProperty,
    SetNodeDisplayName,
    SetNodeSubgraph,
    SetGraphInterface,
    ConnectIntergraph,
    DisconnectIntergraph,
    SetProtection,
    SetNodePresentation,
    SetLinkPresentation,
    AddGroup,
    RemoveGroup,
    SetGroupGeometry,
    SetGroupStyle,
    SetGroupMembers,
    UpdateGraph,
    UpdateNode,
    AddPin,
    RemovePin,
    UpdatePin,
};

enum class OperationAction { Set, Erase };
enum class ProtectionKind { ReadOnly, Locked };

enum class LinkPresentationImpact : std::uint8_t {
    None = 0,
    Style = 1U << 0U,
    Route = 1U << 1U,
    Geometry = 1U << 2U,
    Protection = 1U << 3U,
    Lifecycle = 1U << 4U,
};

[[nodiscard]] constexpr LinkPresentationImpact operator|(
    const LinkPresentationImpact left,
    const LinkPresentationImpact right) noexcept {
    return static_cast<LinkPresentationImpact>(
        static_cast<std::uint8_t>(left) | static_cast<std::uint8_t>(right));
}

constexpr LinkPresentationImpact& operator|=(
    LinkPresentationImpact& left,
    const LinkPresentationImpact right) noexcept {
    left = left | right;
    return left;
}

[[nodiscard]] constexpr bool HasImpact(
    const LinkPresentationImpact value,
    const LinkPresentationImpact impact) noexcept {
    return (static_cast<std::uint8_t>(value) & static_cast<std::uint8_t>(impact)) != 0;
}

struct CommandPathEntry final {
    std::size_t child{0};
    std::string name;

    bool operator==(const CommandPathEntry&) const = default;
};

class CommandPath final {
public:
    using Storage = std::vector<CommandPathEntry>;
    using const_iterator = Storage::const_iterator;

    CommandPath() = default;
    explicit CommandPath(std::shared_ptr<const Storage> entries)
        : m_entries(std::move(entries)) {}

    [[nodiscard]] bool empty() const noexcept { return !m_entries || m_entries->empty(); }
    [[nodiscard]] std::size_t size() const noexcept { return m_entries ? m_entries->size() : 0; }
    [[nodiscard]] const CommandPathEntry& operator[](const std::size_t index) const { return Entries()[index]; }
    [[nodiscard]] const_iterator begin() const noexcept { return Entries().begin(); }
    [[nodiscard]] const_iterator end() const noexcept { return Entries().end(); }

    [[nodiscard]] bool operator==(const CommandPath& other) const {
        return m_entries == other.m_entries || Entries() == other.Entries();
    }

private:
    [[nodiscard]] const Storage& Entries() const noexcept {
        static const Storage empty;
        return m_entries ? *m_entries : empty;
    }

    std::shared_ptr<const Storage> m_entries;
};

struct GraphMetadata final {
    std::string display_name;
    GraphLifetime lifetime{GraphLifetime::Reusable};
    GraphInterface interface;
    bool read_only{false};
};

struct SchemaVersionOperation final { std::uint32_t version{0}; };
struct RootGraphOperation final { GraphId graph; };
struct GraphOperation final {
    GraphId graph;
    std::shared_ptr<const GraphMetadata> value;
};
struct NodeOperation final {
    GraphId graph;
    NodeId node;
    std::shared_ptr<const NodeInstance> value;
    std::shared_ptr<const TypeId> preview_type;

    [[nodiscard]] const TypeId* Type() const noexcept {
        return value ? &value->type : preview_type.get();
    }
};
struct PinOperation final {
    static constexpr std::size_t NoIndex = std::numeric_limits<std::size_t>::max();

    GraphId graph;
    NodeId node;
    PinId pin;
    std::size_t index{NoIndex};
    std::shared_ptr<const PinInstance> value;

    [[nodiscard]] bool HasIndex() const noexcept { return index != NoIndex; }
};
struct LinkOperation final {
    GraphId graph;
    Link value;
};
struct ReorderPinsOperation final {
    GraphId graph;
    NodeId node;
    std::shared_ptr<const std::vector<PinId>> order;
};
struct PropertyChange final {
    std::string key;
    std::optional<PropertyValue> current;
    std::optional<PropertyValue> previous;
};
struct PropertyOperation final {
    GraphId graph;
    NodeId node;
    std::shared_ptr<const PropertyChange> change;
};
struct NodeTextOperation final {
    GraphId graph;
    NodeId node;
    std::shared_ptr<const std::string> text;
};
struct SubgraphChange final {
    std::optional<SubgraphReference> current;
    std::optional<SubgraphReference> previous;
};
struct NodeSubgraphOperation final {
    GraphId graph;
    NodeId node;
    std::shared_ptr<const SubgraphChange> change;
};
struct GraphInterfaceOperation final {
    GraphId graph;
    std::shared_ptr<const GraphInterface> value;
};
struct IntergraphOperation final { std::shared_ptr<const IntergraphLink> value; };
struct GraphProtectionTarget final { GraphId graph; };
struct NodeProtectionTarget final { GraphId graph; NodeId node; };
struct PinProtectionTarget final { GraphId graph; PinId pin; };
struct LinkProtectionTarget final { GraphId graph; LinkId link; };
struct GroupProtectionTarget final { GraphId graph; GroupId group; };
using ProtectionTarget = std::variant<
    GraphProtectionTarget,
    NodeProtectionTarget,
    PinProtectionTarget,
    LinkProtectionTarget,
    GroupProtectionTarget>;
struct ProtectionOperation final {
    ProtectionTarget target;
    ProtectionKind kind{ProtectionKind::ReadOnly};
    bool value{false};
};
struct NodePresentationOperation final {
    GraphId graph;
    NodeId node;
    std::shared_ptr<const NodePresentation> value;
};
struct LinkPresentationOperation final {
    GraphId graph;
    LinkId link;
    LinkPresentationImpact impact{LinkPresentationImpact::None};
    std::shared_ptr<const LinkStyle> style;
    PersistentRoutePointSequence route;
};
struct GroupLifecycleOperation final {
    GraphId graph;
    GroupId group;
    std::shared_ptr<const GroupPresentation> value;
};
struct GroupGeometryOperation final {
    GraphId graph;
    GroupId group;
    GroupGeometry value;
};
struct GroupStyleOperation final {
    GraphId graph;
    GroupId group;
    GroupStyleHandle value;
};
struct GroupMembershipChange final {
    std::vector<NodeId> added;
    std::vector<NodeId> removed;
};
struct GroupMembershipOperation final {
    GraphId graph;
    GroupId group;
    std::shared_ptr<const GroupMembershipChange> change;
};

using OperationPayload = std::variant<
    SchemaVersionOperation,
    RootGraphOperation,
    GraphOperation,
    NodeOperation,
    PinOperation,
    LinkOperation,
    ReorderPinsOperation,
    PropertyOperation,
    NodeTextOperation,
    NodeSubgraphOperation,
    GraphInterfaceOperation,
    IntergraphOperation,
    ProtectionOperation,
    NodePresentationOperation,
    LinkPresentationOperation,
    GroupLifecycleOperation,
    GroupGeometryOperation,
    GroupStyleOperation,
    GroupMembershipOperation>;

struct OperationIntent final {
    OperationKind kind;
    OperationAction action;
    CommandPath path;
    OperationPayload payload;

    OperationIntent(OperationKind operation_kind, OperationAction operation_action, OperationPayload operation_payload)
        : kind(operation_kind), action(operation_action), payload(std::move(operation_payload)) {}

    template<typename Payload>
    [[nodiscard]] const Payload* Get() const noexcept {
        return std::get_if<Payload>(&payload);
    }
};

static_assert(sizeof(CommandPath) <= 2 * sizeof(void*));
static_assert(sizeof(OperationIntent) <= 96, "OperationIntent must remain compact enough for massive batches");

enum class OperationPhase { Execute, Undo, Redo, Preview };
enum class PolicyEvaluationPass { Initial, Resume };

struct OperationPolicyContext final {
    const GraphDocument& before_document;
    const GraphPresentation& before_presentation;
    const GraphDocument& staged_document;
    const GraphPresentation& staged_presentation;
    OperationPhase phase{OperationPhase::Execute};
    PolicyEvaluationPass pass{PolicyEvaluationPass::Initial};
    std::size_t operation_index{0};
    std::size_t batch_size{0};
};

struct BatchPolicyContext final {
    const GraphDocument& before_document;
    const GraphPresentation& before_presentation;
    const GraphDocument& staged_document;
    const GraphPresentation& staged_presentation;
    OperationPhase phase{OperationPhase::Execute};
    PolicyEvaluationPass pass{PolicyEvaluationPass::Initial};
    std::size_t batch_size{0};
};

struct AllowOperation final {};
struct DenyOperation final {
    std::string reason;
};
struct DeferOperation final {
    std::any request;
};

using OperationPolicyDecision = std::variant<AllowOperation, DenyOperation, DeferOperation>;
using EvaluateOperationFn =
    std::function<OperationPolicyDecision(const OperationPolicyContext&, const OperationIntent&)>;

struct AllowBatch final {};
struct DenyBatch final { std::string reason; };
struct ReplaceBatch final { std::function<std::unique_ptr<Command>()> make_command; };
struct DeferBatch final { std::any request; };
using BatchPolicyDecision = std::variant<AllowBatch, DenyBatch, ReplaceBatch, DeferBatch>;
using EvaluateBatchFn = std::function<BatchPolicyDecision(
    const BatchPolicyContext&,
    std::span<const OperationIntent>)>;

struct DeferredOperationId final {
    std::uint64_t value{0};

    explicit operator bool() const noexcept { return value != 0; }
    bool operator==(const DeferredOperationId&) const = default;
};

enum class DeferredRequestScope { Operation, Batch };

struct DeferredRequest final {
    DeferredRequestScope scope{DeferredRequestScope::Operation};
    std::optional<std::size_t> operation_index;
    CommandPath path;
    std::any request;
};

struct DeferredOperation final {
    DeferredOperationId id;
    OperationPhase phase{OperationPhase::Execute};
    std::vector<OperationIntent> batch;
    std::vector<DeferredRequest> requests;
};

struct ConnectionPolicyRequest final {
    GraphId graph;
    const NodeInstance& output_node;
    const PinInstance& output;
    const NodeInstance& input_node;
    const PinInstance& input;
    std::optional<Link> replacing;
};
struct CreateNodePolicyRequest final {
    GraphId graph;
    TypeId type;
};
struct DeleteNodePolicyRequest final {
    GraphId graph;
    NodeId node;
};
struct DeleteLinkPolicyRequest final {
    GraphId graph;
    LinkId link;
};
struct GroupLifecyclePolicyRequest final {
    GraphId graph;
    const GroupPresentation* before;
    const GroupPresentation* after;
};

struct UNI_GUI_EXPORT GraphPolicy final {
    EvaluateOperationFn evaluate_operation;
    EvaluateBatchFn evaluate_batch;

    [[nodiscard]] bool Empty() const noexcept;
    [[nodiscard]] OperationPolicyDecision EvaluateOperation(
        const OperationPolicyContext& context,
        const OperationIntent& intent) const;
    [[nodiscard]] BatchPolicyDecision EvaluateBatch(
        const BatchPolicyContext& context,
        std::span<const OperationIntent> batch) const;
    [[nodiscard]] OperationPolicyDecision
    CheckCreateNode(const GraphDocument&, const GraphPresentation&, const CreateNodePolicyRequest&) const;
    [[nodiscard]] OperationPolicyDecision
    CheckDeleteNode(const GraphDocument&, const GraphPresentation&, const DeleteNodePolicyRequest&) const;
    [[nodiscard]] OperationPolicyDecision
    CheckConnection(const GraphDocument&, const GraphPresentation&, const ConnectionPolicyRequest&) const;
    [[nodiscard]] OperationPolicyDecision
    CheckGroupLifecycle(
        const GraphDocument&,
        const GraphPresentation&,
        const GroupLifecyclePolicyRequest&) const;
};

enum class UndoPolicyMode {
    RestoreHistory,
    RespectCurrentPolicy,
};

enum class ResumeMode {
    CommitPrepared,
    Reauthorize,
};

} // namespace Uni::GUI::Nodes
