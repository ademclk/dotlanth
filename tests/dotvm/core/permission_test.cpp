/// @file permission_test.cpp
/// @brief Unit tests for SEC-002 Permission Model

#include <dotvm/core/security/permission.hpp>

#include <gtest/gtest.h>

using namespace dotvm::core::security;

// ============================================================================
// Permission Enum Bit Position Tests
// ============================================================================

TEST(PermissionEnumTest, BitPositionsAreCorrect) {
    EXPECT_EQ(static_cast<std::uint32_t>(Permission::None), 0U);
    EXPECT_EQ(static_cast<std::uint32_t>(Permission::Execute), 1U << 0);
    EXPECT_EQ(static_cast<std::uint32_t>(Permission::ReadMemory), 1U << 1);
    EXPECT_EQ(static_cast<std::uint32_t>(Permission::WriteMemory), 1U << 2);
    EXPECT_EQ(static_cast<std::uint32_t>(Permission::Allocate), 1U << 3);
    EXPECT_EQ(static_cast<std::uint32_t>(Permission::ReadState), 1U << 4);
    EXPECT_EQ(static_cast<std::uint32_t>(Permission::WriteState), 1U << 5);
    EXPECT_EQ(static_cast<std::uint32_t>(Permission::SpawnDot), 1U << 6);
    EXPECT_EQ(static_cast<std::uint32_t>(Permission::SendMessage), 1U << 7);
    EXPECT_EQ(static_cast<std::uint32_t>(Permission::Crypto), 1U << 8);
    EXPECT_EQ(static_cast<std::uint32_t>(Permission::SystemCall), 1U << 9);
    EXPECT_EQ(static_cast<std::uint32_t>(Permission::Debug), 1U << 10);
    EXPECT_EQ(static_cast<std::uint32_t>(Permission::Full), 0xFFFF'FFFFU);
}

TEST(PermissionEnumTest, BitwiseOr) {
    Permission combined = Permission::Execute | Permission::ReadMemory;
    EXPECT_EQ(static_cast<std::uint32_t>(combined), 0b11U);
}

TEST(PermissionEnumTest, BitwiseAnd) {
    Permission combined = Permission::Execute | Permission::ReadMemory;
    Permission result = combined & Permission::Execute;
    EXPECT_EQ(result, Permission::Execute);
}

TEST(PermissionEnumTest, BitwiseXor) {
    Permission a = Permission::Execute | Permission::ReadMemory;
    Permission b = Permission::ReadMemory | Permission::WriteMemory;
    Permission result = a ^ b;
    EXPECT_EQ(static_cast<std::uint32_t>(result),
              static_cast<std::uint32_t>(Permission::Execute) |
                  static_cast<std::uint32_t>(Permission::WriteMemory));
}

TEST(PermissionEnumTest, BitwiseNot) {
    Permission result = ~Permission::None;
    EXPECT_EQ(result, Permission::Full);
}

TEST(PermissionEnumTest, CompoundOrAssignment) {
    Permission perms = Permission::Execute;
    perms |= Permission::ReadMemory;
    EXPECT_TRUE(has_permission(perms, Permission::Execute));
    EXPECT_TRUE(has_permission(perms, Permission::ReadMemory));
}

TEST(PermissionEnumTest, CompoundAndAssignment) {
    Permission perms = Permission::Execute | Permission::ReadMemory;
    perms &= Permission::Execute;
    EXPECT_TRUE(has_permission(perms, Permission::Execute));
    EXPECT_FALSE(has_permission(perms, Permission::ReadMemory));
}

TEST(PermissionEnumTest, CompoundXorAssignment) {
    Permission perms = Permission::Execute | Permission::ReadMemory;
    perms ^= Permission::ReadMemory;
    EXPECT_TRUE(has_permission(perms, Permission::Execute));
    EXPECT_FALSE(has_permission(perms, Permission::ReadMemory));
}

// ============================================================================
// Combo Permission Tests
// ============================================================================

TEST(PermissionComboTest, ReadOnlyContainsCorrectPermissions) {
    EXPECT_TRUE(has_permission(kReadOnly, Permission::Execute));
    EXPECT_TRUE(has_permission(kReadOnly, Permission::ReadMemory));
    EXPECT_TRUE(has_permission(kReadOnly, Permission::ReadState));
    EXPECT_FALSE(has_permission(kReadOnly, Permission::WriteMemory));
    EXPECT_FALSE(has_permission(kReadOnly, Permission::WriteState));
}

TEST(PermissionComboTest, ReadWriteContainsCorrectPermissions) {
    EXPECT_TRUE(has_permission(kReadWrite, Permission::Execute));
    EXPECT_TRUE(has_permission(kReadWrite, Permission::ReadMemory));
    EXPECT_TRUE(has_permission(kReadWrite, Permission::ReadState));
    EXPECT_TRUE(has_permission(kReadWrite, Permission::WriteMemory));
    EXPECT_TRUE(has_permission(kReadWrite, Permission::WriteState));
    EXPECT_FALSE(has_permission(kReadWrite, Permission::SpawnDot));
}

TEST(PermissionComboTest, ReadWriteIsSupersetOfReadOnly) {
    EXPECT_TRUE(is_subset(kReadWrite, kReadOnly));
    EXPECT_FALSE(is_subset(kReadOnly, kReadWrite));
}

// ============================================================================
// Helper Function Tests
// ============================================================================

TEST(PermissionHelperTest, HasPermissionSingle) {
    Permission perms = Permission::Execute;
    EXPECT_TRUE(has_permission(perms, Permission::Execute));
    EXPECT_FALSE(has_permission(perms, Permission::ReadMemory));
}

TEST(PermissionHelperTest, HasPermissionMultiple) {
    Permission perms = Permission::Execute | Permission::ReadMemory;
    EXPECT_TRUE(has_permission(perms, Permission::Execute));
    EXPECT_TRUE(has_permission(perms, Permission::ReadMemory));
    EXPECT_TRUE(
        has_permission(perms, Permission::Execute | Permission::ReadMemory));
    EXPECT_FALSE(has_permission(
        perms, Permission::Execute | Permission::WriteMemory));
}

TEST(PermissionHelperTest, IsSubset) {
    Permission parent = Permission::Execute | Permission::ReadMemory |
                        Permission::WriteMemory;
    Permission child = Permission::Execute | Permission::ReadMemory;

    EXPECT_TRUE(is_subset(parent, child));
    EXPECT_FALSE(is_subset(child, parent));
    EXPECT_TRUE(is_subset(parent, parent));
}

TEST(PermissionHelperTest, ToStringNone) {
    EXPECT_EQ(to_string(Permission::None), "None");
}

TEST(PermissionHelperTest, ToStringFull) {
    EXPECT_EQ(to_string(Permission::Full), "Full");
}

TEST(PermissionHelperTest, ToStringSingle) {
    EXPECT_EQ(to_string(Permission::Execute), "Execute");
    EXPECT_EQ(to_string(Permission::ReadMemory), "ReadMemory");
    EXPECT_EQ(to_string(Permission::Crypto), "Crypto");
}

TEST(PermissionHelperTest, ToStringMultiple) {
    std::string s = to_string(Permission::Execute | Permission::ReadMemory);
    EXPECT_TRUE(s.find("Execute") != std::string::npos);
    EXPECT_TRUE(s.find("ReadMemory") != std::string::npos);
    EXPECT_TRUE(s.find(" | ") != std::string::npos);
}

// ============================================================================
// PermissionSet Basic Tests
// ============================================================================

TEST(PermissionSetTest, DefaultConstruction) {
    PermissionSet ps;
    EXPECT_TRUE(ps.empty());
    EXPECT_EQ(ps.value(), Permission::None);
    EXPECT_EQ(ps.bits(), 0U);
}

TEST(PermissionSetTest, ConstructionWithPermission) {
    PermissionSet ps{Permission::Execute | Permission::ReadMemory};
    EXPECT_FALSE(ps.empty());
    EXPECT_TRUE(ps.has_permission(Permission::Execute));
    EXPECT_TRUE(ps.has_permission(Permission::ReadMemory));
    EXPECT_FALSE(ps.has_permission(Permission::WriteMemory));
}

TEST(PermissionSetTest, FromBits) {
    auto ps = PermissionSet::from_bits(0b11);  // Execute | ReadMemory
    EXPECT_TRUE(ps.has_permission(Permission::Execute));
    EXPECT_TRUE(ps.has_permission(Permission::ReadMemory));
    EXPECT_FALSE(ps.has_permission(Permission::WriteMemory));
}

// ============================================================================
// Preset Constructor Tests
// ============================================================================

TEST(PermissionSetTest, ReadOnlyPreset) {
    auto ps = PermissionSet::read_only();
    EXPECT_TRUE(ps.has_permission(Permission::Execute));
    EXPECT_TRUE(ps.has_permission(Permission::ReadMemory));
    EXPECT_TRUE(ps.has_permission(Permission::ReadState));
    EXPECT_FALSE(ps.has_permission(Permission::WriteMemory));
    EXPECT_FALSE(ps.has_permission(Permission::WriteState));
}

TEST(PermissionSetTest, ReadWritePreset) {
    auto ps = PermissionSet::read_write();
    EXPECT_TRUE(ps.has_permission(Permission::Execute));
    EXPECT_TRUE(ps.has_permission(Permission::ReadMemory));
    EXPECT_TRUE(ps.has_permission(Permission::ReadState));
    EXPECT_TRUE(ps.has_permission(Permission::WriteMemory));
    EXPECT_TRUE(ps.has_permission(Permission::WriteState));
    EXPECT_FALSE(ps.has_permission(Permission::SpawnDot));
}

TEST(PermissionSetTest, FullPreset) {
    auto ps = PermissionSet::full();
    EXPECT_EQ(ps.value(), Permission::Full);
    EXPECT_TRUE(ps.has_permission(Permission::Execute));
    EXPECT_TRUE(ps.has_permission(Permission::Crypto));
    EXPECT_TRUE(ps.has_permission(Permission::SystemCall));
    EXPECT_TRUE(ps.has_permission(Permission::Debug));
}

TEST(PermissionSetTest, NonePreset) {
    auto ps = PermissionSet::none();
    EXPECT_TRUE(ps.empty());
    EXPECT_EQ(ps.value(), Permission::None);
}

// ============================================================================
// Query Method Tests
// ============================================================================

TEST(PermissionSetTest, HasPermission) {
    PermissionSet ps{Permission::Execute | Permission::ReadMemory};

    EXPECT_TRUE(ps.has_permission(Permission::Execute));
    EXPECT_TRUE(ps.has_permission(Permission::ReadMemory));
    EXPECT_FALSE(ps.has_permission(Permission::WriteMemory));

    // Check combined permission
    EXPECT_TRUE(
        ps.has_permission(Permission::Execute | Permission::ReadMemory));
    EXPECT_FALSE(
        ps.has_permission(Permission::Execute | Permission::WriteMemory));
}

TEST(PermissionSetTest, HasAny) {
    PermissionSet ps{Permission::Execute};

    EXPECT_TRUE(ps.has_any(Permission::Execute));
    EXPECT_TRUE(ps.has_any(Permission::Execute | Permission::WriteMemory));
    EXPECT_FALSE(ps.has_any(Permission::ReadMemory | Permission::WriteMemory));
}

TEST(PermissionSetTest, Satisfies) {
    PermissionSet ps{Permission::Execute | Permission::ReadMemory};

    EXPECT_TRUE(ps.satisfies(Permission::Execute));
    EXPECT_TRUE(ps.satisfies(Permission::ReadMemory));
    EXPECT_TRUE(ps.satisfies(Permission::Execute | Permission::ReadMemory));
    EXPECT_FALSE(ps.satisfies(Permission::WriteMemory));
}

// ============================================================================
// Modification Method Tests
// ============================================================================

TEST(PermissionSetTest, Grant) {
    PermissionSet ps;

    ps.grant(Permission::Execute);
    EXPECT_TRUE(ps.has_permission(Permission::Execute));

    ps.grant(Permission::ReadMemory | Permission::WriteMemory);
    EXPECT_TRUE(ps.has_permission(Permission::ReadMemory));
    EXPECT_TRUE(ps.has_permission(Permission::WriteMemory));
}

TEST(PermissionSetTest, GrantChaining) {
    PermissionSet ps;

    ps.grant(Permission::Execute)
        .grant(Permission::ReadMemory)
        .grant(Permission::WriteMemory);

    EXPECT_TRUE(ps.has_permission(Permission::Execute));
    EXPECT_TRUE(ps.has_permission(Permission::ReadMemory));
    EXPECT_TRUE(ps.has_permission(Permission::WriteMemory));
}

TEST(PermissionSetTest, Revoke) {
    PermissionSet ps{Permission::Execute | Permission::ReadMemory |
                     Permission::WriteMemory};

    ps.revoke(Permission::WriteMemory);
    EXPECT_TRUE(ps.has_permission(Permission::Execute));
    EXPECT_TRUE(ps.has_permission(Permission::ReadMemory));
    EXPECT_FALSE(ps.has_permission(Permission::WriteMemory));
}

TEST(PermissionSetTest, RevokeChaining) {
    auto ps = PermissionSet::full();

    ps.revoke(Permission::SpawnDot)
        .revoke(Permission::SendMessage)
        .revoke(Permission::SystemCall);

    EXPECT_FALSE(ps.has_permission(Permission::SpawnDot));
    EXPECT_FALSE(ps.has_permission(Permission::SendMessage));
    EXPECT_FALSE(ps.has_permission(Permission::SystemCall));
    EXPECT_TRUE(ps.has_permission(Permission::Execute));
}

TEST(PermissionSetTest, Clear) {
    PermissionSet ps{Permission::Full};

    ps.clear();
    EXPECT_TRUE(ps.empty());
    EXPECT_EQ(ps.value(), Permission::None);
}

TEST(PermissionSetTest, Set) {
    PermissionSet ps{Permission::Execute};

    ps.set(kReadOnly);
    EXPECT_TRUE(ps.has_permission(Permission::Execute));
    EXPECT_TRUE(ps.has_permission(Permission::ReadMemory));
    EXPECT_TRUE(ps.has_permission(Permission::ReadState));
}

// ============================================================================
// Require Method Tests
// ============================================================================

TEST(PermissionSetTest, RequireSuccess) {
    PermissionSet ps{Permission::Execute | Permission::ReadMemory};

    EXPECT_NO_THROW(ps.require(Permission::Execute));
    EXPECT_NO_THROW(ps.require(Permission::ReadMemory));
    EXPECT_NO_THROW(
        ps.require(Permission::Execute | Permission::ReadMemory));
}

TEST(PermissionSetTest, RequireFailure) {
    PermissionSet ps{Permission::Execute};

    EXPECT_THROW(ps.require(Permission::WriteMemory),
                 PermissionDeniedException);
}

TEST(PermissionSetTest, RequireExceptionDetails) {
    PermissionSet ps{Permission::Execute};

    try {
        ps.require(Permission::WriteMemory | Permission::Crypto,
                   "test_operation");
        FAIL() << "Expected PermissionDeniedException";
    } catch (const PermissionDeniedException& e) {
        EXPECT_EQ(e.required(),
                  Permission::WriteMemory | Permission::Crypto);
        EXPECT_EQ(e.actual(), Permission::Execute);
        EXPECT_EQ(e.context(), "test_operation");
        EXPECT_EQ(e.missing(),
                  Permission::WriteMemory | Permission::Crypto);

        std::string msg = e.what();
        EXPECT_TRUE(msg.find("Permission denied") != std::string::npos);
        EXPECT_TRUE(msg.find("test_operation") != std::string::npos);
    }
}

TEST(PermissionSetTest, RequirePartialMissing) {
    PermissionSet ps{Permission::Execute | Permission::ReadMemory};

    try {
        ps.require(Permission::Execute | Permission::WriteMemory);
        FAIL() << "Expected PermissionDeniedException";
    } catch (const PermissionDeniedException& e) {
        EXPECT_EQ(e.missing(), Permission::WriteMemory);
    }
}

// ============================================================================
// Set Operation Tests
// ============================================================================

TEST(PermissionSetTest, Union) {
    PermissionSet a{Permission::Execute};
    PermissionSet b{Permission::ReadMemory};

    auto c = a | b;
    EXPECT_TRUE(c.has_permission(Permission::Execute));
    EXPECT_TRUE(c.has_permission(Permission::ReadMemory));
}

TEST(PermissionSetTest, Intersection) {
    PermissionSet a{Permission::Execute | Permission::ReadMemory};
    PermissionSet b{Permission::ReadMemory | Permission::WriteMemory};

    auto c = a & b;
    EXPECT_FALSE(c.has_permission(Permission::Execute));
    EXPECT_TRUE(c.has_permission(Permission::ReadMemory));
    EXPECT_FALSE(c.has_permission(Permission::WriteMemory));
}

TEST(PermissionSetTest, SymmetricDifference) {
    PermissionSet a{Permission::Execute | Permission::ReadMemory};
    PermissionSet b{Permission::ReadMemory | Permission::WriteMemory};

    auto c = a ^ b;
    EXPECT_TRUE(c.has_permission(Permission::Execute));
    EXPECT_FALSE(c.has_permission(Permission::ReadMemory));
    EXPECT_TRUE(c.has_permission(Permission::WriteMemory));
}

TEST(PermissionSetTest, Complement) {
    PermissionSet ps{Permission::None};
    auto complement = ~ps;
    EXPECT_EQ(complement.value(), Permission::Full);
}

TEST(PermissionSetTest, CompoundUnionAssignment) {
    PermissionSet ps{Permission::Execute};

    ps |= PermissionSet{Permission::ReadMemory};
    EXPECT_TRUE(ps.has_permission(Permission::Execute));
    EXPECT_TRUE(ps.has_permission(Permission::ReadMemory));
}

TEST(PermissionSetTest, CompoundIntersectionAssignment) {
    PermissionSet ps{Permission::Execute | Permission::ReadMemory};

    ps &= PermissionSet{Permission::Execute};
    EXPECT_TRUE(ps.has_permission(Permission::Execute));
    EXPECT_FALSE(ps.has_permission(Permission::ReadMemory));
}

TEST(PermissionSetTest, CompoundXorAssignment) {
    PermissionSet ps{Permission::Execute | Permission::ReadMemory};

    ps ^= PermissionSet{Permission::ReadMemory};
    EXPECT_TRUE(ps.has_permission(Permission::Execute));
    EXPECT_FALSE(ps.has_permission(Permission::ReadMemory));
}

// ============================================================================
// Subset/Superset Tests
// ============================================================================

TEST(PermissionSetTest, IsSubsetOf) {
    PermissionSet small{Permission::Execute};
    PermissionSet large{Permission::Execute | Permission::ReadMemory};

    EXPECT_TRUE(small.is_subset_of(large));
    EXPECT_FALSE(large.is_subset_of(small));
    EXPECT_TRUE(small.is_subset_of(small));
}

TEST(PermissionSetTest, IsSupersetOf) {
    PermissionSet small{Permission::Execute};
    PermissionSet large{Permission::Execute | Permission::ReadMemory};

    EXPECT_FALSE(small.is_superset_of(large));
    EXPECT_TRUE(large.is_superset_of(small));
    EXPECT_TRUE(small.is_superset_of(small));
}

// ============================================================================
// String Conversion Tests
// ============================================================================

TEST(PermissionSetTest, ToString) {
    PermissionSet ps{Permission::Execute | Permission::ReadMemory};
    std::string s = ps.to_string();

    EXPECT_TRUE(s.find("Execute") != std::string::npos);
    EXPECT_TRUE(s.find("ReadMemory") != std::string::npos);
}

TEST(PermissionSetTest, ToStringEmpty) {
    PermissionSet ps;
    EXPECT_EQ(ps.to_string(), "None");
}

TEST(PermissionSetTest, ToStringFull) {
    PermissionSet ps = PermissionSet::full();
    EXPECT_EQ(ps.to_string(), "Full");
}

// ============================================================================
// Comparison Tests
// ============================================================================

TEST(PermissionSetTest, Equality) {
    PermissionSet a{Permission::Execute};
    PermissionSet b{Permission::Execute};
    PermissionSet c{Permission::ReadMemory};

    EXPECT_EQ(a, b);
    EXPECT_NE(a, c);
}

// ============================================================================
// PermissionDeniedException Tests
// ============================================================================

TEST(PermissionDeniedExceptionTest, Construction) {
    PermissionDeniedException e(Permission::WriteMemory, Permission::Execute,
                                "test_context");

    EXPECT_EQ(e.required(), Permission::WriteMemory);
    EXPECT_EQ(e.actual(), Permission::Execute);
    EXPECT_EQ(e.context(), "test_context");
    EXPECT_EQ(e.missing(), Permission::WriteMemory);
}

TEST(PermissionDeniedExceptionTest, SourceLocation) {
    PermissionDeniedException e(Permission::WriteMemory, Permission::Execute);

    EXPECT_TRUE(e.location().line() > 0);
    std::string file = e.location().file_name();
    EXPECT_FALSE(file.empty());
}

TEST(PermissionDeniedExceptionTest, WhatMessage) {
    PermissionDeniedException e(Permission::Crypto, Permission::Execute,
                                "crypto_op");

    std::string msg = e.what();
    EXPECT_TRUE(msg.find("Permission denied") != std::string::npos);
    EXPECT_TRUE(msg.find("crypto_op") != std::string::npos);
    EXPECT_TRUE(msg.find("Crypto") != std::string::npos);
}

TEST(PermissionDeniedExceptionTest, MissingCalculation) {
    PermissionDeniedException e(
        Permission::Execute | Permission::WriteMemory,
        Permission::Execute,
        "test");

    EXPECT_EQ(e.missing(), Permission::WriteMemory);
}

// ============================================================================
// Constexpr Tests
// ============================================================================

TEST(PermissionConstexprTest, PermissionSetConstexpr) {
    constexpr PermissionSet ps{Permission::Execute};
    static_assert(ps.has_permission(Permission::Execute));
    static_assert(!ps.has_permission(Permission::WriteMemory));
    static_assert(ps.bits() == 1U);
}

TEST(PermissionConstexprTest, PresetConstexpr) {
    constexpr auto ro = PermissionSet::read_only();
    static_assert(ro.has_permission(Permission::Execute));
    static_assert(ro.has_permission(Permission::ReadMemory));
    static_assert(ro.has_permission(Permission::ReadState));
}

TEST(PermissionConstexprTest, ComboConstexpr) {
    static_assert(has_permission(kReadOnly, Permission::Execute));
    static_assert(has_permission(kReadWrite, Permission::WriteMemory));
    static_assert(is_subset(kReadWrite, kReadOnly));
}
