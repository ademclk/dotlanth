/// @file capabilities.cpp
/// @brief Implementation of CapabilityManager for SEC-001

#include <dotvm/core/capabilities/capability_manager.hpp>
#include <dotvm/core/security_stats.hpp>

#include <algorithm>

namespace dotvm::core::capabilities {

// ============================================================================
// Constructor
// ============================================================================

CapabilityManager::CapabilityManager(SecurityStats* stats) noexcept
    : stats_(stats) {}

CapabilityManager::CapabilityManager(CapabilityManager&& other) noexcept
    : capabilities_(std::move(other.capabilities_)),
      children_(std::move(other.children_)),
      next_id_(other.next_id_.load(std::memory_order_relaxed)),
      total_revoked_(other.total_revoked_.load(std::memory_order_relaxed)),
      stats_(other.stats_) {
    other.stats_ = nullptr;
}

CapabilityManager& CapabilityManager::operator=(CapabilityManager&& other) noexcept {
    if (this != &other) {
        std::unique_lock lock(mutex_);
        capabilities_ = std::move(other.capabilities_);
        children_ = std::move(other.children_);
        next_id_.store(other.next_id_.load(std::memory_order_relaxed),
                       std::memory_order_relaxed);
        total_revoked_.store(other.total_revoked_.load(std::memory_order_relaxed),
                            std::memory_order_relaxed);
        stats_ = other.stats_;
        other.stats_ = nullptr;
    }
    return *this;
}

// ============================================================================
// ID Generation
// ============================================================================

std::uint64_t CapabilityManager::next_id() noexcept {
    return next_id_.fetch_add(1, std::memory_order_relaxed);
}

// ============================================================================
// Root Capability Creation
// ============================================================================

CapabilityHandle CapabilityManager::create_root(
    std::string name,
    Permission perms,
    CapabilityLimits limits,
    TimePoint expires) noexcept {

    std::uint64_t id = next_id();

    Capability cap{
        .id = id,
        .name = std::move(name),
        .permissions = perms,
        .limits = limits,
        .expires_at = expires,
        .granted_by = 0,  // Root has no parent
        .generation = 1,
        .is_active = true
    };

    CapabilityHandle handle{
        .id = id,
        .generation = cap.generation
    };

    {
        std::unique_lock lock(mutex_);
        capabilities_.emplace(id, std::move(cap));
    }

    record_creation();
    return handle;
}

// ============================================================================
// Capability Derivation
// ============================================================================

CapabilityManager::Result<CapabilityHandle> CapabilityManager::derive(
    CapabilityHandle parent,
    std::string name,
    Permission perms,
    CapabilityLimits limits,
    TimePoint expires) noexcept {

    std::unique_lock lock(mutex_);

    // Validate parent handle
    const Capability* parent_cap = get_internal(parent);
    if (parent_cap == nullptr) {
        return std::unexpected(CapabilityError::InvalidParent);
    }

    // Check parent is valid
    if (!parent_cap->is_valid()) {
        if (!parent_cap->is_active) {
            return std::unexpected(CapabilityError::Revoked);
        }
        return std::unexpected(CapabilityError::Expired);
    }

    // Check parent has Derive permission
    if (!parent_cap->has(Permission::Derive)) {
        record_permission_violation();
        return std::unexpected(CapabilityError::DerivationNotAllowed);
    }

    // Check permissions are subset
    if (!is_subset(parent_cap->permissions, perms)) {
        record_permission_violation();
        return std::unexpected(CapabilityError::PermissionNotSubset);
    }

    // Check limits are within parent's limits
    if (!limits.is_within(parent_cap->limits)) {
        record_limit_violation();
        return std::unexpected(CapabilityError::LimitsNotWithin);
    }

    // Check expiration doesn't exceed parent's
    if (parent_cap->expires_at != NO_EXPIRATION) {
        if (expires == NO_EXPIRATION || expires > parent_cap->expires_at) {
            expires = parent_cap->expires_at;
        }
    }

    // Create the child capability
    std::uint64_t id = next_id();

    Capability cap{
        .id = id,
        .name = std::move(name),
        .permissions = perms,
        .limits = limits,
        .expires_at = expires,
        .granted_by = parent.id,
        .generation = 1,
        .is_active = true
    };

    CapabilityHandle handle{
        .id = id,
        .generation = cap.generation
    };

    capabilities_.emplace(id, std::move(cap));
    children_[parent.id].insert(id);

    lock.unlock();
    record_derivation();

    return handle;
}

// ============================================================================
// Capability Revocation
// ============================================================================

CapabilityError CapabilityManager::revoke(CapabilityHandle handle) noexcept {
    std::unique_lock lock(mutex_);

    auto it = capabilities_.find(handle.id);
    if (it == capabilities_.end()) {
        return CapabilityError::InvalidHandle;
    }

    Capability& cap = it->second;

    // Check generation
    if (cap.generation != handle.generation) {
        return CapabilityError::GenerationMismatch;
    }

    // Check if already revoked
    if (!cap.is_active) {
        return CapabilityError::AlreadyRevoked;
    }

    // Revoke this capability and all descendants
    revoke_recursive(handle.id);

    return CapabilityError::Success;
}

void CapabilityManager::revoke_recursive(std::uint64_t id) noexcept {
    // Called with lock held
    auto it = capabilities_.find(id);
    if (it == capabilities_.end() || !it->second.is_active) {
        return;
    }

    // Mark as inactive and bump generation
    it->second.is_active = false;
    it->second.generation++;
    total_revoked_.fetch_add(1, std::memory_order_relaxed);
    record_revocation();

    // Revoke all children
    auto children_it = children_.find(id);
    if (children_it != children_.end()) {
        for (std::uint64_t child_id : children_it->second) {
            revoke_recursive(child_id);
        }
    }
}

// ============================================================================
// Capability Validation
// ============================================================================

bool CapabilityManager::is_valid(CapabilityHandle handle) const noexcept {
    std::shared_lock lock(mutex_);
    const Capability* cap = get_internal(handle);
    return cap != nullptr && cap->is_valid();
}

const Capability* CapabilityManager::get(CapabilityHandle handle) const noexcept {
    std::shared_lock lock(mutex_);
    return get_internal(handle);
}

const Capability* CapabilityManager::get_internal(CapabilityHandle handle) const noexcept {
    // Called with lock held
    if (handle.is_null()) {
        return nullptr;
    }

    auto it = capabilities_.find(handle.id);
    if (it == capabilities_.end()) {
        return nullptr;
    }

    // Check generation
    if (it->second.generation != handle.generation) {
        return nullptr;
    }

    return &it->second;
}

bool CapabilityManager::check_permission(
    CapabilityHandle handle,
    Permission required) const noexcept {

    std::shared_lock lock(mutex_);
    const Capability* cap = get_internal(handle);

    if (cap == nullptr || !cap->is_valid()) {
        return false;
    }

    bool has_perm = cap->has(required);
    if (!has_perm) {
        // Note: Can't record violation here as stats_ recording isn't const
        // This is acceptable as check_permission is a query operation
    }

    return has_perm;
}

bool CapabilityManager::check_limits(
    CapabilityHandle handle,
    std::uint64_t memory,
    std::uint64_t instructions) const noexcept {

    std::shared_lock lock(mutex_);
    const Capability* cap = get_internal(handle);

    if (cap == nullptr || !cap->is_valid()) {
        return false;
    }

    const auto& limits = cap->limits;

    // Check memory limit
    if (limits.max_memory > 0 && memory > limits.max_memory) {
        return false;
    }

    // Check instruction limit
    if (limits.max_instructions > 0 && instructions > limits.max_instructions) {
        return false;
    }

    return true;
}

// ============================================================================
// Preset Capabilities
// ============================================================================

CapabilityHandle CapabilityManager::create_untrusted(std::string name) noexcept {
    Permission perms = Permission::Execute |
                       Permission::MemoryRead |
                       Permission::MemoryWrite;

    return create_root(std::move(name), perms, CapabilityLimits::untrusted());
}

CapabilityHandle CapabilityManager::create_sandbox(std::string name) noexcept {
    Permission perms = Permission::ExecuteBasic |
                       Permission::MemoryAll |
                       Permission::Derive;

    return create_root(std::move(name), perms, CapabilityLimits::sandbox());
}

CapabilityHandle CapabilityManager::create_trusted(std::string name) noexcept {
    // All permissions except BypassCfi
    Permission perms = Permission::All & ~Permission::BypassCfi;

    return create_root(std::move(name), perms, CapabilityLimits::trusted());
}

// ============================================================================
// Statistics
// ============================================================================

std::size_t CapabilityManager::active_count() const noexcept {
    std::shared_lock lock(mutex_);
    std::size_t count = 0;
    for (const auto& [id, cap] : capabilities_) {
        if (cap.is_active) {
            ++count;
        }
    }
    return count;
}

std::uint64_t CapabilityManager::total_created() const noexcept {
    // next_id_ - 1 gives us total created (next_id starts at 1)
    return next_id_.load(std::memory_order_relaxed) - 1;
}

std::uint64_t CapabilityManager::total_revoked() const noexcept {
    return total_revoked_.load(std::memory_order_relaxed);
}

// ============================================================================
// Advanced Operations
// ============================================================================

std::vector<CapabilityHandle> CapabilityManager::get_children(
    CapabilityHandle parent) const noexcept {

    std::shared_lock lock(mutex_);
    std::vector<CapabilityHandle> result;

    auto children_it = children_.find(parent.id);
    if (children_it == children_.end()) {
        return result;
    }

    result.reserve(children_it->second.size());
    for (std::uint64_t child_id : children_it->second) {
        auto cap_it = capabilities_.find(child_id);
        if (cap_it != capabilities_.end()) {
            result.push_back(CapabilityHandle{
                .id = child_id,
                .generation = cap_it->second.generation
            });
        }
    }

    return result;
}

void CapabilityManager::set_security_stats(SecurityStats* stats) noexcept {
    stats_ = stats;
}

// ============================================================================
// Statistics Recording
// ============================================================================

void CapabilityManager::record_creation() noexcept {
    if (stats_ != nullptr) {
        stats_->record_capability_creation();
    }
}

void CapabilityManager::record_derivation() noexcept {
    if (stats_ != nullptr) {
        stats_->record_capability_derivation();
    }
}

void CapabilityManager::record_revocation() noexcept {
    if (stats_ != nullptr) {
        stats_->record_capability_revocation();
    }
}

void CapabilityManager::record_permission_violation() noexcept {
    if (stats_ != nullptr) {
        stats_->record_permission_violation();
    }
}

void CapabilityManager::record_limit_violation() noexcept {
    if (stats_ != nullptr) {
        stats_->record_limit_violation();
    }
}

}  // namespace dotvm::core::capabilities
