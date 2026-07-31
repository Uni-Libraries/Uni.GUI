#pragma once

#include <uni/gui/nodes/editor.h>
#include <uni/gui/nodes/io.h>
#include <uni/gui/nodes/snapshot.h>

#include <memory>
#include <string_view>
#include <utility>
#include <vector>

namespace Uni::GUI::Nodes {

class NodeEditorWorkspace final {
public:
    NodeEditorWorkspace() = default;
    ~NodeEditorWorkspace() = default;
    NodeEditorWorkspace(const NodeEditorWorkspace&) = delete;
    NodeEditorWorkspace& operator=(const NodeEditorWorkspace&) = delete;
    NodeEditorWorkspace(NodeEditorWorkspace&&) = delete;
    NodeEditorWorkspace& operator=(NodeEditorWorkspace&&) = delete;

    [[nodiscard]] Result<CommandResult> Execute(
        std::unique_ptr<Command> command,
        const GraphPolicy& policy = {}) {
        if (m_loading) return LoadingError<CommandResult>();
        return commands.Execute(std::move(command), document, presentation, m_registry, policy);
    }

    [[nodiscard]] Result<CommandResult> Undo() {
        if (m_loading) return LoadingError<CommandResult>();
        return commands.Undo(document, presentation, m_registry);
    }

    [[nodiscard]] Result<CommandResult> Undo(
        const GraphPolicy& policy,
        const UndoPolicyMode mode = UndoPolicyMode::RespectCurrentPolicy) {
        if (m_loading) return LoadingError<CommandResult>();
        return commands.Undo(document, presentation, policy, mode, m_registry);
    }

    [[nodiscard]] Result<CommandResult> Redo(const GraphPolicy& policy = {}) {
        if (m_loading) return LoadingError<CommandResult>();
        return commands.Redo(document, presentation, m_registry, policy);
    }

    [[nodiscard]] Result<CommandResult> Resume(
        const DeferredOperationId operation,
        const ResumeMode mode,
        const GraphPolicy& policy = {}) {
        if (m_loading) return LoadingError<CommandResult>();
        return commands.Resume(operation, document, presentation, mode, policy);
    }

    [[nodiscard]] Result<void> Cancel(const DeferredOperationId operation) {
        if (m_loading) return LoadingError<void>();
        return commands.Cancel(operation);
    }

    [[nodiscard]] bool HasPending() const noexcept { return commands.HasPending(); }

    [[nodiscard]] const RegistryCatalog& Registry() const noexcept { return m_registry; }

    [[nodiscard]] Result<RegistryUpdate> BeginUpdate() {
        if (m_loading) return LoadingError<RegistryUpdate>();
        if (commands.IsBusy()) {
            return std::unexpected(Error{
                ErrorCode::CommandFailed,
                "Cannot update registries while the command stack is executing"});
        }
        return m_registry.BeginUpdate();
    }

    [[nodiscard]] Result<void> RegisterNodeType(NodeTypeDescriptor descriptor) {
        if (m_loading) return LoadingError<void>();
        if (commands.IsBusy()) return BusyRegistryError<void>();
        return m_registry.RegisterNodeType(std::move(descriptor));
    }

    [[nodiscard]] Result<bool> UnregisterNodeType(const TypeId& type) {
        if (m_loading) return LoadingError<bool>();
        if (commands.IsBusy()) return BusyRegistryError<bool>();
        if (m_registry.HasConversionsForNodeType(type)) {
            return std::unexpected(Error{
                ErrorCode::TypeInUse,
                "Cannot unregister a node type while conversions reference it"});
        }
        return m_registry.UnregisterNodeType(type);
    }

    [[nodiscard]] Result<void> ReplaceNodeType(NodeTypeDescriptor descriptor) {
        if (m_loading) return LoadingError<void>();
        if (commands.IsBusy()) return BusyRegistryError<void>();
        return m_registry.ReplaceNodeType(std::move(descriptor));
    }

    [[nodiscard]] Result<NodeCreation> InstantiateNode(
        const TypeId& type,
        const std::string_view display_name = {}) {
        if (m_loading) return LoadingError<NodeCreation>();
        return m_registry.Instantiate(document, type, display_name);
    }

    [[nodiscard]] Result<ConversionRegistrationToken> RegisterConversion(ConversionDescriptor descriptor) {
        if (m_loading) return LoadingError<ConversionRegistrationToken>();
        if (commands.IsBusy()) return BusyRegistryError<ConversionRegistrationToken>();
        return m_registry.RegisterConversion(std::move(descriptor));
    }

