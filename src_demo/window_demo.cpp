//
// Includes
//

// ImGUI
#include <imgui.h>
#include <imgui_stdlib.h>
#include <implot.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

// Uni.GUI
#include <uni/gui/app.h>

#include "window_demo.h"

//
// Implementation
//

namespace Uni::GUI::Example {
    namespace {
        using namespace Nodes;

        [[nodiscard]] Error DemoError(std::string message) {
            return Error{ErrorCode::CommandFailed, std::move(message)};
        }

        [[nodiscard]] GraphInterface SignalInterface() {
            return GraphInterface{
                .version = 1,
                .pins = {
                    GraphInterfacePin{
                        .key = "signal_in",
                        .label = "Signal In",
                        .type = TypeId{"float"},
                        .direction = PinDirection::Input,
                    },
                    GraphInterfacePin{
                        .key = "signal_out",
                        .label = "Signal Out",
                        .type = TypeId{"float"},
                        .direction = PinDirection::Output,
                        .caller_cardinality = PinCardinality::Multiple,
                        .boundary_cardinality = PinCardinality::Single,
                    },
                },
            };
        }

        [[nodiscard]] PinId FindPinByKey(
            const GraphDocument& document,
            const GraphId graph,
            const NodeId node,
            const std::string_view key) {
            const auto* instance = document.FindNode(graph, node);
            if (instance == nullptr) {
                return {};
            }
            for (const PinId pin_id : instance->pins) {
                const auto* pin = document.FindPin(graph, pin_id);
                if (pin != nullptr && pin->key == key) {
                    return pin_id;
                }
            }
            return {};
        }
    }

    void WindowDemo::SetNodeStatus(std::string status, const bool failed) {
        m_node_io_status = std::move(status);
        m_node_io_failed = failed;
    }

