/// @file isolation_manager_test.cpp
/// @brief Unit tests for SEC-007 Isolation Boundaries

#include "dotvm/core/security/isolation_manager.hpp"

#include <gtest/gtest.h>

namespace dotvm::core::security {
namespace {

// ============================================================================
// IsolationLevel Tests
// ============================================================================

TEST(IsolationLevelTest, EnumValues) {
    EXPECT_EQ(static_cast<std::uint8_t>(IsolationLevel::None), 0);
    EXPECT_EQ(static_cast<std::uint8_t>(IsolationLevel::Basic), 1);
    EXPECT_EQ(static_cast<std::uint8_t>(IsolationLevel::Strict), 2);
}

TEST(IsolationLevelTest, RequiresMemoryIsolation) {
    EXPECT_FALSE(requires_memory_isolation(IsolationLevel::None));
    EXPECT_TRUE(requires_memory_isolation(IsolationLevel::Basic));
    EXPECT_TRUE(requires_memory_isolation(IsolationLevel::Strict));
}

TEST(IsolationLevelTest, RequiresSyscallWhitelist) {
    EXPECT_FALSE(requires_syscall_whitelist(IsolationLevel::None));
    EXPECT_FALSE(requires_syscall_whitelist(IsolationLevel::Basic));
    EXPECT_TRUE(requires_syscall_whitelist(IsolationLevel::Strict));
}

TEST(IsolationLevelTest, RestrictsNetwork) {
    EXPECT_FALSE(restricts_network(IsolationLevel::None));
    EXPECT_FALSE(restricts_network(IsolationLevel::Basic));
    EXPECT_TRUE(restricts_network(IsolationLevel::Strict));
}

TEST(IsolationLevelTest, RestrictsFilesystem) {
    EXPECT_FALSE(restricts_filesystem(IsolationLevel::None));
    EXPECT_FALSE(restricts_filesystem(IsolationLevel::Basic));
    EXPECT_TRUE(restricts_filesystem(IsolationLevel::Strict));
}

TEST(IsolationLevelTest, RequiresExplicitGrants) {
    EXPECT_FALSE(requires_explicit_grants(IsolationLevel::None));
    EXPECT_TRUE(requires_explicit_grants(IsolationLevel::Basic));
    EXPECT_TRUE(requires_explicit_grants(IsolationLevel::Strict));
}

TEST(IsolationLevelTest, ToString) {
    EXPECT_STREQ(to_string(IsolationLevel::None), "None");
    EXPECT_STREQ(to_string(IsolationLevel::Basic), "Basic");
    EXPECT_STREQ(to_string(IsolationLevel::Strict), "Strict");
}

// ============================================================================
// SyscallWhitelist Tests
// ============================================================================

TEST(SyscallWhitelistTest, DefaultConstructorBlocksAll) {
    SyscallWhitelist whitelist;
    EXPECT_TRUE(whitelist.none());
    EXPECT_EQ(whitelist.count(), 0);
    EXPECT_FALSE(whitelist.is_allowed(SyscallId::MemAllocate));
}

TEST(SyscallWhitelistTest, AllowAndCheck) {
    SyscallWhitelist whitelist;
    whitelist.allow(SyscallId::MemAllocate);

    EXPECT_TRUE(whitelist.is_allowed(SyscallId::MemAllocate));
    EXPECT_FALSE(whitelist.is_allowed(SyscallId::NetSocket));
    EXPECT_EQ(whitelist.count(), 1);
}

TEST(SyscallWhitelistTest, BlockSyscall) {
    SyscallWhitelist whitelist;
    whitelist.allow(SyscallId::MemAllocate);
    whitelist.allow(SyscallId::MemDeallocate);
    EXPECT_EQ(whitelist.count(), 2);

    whitelist.block(SyscallId::MemAllocate);
    EXPECT_FALSE(whitelist.is_allowed(SyscallId::MemAllocate));
    EXPECT_TRUE(whitelist.is_allowed(SyscallId::MemDeallocate));
    EXPECT_EQ(whitelist.count(), 1);
}

TEST(SyscallWhitelistTest, ChainedOperations) {
    SyscallWhitelist whitelist;
    whitelist.allow(SyscallId::MemAllocate)
        .allow(SyscallId::MemDeallocate)
        .allow(SyscallId::IoRead);

    EXPECT_EQ(whitelist.count(), 3);
    EXPECT_TRUE(whitelist.is_allowed(SyscallId::MemAllocate));
    EXPECT_TRUE(whitelist.is_allowed(SyscallId::MemDeallocate));
    EXPECT_TRUE(whitelist.is_allowed(SyscallId::IoRead));
}

TEST(SyscallWhitelistTest, Clear) {
    SyscallWhitelist whitelist;
    whitelist.allow(SyscallId::MemAllocate);
    EXPECT_TRUE(whitelist.any());

    whitelist.clear();
    EXPECT_TRUE(whitelist.none());
}

TEST(SyscallWhitelistTest, StrictDefault) {
    auto whitelist = SyscallWhitelist::strict_default();

    // Memory operations allowed
    EXPECT_TRUE(whitelist.is_allowed(SyscallId::MemAllocate));
    EXPECT_TRUE(whitelist.is_allowed(SyscallId::MemDeallocate));
    EXPECT_TRUE(whitelist.is_allowed(SyscallId::MemQuery));

    // I/O operations allowed
    EXPECT_TRUE(whitelist.is_allowed(SyscallId::IoRead));
    EXPECT_TRUE(whitelist.is_allowed(SyscallId::IoWrite));

    // Safe Dot operations allowed
    EXPECT_TRUE(whitelist.is_allowed(SyscallId::DotYield));
    EXPECT_TRUE(whitelist.is_allowed(SyscallId::DotGetId));

    // Network operations blocked
    EXPECT_FALSE(whitelist.is_allowed(SyscallId::NetSocket));
    EXPECT_FALSE(whitelist.is_allowed(SyscallId::NetConnect));

    // Filesystem operations blocked
    EXPECT_FALSE(whitelist.is_allowed(SyscallId::FsOpen));
    EXPECT_FALSE(whitelist.is_allowed(SyscallId::FsRead));

    // Crypto operations allowed
    EXPECT_TRUE(whitelist.is_allowed(SyscallId::CryptoRandom));
    EXPECT_TRUE(whitelist.is_allowed(SyscallId::CryptoHash));
}

TEST(SyscallWhitelistTest, AllowAll) {
    auto whitelist = SyscallWhitelist::allow_all();

    EXPECT_TRUE(whitelist.is_allowed(SyscallId::MemAllocate));
    EXPECT_TRUE(whitelist.is_allowed(SyscallId::NetSocket));
    EXPECT_TRUE(whitelist.is_allowed(SyscallId::FsOpen));
    EXPECT_EQ(whitelist.count(), 256);
}

TEST(SyscallWhitelistTest, Equality) {
    auto whitelist1 = SyscallWhitelist::strict_default();
    auto whitelist2 = SyscallWhitelist::strict_default();
    auto whitelist3 = SyscallWhitelist::allow_all();

    EXPECT_EQ(whitelist1, whitelist2);
    EXPECT_NE(whitelist1, whitelist3);
}

// ============================================================================
// AccessType Tests
// ============================================================================

TEST(AccessTypeTest, EnumValues) {
    EXPECT_EQ(static_cast<std::uint8_t>(AccessType::Read), 0);
    EXPECT_EQ(static_cast<std::uint8_t>(AccessType::Write), 1);
}

TEST(AccessTypeTest, ToString) {
    EXPECT_STREQ(to_string(AccessType::Read), "Read");
    EXPECT_STREQ(to_string(AccessType::Write), "Write");
}

// ============================================================================
// IsolationError Tests
// ============================================================================

TEST(IsolationErrorTest, ToStringAllValues) {
    EXPECT_STREQ(to_string(IsolationError::Success), "Success");
    EXPECT_STREQ(to_string(IsolationError::DotNotFound), "DotNotFound");
    EXPECT_STREQ(to_string(IsolationError::DotAlreadyExists), "DotAlreadyExists");
    EXPECT_STREQ(to_string(IsolationError::ParentNotFound), "ParentNotFound");
    EXPECT_STREQ(to_string(IsolationError::AccessDenied), "AccessDenied");
    EXPECT_STREQ(to_string(IsolationError::GrantNotFound), "GrantNotFound");
    EXPECT_STREQ(to_string(IsolationError::GrantRevoked), "GrantRevoked");
    EXPECT_STREQ(to_string(IsolationError::SyscallDenied), "SyscallDenied");
    EXPECT_STREQ(to_string(IsolationError::HandleNotOwned), "HandleNotOwned");
    EXPECT_STREQ(to_string(IsolationError::InvalidRelationship), "InvalidRelationship");
    EXPECT_STREQ(to_string(IsolationError::HasActiveChildren), "HasActiveChildren");
    EXPECT_STREQ(to_string(IsolationError::NetworkDenied), "NetworkDenied");
    EXPECT_STREQ(to_string(IsolationError::FilesystemDenied), "FilesystemDenied");
    EXPECT_STREQ(to_string(IsolationError::InternalError), "InternalError");
}

// ============================================================================
// HandleGrant Tests
// ============================================================================

TEST(HandleGrantTest, AllowsReadAccess) {
    HandleGrant grant{
        .source_handle = Handle{.index = 1, .generation = 1},
        .granted_handle = Handle{.index = 2, .generation = 1},
        .parent_dot = 1,
        .child_dot = 2,
        .can_read = true,
        .can_write = false,
        .revoked = false,
    };

    EXPECT_TRUE(grant.allows(AccessType::Read));
    EXPECT_FALSE(grant.allows(AccessType::Write));
}

TEST(HandleGrantTest, AllowsWriteAccess) {
    HandleGrant grant{
        .source_handle = Handle{.index = 1, .generation = 1},
        .granted_handle = Handle{.index = 2, .generation = 1},
        .parent_dot = 1,
        .child_dot = 2,
        .can_read = false,
        .can_write = true,
        .revoked = false,
    };

    EXPECT_FALSE(grant.allows(AccessType::Read));
    EXPECT_TRUE(grant.allows(AccessType::Write));
}

TEST(HandleGrantTest, RevokedBlocksAccess) {
    HandleGrant grant{
        .source_handle = Handle{.index = 1, .generation = 1},
        .granted_handle = Handle{.index = 2, .generation = 1},
        .parent_dot = 1,
        .child_dot = 2,
        .can_read = true,
        .can_write = true,
        .revoked = true,
    };

    EXPECT_FALSE(grant.allows(AccessType::Read));
    EXPECT_FALSE(grant.allows(AccessType::Write));
}

// ============================================================================
// IsolationManager Sandbox Lifecycle Tests
// ============================================================================

class IsolationManagerTest : public ::testing::Test {
protected:
    IsolationManager manager_;
};

TEST_F(IsolationManagerTest, CreateSandboxNoneIsolation) {
    auto result = manager_.create_sandbox(1, IsolationLevel::None);
    EXPECT_TRUE(result.has_value());
    EXPECT_TRUE(manager_.has_sandbox(1));
    EXPECT_EQ(manager_.sandbox_count(), 1);

    // No HandleTable for None isolation
    EXPECT_EQ(manager_.get_handle_table(1), nullptr);
}

TEST_F(IsolationManagerTest, CreateSandboxBasicIsolation) {
    auto result = manager_.create_sandbox(1, IsolationLevel::Basic);
    EXPECT_TRUE(result.has_value());

    // Should have HandleTable for Basic isolation
    EXPECT_NE(manager_.get_handle_table(1), nullptr);
}

TEST_F(IsolationManagerTest, CreateSandboxStrictIsolation) {
    auto result = manager_.create_sandbox(1, IsolationLevel::Strict);
    EXPECT_TRUE(result.has_value());

    // Should have HandleTable
    EXPECT_NE(manager_.get_handle_table(1), nullptr);

    // Strict mode should have strict syscall whitelist
    auto* whitelist = manager_.get_syscall_whitelist(1);
    EXPECT_NE(whitelist, nullptr);
    EXPECT_FALSE(whitelist->is_allowed(SyscallId::NetSocket));
}

TEST_F(IsolationManagerTest, CreateSandboxDuplicateId) {
    EXPECT_TRUE(manager_.create_sandbox(1, IsolationLevel::Basic).has_value());
    auto result = manager_.create_sandbox(1, IsolationLevel::Basic);
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), IsolationError::DotAlreadyExists);
}

