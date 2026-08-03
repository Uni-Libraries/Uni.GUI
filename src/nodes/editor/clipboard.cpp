#include "internal/frame.h"

#include <exception>
#include <string>
#include <variant>

namespace Uni::GUI::Nodes::EditorDetail {

bool EditorFrame::CopySelection() {
    auto fragment = CaptureGraphFragment(document, presentation, context.Selection());
    if (!fragment) {
        session.last_error = fragment.error().message;
        return false;
    }
    auto serialized = SerializeGraphFragmentJson(*fragment);
    if (!serialized) {
        session.last_error = serialized.error().message;
        return false;
    }
    if (ImGui::GetPlatformIO().Platform_SetClipboardTextFn == nullptr) {
        session.last_error = "OS clipboard is unavailable";
        return false;
    }
    ImGui::SetClipboardText(serialized->c_str());
    session.last_error.clear();
    return true;
}

void EditorFrame::PasteFragment(const GraphFragment& fragment, const Vec2 position) {
    auto prepared = PrepareGraphFragmentPaste(
        document, presentation, registry, fragment, graph_id, position);
    if (!prepared) {
        session.last_error = prepared.error().message;
        return;
    }
    const FragmentIdRemap remap = prepared->remap;
    auto pasted = commands.Execute(
        std::make_unique<PasteGraphFragmentCommand>(std::move(*prepared)),
        document,
        presentation,
        registry,
        policy);
    if (!pasted) {
        session.last_error = pasted.error().message;
        return;
    }
    RecordChange(*pasted);
    (void)ClearSelection();
    for (const auto& [source, node] : remap.nodes) {
        (void)source;
        session.selected_nodes.insert(node);
    }
    for (const auto& [source, group] : remap.groups) {
        (void)source;
        session.selected_groups.insert(group);
    }
    result.selection_changed = true;
    RefreshGraph();
    session.last_error.clear();
}

void EditorFrame::PasteClipboard(const Vec2 position) {
    if (ImGui::GetPlatformIO().Platform_GetClipboardTextFn == nullptr) {
        session.last_error = "OS clipboard is unavailable";
        return;
    }
    const char* text = ImGui::GetClipboardText();
    if (text == nullptr) {
        session.last_error = "Clipboard does not contain text";
        return;
    }
    auto fragment = DeserializeGraphFragmentJson(std::string_view{text}, registry);
    if (!fragment) {
        session.last_error = fragment.error().message;
        return;
    }
    PasteFragment(*fragment, position);
}

void EditorFrame::DuplicateSelection() {
    if (callbacks.duplicate_selection) {
        const DuplicateSelectionFn callback = callbacks.duplicate_selection;
        const GraphSelection selection = context.Selection();
        const Revisions before{document.ModelRevision(), presentation.PresentationRevision()};
        const std::uint64_t document_identity = document.Identity();
        const std::uint64_t presentation_identity = presentation.Identity();
        const std::uint64_t document_allocation = document.AllocationEpoch();
        const std::uint64_t presentation_allocation = presentation.AllocationEpoch();
        const std::uint64_t editor_revision = session.external_revision;
        try {
            callback(selection);
        } catch (const std::exception& exception) {
            session.last_error = std::string{"Duplicate callback failed: "} + exception.what();
            context_state_invalidated = true;
            return;
        } catch (...) {
            session.last_error = "Duplicate callback failed with an unknown exception";
            context_state_invalidated = true;
            return;
        }
        if (before != Revisions{document.ModelRevision(), presentation.PresentationRevision()} ||
            document_identity != document.Identity() || presentation_identity != presentation.Identity() ||
            document_allocation != document.AllocationEpoch() ||
            presentation_allocation != presentation.AllocationEpoch() ||
            editor_revision != session.external_revision) {
            session.last_error = "Duplicate callbacks must enqueue application work without mutating editor state";
            context_state_invalidated = true;
        } else {
            session.last_error.clear();
        }
        return;
    }
    auto fragment = CaptureGraphFragment(document, presentation, context.Selection());
    if (!fragment) {
        session.last_error = fragment.error().message;
        return;
    }
    PasteFragment(*fragment, fragment->origin + Vec2{config.snap_size, config.snap_size});
}

void EditorFrame::ApplyLayout(Result<NodeLayout> layout) {
    if (!layout) {
        session.last_error = layout.error().message;
        return;
    }
    auto moved = commands.Execute(
        std::make_unique<MoveNodesCommand>(
            graph_id, std::move(layout->before), std::move(layout->after)),
        document,
        presentation,
        registry,
        policy);
    if (moved) {
        RecordChange(*moved);
        session.last_error.clear();
    } else {
        session.last_error = moved.error().message;
    }
}

void EditorFrame::ProcessClipboardRequests() {
    if (!std::holds_alternative<Idle>(session.interaction)) return;
    if (session.copy_requested) {
        (void)CopySelection();
        session.copy_requested = false;
    }
    if (session.duplicate_requested) {
        DuplicateSelection();
        session.duplicate_requested = false;
        if (context_state_invalidated) return;
    }
    if (session.paste_requested) {
        PasteClipboard(*session.paste_requested);
        session.paste_requested.reset();
    }
    if (session.alignment_requested) {
        const auto selected = context.Selection().nodes;
        if (selected.size() < 2) {
            session.last_error = "Alignment requires at least two selected visible nodes";
        } else {
            NodeSizes sizes;
            for (const auto& [node_id, geometry] : session.geometry.nodes) {
                sizes.emplace(node_id, geometry.bounds.max - geometry.bounds.min);
            }
            ApplyLayout(ComputeNodeAlignment(
                document,
                presentation,
                graph_id,
                selected,
                *session.alignment_requested,
                {config.node_width, 100.0f},
                sizes));
        }
        session.alignment_requested.reset();
    }
    if (session.layout_requested) {
        auto selected = context.Selection().nodes;
        if (selected.empty()) {
            for (const NodeId node : session.geometry.ordered_nodes) {
                if (session.geometry.nodes.contains(node)) selected.push_back(node);
            }
        }
        if (selected.empty()) {
            session.last_error = "Auto-layout requires at least one visible node";
        } else {
            LayoutOptions options = *session.layout_requested;
            options.fallback_node_size.x = config.node_width;
            for (const auto& [node_id, geometry] : session.geometry.nodes) {
                options.node_sizes.insert_or_assign(node_id, geometry.bounds.max - geometry.bounds.min);
            }
            ApplyLayout(ComputeAutoLayout(document, presentation, graph_id, selected, options));
        }
        session.layout_requested.reset();
    }
}

} // namespace Uni::GUI::Nodes::EditorDetail
