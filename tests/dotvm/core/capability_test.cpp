/// @file capability_test.cpp
/// @brief Unit tests for SEC-001 Capability System

#include <dotvm/core/capabilities/capability.hpp>
#include <dotvm/core/capabilities/capability_manager.hpp>
#include <dotvm/core/security_stats.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <future>
#include <thread>
#include <vector>

using namespace dotvm::core::capabilities;
using namespace dotvm::core;

// ============================================================================
// Permission Bitmask Tests
// ============================================================================

TEST(PermissionTest, BitwiseOr) {
    Permission p = Permission::MemoryRead | Permission::MemoryWrite;
    EXPECT_TRUE(has_permission(p, Permission::MemoryRead));
    EXPECT_TRUE(has_permission(p, Permission::MemoryWrite));
    EXPECT_FALSE(has_permission(p, Permission::Execute));
}

TEST(PermissionTest, BitwiseAnd) {
    Permission p1 = Permission::MemoryRead | Permission::MemoryWrite | Permission::Execute;
    Permission p2 = Permission::MemoryRead | Permission::Execute;
    Permission intersection = p1 & p2;
    EXPECT_TRUE(has_permission(intersection, Permission::MemoryRead));
    EXPECT_TRUE(has_permission(intersection, Permission::Execute));
    EXPECT_FALSE(has_permission(intersection, Permission::MemoryWrite));
}

TEST(PermissionTest, BitwiseXor) {
    Permission p1 = Permission::MemoryRead | Permission::MemoryWrite;
    Permission p2 = Permission::MemoryRead | Permission::Execute;
    Permission diff = p1 ^ p2;
    // MemoryWrite and Execute should be in the result (not MemoryRead)
    EXPECT_TRUE(has_permission(diff, Permission::MemoryWrite));
    EXPECT_TRUE(has_permission(diff, Permission::Execute));
    EXPECT_FALSE(has_permission(diff, Permission::MemoryRead));
}

TEST(PermissionTest, BitwiseNot) {
    Permission p = ~Permission::None;
    EXPECT_EQ(p, Permission::All);
}

TEST(PermissionTest, CompoundAssignment) {
    Permission p = Permission::MemoryRead;
    p |= Permission::MemoryWrite;
    EXPECT_TRUE(has_permission(p, Permission::MemoryRead));
    EXPECT_TRUE(has_permission(p, Permission::MemoryWrite));

    p &= Permission::MemoryRead;
    EXPECT_TRUE(has_permission(p, Permission::MemoryRead));
    EXPECT_FALSE(has_permission(p, Permission::MemoryWrite));
}

TEST(PermissionTest, IsSubset) {
    Permission parent = Permission::MemoryAll | Permission::Execute;
    Permission child_valid = Permission::MemoryRead | Permission::MemoryWrite;
    Permission child_invalid = Permission::MemoryRead | Permission::Network;

    EXPECT_TRUE(is_subset(parent, child_valid));
    EXPECT_FALSE(is_subset(parent, child_invalid));
}

TEST(PermissionTest, CompositePermissions) {
    // MemoryAll should include all memory permissions
    EXPECT_TRUE(has_permission(Permission::MemoryAll, Permission::MemoryRead));
    EXPECT_TRUE(has_permission(Permission::MemoryAll, Permission::MemoryWrite));
    EXPECT_TRUE(has_permission(Permission::MemoryAll, Permission::MemoryAllocate));
    EXPECT_TRUE(has_permission(Permission::MemoryAll, Permission::MemoryDeallocate));

    // ExecuteBasic should include Execute and Call
    EXPECT_TRUE(has_permission(Permission::ExecuteBasic, Permission::Execute));
    EXPECT_TRUE(has_permission(Permission::ExecuteBasic, Permission::Call));
}