TEST_F(IsolationManagerTest, CreateSandboxWithParent) {
    EXPECT_TRUE(manager_.create_sandbox(1, IsolationLevel::Basic).has_value());
    auto result = manager_.create_sandbox(2, IsolationLevel::Basic, 1);
    EXPECT_TRUE(result.has_value());

    auto parent = manager_.get_parent(2);
    EXPECT_TRUE(parent.has_value());
    EXPECT_EQ(parent.value(), 1);

    auto children = manager_.get_children(1);
    EXPECT_TRUE(children.has_value());
    EXPECT_EQ(children.value().size(), 1);
    EXPECT_EQ(children.value()[0], 2);
}

TEST_F(IsolationManagerTest, CreateSandboxWithInvalidParent) {
    auto result = manager_.create_sandbox(2, IsolationLevel::Basic, 999);
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), IsolationError::ParentNotFound);
}

TEST_F(IsolationManagerTest, DestroySandbox) {
    EXPECT_TRUE(manager_.create_sandbox(1, IsolationLevel::Basic).has_value());
    EXPECT_EQ(manager_.sandbox_count(), 1);

    auto error = manager_.destroy_sandbox(1);
    EXPECT_EQ(error, IsolationError::Success);
    EXPECT_EQ(manager_.sandbox_count(), 0);
    EXPECT_FALSE(manager_.has_sandbox(1));
}

