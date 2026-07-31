#include "nodes/commands/internal.h"

namespace Uni::GUI::Nodes {

using CommandDetail::MakeError;

struct AddGroupCommand::Impl final { GroupPresentation group; };

AddGroupCommand::AddGroupCommand(GroupPresentation group)
    : m_impl(std::make_unique<Impl>(Impl{std::move(group)})) {}
AddGroupCommand::~AddGroupCommand() = default;
std::string_view AddGroupCommand::Name() const noexcept { return "Add group"; }
Result<void> AddGroupCommand::Apply(GraphTransaction& transaction, const RegistrySnapshot&) {
    return transaction.AddGroup(m_impl->group);
}
Result<void> AddGroupCommand::Revert(GraphTransaction& transaction) {
    auto removed = transaction.RemoveGroup(m_impl->group.id);
    return removed ? Result<void>{} : std::unexpected(std::move(removed.error()));
}

struct RemoveGroupCommand::Impl final {
    GroupId group;
    std::optional<GroupPresentation> removed;
};

RemoveGroupCommand::RemoveGroupCommand(const GroupId group)
    : m_impl(std::make_unique<Impl>()) { m_impl->group = group; }
RemoveGroupCommand::~RemoveGroupCommand() = default;
std::string_view RemoveGroupCommand::Name() const noexcept { return "Remove group"; }
Result<void> RemoveGroupCommand::Apply(GraphTransaction& transaction, const RegistrySnapshot&) {
    auto removed = transaction.RemoveGroup(m_impl->group);
    if (!removed) return std::unexpected(std::move(removed.error()));
    if (!m_impl->removed) m_impl->removed = *removed;
    return {};
}
Result<void> RemoveGroupCommand::Revert(GraphTransaction& transaction) {
    if (!m_impl->removed) return std::unexpected(MakeError(ErrorCode::CommandFailed, "Remove group command was not executed"));
    return transaction.AddGroup(*m_impl->removed);
}

struct SetGroupStyleCommand::Impl final {
    GroupId group;
    GroupStyleHandle value;
    GroupStyleHandle previous;
};

SetGroupStyleCommand::SetGroupStyleCommand(const GroupId group, GroupStyle style)
    : SetGroupStyleCommand(group, MakeGroupStyle(std::move(style))) {}
SetGroupStyleCommand::SetGroupStyleCommand(const GroupId group, GroupStyleHandle style)
    : m_impl(std::make_unique<Impl>(Impl{group, std::move(style), {}})) {}
SetGroupStyleCommand::~SetGroupStyleCommand() = default;
std::string_view SetGroupStyleCommand::Name() const noexcept { return "Set group style"; }
Result<void> SetGroupStyleCommand::Apply(GraphTransaction& transaction, const RegistrySnapshot&) {
    if (!m_impl->previous) {
        const auto* current = transaction.Presentation().FindGroup(m_impl->group);
        if (current == nullptr) return std::unexpected(MakeError(ErrorCode::InvalidArgument, "Group does not exist"));
        m_impl->previous = current->style;
    }
    return transaction.SetGroupStyle(m_impl->group, m_impl->value);
}
Result<void> SetGroupStyleCommand::Revert(GraphTransaction& transaction) {
    if (!m_impl->previous) return std::unexpected(MakeError(ErrorCode::CommandFailed, "Group command was not executed"));
    return transaction.SetGroupStyle(m_impl->group, m_impl->previous);
}

struct SetGroupMembersCommand::Impl final {
    GroupId group;
    GroupMemberSet value;
    std::optional<GroupMemberSet> previous;
};

SetGroupMembersCommand::SetGroupMembersCommand(const GroupId group, GroupMemberSet members)
    : m_impl(std::make_unique<Impl>(Impl{group, std::move(members), std::nullopt})) {}
SetGroupMembersCommand::~SetGroupMembersCommand() = default;
std::string_view SetGroupMembersCommand::Name() const noexcept { return "Set group members"; }
Result<void> SetGroupMembersCommand::Apply(GraphTransaction& transaction, const RegistrySnapshot&) {
    if (!m_impl->previous) {
        const auto* current = transaction.Presentation().FindGroup(m_impl->group);
        if (current == nullptr) return std::unexpected(MakeError(ErrorCode::InvalidArgument, "Group does not exist"));
        m_impl->previous = current->members;
    }
    return transaction.SetGroupMembers(m_impl->group, m_impl->value);
}
Result<void> SetGroupMembersCommand::Revert(GraphTransaction& transaction) {
    if (!m_impl->previous) return std::unexpected(MakeError(ErrorCode::CommandFailed, "Group command was not executed"));
    return transaction.SetGroupMembers(m_impl->group, *m_impl->previous);
}

struct ChangeGroupMembersCommand::Impl final {
    GroupId group;
    std::vector<NodeId> added;
    std::vector<NodeId> removed;
    bool captured{false};
};

ChangeGroupMembersCommand::ChangeGroupMembersCommand(
    const GroupId group,
    std::vector<NodeId> added,
    std::vector<NodeId> removed)
    : m_impl(std::make_unique<Impl>(Impl{
          .group = group,
          .added = std::move(added),
          .removed = std::move(removed),
      })) {}
ChangeGroupMembersCommand::~ChangeGroupMembersCommand() = default;
std::string_view ChangeGroupMembersCommand::Name() const noexcept { return "Change group members"; }
Result<void> ChangeGroupMembersCommand::Apply(GraphTransaction& transaction, const RegistrySnapshot&) {
    if (!m_impl->captured) {
        const auto* current = transaction.Presentation().FindGroup(m_impl->group);
        if (current == nullptr) return std::unexpected(MakeError(ErrorCode::InvalidArgument, "Group does not exist"));
        std::unordered_set<NodeId, IdHash> requested_additions(m_impl->added.begin(), m_impl->added.end());
        std::unordered_set<NodeId, IdHash> seen_additions;
        std::unordered_set<NodeId, IdHash> seen_removals;
        std::vector<NodeId> effective_additions;
        std::vector<NodeId> effective_removals;
        effective_additions.reserve(m_impl->added.size());
        effective_removals.reserve(m_impl->removed.size());
        for (const NodeId node : m_impl->removed) {
            if (current->members.contains(node) && !requested_additions.contains(node) &&
                seen_removals.insert(node).second) {
                effective_removals.push_back(node);
            }
        }
        for (const NodeId node : m_impl->added) {
            if (!current->members.contains(node) && seen_additions.insert(node).second) {
                effective_additions.push_back(node);
            }
        }
        m_impl->added = std::move(effective_additions);
        m_impl->removed = std::move(effective_removals);
        m_impl->captured = true;
    }
    if (auto removed = transaction.RemoveGroupMembers(m_impl->group, m_impl->removed); !removed) return removed;
    return transaction.AddGroupMembers(m_impl->group, m_impl->added);
}
Result<void> ChangeGroupMembersCommand::Revert(GraphTransaction& transaction) {
    if (!m_impl->captured) {
        return std::unexpected(MakeError(ErrorCode::CommandFailed, "Group membership command was not executed"));
    }
    if (auto removed = transaction.RemoveGroupMembers(m_impl->group, m_impl->added); !removed) return removed;
    return transaction.AddGroupMembers(m_impl->group, m_impl->removed);
}

struct SetGroupZOrderCommand::Impl final {
    GroupId group;
    std::uint64_t value{0};
    std::uint64_t previous{0};
    bool captured{false};
};

SetGroupZOrderCommand::SetGroupZOrderCommand(const GroupId group, const std::uint64_t z_order)
    : m_impl(std::make_unique<Impl>(Impl{.group = group, .value = z_order})) {}
SetGroupZOrderCommand::~SetGroupZOrderCommand() = default;
std::string_view SetGroupZOrderCommand::Name() const noexcept { return "Set group z-order"; }
Result<void> SetGroupZOrderCommand::Apply(GraphTransaction& transaction, const RegistrySnapshot&) {
    const auto* current = transaction.Presentation().FindGroup(m_impl->group);
    if (current == nullptr) return std::unexpected(MakeError(ErrorCode::InvalidArgument, "Group does not exist"));
    if (!m_impl->captured) { m_impl->previous = current->geometry.z_order; m_impl->captured = true; }
    return transaction.SetGroupZOrder(m_impl->group, m_impl->value);
}
Result<void> SetGroupZOrderCommand::Revert(GraphTransaction& transaction) {
    return transaction.SetGroupZOrder(m_impl->group, m_impl->previous);
}

struct MoveGroupCommand::Impl final {
    GroupId group;
    Vec2 value;
    Vec2 previous;
    bool captured{false};
};

MoveGroupCommand::MoveGroupCommand(const GroupId group, const Vec2 position)
    : m_impl(std::make_unique<Impl>(Impl{.group = group, .value = position})) {}
MoveGroupCommand::~MoveGroupCommand() = default;
std::string_view MoveGroupCommand::Name() const noexcept { return "Move group"; }
Result<void> MoveGroupCommand::Apply(GraphTransaction& transaction, const RegistrySnapshot&) {
    const auto* current = transaction.Presentation().FindGroup(m_impl->group);
    if (current == nullptr) return std::unexpected(MakeError(ErrorCode::InvalidArgument, "Group does not exist"));
    if (!m_impl->captured) { m_impl->previous = current->geometry.position; m_impl->captured = true; }
    return transaction.SetGroupPosition(m_impl->group, m_impl->value);
}
Result<void> MoveGroupCommand::Revert(GraphTransaction& transaction) {
    return transaction.SetGroupPosition(m_impl->group, m_impl->previous);
}

struct ResizeGroupCommand::Impl final {
    GroupId group;
    Vec2 value;
    Vec2 previous;
    bool captured{false};
};

ResizeGroupCommand::ResizeGroupCommand(const GroupId group, const Vec2 size)
    : m_impl(std::make_unique<Impl>(Impl{.group = group, .value = size})) {}
ResizeGroupCommand::~ResizeGroupCommand() = default;
std::string_view ResizeGroupCommand::Name() const noexcept { return "Resize group"; }
Result<void> ResizeGroupCommand::Apply(GraphTransaction& transaction, const RegistrySnapshot&) {
    const auto* current = transaction.Presentation().FindGroup(m_impl->group);
    if (current == nullptr) return std::unexpected(MakeError(ErrorCode::InvalidArgument, "Group does not exist"));
    if (!m_impl->captured) { m_impl->previous = current->geometry.size; m_impl->captured = true; }
    return transaction.SetGroupSize(m_impl->group, m_impl->value);
}
Result<void> ResizeGroupCommand::Revert(GraphTransaction& transaction) {
    return transaction.SetGroupSize(m_impl->group, m_impl->previous);
}

struct SetGroupCollapsedCommand::Impl final {
    GroupId group;
    bool value;
    bool previous{false};
    bool captured{false};
};

SetGroupCollapsedCommand::SetGroupCollapsedCommand(const GroupId group, const bool collapsed)
    : m_impl(std::make_unique<Impl>(Impl{.group = group, .value = collapsed})) {}
SetGroupCollapsedCommand::~SetGroupCollapsedCommand() = default;
std::string_view SetGroupCollapsedCommand::Name() const noexcept { return "Set group collapsed"; }
Result<void> SetGroupCollapsedCommand::Apply(GraphTransaction& transaction, const RegistrySnapshot&) {
    const auto* current = transaction.Presentation().FindGroup(m_impl->group);
    if (current == nullptr) return std::unexpected(MakeError(ErrorCode::InvalidArgument, "Group does not exist"));
    if (!m_impl->captured) { m_impl->previous = current->geometry.collapsed; m_impl->captured = true; }
    return transaction.SetGroupCollapsed(m_impl->group, m_impl->value);
}
Result<void> SetGroupCollapsedCommand::Revert(GraphTransaction& transaction) {
    return transaction.SetGroupCollapsed(m_impl->group, m_impl->previous);
}

} // namespace Uni::GUI::Nodes
