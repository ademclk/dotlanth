/// @file opcode_permission_test.cpp
/// @brief Unit tests for SEC-005 Opcode Authorization

#include <gtest/gtest.h>

#include "dotvm/core/security/opcode_permission.hpp"
#include "dotvm/exec/execution_context.hpp"

namespace dotvm::core::security {
namespace {

// ============================================================================
// Table Size Tests
// ============================================================================

TEST(OpcodePermissionTableTest, TableSizeIs256) {
    EXPECT_EQ(opcode_permission_table.size(), 256);
}

// ============================================================================
// Arithmetic Opcodes (0x00-0x1F) - Execute Only
// ============================================================================

TEST(OpcodePermissionTest, ArithmeticOpcodesRequireExecuteOnly) {
    for (std::uint8_t opcode = 0x00; opcode <= 0x1F; ++opcode) {
        Permission required = get_required_permission(opcode);
        EXPECT_EQ(required, Permission::Execute)
            << "Opcode 0x" << std::hex << static_cast<int>(opcode)
            << " should require only Execute permission";
    }
}

// ============================================================================
// Bitwise Opcodes (0x20-0x2F) - Execute Only
// ============================================================================

TEST(OpcodePermissionTest, BitwiseOpcodesRequireExecuteOnly) {
    for (std::uint8_t opcode = 0x20; opcode <= 0x2F; ++opcode) {
        Permission required = get_required_permission(opcode);
        EXPECT_EQ(required, Permission::Execute)
            << "Opcode 0x" << std::hex << static_cast<int>(opcode)
            << " should require only Execute permission";
    }
}

// ============================================================================
// Comparison Opcodes (0x30-0x3F) - Execute Only
// ============================================================================

TEST(OpcodePermissionTest, ComparisonOpcodesRequireExecuteOnly) {
    for (std::uint8_t opcode = 0x30; opcode <= 0x3F; ++opcode) {
        Permission required = get_required_permission(opcode);
        EXPECT_EQ(required, Permission::Execute)
            << "Opcode 0x" << std::hex << static_cast<int>(opcode)
            << " should require only Execute permission";
    }
}

// ============================================================================
// Control Flow Opcodes (0x40-0x5F) - Execute Only
// ============================================================================

TEST(OpcodePermissionTest, ControlFlowOpcodesRequireExecuteOnly) {
    for (std::uint8_t opcode = 0x40; opcode <= 0x5F; ++opcode) {
        Permission required = get_required_permission(opcode);
        EXPECT_EQ(required, Permission::Execute)
            << "Opcode 0x" << std::hex << static_cast<int>(opcode)
            << " should require only Execute permission";
    }
}

// ============================================================================
// LOAD Opcodes (0x60-0x63) - Execute + ReadMemory
// ============================================================================

TEST(OpcodePermissionTest, LoadOpcodesRequireReadMemory) {
    const Permission expected = Permission::Execute | Permission::ReadMemory;
    for (std::uint8_t opcode = 0x60; opcode <= 0x63; ++opcode) {
        Permission required = get_required_permission(opcode);
        EXPECT_EQ(required, expected) << "Opcode 0x" << std::hex << static_cast<int>(opcode)
                                      << " should require Execute + ReadMemory";
    }
}

// ============================================================================
// STORE Opcodes (0x64-0x67) - Execute + WriteMemory
// ============================================================================

TEST(OpcodePermissionTest, StoreOpcodesRequireWriteMemory) {
    const Permission expected = Permission::Execute | Permission::WriteMemory;
    for (std::uint8_t opcode = 0x64; opcode <= 0x67; ++opcode) {
        Permission required = get_required_permission(opcode);
        EXPECT_EQ(required, expected) << "Opcode 0x" << std::hex << static_cast<int>(opcode)
                                      << " should require Execute + WriteMemory";
    }
}

// ============================================================================
// LEA Opcode (0x68) - Execute + ReadMemory
// ============================================================================

TEST(OpcodePermissionTest, LeaRequiresReadMemory) {
    Permission required = get_required_permission(0x68);
    EXPECT_EQ(required, Permission::Execute | Permission::ReadMemory);
}

// ============================================================================
// Reserved Range 1 (0x69-0x7F) - None (Always Denied)
// ============================================================================

TEST(OpcodePermissionTest, ReservedRange1AlwaysDenied) {
    for (std::uint8_t opcode = 0x69; opcode <= 0x7F; ++opcode) {
        Permission required = get_required_permission(opcode);
        EXPECT_EQ(required, Permission::None)
            << "Reserved opcode 0x" << std::hex << static_cast<int>(opcode)
            << " should require None (always denied)";
        EXPECT_TRUE(is_reserved_opcode(opcode))
            << "Opcode 0x" << std::hex << static_cast<int>(opcode)
            << " should be marked as reserved";
    }
}

// ============================================================================
// DataMove Opcodes (0x80-0x8F) - Execute Only
// ============================================================================

TEST(OpcodePermissionTest, DataMoveOpcodesRequireExecuteOnly) {
    for (std::uint8_t opcode = 0x80; opcode <= 0x8F; ++opcode) {
        Permission required = get_required_permission(opcode);
        EXPECT_EQ(required, Permission::Execute)
            << "Opcode 0x" << std::hex << static_cast<int>(opcode)
            << " should require only Execute permission";
    }
}

// ============================================================================
// Reserved Range 2 (0x90-0x9F) - None (Always Denied)
// ============================================================================

TEST(OpcodePermissionTest, ReservedRange2AlwaysDenied) {
    for (std::uint8_t opcode = 0x90; opcode <= 0x9F; ++opcode) {
        Permission required = get_required_permission(opcode);
        EXPECT_EQ(required, Permission::None)
            << "Reserved opcode 0x" << std::hex << static_cast<int>(opcode)
            << " should require None (always denied)";
        EXPECT_TRUE(is_reserved_opcode(opcode));
    }
}

// ============================================================================
// STATE_GET Opcodes (0xA0-0xA7) - Execute + ReadState
// ============================================================================

TEST(OpcodePermissionTest, StateGetOpcodesRequireReadState) {
    const Permission expected = Permission::Execute | Permission::ReadState;
    for (std::uint8_t opcode = 0xA0; opcode <= 0xA7; ++opcode) {
        Permission required = get_required_permission(opcode);
        EXPECT_EQ(required, expected) << "Opcode 0x" << std::hex << static_cast<int>(opcode)
                                      << " should require Execute + ReadState";
    }
}

// ============================================================================
// STATE_PUT Opcodes (0xA8-0xAF) - Execute + WriteState
// ============================================================================

TEST(OpcodePermissionTest, StatePutOpcodesRequireWriteState) {
    const Permission expected = Permission::Execute | Permission::WriteState;
    for (std::uint8_t opcode = 0xA8; opcode <= 0xAF; ++opcode) {
        Permission required = get_required_permission(opcode);
        EXPECT_EQ(required, expected) << "Opcode 0x" << std::hex << static_cast<int>(opcode)
                                      << " should require Execute + WriteState";
    }
}

// ============================================================================
// Crypto Opcodes (0xB0-0xBF) - Execute + Crypto
// ============================================================================

TEST(OpcodePermissionTest, CryptoOpcodesRequireCrypto) {
    const Permission expected = Permission::Execute | Permission::Crypto;
    for (std::uint8_t opcode = 0xB0; opcode <= 0xBF; ++opcode) {
        Permission required = get_required_permission(opcode);
        EXPECT_EQ(required, expected) << "Opcode 0x" << std::hex << static_cast<int>(opcode)
                                      << " should require Execute + Crypto";
    }
}

// ============================================================================
// SPAWN Opcodes (0xC0-0xC3) - Execute + SpawnDot
// ============================================================================

TEST(OpcodePermissionTest, SpawnOpcodesRequireSpawnDot) {
    const Permission expected = Permission::Execute | Permission::SpawnDot;
    for (std::uint8_t opcode = 0xC0; opcode <= 0xC3; ++opcode) {
        Permission required = get_required_permission(opcode);
        EXPECT_EQ(required, expected) << "Opcode 0x" << std::hex << static_cast<int>(opcode)
                                      << " should require Execute + SpawnDot";
    }
}

// ============================================================================
// SEND/RECV Opcodes (0xC4-0xCF) - Execute + SendMessage
// ============================================================================

TEST(OpcodePermissionTest, MessageOpcodesRequireSendMessage) {
    const Permission expected = Permission::Execute | Permission::SendMessage;
    for (std::uint8_t opcode = 0xC4; opcode <= 0xCF; ++opcode) {
        Permission required = get_required_permission(opcode);
        EXPECT_EQ(required, expected) << "Opcode 0x" << std::hex << static_cast<int>(opcode)
                                      << " should require Execute + SendMessage";
    }
}

// ============================================================================
// Reserved Range 3 (0xD0-0xEF) - None (Always Denied)
// ============================================================================

TEST(OpcodePermissionTest, ReservedRange3AlwaysDenied) {
    for (std::size_t i = 0xD0; i <= 0xEF; ++i) {
        auto opcode = static_cast<std::uint8_t>(i);
        Permission required = get_required_permission(opcode);
        EXPECT_EQ(required, Permission::None)
            << "Reserved opcode 0x" << std::hex << static_cast<int>(opcode)
            << " should require None (always denied)";
        EXPECT_TRUE(is_reserved_opcode(opcode));
    }
}

// ============================================================================
// System Opcodes (0xF0-0xFF)
// ============================================================================

TEST(OpcodePermissionTest, NopRequiresExecuteOnly) {
    Permission required = get_required_permission(0xF0);
    EXPECT_EQ(required, Permission::Execute);
}

TEST(OpcodePermissionTest, BreakRequiresExecuteOnly) {
    Permission required = get_required_permission(0xF1);
    EXPECT_EQ(required, Permission::Execute);
}

TEST(OpcodePermissionTest, DebugRequiresDebugPermission) {
    Permission required = get_required_permission(0xFD);
    EXPECT_EQ(required, Permission::Execute | Permission::Debug);
}

TEST(OpcodePermissionTest, SyscallRequiresSystemCallPermission) {
    Permission required = get_required_permission(0xFE);
    EXPECT_EQ(required, Permission::Execute | Permission::SystemCall);
}

TEST(OpcodePermissionTest, Opcode0xFFRequiresExecuteOnly) {
    Permission required = get_required_permission(0xFF);
    EXPECT_EQ(required, Permission::Execute);
}

// ============================================================================
// Authorization Tests
// ============================================================================

TEST(OpcodeAuthorizationTest, FullPermissionsAuthorizesAllNonReserved) {
    Permission full = Permission::Full;

    for (std::size_t i = 0; i <= 0xFF; ++i) {
        auto opcode = static_cast<std::uint8_t>(i);
        bool authorized = is_opcode_authorized(opcode, full);

        if (is_reserved_opcode(opcode)) {
            EXPECT_FALSE(authorized) << "Reserved opcode 0x" << std::hex << static_cast<int>(opcode)
                                     << " should never be authorized";
        } else {
            EXPECT_TRUE(authorized) << "Opcode 0x" << std::hex << static_cast<int>(opcode)
                                    << " should be authorized with full permissions";
        }
    }
}

TEST(OpcodeAuthorizationTest, ExecuteOnlyFailsMemoryOpcodes) {
    Permission execute_only = Permission::Execute;

    // LOAD should fail
    EXPECT_FALSE(is_opcode_authorized(0x60, execute_only));
    EXPECT_FALSE(is_opcode_authorized(0x63, execute_only));

    // STORE should fail
    EXPECT_FALSE(is_opcode_authorized(0x64, execute_only));
    EXPECT_FALSE(is_opcode_authorized(0x67, execute_only));

    // LEA should fail
    EXPECT_FALSE(is_opcode_authorized(0x68, execute_only));

    // Arithmetic should pass
    EXPECT_TRUE(is_opcode_authorized(0x00, execute_only));
    EXPECT_TRUE(is_opcode_authorized(0x1F, execute_only));
}

TEST(OpcodeAuthorizationTest, ExecuteOnlyFailsCryptoOpcodes) {
    Permission execute_only = Permission::Execute;

    for (std::uint8_t opcode = 0xB0; opcode <= 0xBF; ++opcode) {
        EXPECT_FALSE(is_opcode_authorized(opcode, execute_only))
            << "Crypto opcode 0x" << std::hex << static_cast<int>(opcode)
            << " should fail with Execute-only permissions";
    }
}

TEST(OpcodeAuthorizationTest, ExecuteOnlyFailsStateOpcodes) {
    Permission execute_only = Permission::Execute;

    // STATE_GET should fail
    for (std::uint8_t opcode = 0xA0; opcode <= 0xA7; ++opcode) {
        EXPECT_FALSE(is_opcode_authorized(opcode, execute_only));
    }

    // STATE_PUT should fail
    for (std::uint8_t opcode = 0xA8; opcode <= 0xAF; ++opcode) {
        EXPECT_FALSE(is_opcode_authorized(opcode, execute_only));
    }
}

TEST(OpcodeAuthorizationTest, ReadWriteMemoryAuthorization) {
    Permission read_write = Permission::Execute | Permission::ReadMemory | Permission::WriteMemory;

    // LOAD should pass
    EXPECT_TRUE(is_opcode_authorized(0x60, read_write));
    EXPECT_TRUE(is_opcode_authorized(0x63, read_write));

    // STORE should pass
    EXPECT_TRUE(is_opcode_authorized(0x64, read_write));
    EXPECT_TRUE(is_opcode_authorized(0x67, read_write));

    // LEA should pass
    EXPECT_TRUE(is_opcode_authorized(0x68, read_write));
}

TEST(OpcodeAuthorizationTest, PermissionSetOverload) {
    PermissionSet perms = PermissionSet::full();

    EXPECT_TRUE(is_opcode_authorized(0x00, perms));  // Arithmetic
    EXPECT_TRUE(is_opcode_authorized(0x60, perms));  // LOAD
    EXPECT_TRUE(is_opcode_authorized(0x64, perms));  // STORE
    EXPECT_TRUE(is_opcode_authorized(0xB0, perms));  // Crypto

    // Reserved should still fail
    EXPECT_FALSE(is_opcode_authorized(0x69, perms));
    EXPECT_FALSE(is_opcode_authorized(0x90, perms));
    EXPECT_FALSE(is_opcode_authorized(0xD0, perms));
}

// ============================================================================
// is_reserved_opcode Tests
// ============================================================================

TEST(IsReservedOpcodeTest, ReservedRangesAreMarked) {
    // Reserved range 1: 0x69-0x7F
    for (std::uint8_t opcode = 0x69; opcode <= 0x7F; ++opcode) {
        EXPECT_TRUE(is_reserved_opcode(opcode));
    }

    // Reserved range 2: 0x90-0x9F
    for (std::uint8_t opcode = 0x90; opcode <= 0x9F; ++opcode) {
        EXPECT_TRUE(is_reserved_opcode(opcode));
    }

    // Reserved range 3: 0xD0-0xEF
    for (std::size_t i = 0xD0; i <= 0xEF; ++i) {
        EXPECT_TRUE(is_reserved_opcode(static_cast<std::uint8_t>(i)));
    }
}

TEST(IsReservedOpcodeTest, NonReservedAreNotMarked) {
    // Arithmetic
    EXPECT_FALSE(is_reserved_opcode(0x00));
    EXPECT_FALSE(is_reserved_opcode(0x1F));

    // Memory
    EXPECT_FALSE(is_reserved_opcode(0x60));
    EXPECT_FALSE(is_reserved_opcode(0x68));

    // Crypto
    EXPECT_FALSE(is_reserved_opcode(0xB0));
    EXPECT_FALSE(is_reserved_opcode(0xBF));

    // System
    EXPECT_FALSE(is_reserved_opcode(0xF0));
    EXPECT_FALSE(is_reserved_opcode(0xFE));
    EXPECT_FALSE(is_reserved_opcode(0xFF));
}

// ============================================================================
// get_opcode_category Tests
// ============================================================================

TEST(OpcodeCategoryTest, CategoriesAreCorrect) {
    EXPECT_EQ(get_opcode_category(0x00), "Arithmetic");
    EXPECT_EQ(get_opcode_category(0x1F), "Arithmetic");
    EXPECT_EQ(get_opcode_category(0x20), "Bitwise");
    EXPECT_EQ(get_opcode_category(0x2F), "Bitwise");
    EXPECT_EQ(get_opcode_category(0x30), "Comparison");
    EXPECT_EQ(get_opcode_category(0x3F), "Comparison");
    EXPECT_EQ(get_opcode_category(0x40), "ControlFlow");
    EXPECT_EQ(get_opcode_category(0x5F), "ControlFlow");
    EXPECT_EQ(get_opcode_category(0x60), "Load");
    EXPECT_EQ(get_opcode_category(0x63), "Load");
    EXPECT_EQ(get_opcode_category(0x64), "Store");
    EXPECT_EQ(get_opcode_category(0x67), "Store");
    EXPECT_EQ(get_opcode_category(0x68), "LEA");
    EXPECT_EQ(get_opcode_category(0x69), "Reserved");
    EXPECT_EQ(get_opcode_category(0x7F), "Reserved");
    EXPECT_EQ(get_opcode_category(0x80), "DataMove");
    EXPECT_EQ(get_opcode_category(0x8F), "DataMove");
    EXPECT_EQ(get_opcode_category(0x90), "Reserved");
    EXPECT_EQ(get_opcode_category(0x9F), "Reserved");
    EXPECT_EQ(get_opcode_category(0xA0), "StateGet");
    EXPECT_EQ(get_opcode_category(0xA7), "StateGet");
    EXPECT_EQ(get_opcode_category(0xA8), "StatePut");
    EXPECT_EQ(get_opcode_category(0xAF), "StatePut");
    EXPECT_EQ(get_opcode_category(0xB0), "Crypto");
    EXPECT_EQ(get_opcode_category(0xBF), "Crypto");
    EXPECT_EQ(get_opcode_category(0xC0), "Spawn");
    EXPECT_EQ(get_opcode_category(0xC3), "Spawn");
    EXPECT_EQ(get_opcode_category(0xC4), "Message");
    EXPECT_EQ(get_opcode_category(0xCF), "Message");
    EXPECT_EQ(get_opcode_category(0xD0), "Reserved");
    EXPECT_EQ(get_opcode_category(0xEF), "Reserved");
    EXPECT_EQ(get_opcode_category(0xF0), "NOP");
    EXPECT_EQ(get_opcode_category(0xF1), "BREAK");
    EXPECT_EQ(get_opcode_category(0xFD), "DEBUG");
    EXPECT_EQ(get_opcode_category(0xFE), "SYSCALL");
    EXPECT_EQ(get_opcode_category(0xFF), "System");
}

// ============================================================================
// SecurityContext Integration Tests
// ============================================================================

TEST(CheckOpcodePermissionTest, SuccessWithCorrectPermissions) {
    SecurityContext ctx(capabilities::CapabilityLimits::unlimited(), PermissionSet::full());

    // Non-reserved opcodes should succeed
    EXPECT_EQ(check_opcode_permission(0x00, ctx), SecurityContextError::Success);
    EXPECT_EQ(check_opcode_permission(0x60, ctx), SecurityContextError::Success);
    EXPECT_EQ(check_opcode_permission(0xB0, ctx), SecurityContextError::Success);
}

TEST(CheckOpcodePermissionTest, DeniedForReservedOpcodes) {
    BufferedAuditLogger logger(100);
    SecurityContext ctx(capabilities::CapabilityLimits::unlimited(), PermissionSet::full(),
                        &logger);

    // Reserved opcodes should always fail
    EXPECT_EQ(check_opcode_permission(0x69, ctx), SecurityContextError::PermissionDenied);
    EXPECT_EQ(check_opcode_permission(0x90, ctx), SecurityContextError::PermissionDenied);
    EXPECT_EQ(check_opcode_permission(0xD0, ctx), SecurityContextError::PermissionDenied);

    // Should have logged OpcodeDenied events
    EXPECT_GE(logger.size(), 3);
}

TEST(CheckOpcodePermissionTest, DeniedForMissingPermissions) {
    BufferedAuditLogger logger(100);
    SecurityContext ctx(capabilities::CapabilityLimits::unlimited(),
                        PermissionSet(Permission::Execute),  // Execute only
                        &logger);

    // Memory opcodes should fail
    EXPECT_EQ(check_opcode_permission(0x60, ctx), SecurityContextError::PermissionDenied);
    EXPECT_EQ(check_opcode_permission(0x64, ctx), SecurityContextError::PermissionDenied);

    // Crypto should fail
    EXPECT_EQ(check_opcode_permission(0xB0, ctx), SecurityContextError::PermissionDenied);

    // Arithmetic should succeed
    EXPECT_EQ(check_opcode_permission(0x00, ctx), SecurityContextError::Success);

    // Check audit log contains OpcodeDenied
    bool found_opcode_denied = false;
    for (const auto& event : logger.events()) {
        if (event.type == AuditEventType::OpcodeDenied) {
            found_opcode_denied = true;
            break;
        }
    }
    EXPECT_TRUE(found_opcode_denied);
}

TEST(CheckOpcodePermissionTest, LogsContextString) {
    BufferedAuditLogger logger(100);
    SecurityContext ctx(capabilities::CapabilityLimits::unlimited(),
                        PermissionSet(Permission::Execute), &logger);

    auto result = check_opcode_permission(0x69, ctx, "test_dispatch");
    EXPECT_EQ(result, SecurityContextError::PermissionDenied);

    EXPECT_GE(logger.size(), 1);
    // The context is captured in the event
    EXPECT_EQ(logger.events().back().type, AuditEventType::OpcodeDenied);
}

// ============================================================================
// ExecResult and AuditEventType Tests
// ============================================================================

TEST(ExecResultTest, CapabilityDeniedExists) {
    EXPECT_EQ(static_cast<int>(exec::ExecResult::CapabilityDenied), 13);
    EXPECT_STREQ(exec::to_string(exec::ExecResult::CapabilityDenied), "CapabilityDenied");
}

TEST(AuditEventTypeTest, OpcodeDeniedExists) {
    EXPECT_EQ(static_cast<int>(AuditEventType::OpcodeDenied), 20);
    EXPECT_STREQ(to_string(AuditEventType::OpcodeDenied), "OpcodeDenied");
}

// ============================================================================
// Constexpr Tests (Compile-time verification)
// ============================================================================

static_assert(opcode_permission_table.size() == 256, "Table must have 256 entries");

static_assert(get_required_permission(0x00) == Permission::Execute, "Arithmetic requires Execute");

static_assert(get_required_permission(0x60) == (Permission::Execute | Permission::ReadMemory),
              "LOAD requires Execute + ReadMemory");

static_assert(get_required_permission(0x64) == (Permission::Execute | Permission::WriteMemory),
              "STORE requires Execute + WriteMemory");

static_assert(get_required_permission(0x69) == Permission::None, "Reserved opcodes require None");

static_assert(is_reserved_opcode(0x69), "0x69 is reserved");
static_assert(!is_reserved_opcode(0x00), "0x00 is not reserved");

static_assert(is_opcode_authorized(0x00, Permission::Execute),
              "Arithmetic is authorized with Execute");

static_assert(!is_opcode_authorized(0x60, Permission::Execute),
              "LOAD is not authorized with Execute only");

static_assert(is_opcode_authorized(0x60, Permission::Execute | Permission::ReadMemory),
              "LOAD is authorized with Execute + ReadMemory");

}  // namespace
}  // namespace dotvm::core::security