TEST_F(IsolationManagerTest, DestroyNonexistentSandbox) {
    auto error = manager_.destroy_sandbox(999);
    EXPECT_EQ(error, IsolationError::DotNotFound);
}

TEST_F(IsolationManagerTest, DestroyWithActiveChildren) {
    EXPECT_TRUE(manager_.create_sandbox(1, IsolationLevel::Basic).has_value());
    EXPECT_TRUE(manager_.create_sandbox(2, IsolationLevel::Basic, 1).has_value());

    auto error = manager_.destroy_sandbox(1);
    EXPECT_EQ(error, IsolationError::HasActiveChildren);
}

TEST_F(IsolationManagerTest, DestroyChildUpdatesParent) {
    EXPECT_TRUE(manager_.create_sandbox(1, IsolationLevel::Basic).has_value());
    EXPECT_TRUE(manager_.create_sandbox(2, IsolationLevel::Basic, 1).has_value());

    auto children_before = manager_.get_children(1);
    EXPECT_EQ(children_before.value().size(), 1);

    EXPECT_EQ(manager_.destroy_sandbox(2), IsolationError::Success);

    auto children_after = manager_.get_children(1);
    EXPECT_EQ(children_after.value().size(), 0);
}

// ============================================================================
// IsolationManager Boundary Enforcement Tests
// ============================================================================

