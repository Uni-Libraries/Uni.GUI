#include "ui_test_engine_cases.h"

#include <uni/gui/nodes/nodes.h>

#include <imgui.h>
#include <imgui_test_engine/imgui_te_context.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace {

using namespace Uni::GUI;
using namespace Uni::GUI::Nodes;

enum class NodesScenario {
    LinkCreation,
    ConverterInsertion,
    Reconnect,
    ContextMenus,
    Minimap,
    Breadcrumbs,
    CustomBody,
    Groups,
    Clipboard,
    PolicyRejection,
};

struct NodesFixture final {
    GraphDocument document;
    GraphPresentation presentation;
    CommandStack commands;
    RegistryCatalog nodes;
    NodeUiRegistry ui;
    LinkRouterRegistry routers;
    RegistryCatalog& types{nodes};
    EditorContext editor;
    EditorConfig config;
    EditorCallbacks callbacks;
    GraphPolicy policy;
    GraphId graph;
    GraphId child_graph;
    NodeId source;
    NodeId sink;
    NodeId second_sink;
    PinId output;
    PinId input;
    PinId second_input;
    LinkId link;
    GroupId group;
    EditorResult last_result;
    bool saw_model_change{};
    bool saw_presentation_change{};
    bool saw_active_graph_change{};
    std::string setup_error;

    NodesFixture() : graph(document.RootGraph()) {
        config.snap_to_grid = false;
    }

    template<typename T>
    bool Check(Result<T> result, const std::string_view action) {
        if (result) return true;
        setup_error = std::string{action} + ": " + result.error().message;
        return false;
    }

    bool ExecuteSetup(std::unique_ptr<Command> command, const std::string_view action) {
        return Check(
            commands.Execute(
                std::move(command),
                document,
                presentation,
                types),
            action);
    }

    bool RegisterDescriptors() {
        if (!Check(nodes.RegisterNodeType(NodeTypeDescriptor{
                .type = TypeId{"ui-test.source"},
                .display_name = "Source",
                .category = "Tests",
                .pin_schema = {PinDescriptor{
                    .key = "value",
                    .label = "Value",
                    .type = TypeId{"float"},
                    .direction = PinDirection::Output,
                    .cardinality = PinCardinality::Multiple,
                }},
            }), "register source")) return false;
        if (!Check(nodes.RegisterNodeType(NodeTypeDescriptor{
                .type = TypeId{"ui-test.int-source"},
                .display_name = "Integer source",
                .category = "Tests",
                .pin_schema = {PinDescriptor{
                    .key = "value",
                    .label = "Integer",
                    .type = TypeId{"int"},
                    .direction = PinDirection::Output,
                    .cardinality = PinCardinality::Multiple,
                }},
            }), "register integer source")) return false;
        if (!Check(nodes.RegisterNodeType(NodeTypeDescriptor{
                .type = TypeId{"ui-test.sink"},
                .display_name = "Sink",
                .category = "Tests",
                .pin_schema = {PinDescriptor{
                    .key = "value",
                    .label = "Value",
                    .type = TypeId{"float"},
                    .direction = PinDirection::Input,
                }},
            }), "register sink")) return false;
        if (!Check(nodes.RegisterNodeType(NodeTypeDescriptor{
                .type = TypeId{"ui-test.converter"},
                .display_name = "Integer to float",
                .category = "Tests",
                .pin_schema = {
                    PinDescriptor{
                        .key = "input",
                        .label = "Integer",
                        .type = TypeId{"int"},
                        .direction = PinDirection::Input,
                    },
                    PinDescriptor{
                        .key = "output",
                        .label = "Float",
                        .type = TypeId{"float"},
                        .direction = PinDirection::Output,
                        .cardinality = PinCardinality::Multiple,
                    },
                },
            }), "register converter")) return false;
        if (!Check(nodes.RegisterNodeType(NodeTypeDescriptor{
                .type = TypeId{"ui-test.custom"},
                .display_name = "Custom body",
                .category = "Tests",
                .default_properties = {{"enabled", PropertyValue{false}}},
            }), "register custom node")) return false;
        if (!Check(nodes.RegisterNodeType(NodeTypeDescriptor{
                .type = TypeId{"ui-test.subgraph"},
                .display_name = "Subgraph",
                .category = "Tests",
            }), "register subgraph node")) return false;
        if (!Check(ui.Register(NodeUiDescriptor{
                .type = TypeId{"ui-test.custom"},
                .draw_body = [](NodeUiContext& context) {
                    const auto* value = context.FindProperty("enabled");
                    const bool enabled = value != nullptr && std::get<bool>(*value);
                    if (ImGui::Button(enabled ? "Custom node enabled" : "Enable custom node") && !enabled) {
                        context.SetProperty("enabled", PropertyValue{true});
                    }
                },
                .default_size = {240.0f, 100.0f},
                .header_color = 0xFF69452FU,
            }), "register custom body")) return false;
        return true;
    }

