#include <uni/gui/nodes/snapshot.h>

#include <utility>

namespace Uni::GUI::Nodes {

struct GraphDocumentSnapshot::Impl final {
    explicit Impl(GraphDocument document)
        : document(std::move(document)) {}

    GraphDocument document;
};

GraphDocumentSnapshot::GraphDocumentSnapshot(std::shared_ptr<const Impl> impl) noexcept
    : m_impl(std::move(impl)) {}

GraphDocumentSnapshot::~GraphDocumentSnapshot() = default;
GraphDocumentSnapshot::GraphDocumentSnapshot(const GraphDocumentSnapshot& other) noexcept = default;
GraphDocumentSnapshot& GraphDocumentSnapshot::operator=(const GraphDocumentSnapshot& other) noexcept = default;

std::uint32_t GraphDocumentSnapshot::SchemaVersion() const noexcept {
    return m_impl->document.SchemaVersion();
}

std::uint64_t GraphDocumentSnapshot::SourceIdentity() const noexcept {
    return m_impl->document.Identity();
}

GraphId GraphDocumentSnapshot::RootGraph() const noexcept {
    return m_impl->document.RootGraph();
}

std::uint64_t GraphDocumentSnapshot::ModelRevision() const noexcept {
    return m_impl->document.ModelRevision();
}

SemanticRevisionSet GraphDocumentSnapshot::SemanticRevisions() const noexcept {
    return m_impl->document.SemanticRevisions();
}

SemanticRevisionSet GraphDocumentSnapshot::GraphRevisions(const GraphId graph) const noexcept {
    return m_impl->document.GraphRevisions(graph);
}

const Graph* GraphDocumentSnapshot::FindGraph(const GraphId graph) const noexcept {
    return m_impl->document.FindGraph(graph);
}

const NodeInstance* GraphDocumentSnapshot::FindNode(const GraphId graph, const NodeId node) const noexcept {
    return m_impl->document.FindNode(graph, node);
}

const PinInstance* GraphDocumentSnapshot::FindPin(const GraphId graph, const PinId pin) const noexcept {
    return m_impl->document.FindPin(graph, pin);
}

const Link* GraphDocumentSnapshot::FindLink(const GraphId graph, const LinkId link) const noexcept {
    return m_impl->document.FindLink(graph, link);
}

const IntergraphLink* GraphDocumentSnapshot::FindIntergraphLink(const IntergraphLinkId link) const noexcept {
    return m_impl->document.FindIntergraphLink(link);
}

const IntergraphLinkMap& GraphDocumentSnapshot::IntergraphLinks() const noexcept {
    return m_impl->document.IntergraphLinks();
}

std::vector<std::reference_wrapper<const Graph>> GraphDocumentSnapshot::Graphs() const {
    return m_impl->document.Graphs();
}

bool GraphDocumentSnapshot::HasDependencyPath(const GraphId from, const GraphId target) const {
    return m_impl->document.HasDependencyPath(from, target);
}

Result<void> GraphDocumentSnapshot::ValidateStructure() const {
    return m_impl->document.ValidateStructure();
}

GraphDocumentSnapshot CaptureGraphDocumentSnapshot(const GraphDocument& document) {
    return GraphDocumentSnapshot{
        std::make_shared<const GraphDocumentSnapshot::Impl>(document.SnapshotForTransaction())};
}

} // namespace Uni::GUI::Nodes