TEST_F(IsolationManagerTest, EnforceBoundarySameDot) {
    EXPECT_TRUE(manager_.create_sandbox(1, IsolationLevel::Basic).has_value());
    Handle h{.index = 1, .generation = 1};

    auto error = manager_.enforce_boundary(1, 1, h, AccessType::Read);
    EXPECT_EQ(error, IsolationError::Success);
}

TEST_F(IsolationManagerTest, EnforceBoundaryNoneIsolation) {
    EXPECT_TRUE(manager_.create_sandbox(1, IsolationLevel::None).has_value());
    EXPECT_TRUE(manager_.create_sandbox(2, IsolationLevel::None).has_value());
    Handle h{.index = 1, .generation = 1};

    // With None isolation, cross-Dot access is allowed
    auto error = manager_.enforce_boundary(1, 2, h, AccessType::Read);
    EXPECT_EQ(error, IsolationError::Success);
}

TEST_F(IsolationManagerTest, EnforceBoundaryCrossDotDenied) {
    EXPECT_TRUE(manager_.create_sandbox(1, IsolationLevel::Basic).has_value());
    EXPECT_TRUE(manager_.create_sandbox(2, IsolationLevel::Basic).has_value());
    Handle h{.index = 1, .generation = 1};

    // Without grant, cross-Dot access is denied
    auto error = manager_.enforce_boundary(1, 2, h, AccessType::Read);
    EXPECT_EQ(error, IsolationError::AccessDenied);
}

TEST_F(IsolationManagerTest, EnforceBoundaryUnknownDot) {
    auto error =
        manager_.enforce_boundary(999, 1, Handle{.index = 1, .generation = 1}, AccessType::Read);
    EXPECT_EQ(error, IsolationError::DotNotFound);
}

// ============================================================================
// IsolationManager Syscall Validation Tests
// ============================================================================

TEST_F(IsolationManagerTest, ValidateSyscallNoneIsolation) {
    EXPECT_TRUE(manager_.create_sandbox(1, IsolationLevel::None).has_value());

    // All syscalls allowed in None mode
    EXPECT_EQ(manager_.validate_syscall(1, SyscallId::NetSocket), IsolationError::Success);
    EXPECT_EQ(manager_.validate_syscall(1, SyscallId::FsOpen), IsolationError::Success);
}

