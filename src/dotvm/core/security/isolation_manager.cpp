/// @file isolation_manager.cpp
/// @brief SEC-007 Isolation Manager implementation

#include "dotvm/core/security/isolation_manager.hpp"

#include <algorithm>

namespace dotvm::core::security {

// ============================================================================
// Sandbox Lifecycle
// ============================================================================

auto IsolationManager::create_sandbox(DotId dot_id, IsolationLevel level, DotId parent) noexcept
    -> Result<void> {
    // Check if Dot already exists
    if (sandboxes_.contains(dot_id)) {
        return std::unexpected{IsolationError::DotAlreadyExists};
    }

    // Validate parent if specified
    if (parent != INVALID_DOT_ID) {
        auto* parent_sandbox = get_sandbox_mut(parent);
        if (parent_sandbox == nullptr) {
            return std::unexpected{IsolationError::ParentNotFound};
        }
        // Register this Dot as a child of parent
        parent_sandbox->children.push_back(dot_id);
    }

    // Create the sandbox
    Sandbox sandbox{
        .dot_id = dot_id,
        .level = level,
        .handle_table = nullptr,
        .syscall_whitelist = {},
        .parent_id = parent,
        .children = {},
        .incoming_grants = {},
        .outgoing_grants = {},
    };

    // Create HandleTable if memory isolation is required
    if (requires_memory_isolation(level)) {
        sandbox.handle_table = std::make_unique<HandleTable>();
    }

    // Initialize syscall whitelist based on level
    if (requires_syscall_whitelist(level)) {
        sandbox.syscall_whitelist = SyscallWhitelist::strict_default();
    } else {
        sandbox.syscall_whitelist = SyscallWhitelist::allow_all();
    }

    sandboxes_.emplace(dot_id, std::move(sandbox));
    return {};
}

auto IsolationManager::destroy_sandbox(DotId dot_id) noexcept -> IsolationError {
    auto* sandbox = get_sandbox_mut(dot_id);
    if (sandbox == nullptr) {
        return IsolationError::DotNotFound;
    }

    // Cannot destroy sandbox with active children
    if (!sandbox->children.empty()) {
        return IsolationError::HasActiveChildren;
    }

    // Remove from parent's children list
    if (sandbox->parent_id != INVALID_DOT_ID) {
        auto* parent = get_sandbox_mut(sandbox->parent_id);
        if (parent != nullptr) {
            auto& children = parent->children;
            children.erase(std::remove(children.begin(), children.end(), dot_id), children.end());

            // Also remove any outgoing grants to this child
            auto& grants = parent->outgoing_grants;
            grants.erase(
                std::remove_if(grants.begin(), grants.end(),
                               [dot_id](const HandleGrant& g) { return g.child_dot == dot_id; }),
                grants.end());
        }
    }

    sandboxes_.erase(dot_id);
    return IsolationError::Success;
}

// ============================================================================
// Boundary Enforcement
// ============================================================================

auto IsolationManager::enforce_boundary(DotId source_dot, DotId target_dot, Handle handle,
                                        AccessType access) const noexcept -> IsolationError {
    // Same-Dot access is always allowed (handle validation done elsewhere)
    if (source_dot == target_dot) {
        return IsolationError::Success;
    }

    const auto* source_sandbox = get_sandbox(source_dot);
    if (source_sandbox == nullptr) {
        return IsolationError::DotNotFound;
    }

    // If source has no isolation, shared memory is allowed
    if (source_sandbox->level == IsolationLevel::None) {
        return IsolationError::Success;
    }

    // Cross-Dot access requires a valid grant
    const auto* grant = find_grant(source_dot, target_dot, handle);
    if (grant == nullptr) {
        return IsolationError::AccessDenied;
    }

    if (grant->revoked) {
        return IsolationError::GrantRevoked;
    }

    if (!grant->allows(access)) {
        return IsolationError::AccessDenied;
    }

    return IsolationError::Success;
}

auto IsolationManager::validate_syscall(DotId dot_id, SyscallId syscall) const noexcept
    -> IsolationError {
    const auto* sandbox = get_sandbox(dot_id);
    if (sandbox == nullptr) {
        return IsolationError::DotNotFound;
    }

    // Only validate syscalls in Strict mode
    if (!requires_syscall_whitelist(sandbox->level)) {
        return IsolationError::Success;
    }

    if (!sandbox->syscall_whitelist.is_allowed(syscall)) {
        return IsolationError::SyscallDenied;
    }

    return IsolationError::Success;
}

// ============================================================================
// Handle Grants
// ============================================================================

auto IsolationManager::grant_handle(DotId parent, DotId child, Handle handle, bool can_read,
                                    bool can_write) noexcept -> Result<Handle> {
    // At least one permission must be granted
    if (!can_read && !can_write) {
        return std::unexpected{IsolationError::AccessDenied};
    }

    auto* parent_sandbox = get_sandbox_mut(parent);
    if (parent_sandbox == nullptr) {
        return std::unexpected{IsolationError::ParentNotFound};
    }

    auto* child_sandbox = get_sandbox_mut(child);
    if (child_sandbox == nullptr) {
        return std::unexpected{IsolationError::DotNotFound};
    }

    // Verify parent-child relationship
    auto& children = parent_sandbox->children;
    if (std::find(children.begin(), children.end(), child) == children.end()) {
        return std::unexpected{IsolationError::InvalidRelationship};
    }

    // Validate parent owns the handle (if parent has isolated memory)
    if (parent_sandbox->handle_table != nullptr) {
        if (!parent_sandbox->handle_table->is_valid_handle(handle)) {
            return std::unexpected{IsolationError::HandleNotOwned};
        }
    }

    // Generate a handle in the child's namespace
    Handle granted_handle = generate_child_handle(*child_sandbox);

    // Create the grant record
    HandleGrant grant{
        .source_handle = handle,
        .granted_handle = granted_handle,
        .parent_dot = parent,
        .child_dot = child,
        .can_read = can_read,
        .can_write = can_write,
        .revoked = false,
    };

    // Store grant in both parent (outgoing) and child (incoming)
    parent_sandbox->outgoing_grants.push_back(grant);
    child_sandbox->incoming_grants.push_back(grant);

    return granted_handle;
}

auto IsolationManager::revoke_handle(DotId parent, DotId child, Handle handle) noexcept
    -> IsolationError {
    auto* parent_sandbox = get_sandbox_mut(parent);
    if (parent_sandbox == nullptr) {
        return IsolationError::ParentNotFound;
    }

    auto* child_sandbox = get_sandbox_mut(child);
    if (child_sandbox == nullptr) {
        return IsolationError::DotNotFound;
    }

    // Find and revoke in parent's outgoing grants
    bool found = false;
    for (auto& grant : parent_sandbox->outgoing_grants) {
        if (grant.child_dot == child && grant.granted_handle == handle && !grant.revoked) {
            grant.revoked = true;
            found = true;
            break;
        }
    }

    if (!found) {
        return IsolationError::GrantNotFound;
    }

    // Also revoke in child's incoming grants
    for (auto& grant : child_sandbox->incoming_grants) {
        if (grant.parent_dot == parent && grant.granted_handle == handle && !grant.revoked) {
            grant.revoked = true;
            break;
        }
    }

    return IsolationError::Success;
}

// ============================================================================
// Capability Integration
// ============================================================================

auto IsolationManager::can_access_network(DotId dot_id) const noexcept -> bool {
    const auto* sandbox = get_sandbox(dot_id);
    if (sandbox == nullptr) {
        return false;  // Unknown Dot cannot access network
    }
    return !restricts_network(sandbox->level);
}

auto IsolationManager::can_access_filesystem(DotId dot_id) const noexcept -> bool {
    const auto* sandbox = get_sandbox(dot_id);
    if (sandbox == nullptr) {
        return false;  // Unknown Dot cannot access filesystem
    }
    return !restricts_filesystem(sandbox->level);
}

// ============================================================================
// Memory Manager Integration
// ============================================================================

auto IsolationManager::get_handle_table(DotId dot_id) noexcept -> HandleTable* {
    auto* sandbox = get_sandbox_mut(dot_id);
    if (sandbox == nullptr) {
        return nullptr;
    }
    return sandbox->handle_table.get();
}

auto IsolationManager::get_handle_table(DotId dot_id) const noexcept -> const HandleTable* {
    const auto* sandbox = get_sandbox(dot_id);
    if (sandbox == nullptr) {
        return nullptr;
    }
    return sandbox->handle_table.get();
}

// ============================================================================
// Query Methods
// ============================================================================

auto IsolationManager::has_sandbox(DotId dot_id) const noexcept -> bool {
    return sandboxes_.contains(dot_id);
}

auto IsolationManager::get_isolation_level(DotId dot_id) const noexcept -> Result<IsolationLevel> {
    const auto* sandbox = get_sandbox(dot_id);
    if (sandbox == nullptr) {
        return std::unexpected{IsolationError::DotNotFound};
    }
    return sandbox->level;
}

auto IsolationManager::get_parent(DotId dot_id) const noexcept -> Result<DotId> {
    const auto* sandbox = get_sandbox(dot_id);
    if (sandbox == nullptr) {
        return std::unexpected{IsolationError::DotNotFound};
    }
    return sandbox->parent_id;
}

auto IsolationManager::get_children(DotId dot_id) const noexcept -> Result<std::vector<DotId>> {
    const auto* sandbox = get_sandbox(dot_id);
    if (sandbox == nullptr) {
        return std::unexpected{IsolationError::DotNotFound};
    }
    return sandbox->children;
}

auto IsolationManager::sandbox_count() const noexcept -> std::size_t {
    return sandboxes_.size();
}

auto IsolationManager::get_syscall_whitelist(DotId dot_id) const noexcept
    -> const SyscallWhitelist* {
    const auto* sandbox = get_sandbox(dot_id);
    if (sandbox == nullptr) {
        return nullptr;
    }
    return &sandbox->syscall_whitelist;
}

auto IsolationManager::get_syscall_whitelist(DotId dot_id) noexcept -> SyscallWhitelist* {
    auto* sandbox = get_sandbox_mut(dot_id);
    if (sandbox == nullptr) {
        return nullptr;
    }
    return &sandbox->syscall_whitelist;
}

// ============================================================================
// Private Helpers
// ============================================================================

auto IsolationManager::find_grant(DotId source_dot, DotId target_dot, Handle handle) const noexcept
    -> const HandleGrant* {
    const auto* source_sandbox = get_sandbox(source_dot);
    if (source_sandbox == nullptr) {
        return nullptr;
    }

    // Check incoming grants to source from target (source accessing target's handle via grant)
    for (const auto& grant : source_sandbox->incoming_grants) {
        if (grant.parent_dot == target_dot && grant.granted_handle == handle) {
            return &grant;
        }
    }

    return nullptr;
}

auto IsolationManager::find_grant_mut(DotId source_dot, DotId target_dot, Handle handle) noexcept
    -> HandleGrant* {
    auto* source_sandbox = get_sandbox_mut(source_dot);
    if (source_sandbox == nullptr) {
        return nullptr;
    }

    for (auto& grant : source_sandbox->incoming_grants) {
        if (grant.parent_dot == target_dot && grant.granted_handle == handle) {
            return &grant;
        }
    }

    return nullptr;
}

auto IsolationManager::get_sandbox(DotId dot_id) const noexcept -> const Sandbox* {
    auto it = sandboxes_.find(dot_id);
    if (it == sandboxes_.end()) {
        return nullptr;
    }
    return &it->second;
}

auto IsolationManager::get_sandbox_mut(DotId dot_id) noexcept -> Sandbox* {
    auto it = sandboxes_.find(dot_id);
    if (it == sandboxes_.end()) {
        return nullptr;
    }
    return &it->second;
}

auto IsolationManager::generate_child_handle(Sandbox& sandbox) noexcept -> Handle {
    // Generate unique handle in the sandbox's namespace
    // Use the sandbox's handle table if it exists, otherwise use global counter
    std::uint32_t index = next_handle_index_++;
    std::uint32_t generation = 1;

    if (sandbox.handle_table != nullptr) {
        // Allocate a slot in the child's handle table
        index = sandbox.handle_table->allocate_slot();
        if (index != mem_config::INVALID_INDEX) {
            generation = (*sandbox.handle_table)[index].generation;
        }
    }

    return Handle{.index = index, .generation = generation};
}

}  // namespace dotvm::core::security