    [[nodiscard]] Result<bool> UnregisterConversion(const ConversionRegistrationToken registration) {
        if (m_loading) return LoadingError<bool>();
        if (commands.IsBusy()) return BusyRegistryError<bool>();
        return m_registry.UnregisterConversion(registration);
    }

    [[nodiscard]] Result<void> ReplaceConversion(
        const ConversionRegistrationToken registration,
        ConversionDescriptor descriptor) {
        if (m_loading) return LoadingError<void>();
        if (commands.IsBusy()) return BusyRegistryError<void>();
        return m_registry.ReplaceConversion(registration, std::move(descriptor));
    }

    [[nodiscard]] const DeferredOperation* PendingOperation() const noexcept {
        return commands.PendingOperation();
    }

    [[nodiscard]] GraphDocumentSnapshot CaptureSnapshot() const {
        return CaptureGraphDocumentSnapshot(document);
    }

    [[nodiscard]] Result<void> Save(
        const std::string_view path,
        const GraphIoLimits& limits = {}) const {
        return SaveGraphDocumentJson(path, document, presentation, limits);
    }

    [[nodiscard]] Result<std::vector<GraphIoWarning>> Load(
        const std::string_view path,
        const DocumentMigrationRegistry* migrations = nullptr,
        const GraphIoLimits& limits = {}) {
        if (commands.HasPending()) {
            return std::unexpected(Error{
                ErrorCode::OperationPending,
                "Cannot replace a workspace while a deferred operation is pending"});
        }
        if (commands.IsBusy()) {
            return std::unexpected(Error{
                ErrorCode::CommandFailed,
                "Cannot replace a workspace while its command stack is executing"});
        }
        if (m_loading) return LoadingError<std::vector<GraphIoWarning>>();
        struct LoadingGuard final {
            bool& value;
            CommandStack& commands;
            bool owns_stack{false};
            ~LoadingGuard() {
                if (owns_stack) commands.EndExclusiveOperation();
                value = false;
            }
        };
        m_loading = true;
        LoadingGuard loading_guard{m_loading, commands};
        if (!commands.BeginExclusiveOperation()) {
            return std::unexpected(Error{
                ErrorCode::CommandFailed,
                "Cannot replace a workspace while its command stack is unavailable"});
        }
        loading_guard.owns_stack = true;
        const std::uint64_t document_identity = document.Identity();
        const std::uint64_t presentation_identity = presentation.Identity();
        const std::uint64_t model_revision = document.ModelRevision();
        const std::uint64_t presentation_revision = presentation.PresentationRevision();
        const std::uint64_t document_epoch = document.AllocationEpoch();
        const std::uint64_t presentation_epoch = presentation.AllocationEpoch();
        auto loaded = LoadGraphDocumentJson(path, m_registry, migrations, limits);
        if (!loaded) return std::unexpected(std::move(loaded.error()));
        if (document.Identity() != document_identity || presentation.Identity() != presentation_identity ||
            document.ModelRevision() != model_revision ||
            presentation.PresentationRevision() != presentation_revision ||
            document.AllocationEpoch() != document_epoch ||
            presentation.AllocationEpoch() != presentation_epoch) {
            return std::unexpected(Error{
                ErrorCode::RevisionConflict,
                "Workspace changed while load callbacks were running"});
        }
        auto warnings = std::move(loaded->warnings);
        EditorContext reset_editor;
        document.Swap(loaded->document);
        presentation.Swap(loaded->presentation);
        editor.Swap(reset_editor);
        commands.EndExclusiveOperation();
        loading_guard.owns_stack = false;
        commands.Clear();
        return warnings;
    }

    [[nodiscard]] EditorResult Draw(
        const Vec2 size = {},
        const EditorStyle& style = {},
        const EditorConfig& config = {},
        const EditorCallbacks& callbacks = {},
        const GraphPolicy& policy = {}) {
        return DrawEditor(
            editor,
            document,
            presentation,
            commands,
            m_registry,
            node_ui,
            routers,
            size,
            style,
            config,
            callbacks,
            policy);
    }

    GraphDocument document;
    GraphPresentation presentation;
    NodeUiRegistry node_ui;
    LinkRouterRegistry routers;
    CommandStack commands;
    EditorContext editor;

private:
    RegistryCatalog m_registry;

    template<typename Value>
    [[nodiscard]] static Result<Value> LoadingError() {
        return std::unexpected(Error{
            ErrorCode::CommandFailed,
            "Workspace mutation is unavailable while a document is loading"});
    }

    template<typename Value>
    [[nodiscard]] static Result<Value> BusyRegistryError() {
        return std::unexpected(Error{
            ErrorCode::CommandFailed,
            "Registry mutation is unavailable while the command stack is executing"});
    }

    bool m_loading{false};
};

} // namespace Uni::GUI::Nodes
