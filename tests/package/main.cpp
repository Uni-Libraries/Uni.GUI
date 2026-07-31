#include <uni/gui/app.h>

#include <SDL3/SDL_version.h>
#include <uni/gui/asset.h>
#include <uni/gui/callbacks.h>
#include <uni/gui/config.h>
#include <uni/gui/dispatcher.h>
#include <uni/gui/element.h>
#include <uni/gui/error.h>
#include <uni/gui/event.h>
#include <uni/gui/frame.h>
#include <uni/gui/layout.h>
#include <uni/gui/nodes/nodes.h>
#include <uni/gui/state.h>
#include <uni/gui/texture.h>

#include <memory>
#include <type_traits>
#include <utility>

namespace {

class PackageNodeCommand final : public Uni::GUI::Nodes::Command {
public:
    [[nodiscard]] std::string_view Name() const noexcept override { return "Package command"; }

private:
    [[nodiscard]] Uni::GUI::Nodes::Result<void> Apply(
        Uni::GUI::Nodes::GraphTransaction& transaction,
        const Uni::GUI::Nodes::RegistrySnapshot& types) override {
        (void)types;
        return transaction.SetSchemaVersion(2);
    }

    [[nodiscard]] Uni::GUI::Nodes::Result<void> Revert(
        Uni::GUI::Nodes::GraphTransaction& transaction) override {
        return transaction.SetSchemaVersion(1);
    }
};

} // namespace