    bool WindowDemo::ExecuteNodeDemoCommand(std::unique_ptr<Nodes::Command> command, std::string success) {
        const auto started = std::chrono::steady_clock::now();
        auto result = m_node_commands.Execute(
            std::move(command),
            m_node_document,
            m_node_presentation,
            m_registry,
            m_node_policy);
        m_node_last_operation_ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - started).count();
        if (!result) {
            SetNodeStatus("Command rejected: " + result.error().message, true);
            return false;
        }
        SetNodeStatus(std::move(success));
        return true;
    }

    Result<void> WindowDemo::InitializeNodeDemo() {
        m_node_presentation = GraphPresentation{};
        m_registry = RegistryCatalog{};
        m_node_ui_registry = NodeUiRegistry{};
        m_node_routers = LinkRouterRegistry{};
        if (const auto* pending = m_node_commands.PendingOperation()) {
            (void)m_node_commands.Cancel(pending->id);
        }
        m_node_commands.Clear();
        m_node_commands.SetHistoryLimit(256);
        m_node_editor = EditorContext{};
        m_node_document = GraphDocument{};
        m_node_migrations = DocumentMigrationRegistry{2};
        m_node_assets = GraphAssetRegistry{};
        m_node_warnings.clear();
        m_node_ids = {};

        m_node_policy.evaluate_operation = [this](const OperationPolicyContext&, const OperationIntent& intent)
            -> OperationPolicyDecision {
            if (m_node_deny_connect && intent.kind == OperationKind::Connect)
                return DenyOperation{"Stage 3 policy denies connections"};
            if (m_node_deny_create && intent.kind == OperationKind::AddNode)
                return DenyOperation{"Stage 3 policy denies node creation"};
            if (m_node_deny_delete && intent.kind == OperationKind::DeleteElements)
                return DenyOperation{"Stage 3 policy denies deletion"};
            if (m_node_deny_group &&
                (intent.kind == OperationKind::AddGroup || intent.kind == OperationKind::RemoveGroup ||
                 intent.kind == OperationKind::SetGroupGeometry || intent.kind == OperationKind::SetGroupStyle ||
                 intent.kind == OperationKind::SetGroupMembers))
                return DenyOperation{"Stage 3 policy denies group changes"};
            return AllowOperation{};
        };

        if (auto registered = m_node_migrations.Register(1, [](DocumentMigrationContext& context) -> Result<void> {
                for (auto& graph : context.archive.graphs) {
                    std::vector<NodeId> node_ids;
                    node_ids.reserve(graph.nodes.size());
                    for (const auto& [node_id, node] : graph.nodes) {
                        (void)node;
                        node_ids.push_back(node_id);
                    }
                    for (const NodeId node_id : node_ids) {
                        auto node = graph.nodes.at(node_id);
                        node.properties.insert_or_assign("document_migrated", PropertyValue{true});
                        graph.nodes.insert_or_assign(node_id, std::move(node));
                    }
                }
                return {};
            }); !registered) {
            return std::unexpected(registered.error());
        }

        auto register_node = [this](NodeTypeDescriptor descriptor) -> Result<void> {
            return m_registry.RegisterNodeType(std::move(descriptor));
        };
        if (auto result = register_node(NodeTypeDescriptor{
                .type = TypeId{"demo.constant"},
                .display_name = "Float Constant",
                .category = "Stage 1 / Math",
                .static_pins = {
                    PinDescriptor{
                        .key = "value",
                        .label = "Value",
                        .type = TypeId{"float"},
                        .direction = PinDirection::Output,
                        .cardinality = PinCardinality::Multiple,
                    },
                },
                .default_properties = {{"value", PropertyValue{1.0}}},
            }); !result) return result;
        if (auto result = register_node(NodeTypeDescriptor{
                .type = TypeId{"demo.add"},
                .display_name = "Add",
                .category = "Stage 1 / Math",
                .static_pins = {
                    PinDescriptor{.key = "a", .label = "A", .type = TypeId{"float"}},
                    PinDescriptor{.key = "b", .label = "B", .type = TypeId{"float"}},
                    PinDescriptor{
                        .key = "result",
                        .label = "Result",
                        .type = TypeId{"float"},
                        .direction = PinDirection::Output,
                        .cardinality = PinCardinality::Multiple,
                    },
                },
            }); !result) return result;
        if (auto result = register_node(NodeTypeDescriptor{
                .type = TypeId{"demo.int_constant"},
                .display_name = "Integer Constant",
                .category = "Stage 3 / Conversion",
                .version = 2,
                .static_pins = {
                    PinDescriptor{
                        .key = "value",
                        .label = "Integer",
                        .type = TypeId{"int"},
                        .direction = PinDirection::Output,
                        .cardinality = PinCardinality::Multiple,
                    },
                },
                .default_properties = {{"value", PropertyValue{std::int64_t{7}}}},
                .behavior = std::make_shared<const NodeBehavior>(NodeBehavior{
                    .migrate = [](NodeMigrationContext& context) -> Result<void> {
                    if (context.from_version != 1 || context.to_version != 2) {
                        return std::unexpected(DemoError("Unsupported integer node migration step"));
                    }
                    context.creation.node.properties.insert_or_assign("migrated", PropertyValue{true});
                    return {};
                    },
                }),
            }); !result) return result;
        if (auto result = register_node(NodeTypeDescriptor{
                .type = TypeId{"demo.int_to_float"},
                .display_name = "Int to Float",
                .category = "Stage 3 / Conversion",
                .static_pins = {
                    PinDescriptor{.key = "in", .label = "Integer", .type = TypeId{"int"}},
                    PinDescriptor{
                        .key = "out",
                        .label = "Float",
                        .type = TypeId{"float"},
                        .direction = PinDirection::Output,
                        .cardinality = PinCardinality::Multiple,
                    },
                },
            }); !result) return result;
        if (auto result = register_node(NodeTypeDescriptor{
                .type = TypeId{"demo.float_sink"},
                .display_name = "Float Sink",
                .category = "Stage 3 / Conversion",
                .static_pins = {
                    PinDescriptor{.key = "value", .label = "Float", .type = TypeId{"float"}},
                },
            }); !result) return result;
        if (auto result = register_node(NodeTypeDescriptor{
                .type = TypeId{"demo.subgraph"},
                .display_name = "Graph Call",
                .category = "Stage 4 / Subgraphs",
            }); !result) return result;
        if (auto result = register_node(NodeTypeDescriptor{
                .type = TypeId{"demo.asset_call"},
                .display_name = "Graph Asset Call",
                .category = "Stage 4 / Assets",
            }); !result) return result;

        if (auto result = m_registry.RegisterConversion(ConversionDescriptor{
                     .key = ConversionKey{
                         .source_type = TypeId{"int"},
                         .destination_type = TypeId{"float"},
                         .kind = PinKind::Data,
                     },
                     .node_type = TypeId{"demo.int_to_float"},
                     .input_pin = "in",
                     .output_pin = "out",
                 }); !result) {
            return std::unexpected(std::move(result.error()));
        }

        const auto draw_float_value = [](NodeUiContext& context) {
            double value = 0.0;
            if (const auto* property = context.FindProperty("value")) {
                if (const auto* current = std::get_if<double>(property)) value = *current;
            }
            const bool changed = ImGui::InputDouble("##float-value", &value, 0.1, 1.0, "%.3f");
            context.EditProperty("value", PropertyValue{value}, changed);
        };
        const auto draw_int_value = [](NodeUiContext& context) {
            std::int64_t value = 0;
            if (const auto* property = context.FindProperty("value")) {
                if (const auto* current = std::get_if<std::int64_t>(property)) value = *current;
            }
            const bool changed = ImGui::InputScalar("##int-value", ImGuiDataType_S64, &value);
            context.EditProperty("value", PropertyValue{value}, changed);
        };
        if (auto result = m_node_ui_registry.Register(NodeUiDescriptor{
                .type = TypeId{"demo.constant"},
                .draw_body = draw_float_value,
                .draw_inspector = draw_float_value,
                .default_size = {220.0f, 90.0f},
                .header_color = IM_COL32(55, 92, 140, 255),
            }); !result) return result;
        if (auto result = m_node_ui_registry.Register(NodeUiDescriptor{
                .type = TypeId{"demo.int_constant"},
                .draw_body = draw_int_value,
                .draw_inspector = draw_int_value,
                .default_size = {220.0f, 90.0f},
                .header_color = IM_COL32(150, 92, 48, 255),
            }); !result) return result;
        if (auto result = m_node_ui_registry.Register(NodeUiDescriptor{
                .type = TypeId{"demo.add"},
                .draw_inspector = [](NodeUiContext& context) {
                    if (ImGui::Button("Add dynamic input")) {
                        const std::string key = "dynamic_" + std::to_string(context.Node().pins.size());
                        (void)context.AddDynamicPin(PinDescriptor{
                            .key = key,
                            .label = key,
                            .type = TypeId{"float"},
                        });
                    }
                    for (const PinId pin_id : context.Pins()) {
                        const auto* pin = context.FindPin(pin_id);
                        if (pin == nullptr || pin->storage != PinStorage::Dynamic) continue;
                        ImGui::PushID(static_cast<int>(pin_id.Value()));
                        ImGui::TextUnformatted(pin->label.c_str());
                        ImGui::SameLine();
                        if (ImGui::SmallButton("Remove")) context.RemoveDynamicPin(pin_id);
                        ImGui::PopID();
                    }
                },
                .default_size = {220.0f, 110.0f},
                .header_color = IM_COL32(105, 68, 140, 255),
            }); !result) return result;
        if (auto result = m_node_ui_registry.Register(NodeUiDescriptor{
                .type = TypeId{"demo.int_to_float"},
                .draw_body = [](NodeUiContext&) { ImGui::TextDisabled("Automatic conversion"); },
                .default_size = {210.0f, 90.0f},
                .header_color = IM_COL32(160, 102, 42, 255),
            }); !result) return result;
        if (auto result = m_node_ui_registry.Register(NodeUiDescriptor{
                .type = TypeId{"demo.float_sink"},
                .draw_body = [](NodeUiContext&) { ImGui::TextDisabled("Converted float input"); },
                .default_size = {210.0f, 90.0f},
                .header_color = IM_COL32(42, 125, 94, 255),
                .layout = [](const NodeUiLayoutContext& context) -> Result<NodeUiLayout> {
                    NodeUiLayout layout;
                    for (const PinId pin : context.node.pins) {
                        layout.pins.push_back(PinPlacement{
                            .pin = pin,
                            .position = {context.node_size.x * 0.5f, 0.0f},
                            .outward_normal = {0.0f, -1.0f},
                            .label = PinLabelPlacement{
                                .offset = {0.0f, 9.0f},
                                .pivot = {0.5f, 0.0f},
                            },
                        });
                    }
                    return layout;
                },
            }); !result) return result;
        if (auto result = m_node_ui_registry.RegisterPinStyle(
                TypeId{"float"},
                [](const PinStyleContext& context) {
                    return PinStyle{
                        .color = context.connected
                            ? std::optional<std::uint32_t>{IM_COL32(95, 220, 155, 255)}
                            : std::optional<std::uint32_t>{IM_COL32(90, 170, 240, 255)},
                        .radius = 6.0f,
                        .shape = PinShape::Diamond,
                    };
                }); !result) return result;
        if (auto result = m_node_ui_registry.RegisterPinStyle(
                TypeId{"int"},
                [](const PinStyleContext&) {
                    return PinStyle{
                        .color = IM_COL32(238, 162, 76, 255),
                        .radius = 6.0f,
                        .shape = PinShape::Square,
                    };
                }); !result) return result;

        const auto execute = [this](std::unique_ptr<Command> command) -> Result<void> {
            auto result = m_node_commands.Execute(
                std::move(command),
                m_node_document,
                m_node_presentation,
                m_registry,
                m_node_policy);
            return result ? Result<void>{} : std::unexpected(result.error());
        };
        if (auto result = execute(std::make_unique<SetSchemaVersionCommand>(2)); !result) return result;

        const GraphId root = m_node_document.RootGraph();
        m_node_ids.root = root;
        auto constant = m_registry.Instantiate(m_node_document, TypeId{"demo.constant"}, "Stage 1 Constant");
        auto add = m_registry.Instantiate(m_node_document, TypeId{"demo.add"}, "Stage 1 Add");
        auto integer = m_registry.Instantiate(m_node_document, TypeId{"demo.int_constant"}, "Int Source");
        auto sink = m_registry.Instantiate(m_node_document, TypeId{"demo.float_sink"}, "Float Sink");
        if (!constant || !add || !integer || !sink) {
            return std::unexpected(DemoError("Failed to instantiate feature-lab nodes"));
        }
        m_node_ids.metric_node = constant->node.id;
        m_node_ids.conversion_source = integer->node.id;
        m_node_ids.conversion_sink = sink->node.id;
        m_node_ids.conversion_output = integer->pins.front().id;
        m_node_ids.conversion_input = sink->pins.front().id;
        const NodeId constant_id = constant->node.id;
        const NodeId add_id = add->node.id;
        const PinId constant_output = constant->pins.front().id;
        const PinId add_input = add->pins.front().id;
        std::vector<std::unique_ptr<Command>> base_commands;
        base_commands.push_back(std::make_unique<AddNodeCommand>(
            root, std::move(*constant), NodePresentation{.position = {60.0f, 100.0f}}));
        base_commands.push_back(std::make_unique<AddNodeCommand>(
            root, std::move(*add), NodePresentation{.position = {360.0f, 100.0f}}));
        base_commands.push_back(std::make_unique<AddNodeCommand>(
            root, std::move(*integer), NodePresentation{.position = {60.0f, 430.0f}}));
        base_commands.push_back(std::make_unique<AddNodeCommand>(
            root, std::move(*sink), NodePresentation{.position = {620.0f, 430.0f}}));
        const LinkId base_link = m_node_document.AllocateLinkId();
        base_commands.push_back(std::make_unique<ConnectPinsCommand>(
            root,
            Link{
                .id = base_link,
                .output = constant_output,
                .input = add_input,
            }));
        base_commands.push_back(std::make_unique<SetLinkRouterCommand>(
            base_link, OrthogonalLinkRouterType()));
        if (auto result = execute(std::make_unique<CompoundCommand>("Seed stages 1-5", std::move(base_commands)));
            !result) return result;

        const auto add_comment = [&](Vec2 position, Vec2 size, std::string title, std::string body,
                                     std::vector<NodeId> members) -> Result<void> {
            return execute(std::make_unique<AddGroupCommand>(GroupPresentation{
                .id = m_node_presentation.AllocateGroupId(),
                .graph = root,
                .geometry = GroupGeometry{.position = position, .size = size},
                .style = MakeGroupStyle(GroupStyle{
                    .title = std::move(title),
                    .body = std::move(body),
                    .color = IM_COL32(45, 74, 92, 70),
                    .kind = GroupKind::Comment,
                }),
                .members = std::move(members),
            }));
        };
        if (auto result = add_comment(
                {20.0f, 40.0f}, {590.0f, 260.0f}, "Stages 1/5: transactions and routing",
                "Edit values, inspect COW metrics, and switch the orthogonal link router from its context menu.",
                {constant_id, add_id}); !result) return result;
        if (auto result = add_comment(
                {20.0f, 355.0f}, {850.0f, 255.0f}, "Stage 3: conversion pipeline",
                "Connect Int Source to Float Sink or use Insert conversion.",
                {m_node_ids.conversion_source, m_node_ids.conversion_sink}); !result) return result;

        auto make_call = [&](const char* name) -> Result<NodeCreation> {
            return m_registry.Instantiate(m_node_document, TypeId{"demo.subgraph"}, name);
        };
        auto owned_call = make_call("Owned Pipeline");
        if (!owned_call) return std::unexpected(owned_call.error());
        m_node_ids.owned_call = owned_call->node.id;
        m_node_ids.owned_graph = m_node_document.AllocateGraphId();
        std::vector<std::unique_ptr<Command>> owned_commands;
        owned_commands.push_back(std::make_unique<AddNodeCommand>(
            root, std::move(*owned_call), NodePresentation{.position = {60.0f, 760.0f}}));
        owned_commands.push_back(std::make_unique<AddGraphCommand>(Graph{
            .id = m_node_ids.owned_graph,
            .display_name = "Owned Filter",
            .lifetime = GraphLifetime::Owned,
        }));
        owned_commands.push_back(std::make_unique<SetNodeSubgraphCommand>(
            root,
            m_node_ids.owned_call,
            SubgraphReference{
                .ownership = SubgraphOwnership::Owned,
                .target = DocumentGraphTarget{m_node_ids.owned_graph},
            }));
        owned_commands.push_back(std::make_unique<SetGraphInterfaceCommand>(
            m_node_ids.owned_graph, SignalInterface()));
        if (auto result = execute(std::make_unique<CompoundCommand>(
                "Create owned subgraph", std::move(owned_commands))); !result) return result;

        m_node_ids.reusable_graph = m_node_document.AllocateGraphId();
        auto reference_a = make_call("Reusable Call A");
        auto reference_b = make_call("Reusable Call B");
        if (!reference_a || !reference_b) return std::unexpected(DemoError("Failed to create reusable call-sites"));
        m_node_ids.referenced_call_a = reference_a->node.id;
        m_node_ids.referenced_call_b = reference_b->node.id;
        std::vector<std::unique_ptr<Command>> reusable_commands;
        reusable_commands.push_back(std::make_unique<AddGraphCommand>(Graph{
            .id = m_node_ids.reusable_graph,
            .display_name = "Reusable Function",
        }));
        reusable_commands.push_back(std::make_unique<SetGraphInterfaceCommand>(
            m_node_ids.reusable_graph, SignalInterface()));
        reusable_commands.push_back(std::make_unique<AddNodeCommand>(
            root, std::move(*reference_a), NodePresentation{.position = {360.0f, 760.0f}}));
        reusable_commands.push_back(std::make_unique<AddNodeCommand>(
            root, std::move(*reference_b), NodePresentation{.position = {650.0f, 760.0f}}));
        reusable_commands.push_back(std::make_unique<SetNodeSubgraphCommand>(
            root,
            m_node_ids.referenced_call_a,
            SubgraphReference{
                .ownership = SubgraphOwnership::Referenced,
                .target = DocumentGraphTarget{m_node_ids.reusable_graph},
            }));
        reusable_commands.push_back(std::make_unique<SetNodeSubgraphCommand>(
            root,
            m_node_ids.referenced_call_b,
            SubgraphReference{
                .ownership = SubgraphOwnership::Referenced,
                .target = DocumentGraphTarget{m_node_ids.reusable_graph},
            }));
        if (auto result = execute(std::make_unique<CompoundCommand>(
                "Create reusable subgraph", std::move(reusable_commands))); !result) return result;

        const auto wire_boundary_graph = [&](GraphDocument& document, GraphPresentation& presentation,
                                             CommandStack& commands, const GraphId graph,
                                             const GraphPolicy& policy) -> Result<void> {
            const auto* value = document.FindGraph(graph);
            if (value == nullptr) return std::unexpected(DemoError("Boundary graph is missing"));
            NodeId input_node;
            NodeId output_node;
            for (const auto& [node_id, node] : value->nodes) {
                if (node.role == NodeRole::BoundaryInput) input_node = node_id;
                if (node.role == NodeRole::BoundaryOutput) output_node = node_id;
            }
            if (!input_node || !output_node) return std::unexpected(DemoError("Boundary nodes are missing"));
            const auto* input_boundary = document.FindNode(graph, input_node);
            const auto* output_boundary = document.FindNode(graph, output_node);
            if (input_boundary == nullptr || output_boundary == nullptr ||
                input_boundary->pins.empty() || output_boundary->pins.empty()) {
                return std::unexpected(DemoError("Boundary pins are missing"));
            }
            std::vector<std::unique_ptr<Command>> wire;
            wire.push_back(std::make_unique<SetNodePresentationCommand>(
                input_node, NodePresentation{.position = {60.0f, 120.0f}}));
            wire.push_back(std::make_unique<SetNodePresentationCommand>(
                output_node, NodePresentation{.position = {430.0f, 120.0f}}));
            wire.push_back(std::make_unique<ConnectPinsCommand>(
                graph,
                Link{
                    .id = document.AllocateLinkId(),
                    .output = input_boundary->pins.front(),
                    .input = output_boundary->pins.front(),
                }));
            auto result = commands.Execute(
                std::make_unique<CompoundCommand>("Wire graph interface", std::move(wire)),
                document,
                presentation,
                m_registry,
                policy);
            return result ? Result<void>{} : std::unexpected(result.error());
        };
        if (auto result = wire_boundary_graph(
                m_node_document, m_node_presentation, m_node_commands, m_node_ids.owned_graph, m_node_policy);
            !result) return result;
        if (auto result = wire_boundary_graph(
                m_node_document, m_node_presentation, m_node_commands, m_node_ids.reusable_graph, m_node_policy);
            !result) return result;

        GraphAsset asset;
        asset.id = GraphAssetId{"demo.signal_asset"};
        CommandStack asset_commands;
        GraphPolicy asset_policy;
        auto asset_interface = asset_commands.Execute(
            std::make_unique<SetGraphInterfaceCommand>(asset.document.RootGraph(), SignalInterface()),
            asset.document,
            asset.presentation,
            m_registry,
            asset_policy);
        if (!asset_interface) return std::unexpected(asset_interface.error());
        if (auto result = wire_boundary_graph(
                asset.document, asset.presentation, asset_commands, asset.document.RootGraph(), asset_policy);
            !result) return result;
        if (auto result = m_node_assets.Write(std::move(asset), GraphAssetWriteMode::Insert); !result) {
            return std::unexpected(result.error());
        }

        auto asset_call = m_registry.Instantiate(m_node_document, TypeId{"demo.asset_call"}, "Asset Filter");
        if (!asset_call) return std::unexpected(asset_call.error());
        m_node_ids.asset_call = asset_call->node.id;
        std::vector<std::unique_ptr<Command>> asset_call_commands;
        asset_call_commands.push_back(std::make_unique<AddNodeCommand>(
            root, std::move(*asset_call), NodePresentation{.position = {940.0f, 760.0f}}));
        asset_call_commands.push_back(std::make_unique<SetNodeSubgraphCommand>(
            root,
            m_node_ids.asset_call,
            SubgraphReference{
                .ownership = SubgraphOwnership::Referenced,
                .target = GraphAssetTarget{
                    .asset = GraphAssetId{"demo.signal_asset"},
                    .interface = SignalInterface(),
                },
            }));
        if (auto result = execute(std::make_unique<CompoundCommand>(
                "Create graph asset call", std::move(asset_call_commands))); !result) return result;

        if (auto result = add_comment(
                {20.0f, 690.0f}, {1190.0f, 280.0f}, "Stage 4: ownership and reusable assets",
                "Duplicate Owned Pipeline for deep-copy; both reusable calls share one target.",
                {m_node_ids.owned_call, m_node_ids.referenced_call_a,
                 m_node_ids.referenced_call_b, m_node_ids.asset_call}); !result) return result;

        m_node_ids.producer_graph = m_node_document.AllocateGraphId();
        m_node_ids.consumer_graph = m_node_document.AllocateGraphId();
        const NodeId sender = m_node_document.AllocateNodeId();
        const PinId sender_pin = m_node_document.AllocatePinId();
        const NodeId receiver = m_node_document.AllocateNodeId();
        const PinId receiver_pin = m_node_document.AllocatePinId();
        m_node_ids.reverse_sender = m_node_document.AllocateNodeId();
        m_node_ids.reverse_sender_pin = m_node_document.AllocatePinId();
        m_node_ids.reverse_receiver = m_node_document.AllocateNodeId();
        m_node_ids.reverse_receiver_pin = m_node_document.AllocatePinId();
        m_node_ids.intergraph_link = m_node_document.AllocateIntergraphLinkId();
        std::vector<std::unique_ptr<Command>> intergraph_commands;
        intergraph_commands.push_back(std::make_unique<AddGraphCommand>(Graph{
            .id = m_node_ids.producer_graph,
            .display_name = "Producer Area",
        }));
        intergraph_commands.push_back(std::make_unique<AddGraphCommand>(Graph{
            .id = m_node_ids.consumer_graph,
            .display_name = "Consumer Area",
        }));
        intergraph_commands.push_back(std::make_unique<AddNodeCommand>(
            m_node_ids.producer_graph,
            NodeCreation{
                .node = NodeInstance{
                    .id = sender,
                    .type = TypeId{"demo.intergraph_sender"},
                    .display_name = "Send to Consumer",
                    .role = NodeRole::IntergraphOutput,
                },
                .pins = {PinInstance{
                    .id = sender_pin,
                    .node = sender,
                    .key = "channel",
                    .label = "Channel",
                    .type = TypeId{"float"},
                    .direction = PinDirection::Input,
                    .storage = PinStorage::Dynamic,
                }},
            },
            NodePresentation{.position = {100.0f, 120.0f}}));
        intergraph_commands.push_back(std::make_unique<AddNodeCommand>(
            m_node_ids.consumer_graph,
            NodeCreation{
                .node = NodeInstance{
                    .id = receiver,
                    .type = TypeId{"demo.intergraph_receiver"},
                    .display_name = "Receive from Producer",
                    .role = NodeRole::IntergraphInput,
                },
                .pins = {PinInstance{
                    .id = receiver_pin,
                    .node = receiver,
                    .key = "channel",
                    .label = "Channel",
                    .type = TypeId{"float"},
                    .direction = PinDirection::Output,
                    .storage = PinStorage::Dynamic,
                }},
            },
            NodePresentation{.position = {100.0f, 120.0f}}));
        intergraph_commands.push_back(std::make_unique<AddNodeCommand>(
            m_node_ids.consumer_graph,
            NodeCreation{
                .node = NodeInstance{
                    .id = m_node_ids.reverse_sender,
                    .type = TypeId{"demo.intergraph_sender"},
                    .display_name = "Rejected Reverse Send",
                    .role = NodeRole::IntergraphOutput,
                },
                .pins = {PinInstance{
                    .id = m_node_ids.reverse_sender_pin,
                    .node = m_node_ids.reverse_sender,
                    .key = "reverse",
                    .label = "Reverse",
                    .type = TypeId{"float"},
                    .direction = PinDirection::Input,
                    .storage = PinStorage::Dynamic,
                }},
            },
            NodePresentation{.position = {100.0f, 340.0f}}));
        intergraph_commands.push_back(std::make_unique<AddNodeCommand>(
            m_node_ids.producer_graph,
            NodeCreation{
                .node = NodeInstance{
                    .id = m_node_ids.reverse_receiver,
                    .type = TypeId{"demo.intergraph_receiver"},
                    .display_name = "Rejected Reverse Receive",
                    .role = NodeRole::IntergraphInput,
                },
                .pins = {PinInstance{
                    .id = m_node_ids.reverse_receiver_pin,
                    .node = m_node_ids.reverse_receiver,
                    .key = "reverse",
                    .label = "Reverse",
                    .type = TypeId{"float"},
                    .direction = PinDirection::Output,
                    .storage = PinStorage::Dynamic,
                }},
            },
            NodePresentation{.position = {100.0f, 340.0f}}));
        intergraph_commands.push_back(std::make_unique<ConnectIntergraphCommand>(IntergraphLink{
            .id = m_node_ids.intergraph_link,
            .source = {m_node_ids.producer_graph, sender, sender_pin},
            .destination = {m_node_ids.consumer_graph, receiver, receiver_pin},
        }));
        if (auto result = execute(std::make_unique<CompoundCommand>(
                "Create intergraph channel", std::move(intergraph_commands))); !result) return result;

        m_node_commands.Clear();
        (void)m_node_editor.ResetNavigation(m_node_document, root);
        m_node_editor.FrameAll();
        ResetTransactionMetrics();
        SetNodeStatus("Stages 1-4 feature lab initialized");
        return {};
    }

    void WindowDemo::ReindexNodeDemo() {
        const NodeDemoIds previous = m_node_ids;
        NodeDemoIds indexed;
        indexed.root = m_node_document.RootGraph();

        const auto preserve_graph = [&](const GraphId id, const std::string_view name) {
            const auto* graph = m_node_document.FindGraph(id);
            return graph != nullptr && graph->display_name == name ? id : GraphId{};
        };
        const auto unique_graph = [&](const std::string_view name) {
            GraphId result;
            for (const auto& graph : m_node_document.Graphs()) {
                if (graph.get().display_name != name) continue;
                if (result) return GraphId{};
                result = graph.get().id;
            }
            return result;
        };
        indexed.owned_graph = preserve_graph(previous.owned_graph, "Owned Filter");
        indexed.reusable_graph = preserve_graph(previous.reusable_graph, "Reusable Function");
        indexed.producer_graph = preserve_graph(previous.producer_graph, "Producer Area");
        indexed.consumer_graph = preserve_graph(previous.consumer_graph, "Consumer Area");
        if (!indexed.owned_graph) indexed.owned_graph = unique_graph("Owned Filter");
        if (!indexed.reusable_graph) indexed.reusable_graph = unique_graph("Reusable Function");
        if (!indexed.producer_graph) indexed.producer_graph = unique_graph("Producer Area");
        if (!indexed.consumer_graph) indexed.consumer_graph = unique_graph("Consumer Area");

        const auto preserve_node = [&](const GraphId graph, const NodeId id, const std::string_view name) {
            const auto* node = m_node_document.FindNode(graph, id);
            return node != nullptr && node->display_name == name ? id : NodeId{};
        };
        const auto unique_node = [&](const GraphId graph, const std::string_view name) {
            const auto* value = m_node_document.FindGraph(graph);
            if (value == nullptr) return NodeId{};
            NodeId result;
            for (const auto& [node_id, node] : value->nodes) {
                if (node.display_name != name) continue;
                if (result) return NodeId{};
                result = node_id;
            }
            return result;
        };
        const auto restore_node = [&](const NodeId previous_id, const std::string_view name) {
            NodeId result = preserve_node(indexed.root, previous_id, name);
            return result ? result : unique_node(indexed.root, name);
        };
        indexed.metric_node = restore_node(previous.metric_node, "Stage 1 Constant");
        indexed.conversion_source = restore_node(previous.conversion_source, "Int Source");
        indexed.conversion_sink = restore_node(previous.conversion_sink, "Float Sink");
        indexed.owned_call = restore_node(previous.owned_call, "Owned Pipeline");
        indexed.referenced_call_a = restore_node(previous.referenced_call_a, "Reusable Call A");
        indexed.referenced_call_b = restore_node(previous.referenced_call_b, "Reusable Call B");
        indexed.asset_call = restore_node(previous.asset_call, "Asset Filter");
        indexed.conversion_output = FindPinByKey(
            m_node_document, indexed.root, indexed.conversion_source, "value");
        indexed.conversion_input = FindPinByKey(
            m_node_document, indexed.root, indexed.conversion_sink, "value");

        indexed.reverse_sender = preserve_node(
            indexed.consumer_graph, previous.reverse_sender, "Rejected Reverse Send");
        indexed.reverse_receiver = preserve_node(
            indexed.producer_graph, previous.reverse_receiver, "Rejected Reverse Receive");
        if (!indexed.reverse_sender) {
            indexed.reverse_sender = unique_node(indexed.consumer_graph, "Rejected Reverse Send");
        }
        if (!indexed.reverse_receiver) {
            indexed.reverse_receiver = unique_node(indexed.producer_graph, "Rejected Reverse Receive");
        }
        indexed.reverse_sender_pin = FindPinByKey(
            m_node_document, indexed.consumer_graph, indexed.reverse_sender, "reverse");
        indexed.reverse_receiver_pin = FindPinByKey(
            m_node_document, indexed.producer_graph, indexed.reverse_receiver, "reverse");

        if (const auto* link = m_node_document.FindIntergraphLink(previous.intergraph_link)) {
            if (link->source.graph == indexed.producer_graph &&
                link->destination.graph == indexed.consumer_graph) {
                indexed.intergraph_link = previous.intergraph_link;
            }
        }
        if (!indexed.intergraph_link) {
            IntergraphLinkId candidate;
            for (const auto& [link_id, link] : m_node_document.IntergraphLinks()) {
                if (link.source.graph != indexed.producer_graph ||
                    link.destination.graph != indexed.consumer_graph) continue;
                if (candidate) {
                    candidate = {};
                    break;
                }
                candidate = link_id;
            }
            indexed.intergraph_link = candidate;
        }
        m_node_ids = indexed;
    }

    void WindowDemo::DrawNodeFeaturePanel() {
        using namespace Nodes;
        ImGui::TextUnformatted("Nodes Feature Lab");
        ImGui::TextDisabled("Public APIs from roadmap stages 1-5");
        ImGui::Separator();

        if (!ImGui::BeginTabBar("Node feature stages")) return;
        if (ImGui::BeginTabItem("Stage 1")) {
            ImGui::TextWrapped("Transactions use copy-on-write graph snapshots and atomic command commits.");
            ImGui::SeparatorText("Live state");
            const TransactionMetrics metrics = GetTransactionMetrics();
            ImGui::Text("Model revision: %llu", static_cast<unsigned long long>(m_node_document.ModelRevision()));
            ImGui::Text("Presentation revision: %llu",
                        static_cast<unsigned long long>(m_node_presentation.PresentationRevision()));
            ImGui::Text("Graph table/state clones: %llu / %llu",
                        static_cast<unsigned long long>(metrics.graphs.root_clones),
                        static_cast<unsigned long long>(metrics.graphs.value_clones));
            ImGui::Text("Node COW root/directory/shard/value: %llu / %llu / %llu / %llu",
                        static_cast<unsigned long long>(metrics.node_maps.root_clones),
                        static_cast<unsigned long long>(metrics.node_maps.directory_clones),
                        static_cast<unsigned long long>(metrics.node_maps.shard_clones),
                        static_cast<unsigned long long>(metrics.node_maps.value_clones));
            ImGui::Text("Node/pin/link copied handles: %llu / %llu / %llu",
                        static_cast<unsigned long long>(metrics.node_maps.copied_handles),
                        static_cast<unsigned long long>(metrics.pin_maps.copied_handles),
                        static_cast<unsigned long long>(metrics.link_maps.copied_handles));
            ImGui::Text("Journal entries: %llu",
                        static_cast<unsigned long long>(metrics.journal_entries));
            ImGui::Text("Logical copied bytes: %llu",
                        static_cast<unsigned long long>(metrics.copied_logical_bytes));
            ImGui::Text("Last operation: %.3f ms", m_node_last_operation_ms);
            if (ImGui::Button("Reset COW metrics")) {
                ResetTransactionMetrics();
                SetNodeStatus("Transaction metrics reset");
            }
            if (ImGui::Button("Edit one property") && m_node_ids.metric_node) {
                const auto* node = m_node_document.FindNode(m_node_ids.root, m_node_ids.metric_node);
                double value = 0.0;
                if (node != nullptr) {
                    if (const auto found = node->properties.find("value"); found != node->properties.end()) {
                        if (const auto* current = std::get_if<double>(&found->second)) value = *current;
                    }
                }
                (void)ExecuteNodeDemoCommand(
                    std::make_unique<SetNodePropertyCommand>(
                        m_node_ids.root, m_node_ids.metric_node, "value", PropertyValue{value + 1.0}),
                    "One model-only transaction committed");
            }
            if (ImGui::Button("Move selected 24 px")) {
                MoveNodesCommand::Positions before;
                MoveNodesCommand::Positions after;
                const GraphId graph = m_node_editor.ActiveGraph();
                for (const NodeId node : m_node_editor.Selection().nodes) {
                    if (const auto* state = m_node_presentation.FindNode(node)) {
                        before.emplace(node, state->position);
                        after.emplace(node, state->position + Vec2{24.0f, 24.0f});
                    }
                }
                if (before.empty()) {
                    SetNodeStatus("Select one or more positioned nodes first", true);
                } else {
                    (void)ExecuteNodeDemoCommand(
                        std::make_unique<MoveNodesCommand>(graph, std::move(before), std::move(after)),
                        "Presentation-only transaction committed");
                }
            }
            if (ImGui::Button("Run 60 property edits") && m_node_ids.metric_node) {
                ResetTransactionMetrics();
                const auto started = std::chrono::steady_clock::now();
                bool passed = true;
                for (std::int64_t index = 0; index < 60; ++index) {
                    auto result = m_node_commands.Execute(
                        std::make_unique<SetNodePropertyCommand>(
                            m_node_ids.root,
                            m_node_ids.metric_node,
                            "benchmark_step",
                            PropertyValue{index}),
                        m_node_document,
                        m_node_presentation,
                        m_registry,
                        m_node_policy);
                    if (!result) {
                        SetNodeStatus("Benchmark rejected: " + result.error().message, true);
                        passed = false;
                        break;
                    }
                }
                m_node_last_operation_ms = std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - started).count();
                if (passed) SetNodeStatus("60 transactional edits completed");
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Stage 2")) {
            ImGui::TextWrapped("Deterministic versioned JSON, migrations, unknown-node preservation and portable fragments.");
            ImGui::Text("Wire format: %u", GraphJsonFormatVersion);
            ImGui::Text("Schema version: %u", m_node_document.SchemaVersion());
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputTextWithHint("##document-path", "Document path", &m_node_document_path);
            if (ImGui::Button("Save document")) {
                auto saved = SaveGraphDocumentJson(m_node_document_path, m_node_document, m_node_presentation);
                SetNodeStatus(saved ? "Saved " + m_node_document_path : "Save failed: " + saved.error().message,
                              !saved);
            }
            ImGui::SameLine();
            if (ImGui::Button("Load document")) {
                auto loaded = LoadGraphDocumentJson(
                    m_node_document_path, m_registry, &m_node_migrations);
                if (loaded) {
                    m_node_warnings = loaded->warnings;
                    m_node_document = std::move(loaded->document);
                    m_node_presentation = std::move(loaded->presentation);
                    m_node_commands.Clear();
                    m_node_editor = EditorContext{};
                    ReindexNodeDemo();
                    (void)m_node_editor.ResetNavigation(m_node_document);
                    m_node_editor.FrameAll();
                    SetNodeStatus("Loaded " + m_node_document_path + " (migrations applied if required)");
                } else {
                    SetNodeStatus("Load failed: " + loaded.error().message, true);
                }
            }
            if (ImGui::Button("Canonical JSON preview")) {
                auto json = SerializeGraphDocumentJson(m_node_document, m_node_presentation);
                if (json) {
                    m_node_json_preview = std::move(*json);
                    m_node_show_fragment_preview = false;
                    SetNodeStatus("Canonical JSON regenerated");
                } else {
                    SetNodeStatus("Serialization failed: " + json.error().message, true);
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Serialize selection")) {
                auto fragment = CaptureGraphFragment(
                    m_node_document, m_node_presentation, m_node_editor.Selection());
                if (fragment) {
                    auto json = SerializeGraphFragmentJson(*fragment);
                    if (json) {
                        m_node_fragment_preview = std::move(*json);
                        m_node_show_fragment_preview = true;
                        SetNodeStatus("Portable fragment serialized");
                    } else {
                        SetNodeStatus("Fragment is not portable: " + json.error().message, true);
                    }
                } else {
                    SetNodeStatus("Fragment capture failed: " + fragment.error().message, true);
                }
            }
            if (ImGui::Button("Copy selection")) m_node_editor.CopySelection();
            ImGui::SameLine();
            if (ImGui::Button("Paste at 1180, 420")) m_node_editor.PasteAt({1180.0f, 420.0f});
            if (ImGui::Button("Add unknown future node")) {
                const NodeId id = m_node_document.AllocateNodeId();
                (void)ExecuteNodeDemoCommand(
                    std::make_unique<AddNodeCommand>(
                        m_node_ids.root,
                        NodeCreation{.node = NodeInstance{
                            .id = id,
                            .type = TypeId{"demo.unknown_future"},
                            .type_version = 42,
                            .display_name = "Unknown type: preserved on load",
                            .properties = {{
                                "future_payload",
                                PropertyValue{OpaqueJsonProperty{
                                    "{\"kind\":\"demo_future\",\"value\":{\"label\":\"preserved\"}}"}},
                            }},
                        }},
                        NodePresentation{.position = {1180.0f, 120.0f}}),
                    "Unknown node added; save and load to see warning preservation");
            }
            if (ImGui::Button("Run legacy 1 -> 2 migration fixture")) {
                GraphDocument legacy_document;
                GraphPresentation legacy_presentation;
                CommandStack legacy_commands;
                const NodeId legacy_node = legacy_document.AllocateNodeId();
                const PinId legacy_pin = legacy_document.AllocatePinId();
                auto added = legacy_commands.Execute(
                    std::make_unique<AddNodeCommand>(
                        legacy_document.RootGraph(),
                        NodeCreation{
                            .node = NodeInstance{
                                .id = legacy_node,
                                .type = TypeId{"demo.int_constant"},
                                .type_version = 1,
                                .display_name = "Legacy Integer v1",
                                .properties = {{"value", PropertyValue{std::int64_t{3}}}},
                            },
                            .pins = {PinInstance{
                                .id = legacy_pin,
                                .node = legacy_node,
                                .key = "value",
                                .label = "Integer",
                                .type = TypeId{"int"},
                                .direction = PinDirection::Output,
                                .cardinality = PinCardinality::Multiple,
                            }},
                        },
                        NodePresentation{.position = {40.0f, 40.0f}}),
                    legacy_document,
                    legacy_presentation,
                    m_registry);
                auto json = added
                    ? SerializeGraphDocumentJson(legacy_document, legacy_presentation)
                    : Result<std::string>{std::unexpected(added.error())};
                auto migrated = json
                    ? DeserializeGraphDocumentJson(*json, m_registry, &m_node_migrations)
                    : Result<LoadedGraphDocument>{std::unexpected(json.error())};
                const NodeInstance* migrated_node = migrated
                    ? migrated->document.FindNode(migrated->document.RootGraph(), legacy_node)
                    : nullptr;
                const bool node_migrated = migrated_node != nullptr && migrated_node->type_version == 2 &&
                    migrated_node->properties.contains("migrated") &&
                    migrated_node->properties.contains("document_migrated") &&
                    migrated->document.SchemaVersion() == 2;
                SetNodeStatus(
                    node_migrated
                        ? "Legacy fixture ran document and node migrations 1 -> 2"
                        : "Legacy migration fixture failed" +
                            (migrated ? std::string{} : ": " + migrated.error().message),
                    !node_migrated);
            }
            if (!m_node_warnings.empty()) {
                ImGui::SeparatorText("Load warnings");
                for (const auto& warning : m_node_warnings) ImGui::BulletText("%s", warning.message.c_str());
            }
            std::string* preview = m_node_show_fragment_preview
                ? &m_node_fragment_preview
                : &m_node_json_preview;
            if (!preview->empty()) {
                ImGui::Text("Preview: %zu bytes", preview->size());
                ImGui::InputTextMultiline(
                    "##json-preview", preview, {-1.0f, 150.0f}, ImGuiInputTextFlags_ReadOnly);
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Stage 3")) {
            ImGui::TextWrapped("Type conversion descriptors, centralized policies and persisted protection flags.");
            ImGui::SeparatorText("Automatic conversion");
            if (ImGui::Button("Select int -> float endpoints")) {
                (void)m_node_editor.ResetNavigation(m_node_document, m_node_ids.root);
                m_node_editor.SetSelection(GraphSelection{
                    .graph = m_node_ids.root,
                    .nodes = {m_node_ids.conversion_source, m_node_ids.conversion_sink},
                });
                m_node_editor.FrameSelection();
            }
            if (ImGui::Button("Insert conversion atomically")) {
                (void)m_node_editor.ResetNavigation(m_node_document, m_node_ids.root);
                auto command = PrepareConnectionCommand(
                    m_node_document,
                    m_node_presentation,
                    m_registry,
                    ConnectionRequest{
                        .graph = m_node_ids.root,
                        .first = m_node_ids.conversion_output,
                        .second = m_node_ids.conversion_input,
                    },
                    {370.0f, 430.0f},
                    m_node_policy);
                if (command) {
                    (void)ExecuteNodeDemoCommand(std::move(*command), "Conversion node and two links inserted");
                } else {
                    SetNodeStatus("Conversion rejected: " + command.error().message, true);
                }
            }
            ImGui::SeparatorText("Graph policy");
            ImGui::Checkbox("Deny connect", &m_node_deny_connect);
            ImGui::Checkbox("Deny create", &m_node_deny_create);
            ImGui::Checkbox("Deny delete", &m_node_deny_delete);
            ImGui::Checkbox("Deny group changes", &m_node_deny_group);
            ImGui::TextDisabled("Policy applies to editor, inspector, commands and redo.");
            ImGui::SeparatorText("Selected node protection");
            const auto selection = m_node_editor.Selection();
            const NodeId selected = selection.nodes.empty() ? NodeId{} : selection.nodes.front();
            const auto* node = selected ? m_node_document.FindNode(selection.graph, selected) : nullptr;
            if (node != nullptr) {
                if (ImGui::Button(node->read_only ? "Make writable" : "Make read-only")) {
                    (void)ExecuteNodeDemoCommand(
                        std::make_unique<SetNodeReadOnlyCommand>(selection.graph, selected, !node->read_only),
                        node->read_only ? "Node is writable" : "Node is read-only");
                }
                const auto* state = m_node_presentation.FindNode(selected);
                if (state != nullptr && ImGui::Button(state->locked ? "Unlock position" : "Lock position")) {
                    (void)ExecuteNodeDemoCommand(
                        std::make_unique<SetNodeLockedCommand>(selected, !state->locked),
                        state->locked ? "Node position unlocked" : "Node position locked");
                }
            } else {
                ImGui::TextDisabled("Select a node to toggle protection");
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Stage 4")) {
            std::size_t owned_count = 0;
            for (const auto& graph : m_node_document.Graphs()) {
                if (graph.get().lifetime == GraphLifetime::Owned) ++owned_count;
            }
            ImGui::TextWrapped("Boundary interfaces, owned and referenced graphs, breadcrumbs, assets and intergraph channels.");
            ImGui::Text("Graphs: %zu (%zu owned)", m_node_document.Graphs().size(), owned_count);
            ImGui::Text("Breadcrumb depth: %zu", m_node_editor.Breadcrumbs().size());
            if (ImGui::Button("Root graph")) {
                (void)m_node_editor.ResetNavigation(m_node_document, m_node_ids.root);
                m_node_editor.FrameAll();
            }
            if (ImGui::Button("Enter owned call")) {
                (void)m_node_editor.ResetNavigation(m_node_document, m_node_ids.root);
                auto entered = m_node_editor.EnterSubgraph(m_node_document, m_node_ids.owned_call);
                SetNodeStatus(entered ? "Entered owned graph through breadcrumb path"
                                      : "Navigation failed: " + entered.error().message,
                              !entered);
                m_node_editor.FrameAll();
            }
            ImGui::SameLine();
            if (ImGui::Button("Enter reusable call")) {
                (void)m_node_editor.ResetNavigation(m_node_document, m_node_ids.root);
                auto entered = m_node_editor.EnterSubgraph(m_node_document, m_node_ids.referenced_call_a);
                SetNodeStatus(entered ? "Entered shared reusable graph"
                                      : "Navigation failed: " + entered.error().message,
                              !entered);
                m_node_editor.FrameAll();
            }
            if (ImGui::Button("Duplicate owned (deep copy)")) {
                (void)m_node_editor.ResetNavigation(m_node_document, m_node_ids.root);
                m_node_editor.SetSelection(GraphSelection{.graph = m_node_ids.root, .nodes = {m_node_ids.owned_call}});
                m_node_editor.DuplicateSelection();
                SetNodeStatus("Owned call queued for deep-copy duplication");
            }
            ImGui::SameLine();
            if (ImGui::Button("Duplicate referenced")) {
                (void)m_node_editor.ResetNavigation(m_node_document, m_node_ids.root);
                m_node_editor.SetSelection(GraphSelection{
                    .graph = m_node_ids.root,
                    .nodes = {m_node_ids.referenced_call_a},
                });
                m_node_editor.DuplicateSelection();
                SetNodeStatus("Referenced call queued; target graph remains shared");
            }
            ImGui::SeparatorText("Reusable graph asset");
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputTextWithHint("##asset-path", "Graph asset path", &m_node_asset_path);
            if (ImGui::Button("Save asset")) {
                const auto asset = m_node_assets.Find(GraphAssetId{"demo.signal_asset"});
                auto saved = asset
                    ? SaveGraphAssetJson(m_node_asset_path, asset->Asset())
                    : Result<void>{std::unexpected(DemoError("Asset is not registered"))};
                SetNodeStatus(saved ? "Saved graph asset" : "Asset save failed: " + saved.error().message, !saved);
            }
            ImGui::SameLine();
            if (ImGui::Button("Reload asset")) {
                auto loaded = LoadGraphAssetJson(m_node_asset_path, m_registry, &m_node_migrations);
                if (loaded) {
                    auto registered = m_node_assets.Write(std::move(loaded->asset), GraphAssetWriteMode::Upsert);
                    if (registered) {
                        m_node_warnings = std::move(loaded->warnings);
                        SetNodeStatus("Graph asset reloaded and registered");
                    } else {
                        SetNodeStatus("Asset registration failed: " + registered.error().message, true);
                    }
                } else {
                    SetNodeStatus("Asset load failed: " + loaded.error().message, true);
                }
            }
            if (ImGui::Button("Validate asset dependencies")) {
                const auto issues = ValidateGraphDependencies(m_node_document, m_node_assets);
                if (issues.empty()) {
                    SetNodeStatus("Asset dependencies and interface snapshots are valid");
                } else {
                    SetNodeStatus("Asset validation: " + issues.front().message,
                                  issues.front().severity == ValidationSeverity::Error);
                }
            }
            ImGui::SeparatorText("Intergraph channel");
            ImGui::Text("Document channels: %zu", m_node_document.IntergraphLinks().size());
            if (ImGui::Button("Open producer area")) {
                (void)m_node_editor.ResetNavigation(m_node_document, m_node_ids.producer_graph);
                m_node_editor.FrameAll();
            }
            ImGui::SameLine();
            if (ImGui::Button("Open consumer area")) {
                (void)m_node_editor.ResetNavigation(m_node_document, m_node_ids.consumer_graph);
                m_node_editor.FrameAll();
            }
            if (ImGui::Button("Try recursive reverse channel")) {
                const auto* forward = m_node_document.FindIntergraphLink(m_node_ids.intergraph_link);
                const bool fixture_valid = forward != nullptr &&
                    forward->source.graph == m_node_ids.producer_graph &&
                    forward->destination.graph == m_node_ids.consumer_graph &&
                    m_node_document.FindNode(m_node_ids.consumer_graph, m_node_ids.reverse_sender) != nullptr &&
                    m_node_document.FindNode(m_node_ids.producer_graph, m_node_ids.reverse_receiver) != nullptr &&
                    m_node_document.FindPin(m_node_ids.consumer_graph, m_node_ids.reverse_sender_pin) != nullptr &&
                    m_node_document.FindPin(m_node_ids.producer_graph, m_node_ids.reverse_receiver_pin) != nullptr;
                if (!fixture_valid) {
                    SetNodeStatus("Recursive dependency fixture is missing after load", true);
                } else {
                    auto result = m_node_commands.Execute(
                        std::make_unique<ConnectIntergraphCommand>(IntergraphLink{
                            .id = m_node_document.AllocateIntergraphLinkId(),
                            .source = {
                                m_node_ids.consumer_graph,
                                m_node_ids.reverse_sender,
                                m_node_ids.reverse_sender_pin,
                            },
                            .destination = {
                                m_node_ids.producer_graph,
                                m_node_ids.reverse_receiver,
                                m_node_ids.reverse_receiver_pin,
                            },
                        }),
                        m_node_document,
                        m_node_presentation,
                        m_registry,
                        m_node_policy);
                    const bool recursive_rejection = !result &&
                        result.error().code == ErrorCode::InvalidGraph &&
                        result.error().message.find("recursive cycle") != std::string::npos;
                    SetNodeStatus(
                        recursive_rejection
                            ? "Expected recursive dependency rejection: " + result.error().message
                            : result
                                ? "Unexpectedly accepted recursive channel"
                                : "Unexpected rejection: " + result.error().message,
                        !recursive_rejection);
                }
            }
            if (ImGui::Button("Validate complete hierarchy")) {
                auto result = m_node_document.ValidateStructure();
                SetNodeStatus(result ? "Ownership, interfaces and dependency DAG are valid"
                                     : "Hierarchy validation failed: " + result.error().message,
                              !result);
            }
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    UiResult<UiElementUpdate> WindowDemo::Update(UiState& state) {
        static bool win_about = false;
        static bool win_demo = false;
        static bool win_demo_implot = false;
        static bool win_metrics = false;

        if (!m_texture_initialized) {
            m_texture_initialized = true;
            if (auto texture = state.app.CreateTexture(64, 64)) {
                m_texture = std::move(*texture);
                m_texture.Clear(IM_COL32(70, 120, 210, 255));
            }
        }

        ImGui::SetNextWindowSize({800,600}, ImGuiCond_FirstUseEver);
        if (ImGui::Begin("demo")) {
            ImGui::Text("%s", std::string(state.app.RendererName()).c_str());
            if (m_texture) ImGui::Image(m_texture.GetRef(), ImVec2(64.0f, 64.0f));
            if (ImGui::Button("About")) win_about = true;
            if (ImGui::Button("Demo")) win_demo = true;
            if (ImGui::Button("ImPlot Demo")) win_demo_implot = true;
            if (ImGui::Button("Metrics")) win_metrics = true;
            if (ImGui::Button("Nodes Stages 1-4")) m_node_editor_open = true;
        }
        ImGui::End();

        if (win_about) ImGui::ShowAboutWindow(&win_about);
        if (win_demo) ImGui::ShowDemoWindow(&win_demo);
        if (win_demo_implot) ImPlot::ShowDemoWindow(&win_demo_implot);
        if (win_metrics) ImGui::ShowMetricsWindow(&win_metrics);

        if (m_node_editor_open) {
            if (!m_node_editor_initialized && !m_node_initialization_attempted) {
                m_node_initialization_attempted = true;
                auto initialized = InitializeNodeDemo();
                m_node_editor_initialized = initialized.has_value();
                if (!initialized) SetNodeStatus("Feature lab initialization failed: " + initialized.error().message, true);
            }

            const ImGuiViewport* viewport = ImGui::GetMainViewport();
            const ImVec2 feature_size{
                std::min(1450.0f, viewport->WorkSize.x * 0.95f),
                std::min(840.0f, viewport->WorkSize.y * 0.95f),
            };
            ImGui::SetNextWindowSize(feature_size, ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowPos(
                viewport->WorkPos + (viewport->WorkSize - feature_size) * 0.5f,
                ImGuiCond_FirstUseEver);
            if (ImGui::Begin("Nodes: roadmap stages 1-5", &m_node_editor_open)) {
                if (m_node_editor_initialized) {
                    const bool can_undo = m_node_commands.CanUndo();
                    ImGui::BeginDisabled(!can_undo);
                    const std::string undo_label = can_undo
                        ? "Undo " + std::string(m_node_commands.UndoName())
                        : "Undo";
                    if (ImGui::Button(undo_label.c_str())) {
                        auto result = m_node_commands.Undo(
                            m_node_document, m_node_presentation, m_registry);
                        if (!result) SetNodeStatus("Undo failed: " + result.error().message, true);
                    }
                    ImGui::EndDisabled();
                    ImGui::SameLine();
                    const bool can_redo = m_node_commands.CanRedo();
                    ImGui::BeginDisabled(!can_redo);
                    const std::string redo_label = can_redo
                        ? "Redo " + std::string(m_node_commands.RedoName())
                        : "Redo";
                    if (ImGui::Button(redo_label.c_str())) {
                        auto result = m_node_commands.Redo(
                            m_node_document, m_node_presentation, m_registry, m_node_policy);
                        if (!result) SetNodeStatus("Redo failed: " + result.error().message, true);
                    }
                    ImGui::EndDisabled();
                    ImGui::SameLine();
                    if (ImGui::Button("Frame all")) m_node_editor.FrameAll();
                    ImGui::SameLine();
                    if (ImGui::Button("Auto layout")) m_node_editor.AutoLayoutSelection();
                    ImGui::SameLine();
                    ImGui::TextDisabled("Active graph %llu",
                                        static_cast<unsigned long long>(m_node_editor.ActiveGraph().Value()));

                    if (!m_node_io_status.empty()) {
                        ImGui::PushStyleColor(
                            ImGuiCol_Text,
                            m_node_io_failed
                                ? ImVec4{1.0f, 0.35f, 0.35f, 1.0f}
                                : ImVec4{0.35f, 0.85f, 0.55f, 1.0f});
                        ImGui::TextWrapped("%s", m_node_io_status.c_str());
                        ImGui::PopStyleColor();
                    }

                    if (ImGui::BeginTable(
                            "Nodes feature lab layout",
                            3,
                            ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV)) {
                        ImGui::TableSetupColumn("Stages", ImGuiTableColumnFlags_WidthFixed, 330.0f);
                        ImGui::TableSetupColumn("Canvas", ImGuiTableColumnFlags_WidthStretch);
                        ImGui::TableSetupColumn("Inspector", ImGuiTableColumnFlags_WidthFixed, 260.0f);
                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();
                        ImGui::BeginChild("Feature controls", {0.0f, 0.0f}, ImGuiChildFlags_Borders);
                        DrawNodeFeaturePanel();
                        ImGui::EndChild();
                        ImGui::TableNextColumn();
                        EditorConfig config;
                        config.show_breadcrumbs = true;
                        const auto editor_result = DrawEditor(
                            m_node_editor,
                            m_node_document,
                            m_node_presentation,
                            m_node_commands,
                            m_registry,
                            m_node_ui_registry,
                            m_node_routers,
                            {},
                            {},
                            config,
                            {},
                            m_node_policy);
                        (void)editor_result;
                        ImGui::TableNextColumn();
                        ImGui::BeginChild("Node inspector", {0.0f, 0.0f}, ImGuiChildFlags_Borders);
                        ImGui::TextUnformatted("Inspector");
                        ImGui::Separator();
                        const auto selected_nodes = m_node_editor.Selection().nodes;
                        if (!selected_nodes.empty()) {
                            const auto result = DrawNodeInspector(
                                m_node_ui_registry,
                                m_node_document,
                                m_node_presentation,
                                m_node_commands,
                                m_registry,
                                m_node_editor.ActiveGraph(),
                                selected_nodes.front(),
                                m_node_policy);
                            if (!result.error.empty()) ImGui::TextWrapped("%s", result.error.c_str());
                        } else {
                            ImGui::TextDisabled("Select a node");
                        }
                        if (!m_node_editor.LastError().empty()) {
                            ImGui::SeparatorText("Editor status");
                            ImGui::TextWrapped("%s", m_node_editor.LastError().c_str());
                        }
                        ImGui::EndChild();
                        ImGui::EndTable();
                    }
                } else {
                    ImGui::TextWrapped("%s", m_node_io_status.c_str());
                    if (ImGui::Button("Retry initialization")) {
                        m_node_initialization_attempted = false;
                        m_node_io_status.clear();
                    }
                }
            }
            ImGui::End();
        }

        const UiFrameDemand frame_demand = win_demo || win_demo_implot || m_node_editor_open
            ? UiFrameDemand::Continuous
            : UiFrameDemand::None;
        return UiElementUpdate{true, frame_demand};
    }
}