    bool AddNode(
        const TypeId& type,
        const Vec2 position,
        NodeId& node_id,
        std::vector<PinId>& pins) {
        auto creation = nodes.Instantiate(document, type);
        if (!creation) {
            setup_error = "instantiate " + type.Value() + ": " + creation.error().message;
            return false;
        }
        node_id = creation->node.id;
        pins.clear();
        for (const auto& pin : creation->pins) pins.push_back(pin.id);
        return ExecuteSetup(
            std::make_unique<AddNodeCommand>(
                graph,
                std::move(*creation),
                NodePresentation{.position = position}),
            "add node");
    }

    bool AddSourceAndSinks(const bool integer_source, const bool add_second_sink) {
        std::vector<PinId> pins;
        if (!AddNode(
                integer_source ? TypeId{"ui-test.int-source"} : TypeId{"ui-test.source"},
                {70.0f, 90.0f},
                source,
                pins)) return false;
        output = pins.front();
        if (!AddNode(TypeId{"ui-test.sink"}, {390.0f, 70.0f}, sink, pins)) return false;
        input = pins.front();
        if (add_second_sink) {
            if (!AddNode(TypeId{"ui-test.sink"}, {390.0f, 230.0f}, second_sink, pins)) return false;
            second_input = pins.front();
        }
        return true;
    }

    bool ConnectInitialLink() {
        link = document.AllocateLinkId();
        return ExecuteSetup(
            std::make_unique<ConnectPinsCommand>(
                graph,
                Link{.id = link, .output = output, .input = input}),
            "connect initial link");
    }