int main() {
    using namespace Uni::GUI;

    static_assert(SDL_VERSION_ATLEAST(3, 4, 0));
    static_assert(std::is_move_constructible_v<Uni::GUI::UiTexture>);
    static_assert(std::is_move_constructible_v<UiCommand>);
    static_assert(!std::is_copy_constructible_v<UiCommand>);
    static_assert(std::is_copy_constructible_v<UiDispatcher>);
    static_assert(std::is_copy_constructible_v<UiCommandTicket>);

    UiApp app;
    SDL_Event event{};
    UiAssetSpec asset;
    asset.key = "package-consumer";
    asset.source = UiAssetBytes{};

    bool valid = app.State() == UiLifecycleState::Empty;
    valid = valid && !app.IsMainThread() && app.RendererName().empty();
    valid = valid && !app.Tick() && !app.DispatchEvent(event) && !app.SetEventHooks({});
    valid = valid && !app.AddElement({}) && !app.RemoveElement(InvalidUiElementId);
    valid = valid && !app.CreateTexture(1, 1) && !app.SetVsync(UiVsyncMode::Enabled);
    valid = valid && !app.DefineDockLayout({}) && !app.RemoveDockLayout("missing");
    valid = valid && !app.ActivateDockLayout("missing") && app.ActiveDockLayout().empty();
    valid = valid && !app.SaveSettingsNow() && !app.ReloadSettings();
    valid = valid && !app.UpsertAsset(std::move(asset)) && !app.FindAsset("missing");
    valid = valid && !app.RemoveAsset(InvalidUiAssetId) && !app.CreateFont({});
    valid = valid && !app.SetDefaultFont(InvalidUiFontId) && !app.RemoveFont(InvalidUiFontId);
    valid = valid && !app.GetFont(InvalidUiFontId);

    UiDispatcher dispatcher = app.Dispatcher();
    UiDispatcher dispatcher_copy = dispatcher;
    UiDispatcher dispatcher_moved = std::move(dispatcher_copy);
    valid = valid && !dispatcher_moved.IsOpen();
    valid = valid && !dispatcher_moved.Post(
        [state = std::make_unique<int>(42)](UiApp&) -> UiResult<void> {
            return *state == 42
                ? UiResult<void>{}
                : std::unexpected(UiError{UiErrorCode::CommandFailed, "Move-only command state was lost"});
        });
    valid = valid && !dispatcher_moved.RequestFrame() && !dispatcher_moved.RequestExit();

    UiCommandTicket ticket;
    UiCommandTicket ticket_copy = ticket;
    UiCommandTicket ticket_moved = std::move(ticket_copy);
    valid = valid && !ticket_moved && !ticket_moved.Ready();
    valid = valid && ticket_moved.TryResult().has_value() && !ticket_moved.Wait();

    Nodes::GraphDocument node_document;
    Nodes::GraphPresentation node_presentation;
    Nodes::RegistryCatalog node_registry;
    Nodes::NodeUiRegistry node_ui_registry;
    Nodes::CommandStack node_commands;
    valid = valid && node_commands.Execute(
        std::make_unique<PackageNodeCommand>(),
        node_document,
        node_presentation,
        node_registry).has_value();
    valid = valid && node_commands.Undo(node_document, node_presentation, node_registry).has_value();
    const Nodes::GroupId package_group = node_presentation.AllocateGroupId();
    const Nodes::GroupStyleHandle package_style = Nodes::MakeGroupStyle(Nodes::GroupStyle{
        .title = "Package group",
        .body = "Installed API style payload",
        .kind = Nodes::GroupKind::Comment,
    });
    valid = valid && node_commands.Execute(
        std::make_unique<Nodes::AddGroupCommand>(Nodes::GroupPresentation{
            .id = package_group,
            .graph = node_document.RootGraph(),
            .geometry = Nodes::GroupGeometry{.position = {10.0f, 20.0f}},
            .style = package_style,
        }),
        node_document,
        node_presentation,
        node_registry).has_value();
    valid = valid && node_presentation.FindGroup(package_group) != nullptr &&
        node_presentation.FindGroup(package_group)->style == package_style;
    valid = valid && node_registry.RegisterNodeType(Nodes::NodeTypeDescriptor{
        .type = Nodes::TypeId{"package.node"},
        .display_name = "Package node",
        .default_properties = {
            {"value", Nodes::PropertyValue{1.0}},
        },
    }).has_value();
    valid = valid && node_ui_registry.Register(Nodes::NodeUiDescriptor{
        .type = Nodes::TypeId{"package.node"},
        .draw_body = [](Nodes::NodeUiContext& context) {
            context.SetProperty("value", Nodes::PropertyValue{2.0});
        },
        .default_size = {200.0f, 100.0f},
    }).has_value();
    valid = valid && node_ui_registry.RegisterPinStyle(
        Nodes::TypeId{"float"},
        [](const Nodes::PinStyleContext&) {
            return Nodes::PinStyle{.shape = Nodes::PinShape::Diamond};
        }).has_value();
    auto serialized_nodes = Nodes::SerializeGraphDocumentJson(node_document, node_presentation);
    valid = valid && serialized_nodes.has_value() && !serialized_nodes->empty();
    auto loaded_nodes = serialized_nodes
        ? Nodes::DeserializeGraphDocumentJson(*serialized_nodes, node_registry)
        : Nodes::Result<Nodes::LoadedGraphDocument>{std::unexpected(Nodes::Error{
              Nodes::ErrorCode::InvalidFormat,
              "Serialization failed",
          })};
    valid = valid && loaded_nodes.has_value() && loaded_nodes->document.SchemaVersion() == 1;

    Nodes::GraphDocumentSnapshot node_snapshot = Nodes::CaptureGraphDocumentSnapshot(node_document);
    Nodes::GraphDocumentSnapshot node_snapshot_copy = node_snapshot;
    valid = valid && node_snapshot_copy.SourceIdentity() == node_document.Identity() &&
        node_snapshot_copy.RootGraph() == node_document.RootGraph() &&
        node_snapshot_copy.SemanticRevisions() == node_document.SemanticRevisions() &&
        node_snapshot_copy.FindGraph(node_document.RootGraph()) != nullptr;

    Nodes::EditorContext node_editor;
    const auto missing_flow = node_editor.TriggerLinkFlow(
        node_document,
        node_document.RootGraph(),
        Nodes::LinkId{1},
        Nodes::LinkFlowDirection::InputToOutput);
    valid = valid && !missing_flow && missing_flow.error().code == Nodes::ErrorCode::LinkNotFound;
    node_editor.ClearLinkFlow(node_document, node_document.RootGraph(), Nodes::LinkId{1});
    node_editor.ClearLinkFlows();

    Nodes::GraphAssetRegistry graph_assets;
    std::size_t graph_asset_changes = 0;
    auto graph_asset_subscription = graph_assets.Subscribe(
        [&](const Nodes::GraphAssetChange&) { ++graph_asset_changes; });
    Nodes::GraphAsset graph_asset;
    graph_asset.id = Nodes::GraphAssetId{"package.asset"};
    const auto graph_asset_hash = Nodes::ComputeGraphAssetContentHash(graph_asset);
    valid = valid && graph_asset_hash.has_value() &&
        graph_assets.ValidateWrite(graph_asset, Nodes::GraphAssetWriteMode::Insert).has_value();
    const auto graph_asset_write = graph_assets.Write(
        std::move(graph_asset),
        Nodes::GraphAssetWriteMode::Insert);
    valid = valid && graph_asset_write.has_value() &&
        graph_asset_write->status == Nodes::GraphAssetWriteStatus::Inserted &&
        graph_asset_write->record != nullptr && graph_asset_write->record->Generation() == 1 &&
        graph_assets.Find(Nodes::GraphAssetId{"package.asset"}) == graph_asset_write->record &&
        graph_assets.DirectDependencies(Nodes::GraphAssetId{"package.asset"}).has_value() &&
        graph_assets.DependentClosure(Nodes::GraphAssetId{"package.asset"}).has_value() &&
        graph_assets.ValidateAll().has_value() && graph_asset_changes == 1;
    valid = valid && graph_assets.Unregister(Nodes::GraphAssetId{"package.asset"}).has_value();
    graph_asset_subscription.Reset();

    UiTexture texture;
    UiTexture moved_texture = std::move(texture);
    const UiTexture& const_texture = moved_texture;
    valid = valid && !moved_texture && moved_texture.GetRef().GetTexID() == ImTextureID_Invalid;
    valid = valid && moved_texture.Width() == 0 && moved_texture.Height() == 0 && moved_texture.Pitch() == 0;
    valid = valid && moved_texture.Pixels() == nullptr && const_texture.Pixels() == nullptr;
    valid = valid && moved_texture.PixelsAt(0, 0) == nullptr && const_texture.PixelsAt(0, 0) == nullptr;
    valid = valid && !moved_texture.Clear() && !moved_texture.Update();
    valid = valid && !moved_texture.UpdateRect(0, 0, 1, 1) && !moved_texture.Destroy();

    valid = valid && app.Shutdown().has_value();
    return valid ? 0 : 1;
}
