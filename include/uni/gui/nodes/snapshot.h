#pragma once

#include <uni/gui/export.h>
#include <uni/gui/nodes/graph.h>

#include <functional>
#include <memory>
#include <vector>

namespace Uni::GUI::Nodes {

class UNI_GUI_EXPORT GraphDocumentSnapshot final {
public:
    ~GraphDocumentSnapshot();
    GraphDocumentSnapshot(const GraphDocumentSnapshot& other) noexcept;
    GraphDocumentSnapshot& operator=(const GraphDocumentSnapshot& other) noexcept;

    [[nodiscard]] std::uint32_t SchemaVersion() const noexcept;
    [[nodiscard]] std::uint64_t SourceIdentity() const noexcept;
    [[nodiscard]] GraphId RootGraph() const noexcept;
    [[nodiscard]] std::uint64_t ModelRevision() const noexcept;
    [[nodiscard]] SemanticRevisionSet SemanticRevisions() const noexcept;
    [[nodiscard]] SemanticRevisionSet GraphRevisions(GraphId graph) const noexcept;

    [[nodiscard]] const Graph* FindGraph(GraphId graph) const noexcept;
    [[nodiscard]] const NodeInstance* FindNode(GraphId graph, NodeId node) const noexcept;
    [[nodiscard]] const PinInstance* FindPin(GraphId graph, PinId pin) const noexcept;
    [[nodiscard]] const Link* FindLink(GraphId graph, LinkId link) const noexcept;
    [[nodiscard]] const IntergraphLink* FindIntergraphLink(IntergraphLinkId link) const noexcept;
    [[nodiscard]] const IntergraphLinkMap& IntergraphLinks() const noexcept;
    [[nodiscard]] std::vector<std::reference_wrapper<const Graph>> Graphs() const;
    [[nodiscard]] bool HasDependencyPath(GraphId from, GraphId target) const;
    [[nodiscard]] Result<void> ValidateStructure() const;

private:
    struct Impl;
    explicit GraphDocumentSnapshot(std::shared_ptr<const Impl> impl) noexcept;

    std::shared_ptr<const Impl> m_impl;

    friend UNI_GUI_EXPORT GraphDocumentSnapshot CaptureGraphDocumentSnapshot(
        const GraphDocument& document);
};

} // namespace Uni::GUI::Nodes