TEST(PermissionTest, ToString) {
    EXPECT_EQ(to_string(Permission::None), "None");
    EXPECT_EQ(to_string(Permission::All), "All");
    EXPECT_EQ(to_string(Permission::MemoryRead), "MemoryRead");

    Permission combo = Permission::Execute | Permission::Call;
    std::string s = to_string(combo);
    EXPECT_TRUE(s.find("Execute") != std::string::npos);
    EXPECT_TRUE(s.find("Call") != std::string::npos);
}

// ============================================================================
// CapabilityLimits Tests
// ============================================================================

TEST(CapabilityLimitsTest, Unlimited) {
    auto limits = CapabilityLimits::unlimited();
    EXPECT_EQ(limits.max_memory, 0ULL);
    EXPECT_EQ(limits.max_instructions, 0ULL);
    EXPECT_EQ(limits.max_stack_depth, 0U);
    EXPECT_FALSE(limits.has_limits());
}

TEST(CapabilityLimitsTest, UntrustedPreset) {
    auto limits = CapabilityLimits::untrusted();
    EXPECT_EQ(limits.max_memory, 1ULL * 1024 * 1024);  // 1MB
    EXPECT_EQ(limits.max_instructions, 100'000ULL);
    EXPECT_EQ(limits.max_stack_depth, 64U);
    EXPECT_TRUE(limits.has_limits());
}

TEST(CapabilityLimitsTest, SandboxPreset) {
    auto limits = CapabilityLimits::sandbox();
    EXPECT_EQ(limits.max_memory, 16ULL * 1024 * 1024);  // 16MB
    EXPECT_EQ(limits.max_instructions, 1'000'000ULL);
    EXPECT_EQ(limits.max_stack_depth, 256U);
    EXPECT_TRUE(limits.has_limits());
}

TEST(CapabilityLimitsTest, TrustedPreset) {
    auto limits = CapabilityLimits::trusted();
    EXPECT_EQ(limits.max_memory, 256ULL * 1024 * 1024);  // 256MB
    EXPECT_EQ(limits.max_instructions, 100'000'000ULL);
    EXPECT_EQ(limits.max_stack_depth, 4096U);
    EXPECT_TRUE(limits.has_limits());
}

TEST(CapabilityLimitsTest, IsWithinUnlimitedParent) {
    auto parent = CapabilityLimits::unlimited();
    auto child = CapabilityLimits::untrusted();
    // Child with limits is NOT within unlimited parent (child=0 but parent=0 is ok,
    // but child>0 should be within parent>0 or parent=0)
    // Actually, if parent is unlimited (0), any child value is within
    EXPECT_TRUE(child.is_within(parent));
}

TEST(CapabilityLimitsTest, IsWithinTrustedParent) {
    auto parent = CapabilityLimits::trusted();
    auto child = CapabilityLimits::sandbox();
    auto too_large = CapabilityLimits{
        .max_memory = 512ULL * 1024 * 1024,  // 512MB > 256MB
        .max_instructions = 50'000'000,
        .max_stack_depth = 1024
    };

    EXPECT_TRUE(child.is_within(parent));
    EXPECT_FALSE(too_large.is_within(parent));
}

TEST(CapabilityLimitsTest, IsWithinUnlimitedChild) {
    auto parent = CapabilityLimits::trusted();
    auto child = CapabilityLimits::unlimited();
    // Unlimited child (0) is NOT within limited parent (child=0 means unlimited,
    // which exceeds parent's finite limits)
    EXPECT_FALSE(child.is_within(parent));
}

// ============================================================================
// Capability Tests
// ============================================================================

TEST(CapabilityTest, DefaultConstruction) {
    Capability cap;
    EXPECT_EQ(cap.id, 0ULL);
    EXPECT_TRUE(cap.name.empty());
    EXPECT_EQ(cap.permissions, Permission::None);
    EXPECT_TRUE(cap.is_active);
    // Default constructed capability is technically valid (active and not expired)
    // but should not be used without being initialized by CapabilityManager
    EXPECT_TRUE(cap.is_valid());  // is_valid checks is_active + expiration
}

TEST(CapabilityTest, IsRootCapability) {
    Capability root;
    root.granted_by = 0;
    EXPECT_TRUE(root.is_root());

    Capability child;
    child.granted_by = 42;
    EXPECT_FALSE(child.is_root());
}

TEST(CapabilityTest, HasPermission) {
    Capability cap;
    cap.permissions = Permission::MemoryRead | Permission::Execute;
    cap.is_active = true;

    EXPECT_TRUE(cap.has(Permission::MemoryRead));
    EXPECT_TRUE(cap.has(Permission::Execute));
    EXPECT_FALSE(cap.has(Permission::MemoryWrite));
}

TEST(CapabilityTest, CanDerivePermissions) {
    Capability cap;
    cap.permissions = Permission::MemoryAll | Permission::Execute | Permission::Derive;
    cap.is_active = true;

    Permission valid_child = Permission::MemoryRead | Permission::MemoryWrite;
    Permission invalid_child = Permission::MemoryRead | Permission::Network;

    EXPECT_TRUE(cap.can_derive(valid_child));
    EXPECT_FALSE(cap.can_derive(invalid_child));
}

TEST(CapabilityTest, CanDeriveRequiresDerive) {
    Capability cap;
    cap.permissions = Permission::MemoryAll | Permission::Execute;  // No Derive
    cap.is_active = true;

    Permission child = Permission::MemoryRead;
    EXPECT_FALSE(cap.can_derive(child));
}

// ============================================================================
// CapabilityHandle Tests
// ============================================================================

TEST(CapabilityHandleTest, NullHandle) {
    CapabilityHandle handle;
    EXPECT_TRUE(handle.is_null());
    EXPECT_EQ(handle, CapabilityHandle::null());
}

TEST(CapabilityHandleTest, NonNullHandle) {
    CapabilityHandle handle{.id = 1, .generation = 1};
    EXPECT_FALSE(handle.is_null());
}

TEST(CapabilityHandleTest, Equality) {
    CapabilityHandle h1{.id = 1, .generation = 1};
    CapabilityHandle h2{.id = 1, .generation = 1};
    CapabilityHandle h3{.id = 1, .generation = 2};
    CapabilityHandle h4{.id = 2, .generation = 1};

    EXPECT_EQ(h1, h2);
    EXPECT_NE(h1, h3);
    EXPECT_NE(h1, h4);
}

// ============================================================================
// CapabilityManager Tests
// ============================================================================

TEST(CapabilityManagerTest, CreateRoot) {
    CapabilityManager mgr;

    auto handle = mgr.create_root(
        "test-root",
        Permission::MemoryAll | Permission::Execute,
        CapabilityLimits::sandbox());

    EXPECT_FALSE(handle.is_null());
    EXPECT_TRUE(mgr.is_valid(handle));

    const Capability* cap = mgr.get(handle);
    ASSERT_NE(cap, nullptr);
    EXPECT_EQ(cap->name, "test-root");
    EXPECT_TRUE(cap->has(Permission::MemoryRead));
    EXPECT_TRUE(cap->has(Permission::Execute));
    EXPECT_TRUE(cap->is_root());
    EXPECT_TRUE(cap->is_active);
}

TEST(CapabilityManagerTest, DeriveSuccess) {
    CapabilityManager mgr;

    auto parent = mgr.create_root(
        "parent",
        Permission::MemoryAll | Permission::Execute | Permission::Derive,
        CapabilityLimits::trusted());

    auto result = mgr.derive(
        parent,
        "child",
        Permission::MemoryRead | Permission::Execute,
        CapabilityLimits::sandbox());

    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(result->is_null());
    EXPECT_TRUE(mgr.is_valid(*result));

    const Capability* child = mgr.get(*result);
    ASSERT_NE(child, nullptr);
    EXPECT_EQ(child->name, "child");
    EXPECT_EQ(child->granted_by, parent.id);
    EXPECT_FALSE(child->is_root());
}

TEST(CapabilityManagerTest, DeriveRequiresPermission) {
    CapabilityManager mgr;

    // Parent without Derive permission
    auto parent = mgr.create_root(
        "parent",
        Permission::MemoryAll | Permission::Execute,  // No Derive
        CapabilityLimits::trusted());

    auto result = mgr.derive(
        parent,
        "child",
        Permission::MemoryRead,
        CapabilityLimits::sandbox());

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), CapabilityError::DerivationNotAllowed);
}

