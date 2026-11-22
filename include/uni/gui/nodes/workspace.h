#pragma once

/// @file
/// @brief UI-thread facade tying a document, presentation, command history and registries to one editor.

#include <uni/gui/nodes/editor.h>
#include <uni/gui/nodes/io.h>
#include <uni/gui/nodes/snapshot.h>

#include <memory>
#include <string_view>
#include <utility>
#include <vector>

namespace Uni::GUI::Nodes {

/// Stable, nonmovable workspace. Command methods always use this document/presentation and its
/// single semantic catalog; their results, conflicts and deferrals follow CommandStack. During
/// a guarded load, command and registration methods return CommandFailed. Const references into
/// live owners are not snapshots; use CaptureSnapshot() before handing model data to a worker.
class NodeEditorWorkspace final {
public:
    NodeEditorWorkspace() = default;
    ~NodeEditorWorkspace() = default;
    NodeEditorWorkspace(const NodeEditorWorkspace&) = delete;
    NodeEditorWorkspace& operator=(const NodeEditorWorkspace&) = delete;
    NodeEditorWorkspace(NodeEditorWorkspace&&) = delete;
    NodeEditorWorkspace& operator=(NodeEditorWorkspace&&) = delete;

    /// Consume a non-null command. Success may be a no-op or pending authorization; inspect
    /// CommandResult::deferred before treating an edit as committed.
    [[nodiscard]] Result<CommandResult> Execute(
        std::unique_ptr<Command> command,
        const GraphPolicy& policy = {}) {
        if (m_loading) return LoadingError<CommandResult>();
        return commands.Execute(std::move(command), document, presentation, m_registry, policy);
    }

    /// Restore recorded history, bypassing current policy/protection; NothingToUndo if empty.
    [[nodiscard]] Result<CommandResult> Undo() {
        if (m_loading) return LoadingError<CommandResult>();
        return commands.Undo(document, presentation, m_registry);
    }

    /// By default authorize the inverse batch with policy, unlike the parameterless overload.
    /// Pass RestoreHistory to bypass authorization. Denial/deferral do not consume the undo entry.
    [[nodiscard]] Result<CommandResult> Undo(
        const GraphPolicy& policy,
        const UndoPolicyMode mode = UndoPolicyMode::RespectCurrentPolicy) {
        if (m_loading) return LoadingError<CommandResult>();
        return commands.Undo(document, presentation, policy, mode, m_registry);
    }

    /// Reapply the last undone command with current protection and policy; NothingToRedo if empty.
    [[nodiscard]] Result<CommandResult> Redo(const GraphPolicy& policy = {}) {
        if (m_loading) return LoadingError<CommandResult>();
        return commands.Redo(document, presentation, m_registry, policy);
    }

    /// Approve or reauthorize the identified prepared edit. Both modes recheck freshness; a
    /// changed owner, allocation epoch or recorded catalog dependency may require cancellation.
    [[nodiscard]] Result<CommandResult> Resume(
        const DeferredOperationId operation,
        const ResumeMode mode,
        const GraphPolicy& policy = {}) {
        if (m_loading) return LoadingError<CommandResult>();
        return commands.Resume(operation, document, presentation, mode, policy);
    }

    /// Drop a retained batch without publishing it; a nonmatching ID gives DeferredOperationNotFound.
    [[nodiscard]] Result<void> Cancel(const DeferredOperationId operation) {
        if (m_loading) return LoadingError<void>();
        return commands.Cancel(operation);
    }

    /// True while this workspace has a staged command awaiting authorization.
    [[nodiscard]] bool HasPending() const noexcept { return commands.HasPending(); }

    /// Borrow the workspace catalog. RegistryCatalog::Snapshot() retains an immutable generation.
    [[nodiscard]] const RegistryCatalog& Registry() const noexcept { return m_registry; }

