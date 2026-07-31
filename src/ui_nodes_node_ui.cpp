#include <uni/gui/nodes/node_ui.h>

#include <imgui.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <exception>
#include <map>
#include <utility>

namespace Uni::GUI::Nodes {
namespace {

std::atomic<std::uint64_t> NextNodeUiRegistryIdentity{1};

[[nodiscard]] std::uint64_t AllocateIdentity() noexcept {
    std::uint64_t identity = NextNodeUiRegistryIdentity.fetch_add(1, std::memory_order_relaxed);
    if (identity == 0) identity = NextNodeUiRegistryIdentity.fetch_add(1, std::memory_order_relaxed);
    return identity;
}

[[nodiscard]] Error MakeError(const ErrorCode code, std::string message) {
    return Error{code, std::move(message)};
}

[[nodiscard]] NodeUiResult ExecuteCommands(
    std::vector<std::unique_ptr<Command>> pending,
    GraphDocument& document,
    GraphPresentation& presentation,
    CommandStack& commands,
    const RegistryCatalog& registry,
    const GraphPolicy& policy,
    std::string name) {
    NodeUiResult result;
    if (pending.empty()) {
        return result;
    }

    std::unique_ptr<Command> command;
    if (pending.size() == 1) {
        command = std::move(pending.front());
    } else {
        command = std::make_unique<CompoundCommand>(std::move(name), std::move(pending));
    }
    auto executed = commands.Execute(std::move(command), document, presentation, registry, policy);
    if (!executed) {
        result.error = executed.error().message;
        return result;
    }
    result.model_changed = executed->model_changed;
    result.presentation_changed = executed->presentation_changed;
    return result;
}

} // namespace

NodeUiContext::NodeUiContext(
    const GraphId graph,
    const GraphDocument& document,
    const NodeInstance& node,
    const float zoom,
    const Vec2 available_size,
    CommandSink command_sink,
    PinIdAllocator pin_allocator,
    const bool read_only)
    : m_graph(graph),
      m_document(&document),
      m_node(&node),
      m_zoom(zoom),
      m_available_size(available_size),
      m_command_sink(std::move(command_sink)),
      m_pin_allocator(std::move(pin_allocator)),
      m_read_only(read_only),
      m_pin_order(node.pins) {}

GraphId NodeUiContext::Graph() const noexcept { return m_graph; }
const NodeInstance& NodeUiContext::Node() const noexcept { return *m_node; }
std::vector<PinId> NodeUiContext::Pins() const { return m_pin_order; }
const PinInstance* NodeUiContext::FindPin(const PinId pin) const noexcept {
    if (m_removed_pins.contains(pin)) {
        return nullptr;
    }
    const auto shadow = m_shadow_pins.find(pin);
    if (shadow != m_shadow_pins.end()) {
        return &shadow->second;
    }
    return m_document->FindPin(m_graph, pin);
}
float NodeUiContext::Zoom() const noexcept { return m_zoom; }
Vec2 NodeUiContext::AvailableSize() const noexcept { return m_available_size; }
bool NodeUiContext::ReadOnly() const noexcept { return m_read_only; }
float NodeUiContext::ToScreen(const float logical_size) const noexcept { return logical_size * m_zoom; }
Vec2 NodeUiContext::ToScreen(const Vec2 logical_size) const noexcept { return logical_size * m_zoom; }

const PropertyValue* NodeUiContext::FindProperty(const std::string_view key) const {
    const auto found = m_node->properties.find(std::string(key));
    return found != m_node->properties.end() ? &found->second : nullptr;
}

void NodeUiContext::Submit(std::unique_ptr<Command> command) {
    if (!m_read_only && command && m_command_sink) {
        m_command_sink(std::move(command));
    }
}

void NodeUiContext::SetProperty(std::string key, std::optional<PropertyValue> value) {
    Submit(std::make_unique<SetNodePropertyCommand>(
        m_graph,
        m_node->id,
        std::move(key),
        std::move(value)));
}

void NodeUiContext::EditProperty(std::string key, PropertyValue value, const bool changed) {
    const bool final = ImGui::IsItemDeactivatedAfterEdit();
    const bool active = ImGui::IsItemActive();
    const bool begin = ImGui::IsItemActivated();
    if (!changed && !final && !begin) {
        return;
    }
    if (!active && !final) {
        SetProperty(std::move(key), std::move(value));
        return;
    }

    std::uint64_t merge_key = 1469598103934665603ULL;
    const auto mix = [&](const std::uint64_t part) {
        merge_key ^= part;
        merge_key *= 1099511628211ULL;
    };
    mix(m_graph.Value());
    mix(m_node->id.Value());
    mix(ImGui::GetItemID());
    for (const unsigned char character : key) {
        mix(character);
    }
    if (merge_key == 0) {
        merge_key = 1;
    }
    Submit(std::make_unique<SetNodePropertyCommand>(
        m_graph,
        m_node->id,
        std::move(key),
        std::move(value),
        SetNodePropertyCommand::Edit{
            .merge_key = merge_key,
            .begin = begin,
            .final = final,
        }));
}

PinId NodeUiContext::AddDynamicPin(PinDescriptor descriptor, std::size_t index) {
    if (m_read_only || descriptor.key.empty() || descriptor.type.Empty()) {
        return {};
    }
    const bool duplicate_key = std::ranges::any_of(m_pin_order, [&](const PinId pin) {
        const auto* current = FindPin(pin);
        return current != nullptr && current->key == descriptor.key;
    });
    if (duplicate_key) {
        return {};
    }
    if (descriptor.label.empty()) {
        descriptor.label = descriptor.key;
    }
    if (index == std::numeric_limits<std::size_t>::max()) {
        index = m_pin_order.size();
    }
    if (index > m_pin_order.size()) {
        return {};
    }
    if (!m_pin_allocator) {
        return {};
    }
    const PinId id = m_pin_allocator();
    if (!id) {
        return {};
    }
    PinInstance pin{
        .id = id,
        .node = m_node->id,
        .key = std::move(descriptor.key),
        .label = std::move(descriptor.label),
        .type = std::move(descriptor.type),
        .direction = descriptor.direction,
        .kind = descriptor.kind,
        .cardinality = descriptor.cardinality,
        .storage = PinStorage::Dynamic,
    };
    m_pin_order.insert(m_pin_order.begin() + static_cast<std::ptrdiff_t>(index), id);
    m_removed_pins.erase(id);
    m_shadow_pins.emplace(id, pin);
    Submit(std::make_unique<AddDynamicPinCommand>(m_graph, std::move(pin), index));
    return id;
}

void NodeUiContext::RemoveDynamicPin(const PinId pin) {
    if (m_read_only) return;
    const auto* current = FindPin(pin);
    if (current == nullptr || current->storage != PinStorage::Dynamic) {
        return;
    }
    std::erase(m_pin_order, pin);
    m_shadow_pins.erase(pin);
    m_removed_pins.insert(pin);
    Submit(std::make_unique<RemoveDynamicPinCommand>(m_graph, pin));
}

void NodeUiContext::UpdateDynamicPin(PinInstance pin) {
    if (m_read_only) return;
    const auto* current = FindPin(pin.id);
    if (current == nullptr || current->storage != PinStorage::Dynamic ||
        pin.storage != PinStorage::Dynamic) {
        return;
    }
    m_shadow_pins.insert_or_assign(pin.id, pin);
    Submit(std::make_unique<UpdateDynamicPinCommand>(m_graph, std::move(pin)));
}

void NodeUiContext::ReorderDynamicPins(std::vector<PinId> order) {
    if (m_read_only) return;
    m_pin_order = order;
    Submit(std::make_unique<ReorderDynamicPinsCommand>(
        m_graph,
        m_node->id,
        std::move(order)));
}

struct NodeUiRegistry::Impl final {
    std::map<TypeId, NodeUiDescriptor> nodes;
    std::map<TypeId, PinStyleFn> pin_styles;
    std::uint64_t revision{0};
    std::uint64_t layout_revision{0};
    std::uint64_t identity{AllocateIdentity()};
};

NodeUiRegistry::NodeUiRegistry()
    : m_impl(std::make_unique<Impl>()) {}
NodeUiRegistry::~NodeUiRegistry() = default;
NodeUiRegistry::NodeUiRegistry(NodeUiRegistry&& other)
    : m_impl(std::move(other.m_impl)) {
    other.m_impl = std::make_unique<Impl>();
}
NodeUiRegistry& NodeUiRegistry::operator=(NodeUiRegistry&& other) {
    if (this != &other) {
        auto replacement = std::make_unique<Impl>();
        m_impl = std::move(other.m_impl);
        other.m_impl = std::move(replacement);
    }
    return *this;
}

Result<void> NodeUiRegistry::Register(NodeUiDescriptor descriptor) {
    if (descriptor.type.Empty() || !std::isfinite(descriptor.default_size.x) ||
        !std::isfinite(descriptor.default_size.y) || descriptor.default_size.x < 0.0f ||
        descriptor.default_size.y < 0.0f) {
        return std::unexpected(MakeError(ErrorCode::InvalidArgument, "Node UI descriptor is invalid"));
    }
    if (!m_impl->nodes.emplace(descriptor.type, std::move(descriptor)).second) {
        return std::unexpected(MakeError(ErrorCode::DuplicateId, "Node UI type is already registered"));
    }
    ++m_impl->revision;
    ++m_impl->layout_revision;
    return {};
}

bool NodeUiRegistry::Unregister(const TypeId& type) {
    if (m_impl->nodes.erase(type) == 0) return false;
    ++m_impl->revision;
    ++m_impl->layout_revision;
    return true;
}

const NodeUiDescriptor* NodeUiRegistry::Find(const TypeId& type) const noexcept {
    const auto found = m_impl->nodes.find(type);
    return found != m_impl->nodes.end() ? &found->second : nullptr;
}

std::uint64_t NodeUiRegistry::Identity() const noexcept { return m_impl->identity; }
std::uint64_t NodeUiRegistry::Revision() const noexcept { return m_impl->revision; }
std::uint64_t NodeUiRegistry::LayoutRevision() const noexcept { return m_impl->layout_revision; }

Result<void> NodeUiRegistry::RegisterPinStyle(TypeId type, PinStyleFn style) {
    if (type.Empty() || !style) {
        return std::unexpected(MakeError(ErrorCode::InvalidArgument, "Pin style requires a type and callback"));
    }
    if (!m_impl->pin_styles.emplace(std::move(type), std::move(style)).second) {
        return std::unexpected(MakeError(ErrorCode::DuplicateId, "Pin style is already registered"));
    }
    ++m_impl->revision;
    return {};
}

bool NodeUiRegistry::UnregisterPinStyle(const TypeId& type) {
    if (m_impl->pin_styles.erase(type) == 0) return false;
    ++m_impl->revision;
    return true;
}

const PinStyleFn* NodeUiRegistry::FindPinStyle(const TypeId& type) const noexcept {
    const auto found = m_impl->pin_styles.find(type);
    return found != m_impl->pin_styles.end() ? &found->second : nullptr;
}

NodeUiResult DrawNodeInspector(
    const NodeUiRegistry& ui,
    GraphDocument& document,
    GraphPresentation& presentation,
    CommandStack& commands,
    const RegistryCatalog& registry,
    const GraphId graph,
    const NodeId node_id,
    const GraphPolicy& policy) {
    const auto* node = document.FindNode(graph, node_id);
    if (node == nullptr) {
        return NodeUiResult{.error = "Node does not exist"};
    }
    const auto* descriptor = ui.Find(node->type);
    if (descriptor == nullptr || !descriptor->draw_inspector) {
        return {};
    }
    const DrawNodeInspectorFn inspector_callback = descriptor->draw_inspector;
    const auto* graph_state = document.FindGraph(graph);
    const bool read_only = node->read_only || (graph_state != nullptr && graph_state->read_only);
    const Revisions callback_revisions{
        document.ModelRevision(),
        presentation.PresentationRevision(),
    };
    const std::uint64_t document_identity = document.Identity();
    const std::uint64_t presentation_identity = presentation.Identity();
    const std::uint64_t ui_identity = ui.Identity();
    const std::uint64_t ui_revision = ui.Revision();

    std::vector<std::unique_ptr<Command>> pending;
    const ImVec2 available = ImGui::GetContentRegionAvail();
    NodeUiContext context{
        graph,
        document,
        *node,
        1.0f,
        {std::max(available.x, 0.0f), std::max(available.y, 0.0f)},
        [&](std::unique_ptr<Command> command) { pending.push_back(std::move(command)); },
        [&] { return document.AllocatePinId(); },
        read_only,
    };
    ImGui::PushID(static_cast<int>(node_id.Value() & 0xFFFFFFFFU));
    ImGui::PushID(static_cast<int>(node_id.Value() >> 32U));
    std::string callback_error;
    if (read_only) ImGui::BeginDisabled();
    try {
        inspector_callback(context);
    } catch (const std::exception& exception) {
        callback_error = std::string{"Node inspector callback failed: "} + exception.what();
    } catch (...) {
        callback_error = "Node inspector callback failed with an unknown exception";
    }
    if (read_only) ImGui::EndDisabled();
    ImGui::PopID();
    ImGui::PopID();
    if (!callback_error.empty()) {
        return NodeUiResult{.error = std::move(callback_error)};
    }
    if (callback_revisions != Revisions{
            document.ModelRevision(),
            presentation.PresentationRevision()} ||
        document_identity != document.Identity() ||
        presentation_identity != presentation.Identity() ||
        ui_identity != ui.Identity() || ui_revision != ui.Revision()) {
        return NodeUiResult{
            .error = "Node UI callbacks must queue changes through NodeUiContext",
        };
    }
    return ExecuteCommands(
        std::move(pending),
        document,
        presentation,
        commands,
        registry,
        policy,
        "Edit node inspector");
}

} // namespace Uni::GUI::Nodes
