#include <uni/gui/nodes/nodes.h>

#include <imgui.h>

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace {

using namespace Uni::GUI::Nodes;

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
    RegistryCatalog& registry) {
    const auto result = commands.Execute(std::move(command), document, presentation, registry);
    if (!result) Fail(std::string{"Stage-five fixture command failed: "} + result.error().message);
}

struct EditorFixture final {
    GraphDocument document;
    GraphPresentation presentation;
    RegistryCatalog registry;
    NodeUiRegistry node_ui;
    LinkRouterRegistry routers;
    CommandStack commands;
    GraphId graph{document.RootGraph()};
    NodeId source{document.AllocateNodeId()};
    NodeId sink{document.AllocateNodeId()};
    PinId output{document.AllocatePinId()};
    PinId input{document.AllocatePinId()};
    LinkId link{document.AllocateLinkId()};

    explicit EditorFixture(
        const Vec2 source_position = {40.0f, 80.0f},
        const Vec2 sink_position = {380.0f, 200.0f}) {
        Execute(
            commands,
            std::make_unique<AddNodeCommand>(
                graph,
                NodeCreation{
                    .node = NodeInstance{.id = source, .type = TypeId{"stage5.source"}},
                    .pins = {PinInstance{
                        .id = output,
                        .node = source,
                        .key = "output",
                        .label = "Output",
                        .type = TypeId{"number"},
                        .direction = PinDirection::Output,
                        .storage = PinStorage::Dynamic,
                    }},
                },
                NodePresentation{.position = source_position}),
            document,
            presentation,
            registry);
        Execute(
            commands,
            std::make_unique<AddNodeCommand>(
                graph,
                NodeCreation{
                    .node = NodeInstance{.id = sink, .type = TypeId{"stage5.sink"}},
                    .pins = {PinInstance{
                        .id = input,
                        .node = sink,
                        .key = "input",
                        .label = "Input",
                        .type = TypeId{"number"},
                        .direction = PinDirection::Input,
                        .storage = PinStorage::Dynamic,
                    }},
                },
                NodePresentation{.position = sink_position}),
            document,
            presentation,
            registry);
        Execute(
            commands,
            std::make_unique<ConnectPinsCommand>(
                graph,
                Link{.id = link, .output = output, .input = input}),
            document,
            presentation,
            registry);
    }
};

void TestRetainedWorkspaceFacade() {
    NodeEditorWorkspace workspace;
    const GraphId graph = workspace.document.RootGraph();
    const NodeId node = workspace.document.AllocateNodeId();
    auto added = workspace.Execute(std::make_unique<AddNodeCommand>(
        graph,
        NodeCreation{.node = NodeInstance{.id = node, .type = TypeId{"workspace.node"}}}));
    if (!added) Fail(std::string{"Workspace Execute failed: "} + added.error().message);
    Expect(added && workspace.document.FindNode(graph, node) != nullptr,
        "Workspace Execute must use its retained document and command stack");
    Expect(workspace.Undo().has_value() && workspace.document.FindNode(graph, node) == nullptr,
        "Workspace Undo must operate on the owned retained state");
    Expect(workspace.Redo().has_value() && workspace.document.FindNode(graph, node) != nullptr,
        "Workspace Redo must operate on the owned retained state");

    GraphPolicy defer;
    defer.evaluate_operation = [](const OperationPolicyContext&, const OperationIntent& operation)
        -> OperationPolicyDecision {
        return operation.kind == OperationKind::SetNodeProperty
            ? OperationPolicyDecision{DeferOperation{std::uint64_t{17}}}
            : OperationPolicyDecision{AllowOperation{}};
    };
    auto pending = workspace.Execute(
        std::make_unique<SetNodePropertyCommand>(graph, node, "pending", PropertyValue{true}),
        defer);
    Expect(pending && pending->deferred && workspace.HasPending() &&
               workspace.PendingOperation() != nullptr,
           "Workspace must expose the deferred command lifecycle");
    GraphPolicy allow;
    allow.evaluate_batch = [](const BatchPolicyContext&, std::span<const OperationIntent>)
        -> BatchPolicyDecision { return AllowBatch{}; };
    Expect(workspace.Resume(
               pending->deferred->id,
               ResumeMode::Reauthorize,
               allow).has_value() &&
               workspace.document.FindNode(graph, node)->properties.contains("pending"),
           "Workspace Resume must reauthorize and commit prepared state");

    auto cancelled = workspace.Execute(
        std::make_unique<SetNodePropertyCommand>(graph, node, "cancelled", PropertyValue{true}),
        defer);
    Expect(cancelled && cancelled->deferred &&
               workspace.Cancel(cancelled->deferred->id).has_value() &&
               !workspace.HasPending() &&
               !workspace.document.FindNode(graph, node)->properties.contains("cancelled"),
           "Workspace Cancel must discard prepared state");
    const auto snapshot = workspace.CaptureSnapshot();
    Expect(snapshot.FindNode(graph, node) != nullptr &&
               snapshot.ModelRevision() == workspace.document.ModelRevision(),
           "Workspace CaptureSnapshot must return an immutable semantic snapshot");
}

