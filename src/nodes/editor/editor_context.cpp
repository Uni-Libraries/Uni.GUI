#include "internal.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace Uni::GUI::Nodes {
namespace {

constexpr float MaxEditorCoordinate = 1.0e9f;

[[nodiscard]] Vec2 ClampPan(const Vec2 value) noexcept {
    return {
        std::clamp(value.x, -MaxEditorCoordinate, MaxEditorCoordinate),
        std::clamp(value.y, -MaxEditorCoordinate, MaxEditorCoordinate),
    };
}

[[nodiscard]] bool Finite(const Vec2 value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y);
}

} // namespace

struct EditorContext::Impl final : EditorDetail::EditorState {};

EditorDetail::EditorState& Detail::EditorAccess::Session(EditorContext& context) noexcept {
    return *context.m_impl;
}

const EditorDetail::EditorState& Detail::EditorAccess::Session(const EditorContext& context) noexcept {
    return *context.m_impl;
}

EditorContext::EditorContext()
    : m_impl(std::make_unique<Impl>()) {}

EditorContext::~EditorContext() = default;

EditorContext::EditorContext(EditorContext&& other)
    : m_impl(std::move(other.m_impl)) {
    other.m_impl = std::make_unique<Impl>();
}

EditorContext& EditorContext::operator=(EditorContext&& other) {
    if (this != &other) {
        auto replacement = std::make_unique<Impl>();
        m_impl = std::move(other.m_impl);
        other.m_impl = std::move(replacement);
    }
    return *this;
}

void EditorContext::Swap(EditorContext& other) noexcept {
    m_impl.swap(other.m_impl);
}

void EditorContext::SetPan(const Vec2 pan) noexcept {
    if (Finite(pan)) {
        m_impl->pan = ClampPan(pan);
    }
}

Vec2 EditorContext::Pan() const noexcept {
    return m_impl->pan;
}

void EditorContext::SetZoom(const float zoom) noexcept {
    if (std::isfinite(zoom) && zoom > 0.0f) {
        m_impl->zoom = zoom;
    }
}

float EditorContext::Zoom() const noexcept {
    return m_impl->zoom;
}

void EditorContext::CopySelection() noexcept { m_impl->copy_requested = true; }

void EditorContext::PasteAt(const Vec2 position) noexcept {
    if (Finite(position)) m_impl->paste_requested = position;
}

void EditorContext::DuplicateSelection() noexcept { m_impl->duplicate_requested = true; }

void EditorContext::AlignSelection(const NodeAlignment alignment) noexcept {
    m_impl->alignment_requested = alignment;
}

void EditorContext::AutoLayoutSelection(LayoutOptions options) noexcept {
    m_impl->layout_requested = std::move(options);
}

void EditorContext::FrameAll() noexcept {
    m_impl->frame_all = true;
}

void EditorContext::FrameSelection() noexcept {
    m_impl->frame_selection = true;
}

Result<void> EditorContext::TriggerLinkFlow(
    const GraphDocument& document,
    const GraphId graph,
    const LinkId link,
    const LinkFlowDirection direction) {
    if (document.FindGraph(graph) == nullptr) {
        return std::unexpected(Error{ErrorCode::GraphNotFound, "Flow graph does not exist"});
    }
    if (document.FindLink(graph, link) == nullptr) {
        return std::unexpected(Error{ErrorCode::LinkNotFound, "Flow link does not exist"});
    }
    if (direction != LinkFlowDirection::OutputToInput &&
        direction != LinkFlowDirection::InputToOutput) {
        return std::unexpected(Error{ErrorCode::InvalidArgument, "Flow direction is invalid"});
    }

    const std::uint64_t identity = document.Identity();
    const auto existing = std::ranges::find_if(m_impl->link_flows, [&](const auto& flow) {
        return flow.document_identity == identity && flow.graph == graph && flow.link == link;
    });
    const EditorDetail::LinkFlowAnimation restarted{
        .document_identity = identity,
        .graph = graph,
        .link = link,
        .direction = direction,
    };
    if (existing == m_impl->link_flows.end()) {
        m_impl->link_flows.push_back(restarted);
    } else {
        *existing = restarted;
    }
    return {};
}

void EditorContext::ClearLinkFlow(
    const GraphDocument& document,
    const GraphId graph,
    const LinkId link) noexcept {
    const std::uint64_t identity = document.Identity();
    std::erase_if(m_impl->link_flows, [&](const auto& flow) {
        return flow.document_identity == identity && flow.graph == graph && flow.link == link;
    });
}

void EditorContext::ClearLinkFlows() noexcept {
    m_impl->link_flows.clear();
}

void EditorContext::InvalidateGeometry() noexcept {
    ++m_impl->manual_geometry_revision;
    m_impl->geometry.valid = false;
}

void EditorContext::ResetMetrics() noexcept { m_impl->metrics = {}; }

EditorMetrics EditorContext::Metrics() const noexcept { return m_impl->metrics; }

std::string EditorContext::LastError() const {
    return m_impl->last_error;
}

} // namespace Uni::GUI::Nodes
