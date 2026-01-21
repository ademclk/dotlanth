#pragma once

/// @file stdlib_types.hpp
/// @brief DSL-004 Standard Library type definitions
///
/// Defines core types for the stdlib system including:
/// - StdlibType: Type system for stdlib function signatures
/// - Syscall ID constants organized by module

#include <cstdint>

namespace dotvm::core::dsl::stdlib {

// ============================================================================
// Stdlib Type System
// ============================================================================

/// @brief Type system for stdlib function parameters and return values
///
/// These types map to the VM's value system but are used at compile-time
/// for type checking stdlib function signatures.
enum class StdlibType : std::uint8_t {
    Void = 0,    ///< No value (for functions with no return)
    Int = 1,     ///< 64-bit signed integer
    Float = 2,   ///< 64-bit IEEE 754 float
    Bool = 3,    ///< Boolean (true/false)
    String = 4,  ///< String handle
    Handle = 5,  ///< Generic memory handle
    Any = 6,     ///< Any type (for polymorphic functions)
};

/// @brief Convert StdlibType to string representation
[[nodiscard]] constexpr const char* to_string(StdlibType type) noexcept {
    switch (type) {
        case StdlibType::Void:
            return "void";
        case StdlibType::Int:
            return "int";
        case StdlibType::Float:
            return "float";
        case StdlibType::Bool:
            return "bool";
        case StdlibType::String:
            return "string";
        case StdlibType::Handle:
            return "handle";
        case StdlibType::Any:
            return "any";
    }
    return "unknown";
}

// ============================================================================
// Syscall ID Allocation
// ============================================================================

/// Syscall ID ranges for each stdlib module
///
/// Each module gets a 256-ID range (8-bit module prefix):
/// - 0x0001-0x00FF: prelude (auto-imported basics)
/// - 0x0100-0x01FF: io (filesystem operations)
/// - 0x0200-0x02FF: crypto (cryptographic operations)
/// - 0x0300-0x03FF: net (network operations)
/// - 0x0400-0x04FF: time (time operations)
/// - 0x0500-0x05FF: collections (data structures)
/// - 0x0600-0x06FF: string (string manipulation)
/// - 0x0700-0x07FF: math (mathematical functions)
/// - 0x0800-0x08FF: async (concurrency primitives)
/// - 0x0900-0x09FF: control (control flow patterns)
namespace syscall_id {

// ===== Prelude Module (0x0001-0x00FF) =====

inline constexpr std::uint16_t PRELUDE_PRINT = 0x0001;
inline constexpr std::uint16_t PRELUDE_ASSERT = 0x0002;
inline constexpr std::uint16_t PRELUDE_TYPE_OF = 0x0003;
inline constexpr std::uint16_t PRELUDE_LEN = 0x0004;
inline constexpr std::uint16_t PRELUDE_STR = 0x0005;
inline constexpr std::uint16_t PRELUDE_INT = 0x0006;
inline constexpr std::uint16_t PRELUDE_FLOAT = 0x0007;
// Type checking functions (0x0010-0x001F)
inline constexpr std::uint16_t PRELUDE_IS_INT = 0x0010;
inline constexpr std::uint16_t PRELUDE_IS_FLOAT = 0x0011;
inline constexpr std::uint16_t PRELUDE_IS_BOOL = 0x0012;
inline constexpr std::uint16_t PRELUDE_IS_STRING = 0x0013;
inline constexpr std::uint16_t PRELUDE_IS_HANDLE = 0x0014;

// ===== IO Module (0x0100-0x01FF) =====

inline constexpr std::uint16_t IO_FILE_READ = 0x0100;
inline constexpr std::uint16_t IO_FILE_WRITE = 0x0101;
inline constexpr std::uint16_t IO_FILE_EXISTS = 0x0102;
inline constexpr std::uint16_t IO_FILE_DELETE = 0x0103;
inline constexpr std::uint16_t IO_FILE_APPEND = 0x0104;
inline constexpr std::uint16_t IO_DIR_CREATE = 0x0105;
inline constexpr std::uint16_t IO_DIR_LIST = 0x0106;

// ===== Crypto Module (0x0200-0x02FF) =====

inline constexpr std::uint16_t CRYPTO_HASH_BLAKE3 = 0x0200;
inline constexpr std::uint16_t CRYPTO_HASH_SHA256 = 0x0201;
inline constexpr std::uint16_t CRYPTO_SIGN_ED25519 = 0x0210;
inline constexpr std::uint16_t CRYPTO_VERIFY_ED25519 = 0x0211;
inline constexpr std::uint16_t CRYPTO_ENCRYPT_AES = 0x0220;
inline constexpr std::uint16_t CRYPTO_DECRYPT_AES = 0x0221;
inline constexpr std::uint16_t CRYPTO_RANDOM_BYTES = 0x0230;

// ===== Network Module (0x0300-0x03FF) =====

inline constexpr std::uint16_t NET_HTTP_GET = 0x0300;
inline constexpr std::uint16_t NET_HTTP_POST = 0x0301;
inline constexpr std::uint16_t NET_HTTP_PUT = 0x0302;
inline constexpr std::uint16_t NET_HTTP_DELETE = 0x0303;

// ===== Time Module (0x0400-0x04FF) =====

inline constexpr std::uint16_t TIME_NOW = 0x0400;
inline constexpr std::uint16_t TIME_TIMESTAMP = 0x0401;
inline constexpr std::uint16_t TIME_DURATION = 0x0402;
inline constexpr std::uint16_t TIME_SLEEP = 0x0403;
inline constexpr std::uint16_t TIME_FORMAT = 0x0404;

// ===== Collections Module (0x0500-0x05FF) =====

// List operations (0x0500-0x051F)
inline constexpr std::uint16_t COLLECTIONS_LIST_NEW = 0x0500;
inline constexpr std::uint16_t COLLECTIONS_LIST_PUSH = 0x0501;
inline constexpr std::uint16_t COLLECTIONS_LIST_POP = 0x0502;
inline constexpr std::uint16_t COLLECTIONS_LIST_GET = 0x0503;
inline constexpr std::uint16_t COLLECTIONS_LIST_SET = 0x0504;
inline constexpr std::uint16_t COLLECTIONS_LIST_LEN = 0x0505;
inline constexpr std::uint16_t COLLECTIONS_LIST_CLEAR = 0x0506;
// Map operations (0x0520-0x053F)
inline constexpr std::uint16_t COLLECTIONS_MAP_NEW = 0x0520;
inline constexpr std::uint16_t COLLECTIONS_MAP_GET = 0x0521;
inline constexpr std::uint16_t COLLECTIONS_MAP_SET = 0x0522;
inline constexpr std::uint16_t COLLECTIONS_MAP_HAS = 0x0523;
inline constexpr std::uint16_t COLLECTIONS_MAP_DELETE = 0x0524;
inline constexpr std::uint16_t COLLECTIONS_MAP_KEYS = 0x0525;
inline constexpr std::uint16_t COLLECTIONS_MAP_VALUES = 0x0526;
// Set operations (0x0540-0x055F)
inline constexpr std::uint16_t COLLECTIONS_SET_NEW = 0x0540;
inline constexpr std::uint16_t COLLECTIONS_SET_ADD = 0x0541;
inline constexpr std::uint16_t COLLECTIONS_SET_HAS = 0x0542;
inline constexpr std::uint16_t COLLECTIONS_SET_REMOVE = 0x0543;
inline constexpr std::uint16_t COLLECTIONS_SET_LEN = 0x0544;

// ===== String Module (0x0600-0x06FF) =====

inline constexpr std::uint16_t STRING_CONCAT = 0x0600;
inline constexpr std::uint16_t STRING_SPLIT = 0x0601;
inline constexpr std::uint16_t STRING_JOIN = 0x0602;
inline constexpr std::uint16_t STRING_TRIM = 0x0603;
inline constexpr std::uint16_t STRING_UPPER = 0x0604;
inline constexpr std::uint16_t STRING_LOWER = 0x0605;
inline constexpr std::uint16_t STRING_STARTS_WITH = 0x0606;
inline constexpr std::uint16_t STRING_ENDS_WITH = 0x0607;
inline constexpr std::uint16_t STRING_CONTAINS = 0x0608;
inline constexpr std::uint16_t STRING_REPLACE = 0x0609;
inline constexpr std::uint16_t STRING_SUBSTR = 0x060A;
inline constexpr std::uint16_t STRING_LEN = 0x060B;
inline constexpr std::uint16_t STRING_CHAR_AT = 0x060C;

// ===== Math Module (0x0700-0x07FF) =====

// Basic math (0x0700-0x071F)
inline constexpr std::uint16_t MATH_ABS = 0x0700;
inline constexpr std::uint16_t MATH_MIN = 0x0701;
inline constexpr std::uint16_t MATH_MAX = 0x0702;
inline constexpr std::uint16_t MATH_FLOOR = 0x0703;
inline constexpr std::uint16_t MATH_CEIL = 0x0704;
inline constexpr std::uint16_t MATH_ROUND = 0x0705;
inline constexpr std::uint16_t MATH_SQRT = 0x0706;
inline constexpr std::uint16_t MATH_POW = 0x0707;
// Trigonometric (0x0720-0x073F)
inline constexpr std::uint16_t MATH_SIN = 0x0720;
inline constexpr std::uint16_t MATH_COS = 0x0721;
inline constexpr std::uint16_t MATH_TAN = 0x0722;
inline constexpr std::uint16_t MATH_ASIN = 0x0723;
inline constexpr std::uint16_t MATH_ACOS = 0x0724;
inline constexpr std::uint16_t MATH_ATAN = 0x0725;
inline constexpr std::uint16_t MATH_ATAN2 = 0x0726;
// Exponential/logarithmic (0x0740-0x075F)
inline constexpr std::uint16_t MATH_LOG = 0x0740;
inline constexpr std::uint16_t MATH_LOG10 = 0x0741;
inline constexpr std::uint16_t MATH_LOG2 = 0x0742;
inline constexpr std::uint16_t MATH_EXP = 0x0743;
// Constants (0x0760-0x077F) - these are inline values, not syscalls
inline constexpr std::uint16_t MATH_PI = 0x0760;
inline constexpr std::uint16_t MATH_E = 0x0761;

// ===== Async Module (0x0800-0x08FF) =====

inline constexpr std::uint16_t ASYNC_SPAWN = 0x0800;
inline constexpr std::uint16_t ASYNC_CHANNEL_NEW = 0x0801;
inline constexpr std::uint16_t ASYNC_CHANNEL_SEND = 0x0802;
inline constexpr std::uint16_t ASYNC_CHANNEL_RECV = 0x0803;
inline constexpr std::uint16_t ASYNC_AWAIT = 0x0804;

// ===== Control Module (0x0900-0x09FF) =====

inline constexpr std::uint16_t CONTROL_FOREACH = 0x0900;
inline constexpr std::uint16_t CONTROL_WHILE = 0x0901;
inline constexpr std::uint16_t CONTROL_MATCH = 0x0902;

}  // namespace syscall_id

// ============================================================================
// Inline Syscall Detection
// ============================================================================

/// @brief Check if a syscall ID indicates an inline implementation
///
/// Inline syscalls are handled at compile-time and don't generate SYSCALL
/// bytecode. They typically:
/// - Load constants (like math.PI, math.E)
/// - Perform type checks (is_int, is_float, etc.)
[[nodiscard]] constexpr bool is_inline_syscall(std::uint16_t id) noexcept {
    // Math constants are inline
    if (id >= syscall_id::MATH_PI && id <= syscall_id::MATH_E) {
        return true;
    }
    // Type check functions are inline (can be optimized to type analysis)
    if (id >= syscall_id::PRELUDE_IS_INT && id <= syscall_id::PRELUDE_IS_HANDLE) {
        return true;
    }
    return false;
}

}  // namespace dotvm::core::dsl::stdlib
