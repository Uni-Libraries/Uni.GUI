#include <uni/gui/nodes/nodes.h>

#include <atomic>
#include <concepts>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>

namespace {

using namespace Uni::GUI::Nodes;

template<typename T>
concept AllocatesNodeIds = requires(T& value) {
    value.AllocateNodeId();
};

static_assert(std::is_copy_constructible_v<GraphDocumentSnapshot>);
static_assert(std::is_copy_assignable_v<GraphDocumentSnapshot>);
static_assert(!AllocatesNodeIds<GraphDocumentSnapshot>);
static_assert(std::same_as<
    decltype(std::declval<const GraphDocumentSnapshot&>().FindNode(GraphId{}, NodeId{})),
    const NodeInstance*>);

[[noreturn]] void Fail(const std::string_view message) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(EXIT_FAILURE);
}

void Expect(const bool condition, const std::string_view message) {
    if (!condition) Fail(message);
}

void Execute(
    CommandStack& commands,
    std::unique_ptr<Command> command,
    GraphDocument& document,
    GraphPresentation& presentation,
    RegistryCatalog& types) {
    const auto result = commands.Execute(std::move(command), document, presentation, types);
    if (!result) Fail(std::string{"Snapshot fixture command failed: "} + result.error().message);
}

struct LinkedFixture final {
    GraphDocument document;
    GraphPresentation presentation;
    RegistryCatalog types;
    CommandStack commands;
    GraphId graph{document.RootGraph()};
    NodeId source{document.AllocateNodeId()};
    NodeId sink{document.AllocateNodeId()};
    PinId output{document.AllocatePinId()};
    PinId input{document.AllocatePinId()};
    LinkId link{document.AllocateLinkId()};
    GraphId left{document.AllocateGraphId()};
    GraphId right{document.AllocateGraphId()};
    NodeId sender{document.AllocateNodeId()};
    NodeId receiver{document.AllocateNodeId()};
    PinId sender_pin{document.AllocatePinId()};
    PinId receiver_pin{document.AllocatePinId()};
    IntergraphLinkId channel{document.AllocateIntergraphLinkId()};

    LinkedFixture() {
        Execute(
            commands,
            std::make_unique<AddNodeCommand>(graph, NodeCreation{
                .node = NodeInstance{.id = source, .type = TypeId{"snapshot.source"}},
                .pins = {PinInstance{
                    .id = output,
                    .node = source,
                    .key = "output",
                    .type = TypeId{"number"},
                    .direction = PinDirection::Output,
                    .storage = PinStorage::Dynamic,
                }},
            }),
            document,
            presentation,
            types);
        Execute(
            commands,
            std::make_unique<AddNodeCommand>(graph, NodeCreation{
                .node = NodeInstance{.id = sink, .type = TypeId{"snapshot.sink"}},
                .pins = {PinInstance{
                    .id = input,
                    .node = sink,
                    .key = "input",
                    .type = TypeId{"number"},
                    .direction = PinDirection::Input,
                    .storage = PinStorage::Dynamic,
                }},
            }),
            document,
            presentation,
            types);
        Execute(
            commands,
            std::make_unique<ConnectPinsCommand>(
                graph,
                Link{.id = link, .output = output, .input = input}),
            document,
            presentation,
            types);
        Execute(commands, std::make_unique<AddGraphCommand>(left), document, presentation, types);
        Execute(commands, std::make_unique<AddGraphCommand>(right), document, presentation, types);
        Execute(
            commands,
            std::make_unique<AddNodeCommand>(left, NodeCreation{
                .node = NodeInstance{
                    .id = sender,
                    .type = TypeId{"snapshot.concurrent.sender"},
                    .role = NodeRole::IntergraphOutput,
                },
                .pins = {PinInstance{
                    .id = sender_pin,
                    .node = sender,
                    .key = "channel",
                    .type = TypeId{"number"},
                    .direction = PinDirection::Input,
                    .storage = PinStorage::Dynamic,
                }},
            }),
            document,
            presentation,
            types);
        Execute(
            commands,
            std::make_unique<AddNodeCommand>(right, NodeCreation{
                .node = NodeInstance{
                    .id = receiver,
                    .type = TypeId{"snapshot.concurrent.receiver"},
                    .role = NodeRole::IntergraphInput,
                },
                .pins = {PinInstance{
                    .id = receiver_pin,
                    .node = receiver,
                    .key = "channel",
                    .type = TypeId{"number"},
                    .direction = PinDirection::Output,
                    .storage = PinStorage::Dynamic,
                }},
            }),
            document,
            presentation,
            types);
        Execute(
            commands,
            std::make_unique<ConnectIntergraphCommand>(IntergraphLink{
                .id = channel,
                .source = {left, sender, sender_pin},
                .destination = {right, receiver, receiver_pin},
            }),
            document,
            presentation,
            types);
    }
};