    /// Begin a move-only registry batch (no publication until Commit()). Starting an update during
    /// load or stack execution fails; merely waiting for deferred approval is not a registry lock.
    [[nodiscard]] Result<RegistryUpdate> BeginUpdate() {
        if (m_loading) return LoadingError<RegistryUpdate>();
        if (commands.IsBusy()) {
            return std::unexpected(Error{
                ErrorCode::CommandFailed,
                "Cannot update registries while the command stack is executing"});
        }
        return m_registry.BeginUpdate();
    }

    /// @name Runtime node descriptors and type compatibility
    /// Delegate to RegistryCatalog with a busy/load guard. Registration rejects duplicates;
    /// replacement requires an existing type. Unregister returns false on miss, TypeInUse for
    /// remaining converter registrations; it does not delete instances. An empty compatibility
    /// callback/clear removes the extension, leaving built-in compatibility and conversion lookup.
    /// @{
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

    [[nodiscard]] Result<void> SetTypeCompatibility(TypeCompatibilityFn compatibility) {
        if (m_loading) return LoadingError<void>();
        if (commands.IsBusy()) return BusyRegistryError<void>();
        return m_registry.SetTypeCompatibility(std::move(compatibility));
    }

    [[nodiscard]] Result<void> ClearTypeCompatibility() {
        if (m_loading) return LoadingError<void>();
        if (commands.IsBusy()) return BusyRegistryError<void>();
        return m_registry.ClearTypeCompatibility();
    }

    /// @}

    /// Reserve IDs from document and prepare (do not insert) an instance using defaults and options.
    /// Unknown type, exhausted IDs or schema resolution errors propagate. Use AddNodeCommand to insert.
    [[nodiscard]] Result<NodeCreation> InstantiateNode(
        const TypeId& type,
        NodeInstantiationOptions options = {}) {
        if (m_loading) return LoadingError<NodeCreation>();
        return m_registry.Instantiate(document, type, std::move(options));
    }

    /// @name Converter registrations
    /// Register a unique directed conversion key and receive a catalog-bound token. Replace keeps
    /// its token; unregister returns false for an already removed local token. Foreign/empty tokens
    /// are errors. These runtime changes are blocked while loading or executing, and not serialized.
    /// @{
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

    /// @}

    /// Borrow the pending description, or null. Copy to retain beyond resume, cancel or workspace destruction.
    [[nodiscard]] const DeferredOperation* PendingOperation() const noexcept {
        return commands.PendingOperation();
    }

    /// Capture committed semantic state on the UI thread; worker reads remain valid across load/edits.
    [[nodiscard]] GraphDocumentSnapshot CaptureSnapshot() const {
        return CaptureGraphDocumentSnapshot(document);
    }

    /// Save only committed document and presentation, even while an operation is pending. Does not
    /// persist history, registrations or editor navigation. See SaveGraphDocumentJson() for file errors.
    [[nodiscard]] Result<void> Save(
        const std::string_view path,
        const GraphIoLimits& limits = {}) const {
        return SaveGraphDocumentJson(path, document, presentation, limits);
    }

    /// Load into fresh temporary owners, optionally migrate, and return nonfatal warnings. A null
    /// migrations pointer accepts only application schema one. OperationPending blocks a pending
    /// workspace; a busy stack or recursive load gives CommandFailed. Owner/revision/allocation
    /// changes in callbacks cause RevisionConflict. Only on success swap in the new owners, reset
    /// transient editor state and clear history. Existing runtime registrations are kept.
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

    /// DrawEditor() using this workspace's owners. Call inside an active ImGui frame/window on the
    /// UI thread, not recursively. Zero size components use available space. Style, config, callbacks
    /// and policy are borrowed for this call; inspect the returned frame result and editor.LastError().
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

    GraphDocument document;          ///< Committed semantic state, not the pending transaction.
    GraphPresentation presentation;  ///< Persisted canvas records matching document IDs.
    NodeUiRegistry node_ui;          ///< Runtime draw/layout registrations, independent of semantic types.
    LinkRouterRegistry routers;      ///< Runtime implementations selected by persisted router IDs.
    CommandStack commands;           ///< Workspace history and pending authorization.
    EditorContext editor;            ///< Transient selection, navigation, interaction and caches.

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