TEST(CapabilityManagerTest, DerivePermissionSubset) {
    CapabilityManager mgr;

    auto parent = mgr.create_root(
        "parent",
        Permission::MemoryRead | Permission::Execute | Permission::Derive,
        CapabilityLimits::trusted());

    // Try to derive with permission parent doesn't have
    auto result = mgr.derive(
        parent,
        "child",
        Permission::MemoryRead | Permission::Network,  // Parent doesn't have Network
        CapabilityLimits::sandbox());

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), CapabilityError::PermissionNotSubset);
}

TEST(CapabilityManagerTest, DeriveLimitsWithin) {
    CapabilityManager mgr;

    auto parent = mgr.create_root(
        "parent",
        Permission::MemoryAll | Permission::Execute | Permission::Derive,
        CapabilityLimits::sandbox());  // 16MB limit

    // Try to derive with limits exceeding parent
    CapabilityLimits too_large{
        .max_memory = 256ULL * 1024 * 1024,  // 256MB > 16MB
        .max_instructions = 1'000'000,
        .max_stack_depth = 256
    };

    auto result = mgr.derive(
        parent,
        "child",
        Permission::MemoryRead,
        too_large);

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), CapabilityError::LimitsNotWithin);
}

TEST(CapabilityManagerTest, Revoke) {
    CapabilityManager mgr;

    auto handle = mgr.create_root(
        "test",
        Permission::MemoryRead,
        CapabilityLimits::sandbox());

    EXPECT_TRUE(mgr.is_valid(handle));

    auto err = mgr.revoke(handle);
    EXPECT_EQ(err, CapabilityError::Success);
    EXPECT_FALSE(mgr.is_valid(handle));
}