void TestSnapshotIsolationAndOwnership() {
    LinkedFixture fixture;
    const auto source_identity = fixture.document.Identity();
    const auto source_revisions = fixture.document.SemanticRevisions();
    const auto graph_revisions = fixture.document.GraphRevisions(fixture.graph);
    ResetTransactionMetrics();
    GraphDocumentSnapshot snapshot = CaptureGraphDocumentSnapshot(fixture.document);
    Expect(GetTransactionMetrics().copied_logical_bytes == 0,
        "Snapshot capture must share COW storage instead of copying graph entities");

    Expect(
        snapshot.SourceIdentity() == source_identity &&
            snapshot.RootGraph() == fixture.graph &&
            snapshot.ModelRevision() == fixture.document.ModelRevision() &&
            snapshot.SemanticRevisions() == source_revisions &&
            snapshot.GraphRevisions(fixture.graph) == graph_revisions,
        "Snapshot must record source identity, root, and semantic revisions");
    Expect(
        snapshot.FindNode(fixture.graph, fixture.source) != nullptr &&
            snapshot.FindPin(fixture.graph, fixture.output) != nullptr &&
            snapshot.FindLink(fixture.graph, fixture.link) != nullptr &&
            snapshot.ValidateStructure().has_value(),
        "Snapshot must expose the captured graph through const queries");

    Execute(
        fixture.commands,
        std::make_unique<SetNodeDisplayNameCommand>(fixture.graph, fixture.source, "live mutation"),
        fixture.document,
        fixture.presentation,
        fixture.types);
    Execute(
        fixture.commands,
        std::make_unique<DeleteElementsCommand>(
            fixture.graph,
            std::vector<NodeId>{},
            std::vector<LinkId>{fixture.link}),
        fixture.document,
        fixture.presentation,
        fixture.types);

    Expect(
        snapshot.FindNode(fixture.graph, fixture.source)->display_name.empty() &&
            snapshot.FindLink(fixture.graph, fixture.link) != nullptr &&
            snapshot.SemanticRevisions() == source_revisions,
        "Live value and topology mutations must not change an existing snapshot");

    GraphDocumentSnapshot copied = snapshot;
    GraphDocumentSnapshot assigned = CaptureGraphDocumentSnapshot(fixture.document);
    assigned = copied;
    Expect(
        assigned.SourceIdentity() == source_identity &&
            assigned.FindLink(fixture.graph, fixture.link) != nullptr,
        "Snapshot copies must share safe immutable ownership");
}

void TestConcurrentSnapshotReadsDuringLiveCommits() {
    LinkedFixture fixture;
    const GraphDocumentSnapshot snapshot = CaptureGraphDocumentSnapshot(fixture.document);
    const std::uint64_t identity = snapshot.SourceIdentity();
    const SemanticRevisionSet revisions = snapshot.SemanticRevisions();
    const GraphId graph = fixture.graph;
    const NodeId source_id = fixture.source;
    const PinId output_id = fixture.output;
    const LinkId link_id = fixture.link;
    const IntergraphLinkId channel_id = fixture.channel;
    constexpr std::size_t WorkerCount = 4;
    constexpr std::size_t MinimumReadsPerWorker = 5'000;
    std::atomic<bool> start{false};
    std::atomic<bool> mutations_done{false};
    std::atomic<std::size_t> ready{0};
    std::atomic<std::size_t> failures{0};
    std::atomic<std::size_t> reads{0};

    std::vector<std::thread> workers;
    workers.reserve(WorkerCount);
    for (std::size_t worker = 0; worker < WorkerCount; ++worker) {
        workers.emplace_back([snapshot, identity, revisions, graph, source_id, output_id, link_id,
                              channel_id, &start, &mutations_done, &ready, &failures, &reads] {
            while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
            std::size_t local_reads = 0;
            bool announced = false;
            do {
                const auto graphs = snapshot.Graphs();
                const auto* root = snapshot.FindGraph(graph);
                const auto* source = snapshot.FindNode(graph, source_id);
                const auto* output = snapshot.FindPin(graph, output_id);
                const auto* link = snapshot.FindLink(graph, link_id);
                const auto* channel = snapshot.FindIntergraphLink(channel_id);
                std::size_t entity_count = 0;
                for (const Graph& graph : graphs) {
                    entity_count += graph.nodes.size() + graph.pins.size() + graph.links.size();
                }
                const bool valid = snapshot.SourceIdentity() == identity &&
                    snapshot.SemanticRevisions() == revisions && graphs.size() == 3 &&
                    root != nullptr && source != nullptr && output != nullptr && link != nullptr &&
                    channel != nullptr && snapshot.IntergraphLinks().size() == 1 && entity_count == 9;
                if (!valid) failures.fetch_add(1, std::memory_order_relaxed);
                ++local_reads;
                if (!announced) {
                    ready.fetch_add(1, std::memory_order_release);
                    announced = true;
                }
            } while (local_reads < MinimumReadsPerWorker ||
                !mutations_done.load(std::memory_order_acquire));
            reads.fetch_add(local_reads, std::memory_order_relaxed);
        });
    }

    start.store(true, std::memory_order_release);
    while (ready.load(std::memory_order_acquire) != WorkerCount) std::this_thread::yield();
    Execute(
        fixture.commands,
        std::make_unique<DisconnectIntergraphCommand>(fixture.channel),
        fixture.document,
        fixture.presentation,
        fixture.types);
    for (std::size_t revision = 0; revision < 256; ++revision) {
        Execute(
            fixture.commands,
            std::make_unique<SetNodeDisplayNameCommand>(
                fixture.graph,
                fixture.source,
                "live-" + std::to_string(revision)),
            fixture.document,
            fixture.presentation,
            fixture.types);
    }
    mutations_done.store(true, std::memory_order_release);
    for (auto& worker : workers) worker.join();

    Expect(failures.load(std::memory_order_relaxed) == 0,
        "Concurrent const snapshot reads must remain stable during live commits");
    Expect(reads.load(std::memory_order_relaxed) >= WorkerCount * MinimumReadsPerWorker,
        "Every snapshot worker must complete the deterministic read workload");
    Expect(snapshot.FindIntergraphLink(fixture.channel) != nullptr &&
            snapshot.FindNode(fixture.graph, fixture.source)->display_name.empty(),
        "Concurrent live mutations must remain isolated from the captured snapshot");
}

