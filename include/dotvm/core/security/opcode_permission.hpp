#pragma once

/// @file opcode_permission.hpp
/// @brief SEC-005 Opcode Authorization for per-instruction permission checks
///
/// This header defines the opcode-to-permission mapping table and functions
/// for O(1) permission lookup during bytecode dispatch. Each opcode maps to
/// the SEC-002 permissions required for its execution.
///
/// Performance Target: <10ns for permission check (inline bitmask operation).

#include <array>
#include <cstdint>
#include <string_view>

#include "dotvm/core/security/permission.hpp"
#include "dotvm/core/security/security_context.hpp"

namespace dotvm::core::security {

// ============================================================================
// Opcode Permission Table (SEC-005)
// ============================================================================

/// @brief O(1) lookup table mapping opcodes to required permissions
///
/// The table is constexpr-initialized at compile time for zero runtime cost.
/// Each opcode (0x00-0xFF) maps to the Permission flags required to execute it.
///
/// Opcode Categories:
/// - 0x00-0x1F: Arithmetic (Execute only)
/// - 0x20-0x2F: Bitwise (Execute only)
/// - 0x30-0x3F: Comparison (Execute only)
/// - 0x40-0x5F: Control Flow (Execute only)
/// - 0x60-0x63: LOAD (Execute + ReadMemory)
/// - 0x64-0x67: STORE (Execute + WriteMemory)
/// - 0x68: LEA (Execute + ReadMemory)
/// - 0x69-0x7F: Reserved (None - always denied)
/// - 0x80-0x8F: DataMove (Execute only)
/// - 0x90-0x9F: Reserved (None - always denied)
/// - 0xA0-0xA7: STATE_GET (Execute + ReadState)
/// - 0xA8-0xAF: STATE_PUT (Execute + WriteState)
/// - 0xB0-0xBF: Crypto (Execute + Crypto)
/// - 0xC0-0xCF: SIMD (Execute only)
/// - 0xD0-0xEF: Reserved (None - always denied)
/// - 0xF0: NOP (Execute only)
/// - 0xF1: BREAK (Execute only)
/// - 0xFD: DEBUG (Execute + Debug)
/// - 0xFE: SYSCALL (Execute + SystemCall)
/// - 0xFF: Reserved (Execute only - placeholder)
inline constexpr std::array<Permission, 256> opcode_permission_table = [] {
    std::array<Permission, 256> table{};

    // Default: Execute for valid opcodes
    for (auto& perm : table) {
        perm = Permission::Execute;
    }

    // ========================================================================
    // Arithmetic opcodes (0x00-0x1F): Execute only
    // ========================================================================
    // Already set to Execute by default

    // ========================================================================
    // Bitwise opcodes (0x20-0x2F): Execute only
    // ========================================================================
    // Already set to Execute by default

    // ========================================================================
    // Comparison opcodes (0x30-0x3F): Execute only
    // ========================================================================
    // Already set to Execute by default

    // ========================================================================
    // Control Flow opcodes (0x40-0x5F): Execute only
    // ========================================================================
    // Already set to Execute by default

    // ========================================================================
    // Memory opcodes (0x60-0x68)
    // ========================================================================
    // LOAD opcodes (0x60-0x63): Require ReadMemory
    table[0x60] = Permission::Execute | Permission::ReadMemory;  // LOAD8
    table[0x61] = Permission::Execute | Permission::ReadMemory;  // LOAD16
    table[0x62] = Permission::Execute | Permission::ReadMemory;  // LOAD32
    table[0x63] = Permission::Execute | Permission::ReadMemory;  // LOAD64

    // STORE opcodes (0x64-0x67): Require WriteMemory
    table[0x64] = Permission::Execute | Permission::WriteMemory;  // STORE8
    table[0x65] = Permission::Execute | Permission::WriteMemory;  // STORE16
    table[0x66] = Permission::Execute | Permission::WriteMemory;  // STORE32
    table[0x67] = Permission::Execute | Permission::WriteMemory;  // STORE64

    // LEA (0x68): Require ReadMemory (address calculation)
    table[0x68] = Permission::Execute | Permission::ReadMemory;  // LEA

    // ========================================================================
    // Reserved range 1 (0x69-0x7F): Always denied
    // ========================================================================
    for (std::size_t i = 0x69; i <= 0x7F; ++i) {
        table[i] = Permission::None;
    }

    // ========================================================================
    // DataMove opcodes (0x80-0x8F): Execute only
    // ========================================================================
    // Already set to Execute by default

    // ========================================================================
    // Reserved range 2 (0x90-0x9F): Always denied
    // ========================================================================
    for (std::size_t i = 0x90; i <= 0x9F; ++i) {
        table[i] = Permission::None;
    }

    // ========================================================================
    // State opcodes (0xA0-0xAF)
    // ========================================================================
    // STATE_GET (0xA0-0xA7): Require ReadState
    for (std::size_t i = 0xA0; i <= 0xA7; ++i) {
        table[i] = Permission::Execute | Permission::ReadState;
    }

    // STATE_PUT (0xA8-0xAF): Require WriteState
    for (std::size_t i = 0xA8; i <= 0xAF; ++i) {
        table[i] = Permission::Execute | Permission::WriteState;
    }

    // ========================================================================
    // Crypto opcodes (0xB0-0xBF): Require Crypto
    // ========================================================================
    for (std::size_t i = 0xB0; i <= 0xBF; ++i) {
        table[i] = Permission::Execute | Permission::Crypto;
    }

    // ========================================================================
    // SIMD opcodes (0xC0-0xCF): Execute only
    // ========================================================================
    for (std::size_t i = 0xC0; i <= 0xCF; ++i) {
        table[i] = Permission::Execute;
    }

    // ========================================================================
    // Reserved range 3 (0xD0-0xEF): Always denied
    // ========================================================================
    for (std::size_t i = 0xD0; i <= 0xEF; ++i) {
        table[i] = Permission::None;
    }

    // ========================================================================
    // System opcodes (0xF0-0xFF)
    // ========================================================================
    // NOP (0xF0): Execute only (already set)
    // BREAK (0xF1): Execute only (already set)
    // 0xF2-0xFC: Execute only (already set)

    // DEBUG (0xFD): Require Debug
    table[0xFD] = Permission::Execute | Permission::Debug;

    // SYSCALL (0xFE): Require SystemCall
    table[0xFE] = Permission::Execute | Permission::SystemCall;

    // 0xFF: Reserved - Execute only (placeholder for future extension)
    // Already set to Execute by default

    return table;
}();

// ============================================================================
// Lookup Functions
// ============================================================================

/// @brief Get the permission required to execute an opcode (O(1) lookup)
///
/// @param opcode The opcode to query (0x00-0xFF)
/// @return The Permission flags required to execute this opcode
///
/// @par Performance
/// Direct array indexing - single memory load, ~1-2 CPU cycles.
///
/// @par Example
/// @code
/// Permission required = get_required_permission(0x64); // STORE8
/// // required == Permission::Execute | Permission::WriteMemory
/// @endcode
[[nodiscard]] constexpr Permission get_required_permission(std::uint8_t opcode) noexcept {
    return opcode_permission_table[opcode];
}

/// @brief Check if an opcode is authorized with the given permissions
///
/// @param opcode The opcode to check
/// @param granted The permissions granted to the Dot
/// @return true if all required permissions are present, false for reserved opcodes
///
/// @par Performance
/// Inline bitmask operation - ~3 CPU cycles.
///
/// @par Example
/// @code
/// Permission granted = Permission::Execute | Permission::ReadMemory;
/// bool ok = is_opcode_authorized(0x60, granted);  // LOAD8 - true
/// bool fail = is_opcode_authorized(0x64, granted);  // STORE8 - false
/// @endcode
[[nodiscard]] constexpr bool is_opcode_authorized(std::uint8_t opcode,
                                                  Permission granted) noexcept {
    const Permission required = get_required_permission(opcode);
    // Reserved opcodes (Permission::None) are never authorized
    if (required == Permission::None) {
        return false;
    }
    return has_permission(granted, required);
}

/// @brief Check if an opcode is authorized with a PermissionSet
///
/// @param opcode The opcode to check
/// @param permissions The PermissionSet to check against
/// @return true if all required permissions are present, false for reserved opcodes
[[nodiscard]] constexpr bool is_opcode_authorized(std::uint8_t opcode,
                                                  const PermissionSet& permissions) noexcept {
    const Permission required = get_required_permission(opcode);
    // Reserved opcodes (Permission::None) are never authorized
    if (required == Permission::None) {
        return false;
    }
    return permissions.has_permission(required);
}

/// @brief Check if an opcode is reserved (requires Permission::None)
///
/// Reserved opcodes always fail authorization, regardless of granted permissions.
///
/// @param opcode The opcode to check
/// @return true if the opcode is reserved
[[nodiscard]] constexpr bool is_reserved_opcode(std::uint8_t opcode) noexcept {
    return get_required_permission(opcode) == Permission::None;
}

// ============================================================================
// SecurityContext Integration
// ============================================================================

/// @brief Check opcode permission and log denied attempts
///
/// This function integrates with SecurityContext for permission checking
/// and audit logging. On denial, it logs an OpcodeDenied event.
///
/// @param opcode The opcode to check
/// @param ctx The SecurityContext to check against
/// @param context Optional context string for audit logging
/// @return SecurityContextError::Success if authorized,
///         SecurityContextError::PermissionDenied if denied
///
/// @par Side Effects
/// - Logs OpcodeDenied audit event on denial
///
/// @par Example
/// @code
/// SecurityContext ctx(limits, permissions);
/// if (auto err = check_opcode_permission(0x64, ctx, "dispatch");
///     err != SecurityContextError::Success) {
///     return ExecResult::CapabilityDenied;
/// }
/// @endcode
[[nodiscard]] inline SecurityContextError
check_opcode_permission(std::uint8_t opcode, SecurityContext& ctx,
                        std::string_view context = "") noexcept {
    const Permission required = get_required_permission(opcode);

    // Reserved opcodes are always denied
    if (required == Permission::None) {
        ctx.log_event(AuditEventType::OpcodeDenied, static_cast<std::uint64_t>(opcode),
                      context.empty() ? "reserved_opcode" : context);
        return SecurityContextError::PermissionDenied;
    }

    // Check if all required permissions are granted
    if (!ctx.can(required)) {
        ctx.log_event(AuditEventType::OpcodeDenied, static_cast<std::uint64_t>(opcode), context);
        return SecurityContextError::PermissionDenied;
    }

    return SecurityContextError::Success;
}

/// @brief Get human-readable name for an opcode category
///
/// @param opcode The opcode to describe
/// @return A string describing the opcode category
[[nodiscard]] constexpr std::string_view get_opcode_category(std::uint8_t opcode) noexcept {
    if (opcode <= 0x1F)
        return "Arithmetic";
    if (opcode <= 0x2F)
        return "Bitwise";
    if (opcode <= 0x3F)
        return "Comparison";
    if (opcode <= 0x5F)
        return "ControlFlow";
    if (opcode <= 0x63)
        return "Load";
    if (opcode <= 0x67)
        return "Store";
    if (opcode == 0x68)
        return "LEA";
    if (opcode <= 0x7F)
        return "Reserved";
    if (opcode <= 0x8F)
        return "DataMove";
    if (opcode <= 0x9F)
        return "Reserved";
    if (opcode <= 0xA7)
        return "StateGet";
    if (opcode <= 0xAF)
        return "StatePut";
    if (opcode <= 0xBF)
        return "Crypto";
    if (opcode <= 0xCF)
        return "SIMD";
    if (opcode <= 0xEF)
        return "Reserved";
    if (opcode == 0xF0)
        return "NOP";
    if (opcode == 0xF1)
        return "BREAK";
    if (opcode == 0xFD)
        return "DEBUG";
    if (opcode == 0xFE)
        return "SYSCALL";
    return "System";
}

}  // namespace dotvm::core::security