    bool Setup(const NodesScenario scenario) {
        if (!RegisterDescriptors()) return false;
        std::vector<PinId> pins;
        switch (scenario) {
        case NodesScenario::LinkCreation:
        case NodesScenario::PolicyRejection:
            if (!AddSourceAndSinks(false, false)) return false;
            break;
        case NodesScenario::ConverterInsertion:
             if (!Check(types.RegisterConversion(ConversionDescriptor{
                     .key = ConversionKey{
                         .source_type = TypeId{"int"},
                         .destination_type = TypeId{"float"},
                         .kind = PinKind::Data,
                     },
                    .node_type = TypeId{"ui-test.converter"},
                    .input_pin = "input",
                    .output_pin = "output",
                }), "register conversion")) return false;
            if (!AddSourceAndSinks(true, false)) return false;
            break;
        case NodesScenario::Reconnect:
            if (!AddSourceAndSinks(false, true) || !ConnectInitialLink()) return false;
            break;
        case NodesScenario::ContextMenus:
            if (!AddSourceAndSinks(false, false) || !ConnectInitialLink()) return false;
            break;
        case NodesScenario::Minimap:
            if (!AddNode(TypeId{"ui-test.source"}, {0.0f, 0.0f}, source, pins)) return false;
            output = pins.front();
            if (!AddNode(TypeId{"ui-test.sink"}, {1250.0f, 720.0f}, sink, pins)) return false;
            input = pins.front();
            if (!ConnectInitialLink()) return false;
            break;
        case NodesScenario::Breadcrumbs:
            if (!AddNode(TypeId{"ui-test.subgraph"}, {180.0f, 130.0f}, source, pins)) return false;
            child_graph = document.AllocateGraphId();
            {
                std::vector<std::unique_ptr<Command>> create_child;
                create_child.push_back(std::make_unique<AddGraphCommand>(Graph{
                    .id = child_graph,
                    .display_name = "Child graph",
                    .lifetime = GraphLifetime::Owned,
                }));
                create_child.push_back(std::make_unique<SetNodeSubgraphCommand>(
                    graph,
                    source,
                    SubgraphReference{
                        .ownership = SubgraphOwnership::Owned,
                        .target = DocumentGraphTarget{child_graph},
                    }));
                if (!ExecuteSetup(
                        std::make_unique<CompoundCommand>("Create child graph", std::move(create_child)),
                        "add child graph")) return false;
            }
            break;
        case NodesScenario::CustomBody:
            if (!AddNode(TypeId{"ui-test.custom"}, {160.0f, 120.0f}, source, pins)) return false;
            break;
        case NodesScenario::Groups:
            if (!AddNode(TypeId{"ui-test.source"}, {110.0f, 170.0f}, source, pins)) return false;
            output = pins.front();
            group = presentation.AllocateGroupId();
            if (!ExecuteSetup(
                    std::make_unique<AddGroupCommand>(GroupPresentation{
                        .id = group,
                        .graph = graph,
                        .geometry = GroupGeometry{
                            .position = {60.0f, 110.0f},
                            .size = {300.0f, 220.0f},
                        },
                        .style = MakeGroupStyle(GroupStyle{.title = "Test group"}),
                        .members = {source},
                    }),
                    "add group")) return false;
            break;
        case NodesScenario::Clipboard:
            if (!AddNode(TypeId{"ui-test.source"}, {180.0f, 140.0f}, source, pins)) return false;
            output = pins.front();
            break;
        }

        commands.Clear();
        if (!Check(editor.ResetNavigation(document), "reset editor navigation")) return false;
        if (scenario == NodesScenario::Minimap) editor.SetPan({-620.0f, -360.0f});
        if (scenario == NodesScenario::PolicyRejection) {
            policy.evaluate_operation = [](const OperationPolicyContext&, const OperationIntent& intent)
                -> OperationPolicyDecision {
                if (intent.kind == OperationKind::Connect) {
                    return DenyOperation{"Connections disabled by UI test"};
                }
                return AllowOperation{};
            };
        }
        return true;
    }
};

class NodesTestElement final : public UiElement {
public:
    NodesTestElement() {
        (void)Reset(NodesScenario::LinkCreation);
    }

    bool Reset(const NodesScenario scenario) {
        auto fixture = std::make_unique<NodesFixture>();
        const bool setup = fixture->Setup(scenario);
        m_fixture = std::move(fixture);
        return setup;
    }

    [[nodiscard]] NodesFixture& Fixture() noexcept { return *m_fixture; }