TEST(CapabilityManagerTest, RevokeAlreadyRevoked) {
    CapabilityManager mgr;

    auto handle = mgr.create_root(
        "test",
        Permission::MemoryRead,
        CapabilityLimits::sandbox());

    EXPECT_EQ(mgr.revoke(handle), CapabilityError::Success);
    EXPECT_EQ(mgr.revoke(handle), CapabilityError::GenerationMismatch);
}

TEST(CapabilityManagerTest, RevokeCascades) {
    CapabilityManager mgr;

    auto root = mgr.create_root(
        "root",
        Permission::MemoryAll | Permission::Derive,
        CapabilityLimits::trusted());

    auto child = mgr.derive(
        root,
        "child",
        Permission::MemoryRead | Permission::Derive,
        CapabilityLimits::sandbox());
    ASSERT_TRUE(child.has_value());

    auto grandchild = mgr.derive(
        *child,
        "grandchild",
        Permission::MemoryRead,
        CapabilityLimits::untrusted());
    ASSERT_TRUE(grandchild.has_value());

    // All should be valid
    EXPECT_TRUE(mgr.is_valid(root));
    EXPECT_TRUE(mgr.is_valid(*child));
    EXPECT_TRUE(mgr.is_valid(*grandchild));

    // Revoke root - should cascade
    EXPECT_EQ(mgr.revoke(root), CapabilityError::Success);

    EXPECT_FALSE(mgr.is_valid(root));
    EXPECT_FALSE(mgr.is_valid(*child));
    EXPECT_FALSE(mgr.is_valid(*grandchild));
}