TEST_F(IsolationManagerTest, ValidateSyscallBasicIsolation) {
    EXPECT_TRUE(manager_.create_sandbox(1, IsolationLevel::Basic).has_value());

    // All syscalls allowed in Basic mode (no whitelist enforcement)
    EXPECT_EQ(manager_.validate_syscall(1, SyscallId::NetSocket), IsolationError::Success);
}

TEST_F(IsolationManagerTest, ValidateSyscallStrictIsolation) {
    EXPECT_TRUE(manager_.create_sandbox(1, IsolationLevel::Strict).has_value());

    // Safe syscalls allowed
    EXPECT_EQ(manager_.validate_syscall(1, SyscallId::MemAllocate), IsolationError::Success);
    EXPECT_EQ(manager_.validate_syscall(1, SyscallId::IoRead), IsolationError::Success);

    // Network/Filesystem syscalls denied
    EXPECT_EQ(manager_.validate_syscall(1, SyscallId::NetSocket), IsolationError::SyscallDenied);
    EXPECT_EQ(manager_.validate_syscall(1, SyscallId::FsOpen), IsolationError::SyscallDenied);
}

TEST_F(IsolationManagerTest, ValidateSyscallUnknownDot) {
    EXPECT_EQ(manager_.validate_syscall(999, SyscallId::MemAllocate), IsolationError::DotNotFound);
}

// ============================================================================
// IsolationManager Handle Grant Tests
// ============================================================================

class IsolationManagerGrantTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create parent-child hierarchy with Basic isolation
        EXPECT_TRUE(manager_.create_sandbox(1, IsolationLevel::Basic).has_value());
        EXPECT_TRUE(manager_.create_sandbox(2, IsolationLevel::Basic, 1).has_value());

        // Allocate a handle in parent's namespace
        auto* table = manager_.get_handle_table(1);
        ASSERT_NE(table, nullptr);
        auto slot = table->allocate_slot();
        parent_handle_ = Handle{.index = slot, .generation = (*table)[slot].generation};
    }

    IsolationManager manager_;
    Handle parent_handle_;
};

TEST_F(IsolationManagerGrantTest, GrantReadAccess) {
    auto result = manager_.grant_handle(1, 2, parent_handle_, true, false);
    EXPECT_TRUE(result.has_value());

    Handle granted = result.value();

    // Child can now read via the granted handle
    auto error = manager_.enforce_boundary(2, 1, granted, AccessType::Read);
    EXPECT_EQ(error, IsolationError::Success);

    // But not write
    error = manager_.enforce_boundary(2, 1, granted, AccessType::Write);
    EXPECT_EQ(error, IsolationError::AccessDenied);
}

TEST_F(IsolationManagerGrantTest, GrantWriteAccess) {
    auto result = manager_.grant_handle(1, 2, parent_handle_, false, true);
    EXPECT_TRUE(result.has_value());

    Handle granted = result.value();

    // Child can write
    auto error = manager_.enforce_boundary(2, 1, granted, AccessType::Write);
    EXPECT_EQ(error, IsolationError::Success);

    // But not read
    error = manager_.enforce_boundary(2, 1, granted, AccessType::Read);
    EXPECT_EQ(error, IsolationError::AccessDenied);
}

TEST_F(IsolationManagerGrantTest, GrantReadWriteAccess) {
    auto result = manager_.grant_handle(1, 2, parent_handle_, true, true);
    EXPECT_TRUE(result.has_value());

    Handle granted = result.value();

    EXPECT_EQ(manager_.enforce_boundary(2, 1, granted, AccessType::Read), IsolationError::Success);
    EXPECT_EQ(manager_.enforce_boundary(2, 1, granted, AccessType::Write), IsolationError::Success);
}

TEST_F(IsolationManagerGrantTest, GrantNoPermissions) {
    auto result = manager_.grant_handle(1, 2, parent_handle_, false, false);
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), IsolationError::AccessDenied);
}

TEST_F(IsolationManagerGrantTest, GrantToNonChild) {
    // Create unrelated Dot
    EXPECT_TRUE(manager_.create_sandbox(3, IsolationLevel::Basic).has_value());

    auto result = manager_.grant_handle(1, 3, parent_handle_, true, false);
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), IsolationError::InvalidRelationship);
}