void TestFlowAndDebugRendering() {
    EditorFixture fixture;
    EditorContext editor;
    EditorConfig config;
    config.show_minimap = false;
    config.show_breadcrumbs = false;
    config.link_flow_duration = 0.1f;
    config.link_flow_marker_spacing = 0.001f;
    config.maximum_router_segments = 64;

    Expect(
        !editor.TriggerLinkFlow(fixture.document, GraphId{99999}, fixture.link) &&
            !editor.TriggerLinkFlow(fixture.document, fixture.graph, LinkId{99999}),
        "Flow trigger must reject unknown graph and link identities");

    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = {800.0f, 600.0f};
    io.DeltaTime = 0.02f;
    unsigned char* pixels = nullptr;
    int width = 0;
    int height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    Expect(pixels != nullptr, "Stage-five test font atlas must build");

    int vertices = 0;
    const auto draw = [&] {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({0.0f, 0.0f});
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("Stage-five visual APIs", nullptr, ImGuiWindowFlags_NoDecoration);
        const EditorResult result = DrawEditor(
            editor,
            fixture.document,
            fixture.presentation,
            fixture.commands,
             fixture.registry,
            fixture.node_ui,
            fixture.routers,
            {680.0f, 500.0f},
            {},
            config);
        ImGui::End();
        ImGui::Render();
        vertices = ImGui::GetDrawData()->TotalVtxCount;
        return result;
    };

    (void)draw();
    editor.ResetMetrics();
    (void)draw();
    const int baseline_vertices = vertices;
    Expect(editor.Metrics().geometry_rebuilds == 0,
        "A baseline warm frame must reuse editor geometry");

    const std::uint64_t model_revision = fixture.document.ModelRevision();
    const std::uint64_t presentation_revision = fixture.presentation.PresentationRevision();
    const std::string_view undo_name = fixture.commands.UndoName();
    Expect(
        editor.TriggerLinkFlow(
            fixture.document,
            fixture.graph,
            fixture.link,
            LinkFlowDirection::InputToOutput).has_value(),
        "A valid reverse-direction flow must start");
    Expect(
        fixture.document.ModelRevision() == model_revision &&
            fixture.presentation.PresentationRevision() == presentation_revision &&
            fixture.commands.UndoName() == undo_name,
        "Starting flow must not mutate document, presentation, or history");

    editor.ResetMetrics();
    const EditorResult flowing = draw();
    Expect(flowing.animation_active && vertices > baseline_vertices,
        "Active flow must report frame demand and draw markers over the link");
    if (vertices - baseline_vertices > 64 * 64) {
        Fail("Flow marker drawing exceeded its cap: " +
            std::to_string(vertices - baseline_vertices) + " added vertices");
    }
    Expect(editor.Metrics().geometry_rebuilds == 0 && editor.Metrics().routed_links == 0,
        "Animated warm frames must not rebuild or reroute cached geometry");

    io.DeltaTime = 0.09f;
    Expect(!draw().animation_active, "Flow must expire after its configured transient duration");
    io.DeltaTime = 0.02f;
    Expect(editor.TriggerLinkFlow(fixture.document, fixture.graph, fixture.link).has_value() &&
            draw().animation_active,
        "Triggering an expired flow must restart it");
    Expect(editor.TriggerLinkFlow(fixture.document, fixture.graph, fixture.link).has_value(),
        "Triggering an active flow must restart it");
    EditorFixture same_ids_other_document;
    editor.ClearLinkFlow(
        same_ids_other_document.document,
        same_ids_other_document.graph,
        same_ids_other_document.link);
    Expect(draw().animation_active,
        "Flow state must remain bound to document identity when another document reuses IDs");
    editor.ClearLinkFlow(fixture.document, fixture.graph, fixture.link);
    Expect(!draw().animation_active, "Per-link clear must stop a transient flow");
    Expect(editor.TriggerLinkFlow(fixture.document, fixture.graph, fixture.link).has_value(),
        "Flow must start before clear-all validation");
    editor.ClearLinkFlows();
    Expect(!draw().animation_active, "Clear-all must stop every transient flow");

    config.debug_overlays = EditorDebugOverlay::All;
    editor.ResetMetrics();
    (void)draw();
    Expect(vertices > baseline_vertices,
        "Built-in debug overlays must add visible diagnostic primitives");
    Expect(editor.Metrics().geometry_rebuilds == 0 && editor.Metrics().routed_links == 0,
        "Debug flags must remain visual-only and preserve warm geometry");
    Expect(
        fixture.document.ModelRevision() == model_revision &&
            fixture.presentation.PresentationRevision() == presentation_revision &&
            fixture.commands.UndoName() == undo_name,
        "Debug drawing must not mutate document, presentation, or history");

    ImGui::DestroyContext();
}