TEST(CapabilityManagerTest, RevokeChild) {
    CapabilityManager mgr;

    auto root = mgr.create_root(
        "root",
        Permission::MemoryAll | Permission::Derive,
        CapabilityLimits::trusted());

    auto child = mgr.derive(
        root,
        "child",
        Permission::MemoryRead,
        CapabilityLimits::sandbox());
    ASSERT_TRUE(child.has_value());

    // Revoke only child
    EXPECT_EQ(mgr.revoke(*child), CapabilityError::Success);

    EXPECT_TRUE(mgr.is_valid(root));   // Parent still valid
    EXPECT_FALSE(mgr.is_valid(*child));
}

TEST(CapabilityManagerTest, InvalidHandle) {
    CapabilityManager mgr;

    CapabilityHandle invalid{.id = 999, .generation = 1};
    EXPECT_FALSE(mgr.is_valid(invalid));
    EXPECT_EQ(mgr.get(invalid), nullptr);
}

TEST(CapabilityManagerTest, GenerationMismatch) {
    CapabilityManager mgr;

    auto handle = mgr.create_root(
        "test",
        Permission::MemoryRead,
        CapabilityLimits::sandbox());

    // Modify generation to simulate stale handle
    CapabilityHandle stale{.id = handle.id, .generation = handle.generation + 1};
    EXPECT_FALSE(mgr.is_valid(stale));
}

TEST(CapabilityManagerTest, CheckPermission) {
    CapabilityManager mgr;

    auto handle = mgr.create_root(
        "test",
        Permission::MemoryRead | Permission::Execute,
        CapabilityLimits::sandbox());

    EXPECT_TRUE(mgr.check_permission(handle, Permission::MemoryRead));
    EXPECT_TRUE(mgr.check_permission(handle, Permission::Execute));
    EXPECT_FALSE(mgr.check_permission(handle, Permission::MemoryWrite));
    EXPECT_FALSE(mgr.check_permission(handle, Permission::Network));
}