    UiResult<UiElementUpdate> Update(UiState&) override {
        ImGui::SetNextWindowPos({8.0f, 8.0f}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({784.0f, 584.0f}, ImGuiCond_Always);
        ImGui::Begin(
            "Nodes Test",
            nullptr,
            ImGuiWindowFlags_NoDecoration |
                ImGuiWindowFlags_NoMove |
                ImGuiWindowFlags_NoSavedSettings);
        auto& fixture = *m_fixture;
        fixture.last_result = DrawEditor(
            fixture.editor,
            fixture.document,
            fixture.presentation,
            fixture.commands,
            fixture.nodes,
            fixture.ui,
            fixture.routers,
            {760.0f, 548.0f},
            {},
            fixture.config,
            fixture.callbacks,
            fixture.policy);
        fixture.saw_model_change |= fixture.last_result.model_changed;
        fixture.saw_presentation_change |= fixture.last_result.presentation_changed;
        fixture.saw_active_graph_change |= fixture.last_result.active_graph_changed;
        ImGui::End();
        return UiElementUpdate{};
    }

private:
    std::unique_ptr<NodesFixture> m_fixture;
};

NodesTestElement* g_nodes_element{};

[[nodiscard]] std::string PrimitiveRef(const std::string_view primitive, const std::uint64_t id) {
    return "//Nodes Test/**/" + std::string{primitive} + " " + std::to_string(id);
}

[[nodiscard]] std::string ControlRef(
    const std::string_view primitive,
    const std::uint64_t id,
    const std::string_view control) {
    return PrimitiveRef(primitive, id) + " " + std::string{control};
}

NodesFixture* Prepare(ImGuiTestContext* context, const NodesScenario scenario) {
    if (g_nodes_element == nullptr || !g_nodes_element->Reset(scenario)) {
        if (g_nodes_element != nullptr) {
            context->LogError("Nodes fixture setup failed: %s", g_nodes_element->Fixture().setup_error.c_str());
        }
        return nullptr;
    }
    context->Yield(3);
    context->SetRef("Nodes Test");
    return &g_nodes_element->Fixture();
}

void DragPrimitive(ImGuiTestContext* context, const std::string& source, const std::string& destination) {
    context->MouseMove(source.c_str(), ImGuiTestOpFlags_NoCheckHoveredId);
    context->MouseDown();
    context->MouseLiftDragThreshold();
    context->MouseMove(destination.c_str(), ImGuiTestOpFlags_NoCheckHoveredId);
    context->MouseUp();
    context->Yield(2);
}

void DragPrimitiveBy(ImGuiTestContext* context, const std::string& source, const ImVec2 delta) {
    context->MouseMove(source.c_str(), ImGuiTestOpFlags_NoCheckHoveredId);
    context->MouseDown();
    context->MouseLiftDragThreshold();
    context->MouseDragWithDelta(delta);
    context->MouseUp();
    context->Yield(2);
}

void ClickPrimitive(
    ImGuiTestContext* context,
    const std::string& item,
    const ImGuiMouseButton button = ImGuiMouseButton_Left) {
    context->ItemClick(item.c_str(), button, ImGuiTestOpFlags_NoCheckHoveredId);
    context->Yield();
}

void UndoWithKeyboard(ImGuiTestContext* context) {
    context->MouseMove("//Nodes Test/**/Canvas", ImGuiTestOpFlags_NoCheckHoveredId);
    context->Yield();
    context->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Z);
    context->Yield(2);
}

} // namespace

