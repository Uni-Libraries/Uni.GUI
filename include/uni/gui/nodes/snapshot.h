#pragma once

/// @file
/// @brief Owning, worker-readable snapshot of the semantic authoring model.

#include <uni/gui/export.h>
#include <uni/gui/nodes/graph.h>

#include <functional>
#include <memory>
#include <vector>

namespace Uni::GUI::Nodes {

/// Captured by CaptureGraphDocumentSnapshot() on the UI thread. Copies share immutable model
/// storage, surviving edits, moves and destruction of the source. Contains neither presentation
/// nor runtime/registry state. Returned pointers and references remain valid while any snapshot
/// of **this capture** is kept alive. Const reads of an already captured snapshot are worker-safe.
class UNI_GUI_EXPORT GraphDocumentSnapshot final {
public:
    ~GraphDocumentSnapshot();
    GraphDocumentSnapshot(const GraphDocumentSnapshot& other) noexcept;
    GraphDocumentSnapshot& operator=(const GraphDocumentSnapshot& other) noexcept;

    /// @name Source state at the time of capture
    /// Use SourceIdentity plus ModelRevision to correlate results with a live document. The schema
    /// version is application-defined; GraphRevisions returns zero counters for a missing graph.
    /// @{
    [[nodiscard]] std::uint32_t SchemaVersion() const noexcept;
    [[nodiscard]] std::uint64_t SourceIdentity() const noexcept;
    [[nodiscard]] GraphId RootGraph() const noexcept;
    [[nodiscard]] std::uint64_t ModelRevision() const noexcept;
    [[nodiscard]] SemanticRevisionSet SemanticRevisions() const noexcept;
    [[nodiscard]] SemanticRevisionSet GraphRevisions(GraphId graph) const noexcept;

    /// @}

    /// @name Lookup in the captured generation
    /// A missing graph or entity returns null. Node, pin and local link lookups are graph-scoped.
    /// @{
    [[nodiscard]] const Graph* FindGraph(GraphId graph) const noexcept;
    [[nodiscard]] const NodeInstance* FindNode(GraphId graph, NodeId node) const noexcept;
    [[nodiscard]] const PinInstance* FindPin(GraphId graph, PinId pin) const noexcept;
    [[nodiscard]] const Link* FindLink(GraphId graph, LinkId link) const noexcept;
    [[nodiscard]] const IntergraphLink* FindIntergraphLink(IntergraphLinkId link) const noexcept;
    /// @}
    /// Borrow the entire document-wide proxy-link map.
    [[nodiscard]] const IntergraphLinkMap& IntergraphLinks() const noexcept;
    /// Return an owning vector of borrowed graphs in unspecified order.
    [[nodiscard]] std::vector<std::reference_wrapper<const Graph>> Graphs() const;
    /// Query local graph dependencies as in GraphDocument::HasDependencyPath().
    [[nodiscard]] bool HasDependencyPath(GraphId from, GraphId target) const;
    /// Run GraphDocument::ValidateStructure() on captured state, without consulting any registry.
    [[nodiscard]] Result<void> ValidateStructure() const;

private:
    struct Impl;
    explicit GraphDocumentSnapshot(std::shared_ptr<const Impl> impl) noexcept;

    std::shared_ptr<const Impl> m_impl;

    friend UNI_GUI_EXPORT GraphDocumentSnapshot CaptureGraphDocumentSnapshot(
        const GraphDocument& document);
};

} // namespace Uni::GUI::Nodes