TEST(CapabilityManagerTest, CheckLimits) {
    CapabilityManager mgr;

    auto handle = mgr.create_root(
        "test",
        Permission::MemoryAll,
        CapabilityLimits::sandbox());  // 16MB, 1M instructions

    // Within limits
    EXPECT_TRUE(mgr.check_limits(handle, 1024 * 1024, 500'000));  // 1MB, 500K

    // Memory over limit
    EXPECT_FALSE(mgr.check_limits(handle, 32ULL * 1024 * 1024, 500'000));  // 32MB

    // Instructions over limit
    EXPECT_FALSE(mgr.check_limits(handle, 1024 * 1024, 2'000'000));  // 2M instructions
}

// ============================================================================
// Preset Capability Tests
// ============================================================================

TEST(CapabilityManagerTest, CreateUntrusted) {
    CapabilityManager mgr;

    auto handle = mgr.create_untrusted();
    ASSERT_TRUE(mgr.is_valid(handle));

    const Capability* cap = mgr.get(handle);
    ASSERT_NE(cap, nullptr);
    EXPECT_EQ(cap->name, "untrusted");
    EXPECT_TRUE(cap->has(Permission::Execute));
    EXPECT_TRUE(cap->has(Permission::MemoryRead));
    EXPECT_TRUE(cap->has(Permission::MemoryWrite));
    EXPECT_FALSE(cap->has(Permission::Derive));
    EXPECT_FALSE(cap->has(Permission::Network));

    // Check limits match untrusted preset
    EXPECT_EQ(cap->limits.max_memory, 1ULL * 1024 * 1024);
    EXPECT_EQ(cap->limits.max_instructions, 100'000ULL);
    EXPECT_EQ(cap->limits.max_stack_depth, 64U);
}

TEST(CapabilityManagerTest, CreateSandbox) {
    CapabilityManager mgr;

    auto handle = mgr.create_sandbox();
    ASSERT_TRUE(mgr.is_valid(handle));

    const Capability* cap = mgr.get(handle);
    ASSERT_NE(cap, nullptr);
    EXPECT_EQ(cap->name, "sandbox");
    EXPECT_TRUE(cap->has(Permission::Execute));
    EXPECT_TRUE(cap->has(Permission::Call));
    EXPECT_TRUE(cap->has(Permission::MemoryAll));
    EXPECT_TRUE(cap->has(Permission::Derive));
    EXPECT_FALSE(cap->has(Permission::Network));

    // Check limits match sandbox preset
    EXPECT_EQ(cap->limits.max_memory, 16ULL * 1024 * 1024);
    EXPECT_EQ(cap->limits.max_instructions, 1'000'000ULL);
    EXPECT_EQ(cap->limits.max_stack_depth, 256U);
}

TEST(CapabilityManagerTest, CreateTrusted) {
    CapabilityManager mgr;

    auto handle = mgr.create_trusted();
    ASSERT_TRUE(mgr.is_valid(handle));

    const Capability* cap = mgr.get(handle);
    ASSERT_NE(cap, nullptr);
    EXPECT_EQ(cap->name, "trusted");

    // Should have all permissions except BypassCfi
    EXPECT_TRUE(cap->has(Permission::Execute));
    EXPECT_TRUE(cap->has(Permission::MemoryAll));
    EXPECT_TRUE(cap->has(Permission::Network));
    EXPECT_TRUE(cap->has(Permission::Crypto));
    EXPECT_TRUE(cap->has(Permission::Derive));
    EXPECT_FALSE(cap->has(Permission::BypassCfi));

    // Check limits match trusted preset
    EXPECT_EQ(cap->limits.max_memory, 256ULL * 1024 * 1024);
    EXPECT_EQ(cap->limits.max_instructions, 100'000'000ULL);
    EXPECT_EQ(cap->limits.max_stack_depth, 4096U);
}

// ============================================================================
// Statistics Tests
// ============================================================================

TEST(CapabilityManagerTest, ActiveCount) {
    CapabilityManager mgr;

    EXPECT_EQ(mgr.active_count(), 0U);

    auto h1 = mgr.create_root("cap1", Permission::Execute, CapabilityLimits::sandbox());
    EXPECT_EQ(mgr.active_count(), 1U);

    auto h2 = mgr.create_root("cap2", Permission::Execute, CapabilityLimits::sandbox());
    EXPECT_EQ(mgr.active_count(), 2U);

    (void)mgr.revoke(h1);
    EXPECT_EQ(mgr.active_count(), 1U);

    (void)mgr.revoke(h2);
    EXPECT_EQ(mgr.active_count(), 0U);
}

TEST(CapabilityManagerTest, TotalCreatedAndRevoked) {
    CapabilityManager mgr;

    EXPECT_EQ(mgr.total_created(), 0ULL);
    EXPECT_EQ(mgr.total_revoked(), 0ULL);

    auto h1 = mgr.create_root("cap1", Permission::Execute, CapabilityLimits::sandbox());
    [[maybe_unused]] auto h2 = mgr.create_root("cap2", Permission::Execute, CapabilityLimits::sandbox());
    EXPECT_EQ(mgr.total_created(), 2ULL);

    (void)mgr.revoke(h1);
    EXPECT_EQ(mgr.total_revoked(), 1ULL);
}

TEST(CapabilityManagerTest, GetChildren) {
    CapabilityManager mgr;

    auto root = mgr.create_root(
        "root",
        Permission::MemoryAll | Permission::Derive,
        CapabilityLimits::trusted());

    auto child1 = mgr.derive(root, "child1", Permission::MemoryRead, CapabilityLimits::sandbox());
    auto child2 = mgr.derive(root, "child2", Permission::MemoryRead, CapabilityLimits::sandbox());
    ASSERT_TRUE(child1.has_value());
    ASSERT_TRUE(child2.has_value());

    auto children = mgr.get_children(root);
    EXPECT_EQ(children.size(), 2U);
}

// ============================================================================
// SecurityStats Integration Tests
// ============================================================================

TEST(CapabilityManagerTest, SecurityStatsCreation) {
    SecurityStats stats;
    CapabilityManager mgr(&stats);

    [[maybe_unused]] auto handle = mgr.create_root("test", Permission::Execute, CapabilityLimits::sandbox());

    auto snapshot = stats.snapshot();
    EXPECT_EQ(snapshot.capability_creations, 1U);
}

TEST(CapabilityManagerTest, SecurityStatsDerivation) {
    SecurityStats stats;
    CapabilityManager mgr(&stats);

    auto parent = mgr.create_root(
        "parent",
        Permission::MemoryAll | Permission::Derive,
        CapabilityLimits::trusted());

    auto child = mgr.derive(parent, "child", Permission::MemoryRead, CapabilityLimits::sandbox());
    ASSERT_TRUE(child.has_value());

    auto snapshot = stats.snapshot();
    EXPECT_EQ(snapshot.capability_creations, 1U);
    EXPECT_EQ(snapshot.capability_derivations, 1U);
}

TEST(CapabilityManagerTest, SecurityStatsRevocation) {
    SecurityStats stats;
    CapabilityManager mgr(&stats);

    auto handle = mgr.create_root("test", Permission::Execute, CapabilityLimits::sandbox());
    (void)mgr.revoke(handle);

    auto snapshot = stats.snapshot();
    EXPECT_EQ(snapshot.capability_revocations, 1U);
}

TEST(CapabilityManagerTest, SecurityStatsPermissionViolation) {
    SecurityStats stats;
    CapabilityManager mgr(&stats);

    // Parent without Derive permission
    auto parent = mgr.create_root(
        "parent",
        Permission::MemoryRead,  // No Derive
        CapabilityLimits::trusted());

    auto result = mgr.derive(parent, "child", Permission::MemoryRead, CapabilityLimits::sandbox());
    EXPECT_FALSE(result.has_value());

    auto snapshot = stats.snapshot();
    EXPECT_EQ(snapshot.permission_violations, 1U);
}

TEST(CapabilityManagerTest, SecurityStatsLimitViolation) {
    SecurityStats stats;
    CapabilityManager mgr(&stats);

    auto parent = mgr.create_root(
        "parent",
        Permission::MemoryAll | Permission::Derive,
        CapabilityLimits::sandbox());

    CapabilityLimits too_large{
        .max_memory = 1024ULL * 1024 * 1024,  // 1GB > 16MB
        .max_instructions = 1'000'000,
        .max_stack_depth = 256
    };

    auto result = mgr.derive(parent, "child", Permission::MemoryRead, too_large);
    EXPECT_FALSE(result.has_value());

    auto snapshot = stats.snapshot();
    EXPECT_EQ(snapshot.limit_violations, 1U);
}

// ============================================================================
// Expiration Tests
// ============================================================================

TEST(CapabilityManagerTest, ExpirationValid) {
    CapabilityManager mgr;

    auto future = std::chrono::system_clock::now() + std::chrono::hours(1);
    auto handle = mgr.create_root(
        "test",
        Permission::Execute,
        CapabilityLimits::sandbox(),
        future);

    EXPECT_TRUE(mgr.is_valid(handle));
}

TEST(CapabilityManagerTest, ExpirationExpired) {
    CapabilityManager mgr;

    auto past = std::chrono::system_clock::now() - std::chrono::hours(1);
    auto handle = mgr.create_root(
        "test",
        Permission::Execute,
        CapabilityLimits::sandbox(),
        past);

    EXPECT_FALSE(mgr.is_valid(handle));
}

// ============================================================================
// Thread Safety Tests
// ============================================================================

TEST(CapabilityManagerTest, ConcurrentCreate) {
    CapabilityManager mgr;
    constexpr int NUM_THREADS = 8;
    constexpr int CAPS_PER_THREAD = 100;

    std::vector<std::future<std::vector<CapabilityHandle>>> futures;

    for (int t = 0; t < NUM_THREADS; ++t) {
        futures.push_back(std::async(std::launch::async, [&mgr, t]() {
            std::vector<CapabilityHandle> handles;
            handles.reserve(CAPS_PER_THREAD);
            for (int i = 0; i < CAPS_PER_THREAD; ++i) {
                handles.push_back(mgr.create_root(
                    "thread" + std::to_string(t) + "_cap" + std::to_string(i),
                    Permission::Execute,
                    CapabilityLimits::sandbox()));
            }
            return handles;
        }));
    }

    std::vector<CapabilityHandle> all_handles;
    for (auto& f : futures) {
        auto handles = f.get();
        all_handles.insert(all_handles.end(), handles.begin(), handles.end());
    }

    // All handles should be unique and valid
    EXPECT_EQ(all_handles.size(), static_cast<size_t>(NUM_THREADS * CAPS_PER_THREAD));
    EXPECT_EQ(mgr.total_created(), static_cast<uint64_t>(NUM_THREADS * CAPS_PER_THREAD));

    for (const auto& h : all_handles) {
        EXPECT_TRUE(mgr.is_valid(h));
    }
}

TEST(CapabilityManagerTest, ConcurrentCreateAndRevoke) {
    CapabilityManager mgr;
    std::atomic<int> created{0};
    std::atomic<int> revoked{0};

    constexpr int NUM_ITERATIONS = 100;

    auto creator = std::async(std::launch::async, [&]() {
        for (int i = 0; i < NUM_ITERATIONS; ++i) {
            [[maybe_unused]] auto handle = mgr.create_root(
                "cap" + std::to_string(i),
                Permission::Execute | Permission::Derive,
                CapabilityLimits::sandbox());
            created.fetch_add(1, std::memory_order_relaxed);
        }
    });

    auto revoker = std::async(std::launch::async, [&]() {
        for (int i = 0; i < NUM_ITERATIONS; ++i) {
            CapabilityHandle handle{.id = static_cast<uint64_t>(i + 1), .generation = 1};
            auto err = mgr.revoke(handle);
            if (err == CapabilityError::Success) {
                revoked.fetch_add(1, std::memory_order_relaxed);
            }
            std::this_thread::yield();
        }
    });

    creator.get();
    revoker.get();

    EXPECT_EQ(created.load(), NUM_ITERATIONS);
    EXPECT_LE(revoked.load(), NUM_ITERATIONS);
}

// ============================================================================
// Error Code Tests
// ============================================================================

TEST(CapabilityErrorTest, ToString) {
    EXPECT_EQ(to_string(CapabilityError::Success), "Success");
    EXPECT_EQ(to_string(CapabilityError::InvalidHandle), "InvalidHandle");
    EXPECT_EQ(to_string(CapabilityError::Expired), "Expired");
    EXPECT_EQ(to_string(CapabilityError::Revoked), "Revoked");
    EXPECT_EQ(to_string(CapabilityError::InsufficientPermission), "InsufficientPermission");
    EXPECT_EQ(to_string(CapabilityError::PermissionNotSubset), "PermissionNotSubset");
    EXPECT_EQ(to_string(CapabilityError::LimitsNotWithin), "LimitsNotWithin");
    EXPECT_EQ(to_string(CapabilityError::DerivationNotAllowed), "DerivationNotAllowed");
    EXPECT_EQ(to_string(CapabilityError::AlreadyRevoked), "AlreadyRevoked");
    EXPECT_EQ(to_string(CapabilityError::InvalidParent), "InvalidParent");
    EXPECT_EQ(to_string(CapabilityError::GenerationMismatch), "GenerationMismatch");
}