TEST_F(IsolationManagerGrantTest, RevokeGrant) {
    auto result = manager_.grant_handle(1, 2, parent_handle_, true, true);
    EXPECT_TRUE(result.has_value());
    Handle granted = result.value();

    // Access works before revocation
    EXPECT_EQ(manager_.enforce_boundary(2, 1, granted, AccessType::Read), IsolationError::Success);

    // Revoke the grant
    auto error = manager_.revoke_handle(1, 2, granted);
    EXPECT_EQ(error, IsolationError::Success);

    // Access denied after revocation
    EXPECT_EQ(manager_.enforce_boundary(2, 1, granted, AccessType::Read),
              IsolationError::GrantRevoked);
}

TEST_F(IsolationManagerGrantTest, RevokeNonexistentGrant) {
    Handle fake{.index = 999, .generation = 1};
    auto error = manager_.revoke_handle(1, 2, fake);
    EXPECT_EQ(error, IsolationError::GrantNotFound);
}

// ============================================================================
// IsolationManager Capability Integration Tests
// ============================================================================

TEST_F(IsolationManagerTest, CanAccessNetworkNoneIsolation) {
    EXPECT_TRUE(manager_.create_sandbox(1, IsolationLevel::None).has_value());
    EXPECT_TRUE(manager_.can_access_network(1));
}

TEST_F(IsolationManagerTest, CanAccessNetworkBasicIsolation) {
    EXPECT_TRUE(manager_.create_sandbox(1, IsolationLevel::Basic).has_value());
    EXPECT_TRUE(manager_.can_access_network(1));
}

TEST_F(IsolationManagerTest, CanAccessNetworkStrictIsolation) {
    EXPECT_TRUE(manager_.create_sandbox(1, IsolationLevel::Strict).has_value());
    EXPECT_FALSE(manager_.can_access_network(1));
}

TEST_F(IsolationManagerTest, CanAccessFilesystemNoneIsolation) {
    EXPECT_TRUE(manager_.create_sandbox(1, IsolationLevel::None).has_value());
    EXPECT_TRUE(manager_.can_access_filesystem(1));
}

TEST_F(IsolationManagerTest, CanAccessFilesystemBasicIsolation) {
    EXPECT_TRUE(manager_.create_sandbox(1, IsolationLevel::Basic).has_value());
    EXPECT_TRUE(manager_.can_access_filesystem(1));
}

TEST_F(IsolationManagerTest, CanAccessFilesystemStrictIsolation) {
    EXPECT_TRUE(manager_.create_sandbox(1, IsolationLevel::Strict).has_value());
    EXPECT_FALSE(manager_.can_access_filesystem(1));
}

TEST_F(IsolationManagerTest, CanAccessUnknownDot) {
    EXPECT_FALSE(manager_.can_access_network(999));
    EXPECT_FALSE(manager_.can_access_filesystem(999));
}

// ============================================================================
// IsolationManager Query Method Tests
// ============================================================================

TEST_F(IsolationManagerTest, GetIsolationLevel) {
    EXPECT_TRUE(manager_.create_sandbox(1, IsolationLevel::Strict).has_value());

    auto level = manager_.get_isolation_level(1);
    EXPECT_TRUE(level.has_value());
    EXPECT_EQ(level.value(), IsolationLevel::Strict);
}

TEST_F(IsolationManagerTest, GetIsolationLevelUnknownDot) {
    auto level = manager_.get_isolation_level(999);
    EXPECT_FALSE(level.has_value());
    EXPECT_EQ(level.error(), IsolationError::DotNotFound);
}

TEST_F(IsolationManagerTest, GetSyscallWhitelistModifiable) {
    EXPECT_TRUE(manager_.create_sandbox(1, IsolationLevel::Strict).has_value());

    auto* whitelist = manager_.get_syscall_whitelist(1);
    ASSERT_NE(whitelist, nullptr);

    // Network should be blocked by default
    EXPECT_FALSE(whitelist->is_allowed(SyscallId::NetSocket));

    // Modify whitelist
    whitelist->allow(SyscallId::NetSocket);

    // Verify modification persists
    auto* whitelist2 = manager_.get_syscall_whitelist(1);
    EXPECT_TRUE(whitelist2->is_allowed(SyscallId::NetSocket));
}

}  // namespace
}  // namespace dotvm::core::security