void RegisterUniGuiNodesTestEngineCases(ImGuiTestEngine* engine) {
    ImGuiTest* test = IM_REGISTER_TEST(engine, "unigui", "nodes_link_creation");
    test->TestFunc = [](ImGuiTestContext* context) {
        NodesFixture* fixture = Prepare(context, NodesScenario::LinkCreation);
        IM_CHECK(fixture != nullptr);
        const std::uint64_t revision = fixture->document.ModelRevision();
        DragPrimitive(
            context,
            PrimitiveRef("Pin", fixture->output.Value()),
            PrimitiveRef("Pin", fixture->input.Value()));
        IM_CHECK_EQ(fixture->document.FindGraph(fixture->graph)->links.size(), 1U);
        IM_CHECK_EQ(fixture->document.ModelRevision(), revision + 1);
        IM_CHECK(fixture->saw_model_change && fixture->commands.CanUndo());
        UndoWithKeyboard(context);
        IM_CHECK(fixture->document.FindGraph(fixture->graph)->links.empty());
        IM_CHECK(!fixture->commands.CanUndo() && fixture->commands.CanRedo());
    };

    test = IM_REGISTER_TEST(engine, "unigui", "nodes_converter_insertion");
    test->TestFunc = [](ImGuiTestContext* context) {
        NodesFixture* fixture = Prepare(context, NodesScenario::ConverterInsertion);
        IM_CHECK(fixture != nullptr);
        DragPrimitive(
            context,
            PrimitiveRef("Pin", fixture->output.Value()),
            PrimitiveRef("Pin", fixture->input.Value()));
        const Graph* graph = fixture->document.FindGraph(fixture->graph);
        IM_CHECK_EQ(graph->nodes.size(), 3U);
        IM_CHECK_EQ(graph->links.size(), 2U);
        const auto converter = std::ranges::find_if(graph->nodes, [&](const auto& entry) {
            return entry.first != fixture->source && entry.first != fixture->sink;
        });
        IM_CHECK(converter != graph->nodes.end());
        IM_CHECK(converter->second.type == TypeId{"ui-test.converter"});
        const NodePresentation* converter_presentation = fixture->presentation.FindNode(converter->first);
        IM_CHECK(converter_presentation != nullptr);
        IM_CHECK(converter_presentation->position.x > 250.0f && converter_presentation->position.x < 390.0f);
        IM_CHECK(fixture->saw_model_change && fixture->saw_presentation_change);
        UndoWithKeyboard(context);
        IM_CHECK_EQ(fixture->document.FindGraph(fixture->graph)->nodes.size(), 2U);
        IM_CHECK(fixture->document.FindGraph(fixture->graph)->links.empty());
    };

    test = IM_REGISTER_TEST(engine, "unigui", "nodes_reconnect");
    test->TestFunc = [](ImGuiTestContext* context) {
        NodesFixture* fixture = Prepare(context, NodesScenario::Reconnect);
        IM_CHECK(fixture != nullptr);
        DragPrimitive(
            context,
            PrimitiveRef("Pin", fixture->input.Value()),
            PrimitiveRef("Pin", fixture->second_input.Value()));
        const Link* reconnected = fixture->document.FindLink(fixture->graph, fixture->link);
        IM_CHECK(reconnected != nullptr);
        IM_CHECK(reconnected->output == fixture->output && reconnected->input == fixture->second_input);
        IM_CHECK_EQ(fixture->document.FindGraph(fixture->graph)->links.size(), 1U);
        UndoWithKeyboard(context);
        const Link* restored = fixture->document.FindLink(fixture->graph, fixture->link);
        IM_CHECK(restored != nullptr && restored->input == fixture->input);
    };

    test = IM_REGISTER_TEST(engine, "unigui", "nodes_context_menus");
    test->TestFunc = [](ImGuiTestContext* context) {
        NodesFixture* fixture = Prepare(context, NodesScenario::ContextMenus);
        IM_CHECK(fixture != nullptr);
        ClickPrimitive(context, PrimitiveRef("Node", fixture->source.Value()), ImGuiMouseButton_Right);
        context->MenuClick("//$FOCUSED/Collapse");
        context->Yield(2);
        IM_CHECK(fixture->presentation.FindNode(fixture->source)->collapsed);
        IM_CHECK(fixture->saw_presentation_change);
        UndoWithKeyboard(context);
        IM_CHECK(!fixture->presentation.FindNode(fixture->source)->collapsed);

        ClickPrimitive(context, PrimitiveRef("Link", fixture->link.Value()), ImGuiMouseButton_Right);
        context->MenuClick("//$FOCUSED/Routing/Straight");
        context->Yield(2);
        IM_CHECK(fixture->presentation.FindLink(fixture->link) != nullptr);
        IM_CHECK(
            fixture->presentation.FindLink(fixture->link)->Style().router == StraightLinkRouterType());
        UndoWithKeyboard(context);
        IM_CHECK(fixture->presentation.FindLink(fixture->link) == nullptr);

        ClickPrimitive(context, "//Nodes Test/**/Canvas", ImGuiMouseButton_Right);
        context->MenuClick("//$FOCUSED/Add comment");
        context->Yield(2);
        IM_CHECK_EQ(fixture->presentation.Groups().size(), 1U);
        IM_CHECK(fixture->presentation.Groups().begin()->second.style->kind == GroupKind::Comment);
        UndoWithKeyboard(context);
        IM_CHECK(fixture->presentation.Groups().empty());
    };

    test = IM_REGISTER_TEST(engine, "unigui", "nodes_minimap");
    test->TestFunc = [](ImGuiTestContext* context) {
        NodesFixture* fixture = Prepare(context, NodesScenario::Minimap);
        IM_CHECK(fixture != nullptr);
        const Vec2 before = fixture->editor.Pan();
        ClickPrimitive(context, "//Nodes Test/**/Minimap");
        context->Yield(2);
        const Vec2 after = fixture->editor.Pan();
        IM_CHECK(std::abs(after.x - before.x) > 20.0f || std::abs(after.y - before.y) > 20.0f);
        IM_CHECK(!fixture->saw_model_change && !fixture->saw_presentation_change);
        IM_CHECK(!fixture->commands.CanUndo());
    };

    test = IM_REGISTER_TEST(engine, "unigui", "nodes_breadcrumbs");
    test->TestFunc = [](ImGuiTestContext* context) {
        NodesFixture* fixture = Prepare(context, NodesScenario::Breadcrumbs);
        IM_CHECK(fixture != nullptr);
        const std::string node = PrimitiveRef("Node", fixture->source.Value());
        context->ItemDoubleClick(node.c_str(), ImGuiTestOpFlags_NoCheckHoveredId);
        context->Yield(2);
        IM_CHECK(fixture->editor.ActiveGraph() == fixture->child_graph);
        IM_CHECK_EQ(fixture->editor.Breadcrumbs().size(), 2U);
        IM_CHECK(fixture->saw_active_graph_change);
        context->ItemClick("//Nodes Test/**/Root");
        context->Yield(2);
        IM_CHECK(fixture->editor.ActiveGraph() == fixture->graph);
        IM_CHECK_EQ(fixture->editor.Breadcrumbs().size(), 1U);
    };

    test = IM_REGISTER_TEST(engine, "unigui", "nodes_custom_body");
    test->TestFunc = [](ImGuiTestContext* context) {
        NodesFixture* fixture = Prepare(context, NodesScenario::CustomBody);
        IM_CHECK(fixture != nullptr);
        context->ItemClick("//Nodes Test/**/Enable custom node");
        context->Yield(2);
        const NodeInstance* node = fixture->document.FindNode(fixture->graph, fixture->source);
        IM_CHECK(node != nullptr && std::get<bool>(node->properties.at("enabled")));
        IM_CHECK(fixture->saw_model_change && fixture->commands.CanUndo());
        UndoWithKeyboard(context);
        node = fixture->document.FindNode(fixture->graph, fixture->source);
        IM_CHECK(node != nullptr && !std::get<bool>(node->properties.at("enabled")));
    };

    test = IM_REGISTER_TEST(engine, "unigui", "nodes_groups");
    test->TestFunc = [](ImGuiTestContext* context) {
        NodesFixture* fixture = Prepare(context, NodesScenario::Groups);
        IM_CHECK(fixture != nullptr);
        const Vec2 group_before = fixture->presentation.FindGroup(fixture->group)->geometry.position;
        const Vec2 node_before = fixture->presentation.FindNode(fixture->source)->position;
        DragPrimitiveBy(context, PrimitiveRef("Group", fixture->group.Value()), {48.0f, 32.0f});
        IM_CHECK((fixture->presentation.FindGroup(fixture->group)->geometry.position ==
                  group_before + Vec2{48.0f, 32.0f}));
        IM_CHECK((fixture->presentation.FindNode(fixture->source)->position == node_before + Vec2{48.0f, 32.0f}));
        IM_CHECK(fixture->saw_presentation_change);
        UndoWithKeyboard(context);
        IM_CHECK(fixture->presentation.FindGroup(fixture->group)->geometry.position == group_before);
        IM_CHECK(fixture->presentation.FindNode(fixture->source)->position == node_before);

        ClickPrimitive(context, ControlRef("Group", fixture->group.Value(), "collapse"));
        IM_CHECK(fixture->presentation.FindGroup(fixture->group)->geometry.collapsed);
        UndoWithKeyboard(context);
        IM_CHECK(!fixture->presentation.FindGroup(fixture->group)->geometry.collapsed);
    };

    test = IM_REGISTER_TEST(engine, "unigui", "nodes_clipboard");
    test->TestFunc = [](ImGuiTestContext* context) {
        NodesFixture* fixture = Prepare(context, NodesScenario::Clipboard);
        IM_CHECK(fixture != nullptr);
        ClickPrimitive(context, PrimitiveRef("Node", fixture->source.Value()));
        IM_CHECK_EQ(fixture->editor.Selection().nodes.size(), 1U);
        fixture->saw_model_change = false;
        fixture->saw_presentation_change = false;
        fixture->commands.Clear();
        context->KeyPress(ImGuiMod_Ctrl | ImGuiKey_C);
        context->KeyPress(ImGuiMod_Ctrl | ImGuiKey_V);
        context->Yield(3);
        IM_CHECK_EQ(fixture->document.FindGraph(fixture->graph)->nodes.size(), 2U);
        IM_CHECK_EQ(fixture->editor.Selection().nodes.size(), 1U);
        const NodeId pasted = fixture->editor.Selection().nodes.front();
        IM_CHECK(pasted != fixture->source);
        IM_CHECK(fixture->presentation.FindNode(pasted) != nullptr);
        IM_CHECK(fixture->saw_model_change && fixture->saw_presentation_change);
        UndoWithKeyboard(context);
        IM_CHECK_EQ(fixture->document.FindGraph(fixture->graph)->nodes.size(), 1U);
        IM_CHECK(fixture->document.FindNode(fixture->graph, fixture->source) != nullptr);
        IM_CHECK(!fixture->commands.CanUndo() && fixture->commands.CanRedo());
    };

    test = IM_REGISTER_TEST(engine, "unigui", "nodes_policy_rejection");
    test->TestFunc = [](ImGuiTestContext* context) {
        NodesFixture* fixture = Prepare(context, NodesScenario::PolicyRejection);
        IM_CHECK(fixture != nullptr);
        const std::uint64_t revision = fixture->document.ModelRevision();
        DragPrimitive(
            context,
            PrimitiveRef("Pin", fixture->output.Value()),
            PrimitiveRef("Pin", fixture->input.Value()));
        IM_CHECK(fixture->document.FindGraph(fixture->graph)->links.empty());
        IM_CHECK_EQ(fixture->document.ModelRevision(), revision);
        IM_CHECK_STR_EQ(fixture->editor.LastError().c_str(), "Connections disabled by UI test");
        IM_CHECK(!fixture->saw_model_change && !fixture->saw_presentation_change);
        IM_CHECK(!fixture->commands.CanUndo());
    };
}

Uni::GUI::UiResult<void> InstallUniGuiNodesTestElement(Uni::GUI::UiApp& app) {
    auto element = std::make_unique<NodesTestElement>();
    g_nodes_element = element.get();
    if (auto added = app.AddElement(std::move(element)); !added) {
        g_nodes_element = nullptr;
        return std::unexpected(std::move(added.error()));
    }
    return {};
}
