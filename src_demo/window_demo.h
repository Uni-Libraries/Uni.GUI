#pragma once

//
// Includes
//

// Uni.GUI
#include <uni/gui/element.h>
#include <uni/gui/nodes/nodes.h>
#include <uni/gui/texture.h>

#include <memory>
#include <string>
#include <vector>

//
// Public
//

namespace Uni::GUI::Example {
    class WindowDemo: public Uni::GUI::UiElement {
    public:
        explicit WindowDemo() = default;
        UiResult<UiElementUpdate> Update(UiState&) override;
    private:
        struct NodeDemoIds final {
            Nodes::GraphId root;
            Nodes::GraphId owned_graph;
            Nodes::GraphId reusable_graph;
            Nodes::GraphId producer_graph;
            Nodes::GraphId consumer_graph;
            Nodes::NodeId metric_node;
            Nodes::NodeId conversion_source;
            Nodes::NodeId conversion_sink;
            Nodes::PinId conversion_output;
            Nodes::PinId conversion_input;
            Nodes::NodeId owned_call;
            Nodes::NodeId referenced_call_a;
            Nodes::NodeId referenced_call_b;
            Nodes::NodeId asset_call;
            Nodes::NodeId reverse_sender;
            Nodes::PinId reverse_sender_pin;
            Nodes::NodeId reverse_receiver;
            Nodes::PinId reverse_receiver_pin;
            Nodes::IntergraphLinkId intergraph_link;
        };

        [[nodiscard]] Nodes::Result<void> InitializeNodeDemo();
        void ReindexNodeDemo();
        void DrawNodeFeaturePanel();
        bool ExecuteNodeDemoCommand(std::unique_ptr<Nodes::Command> command, std::string success);
        void SetNodeStatus(std::string status, bool failed = false);

        UiTexture m_texture{};
        bool m_texture_initialized{};
        Nodes::GraphPresentation m_node_presentation;
        Nodes::RegistryCatalog m_registry;
        Nodes::NodeUiRegistry m_node_ui_registry;
        Nodes::LinkRouterRegistry m_node_routers;
        Nodes::GraphPolicy m_node_policy;
        Nodes::DocumentMigrationRegistry m_node_migrations{2};
        Nodes::GraphAssetRegistry m_node_assets;
        Nodes::CommandStack m_node_commands;
        Nodes::EditorContext m_node_editor;
        Nodes::GraphDocument m_node_document;
        NodeDemoIds m_node_ids;
        std::vector<Nodes::GraphIoWarning> m_node_warnings;
        std::string m_node_document_path{"uni-gui-demo.nodes.json"};
        std::string m_node_asset_path{"uni-gui-demo.graph-asset.json"};
        std::string m_node_json_preview;
        std::string m_node_fragment_preview;
        std::string m_node_io_status;
        double m_node_last_operation_ms{};
        bool m_node_deny_connect{};
        bool m_node_deny_create{};
        bool m_node_deny_delete{};
        bool m_node_deny_group{};
        bool m_node_show_fragment_preview{};
        bool m_node_editor_initialized{};
        bool m_node_initialization_attempted{};
        bool m_node_editor_open{true};
        bool m_node_io_failed{};
    };
}