void TestPathologicalOffscreenFlowIsBounded() {
    constexpr std::size_t SegmentCount = 4096;
    EditorFixture fixture{{100'000.0f, 100'000.0f}, {101'000.0f, 101'000.0f}};
    const TypeId router_type{"stage5.router.pathological-offscreen"};
    Expect(fixture.routers.Register(LinkRouterDescriptor{
        .type = router_type,
        .callback = [](const LinkRoutingContext& context) -> Result<LinkPath> {
            LinkPath path;
            path.segments.reserve(SegmentCount);
            Vec2 start = context.output.position;
            for (std::size_t index = 0; index < SegmentCount; ++index) {
                const float amount = static_cast<float>(index + 1) /
                    static_cast<float>(SegmentCount);
                const Vec2 end = index + 1 == SegmentCount
                    ? context.input.position
                    : context.output.position +
                        (context.input.position - context.output.position) * amount;
                const float first_x = start.x + (end.x - start.x) * 0.25f;
                const float second_x = start.x + (end.x - start.x) * 0.75f;
                path.segments.push_back(LinkPathSegment{
                    .primitive = CubicPathSegment{
                        start,
                        {first_x, 500'000'000.0f},
                        {second_x, -500'000'000.0f},
                        end,
                    },
                });
                start = end;
            }
            return path;
        },
    }).has_value(), "Pathological flow router must register");
    Execute(
        fixture.commands,
        std::make_unique<SetLinkRouterCommand>(fixture.link, router_type),
        fixture.document,
        fixture.presentation,
        fixture.registry);

    EditorContext editor;
    EditorConfig config;
    config.show_minimap = false;
    config.show_breadcrumbs = false;
    config.maximum_router_segments = SegmentCount;

    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = {800.0f, 600.0f};
    io.DeltaTime = 1.0f / 60.0f;
    unsigned char* pixels = nullptr;
    int width = 0;
    int height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    Expect(pixels != nullptr, "Pathological flow test font atlas must build");

    int vertices = 0;
    const auto draw = [&] {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({0.0f, 0.0f});
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("Stage-five bounded flow", nullptr, ImGuiWindowFlags_NoDecoration);
        const EditorResult result = DrawEditor(
            editor,
            fixture.document,
            fixture.presentation,
            fixture.commands,
             fixture.registry,
            fixture.node_ui,
            fixture.routers,
            {680.0f, 500.0f},
            {},
            config);
        ImGui::End();
        ImGui::Render();
        vertices = ImGui::GetDrawData()->TotalVtxCount;
        return result;
    };

    (void)draw();
    const int baseline_vertices = vertices;
    Expect(editor.TriggerLinkFlow(fixture.document, fixture.graph, fixture.link).has_value(),
        "Pathological offscreen flow must start");
    editor.ResetMetrics();
    const EditorResult animated = draw();
    Expect(animated.animation_active && vertices == baseline_vertices,
        "Offscreen pathological flow must be culled before adaptive flattening or marker drawing");
    Expect(editor.Metrics().geometry_rebuilds == 0 && editor.Metrics().routed_links == 0,
        "Pathological flow warm frame must preserve cached geometry");

    editor.SetPan({-100'000.0f, -100'000.0f});
    config.link_flatten_tolerance = 0.000001f;
    editor.ResetMetrics();
    const EditorResult visible = draw();
    Expect(visible.animation_active && vertices < 500'000,
        "On-screen pathological flow must obey global adaptive-work and marker budgets");
    Expect(editor.Metrics().geometry_rebuilds == 0 && editor.Metrics().routed_links == 0,
        "Panning a bounded pathological flow must preserve cached geometry");

    ImGui::DestroyContext();
}

} // namespace

int main() {
    TestRetainedWorkspaceFacade();
    TestFlowAndDebugRendering();
    TestPathologicalOffscreenFlowIsBounded();
    return EXIT_SUCCESS;
}