void TestIntergraphRecordsAndDestroyedSource() {
    GraphDocumentSnapshot snapshot = [] {
        GraphDocument document;
        GraphPresentation presentation;
        RegistryCatalog types;
        CommandStack commands;
        const GraphId left = document.AllocateGraphId();
        const GraphId right = document.AllocateGraphId();
        Execute(commands, std::make_unique<AddGraphCommand>(left), document, presentation, types);
        Execute(commands, std::make_unique<AddGraphCommand>(right), document, presentation, types);

        const NodeId sender = document.AllocateNodeId();
        const PinId sender_pin = document.AllocatePinId();
        Execute(
            commands,
            std::make_unique<AddNodeCommand>(left, NodeCreation{
                .node = NodeInstance{
                    .id = sender,
                    .type = TypeId{"snapshot.sender"},
                    .role = NodeRole::IntergraphOutput,
                },
                .pins = {PinInstance{
                    .id = sender_pin,
                    .node = sender,
                    .key = "channel",
                    .type = TypeId{"number"},
                    .direction = PinDirection::Input,
                    .storage = PinStorage::Dynamic,
                }},
            }),
            document,
            presentation,
            types);

        const NodeId receiver = document.AllocateNodeId();
        const PinId receiver_pin = document.AllocatePinId();
        Execute(
            commands,
            std::make_unique<AddNodeCommand>(right, NodeCreation{
                .node = NodeInstance{
                    .id = receiver,
                    .type = TypeId{"snapshot.receiver"},
                    .role = NodeRole::IntergraphInput,
                },
                .pins = {PinInstance{
                    .id = receiver_pin,
                    .node = receiver,
                    .key = "channel",
                    .type = TypeId{"number"},
                    .direction = PinDirection::Output,
                    .storage = PinStorage::Dynamic,
                }},
            }),
            document,
            presentation,
            types);

        const IntergraphLinkId channel = document.AllocateIntergraphLinkId();
        Execute(
            commands,
            std::make_unique<ConnectIntergraphCommand>(IntergraphLink{
                .id = channel,
                .source = {left, sender, sender_pin},
                .destination = {right, receiver, receiver_pin},
            }),
            document,
            presentation,
            types);
        GraphDocumentSnapshot captured = CaptureGraphDocumentSnapshot(document);
        Execute(
            commands,
            std::make_unique<DisconnectIntergraphCommand>(channel),
            document,
            presentation,
            types);
        Expect(captured.FindIntergraphLink(channel) != nullptr,
            "Live intergraph mutations must not change an existing snapshot");
        return captured;
    }();

    Expect(
        snapshot.Graphs().size() == 3 && snapshot.IntergraphLinks().size() == 1 &&
            snapshot.FindIntergraphLink(snapshot.IntergraphLinks().begin()->first) != nullptr &&
            snapshot.ValidateStructure().has_value(),
        "Snapshot must retain graph and intergraph records after the source is destroyed");
}

} // namespace

int main() {
    TestSnapshotIsolationAndOwnership();
    TestIntergraphRecordsAndDestroyedSource();
    TestConcurrentSnapshotReadsDuringLiveCommits();
    return EXIT_SUCCESS;
}
