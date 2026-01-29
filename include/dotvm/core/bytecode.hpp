/// @file bytecode.hpp
/// @brief Bytecode file format definitions and parsing utilities.
///
/// This header defines the binary format for DotVM bytecode files, including:
/// - File header structure (48 bytes)
/// - Constant pool format and parsing
/// - Validation functions for security and correctness
/// - Little-endian read/write helpers
///
/// The bytecode format uses a simple layout:
/// ```
/// +------------------+
/// |    Header (48B)  |  Magic, version, architecture, section offsets
/// +------------------+
/// |  Constant Pool   |  Type-tagged constants (int64, float64)
/// +------------------+
/// |   Code Section   |  32-bit instructions, 4-byte aligned
/// +------------------+
/// ```
///
/// @see BytecodeHeader for the header structure
/// @see load_constant_pool for constant pool parsing

#pragma once

#include <array>
#include <bit>
#include <cstdint>
#include <cstring>
#include <expected>
#include <span>
#include <string_view>
#include <vector>

#include "arch_types.hpp"  // for Architecture enum
#include "value.hpp"

namespace dotvm::core {

// ============================================================================
// Bytecode Format Constants
// ============================================================================

/// @brief Constants defining the bytecode file format.
namespace bytecode {
/// @brief Magic bytes "DOTM" as little-endian u32.
inline constexpr std::uint32_t MAGIC = 0x4D54'4F44U;  // "DOTM" LE
/// @brief Magic bytes as byte array for validation.
inline constexpr std::array<std::uint8_t, 4> MAGIC_BYTES = {'D', 'O', 'T', 'M'};

/// @brief Current bytecode format version.
inline constexpr std::uint8_t CURRENT_VERSION = 26;
/// @brief Minimum supported bytecode version.
inline constexpr std::uint8_t MIN_SUPPORTED_VERSION = 26;
/// @brief Maximum supported bytecode version.
inline constexpr std::uint8_t MAX_SUPPORTED_VERSION = 26;

/// @brief Size of the bytecode file header in bytes.
inline constexpr std::size_t HEADER_SIZE = 48;

/// @brief No flags set.
inline constexpr std::uint16_t FLAG_NONE = 0x0000;
/// @brief Debug information included in bytecode.
inline constexpr std::uint16_t FLAG_DEBUG = 0x0001;
/// @brief Bytecode has been optimized.
inline constexpr std::uint16_t FLAG_OPTIMIZED = 0x0002;

/// @brief Constant pool type tag for 64-bit signed integers.
inline constexpr std::uint8_t CONST_TYPE_I64 = 0x01;
/// @brief Constant pool type tag for 64-bit IEEE 754 floats.
inline constexpr std::uint8_t CONST_TYPE_F64 = 0x02;
/// @brief Constant pool type tag for UTF-8 strings (reserved).
inline constexpr std::uint8_t CONST_TYPE_STRING = 0x03;

/// @brief Maximum string length in constant pool (16 MB).
inline constexpr std::uint32_t MAX_STRING_LENGTH = 0x01'00'00'00U;

/// @brief Maximum number of constant pool entries (1M, prevents DoS).
inline constexpr std::uint32_t MAX_CONST_POOL_ENTRIES = 0x00'10'00'00U;

/// @brief Maximum bytecode file size (2 GB).
inline constexpr std::size_t MAX_FILE_SIZE = 0x80'00'00'00ULL;

/// @brief Instruction alignment requirement (4 bytes).
inline constexpr std::size_t INSTRUCTION_ALIGNMENT = 4;
}  // namespace bytecode

// ============================================================================
// Enumerations
// ============================================================================

// Note: Architecture enum is defined in arch_types.hpp

/// @brief Constant entry type tag for the constant pool.
enum class ConstantType : std::uint8_t {
    Int64 = 0x01,    ///< 64-bit signed integer constant.
    Float64 = 0x02,  ///< 64-bit IEEE 754 floating point constant.
    String = 0x03    ///< UTF-8 string constant (reserved, not yet implemented).
};

/// @brief Error codes for bytecode validation and parsing.
enum class BytecodeError : std::uint8_t {
    Success = 0,

    // Header validation errors
    InvalidMagic = 1,
    UnsupportedVersion = 2,
    InvalidArchitecture = 3,
    InvalidFlags = 4,

    // Section validation errors
    EntryPointOutOfBounds = 5,
    ConstPoolOutOfBounds = 6,
    CodeSectionOutOfBounds = 7,
    SectionsOverlap = 8,

    // Constant pool errors
    InvalidConstantType = 9,
    StringTooLong = 10,
    ConstPoolCorrupted = 11,
    ConstPoolTruncated = 12,

    // General errors
    FileTooSmall = 13,
    UnexpectedEof = 14,

    // Security-related errors
    IntegerOutOfRange = 15,     // 64-bit integer doesn't fit in 48-bit Value
    EntryPointNotAligned = 16,  // Entry point not aligned to instruction boundary
    TooManyConstants = 17,      // Constant pool entry count exceeds maximum
    FileTooLarge = 18,          // Bytecode file exceeds maximum size
    StringNotSupported = 19     // String constants not yet implemented
};

/// @brief Returns a human-readable error message for a BytecodeError.
/// @param err The error code to convert.
/// @return A string_view containing the error description.
[[nodiscard]] constexpr std::string_view to_string(BytecodeError err) noexcept {
    switch (err) {
        case BytecodeError::Success:
            return "";
        case BytecodeError::InvalidMagic:
            return "Invalid magic bytes - expected 'DOTM'";
        case BytecodeError::UnsupportedVersion:
            return "Unsupported bytecode version";
        case BytecodeError::InvalidArchitecture:
            return "Invalid architecture value";
        case BytecodeError::InvalidFlags:
            return "Invalid or reserved flags set";
        case BytecodeError::EntryPointOutOfBounds:
            return "Entry point outside code section";
        case BytecodeError::ConstPoolOutOfBounds:
            return "Constant pool extends beyond file";
        case BytecodeError::CodeSectionOutOfBounds:
            return "Code section extends beyond file";
        case BytecodeError::SectionsOverlap:
            return "Sections overlap";
        case BytecodeError::InvalidConstantType:
            return "Invalid constant type tag";
        case BytecodeError::StringTooLong:
            return "String constant exceeds maximum length";
        case BytecodeError::ConstPoolCorrupted:
            return "Constant pool data corrupted";
        case BytecodeError::ConstPoolTruncated:
            return "Constant pool truncated";
        case BytecodeError::FileTooSmall:
            return "File too small for header";
        case BytecodeError::UnexpectedEof:
            return "Unexpected end of file";
        case BytecodeError::IntegerOutOfRange:
            return "Integer constant out of 48-bit range";
        case BytecodeError::EntryPointNotAligned:
            return "Entry point not aligned to instruction boundary";
        case BytecodeError::TooManyConstants:
            return "Constant pool entry count exceeds maximum";
        case BytecodeError::FileTooLarge:
            return "Bytecode file exceeds maximum size";
        case BytecodeError::StringNotSupported:
            return "String constants not yet supported";
    }
    return "Unknown error";
}

/// @brief Result type alias for bytecode operations.
/// @tparam T The success value type.
template <typename T>
using BytecodeResult = std::expected<T, BytecodeError>;

// ============================================================================
// Little-Endian Read/Write Helpers
// ============================================================================

/// @brief Little-endian byte order conversion utilities.
namespace endian {

/// @brief Reads a 16-bit unsigned integer from little-endian bytes.
/// @param data Pointer to at least 2 bytes of data.
/// @return The decoded value.
[[nodiscard]] constexpr std::uint16_t read_u16_le(const std::uint8_t* data) noexcept {
    return static_cast<std::uint16_t>(static_cast<unsigned>(data[0]) |
                                      (static_cast<unsigned>(data[1]) << 8));
}

/// @brief Reads a 32-bit unsigned integer from little-endian bytes.
/// @param data Pointer to at least 4 bytes of data.
/// @return The decoded value.
[[nodiscard]] constexpr std::uint32_t read_u32_le(const std::uint8_t* data) noexcept {
    return static_cast<std::uint32_t>(data[0]) | (static_cast<std::uint32_t>(data[1]) << 8) |
           (static_cast<std::uint32_t>(data[2]) << 16) |
           (static_cast<std::uint32_t>(data[3]) << 24);
}

/// @brief Reads a 64-bit unsigned integer from little-endian bytes.
/// @param data Pointer to at least 8 bytes of data.
/// @return The decoded value.
[[nodiscard]] constexpr std::uint64_t read_u64_le(const std::uint8_t* data) noexcept {
    return static_cast<std::uint64_t>(data[0]) | (static_cast<std::uint64_t>(data[1]) << 8) |
           (static_cast<std::uint64_t>(data[2]) << 16) |
           (static_cast<std::uint64_t>(data[3]) << 24) |
           (static_cast<std::uint64_t>(data[4]) << 32) |
           (static_cast<std::uint64_t>(data[5]) << 40) |
           (static_cast<std::uint64_t>(data[6]) << 48) |
           (static_cast<std::uint64_t>(data[7]) << 56);
}

/// @brief Reads a 64-bit signed integer from little-endian bytes.
/// @param data Pointer to at least 8 bytes of data.
/// @return The decoded value.
[[nodiscard]] constexpr std::int64_t read_i64_le(const std::uint8_t* data) noexcept {
    return static_cast<std::int64_t>(read_u64_le(data));
}

/// @brief Reads a 64-bit IEEE 754 double from little-endian bytes.
/// @param data Pointer to at least 8 bytes of data.
/// @return The decoded value.
[[nodiscard]] constexpr double read_f64_le(const std::uint8_t* data) noexcept {
    return std::bit_cast<double>(read_u64_le(data));
}

/// @brief Writes a 16-bit unsigned integer as little-endian bytes.
/// @param data Pointer to at least 2 bytes of storage.
/// @param value The value to encode.
constexpr void write_u16_le(std::uint8_t* data, std::uint16_t value) noexcept {
    data[0] = static_cast<std::uint8_t>(value);
    data[1] = static_cast<std::uint8_t>(value >> 8);
}

/// @brief Writes a 32-bit unsigned integer as little-endian bytes.
/// @param data Pointer to at least 4 bytes of storage.
/// @param value The value to encode.
constexpr void write_u32_le(std::uint8_t* data, std::uint32_t value) noexcept {
    data[0] = static_cast<std::uint8_t>(value);
    data[1] = static_cast<std::uint8_t>(value >> 8);
    data[2] = static_cast<std::uint8_t>(value >> 16);
    data[3] = static_cast<std::uint8_t>(value >> 24);
}

/// @brief Writes a 64-bit unsigned integer as little-endian bytes.
/// @param data Pointer to at least 8 bytes of storage.
/// @param value The value to encode.
constexpr void write_u64_le(std::uint8_t* data, std::uint64_t value) noexcept {
    data[0] = static_cast<std::uint8_t>(value);
    data[1] = static_cast<std::uint8_t>(value >> 8);
    data[2] = static_cast<std::uint8_t>(value >> 16);
    data[3] = static_cast<std::uint8_t>(value >> 24);
    data[4] = static_cast<std::uint8_t>(value >> 32);
    data[5] = static_cast<std::uint8_t>(value >> 40);
    data[6] = static_cast<std::uint8_t>(value >> 48);
    data[7] = static_cast<std::uint8_t>(value >> 56);
}

/// @brief Writes a 64-bit signed integer as little-endian bytes.
/// @param data Pointer to at least 8 bytes of storage.
/// @param value The value to encode.
constexpr void write_i64_le(std::uint8_t* data, std::int64_t value) noexcept {
    write_u64_le(data, static_cast<std::uint64_t>(value));
}

/// @brief Writes a 64-bit IEEE 754 double as little-endian bytes.
/// @param data Pointer to at least 8 bytes of storage.
/// @param value The value to encode.
constexpr void write_f64_le(std::uint8_t* data, double value) noexcept {
    write_u64_le(data, std::bit_cast<std::uint64_t>(value));
}

}  // namespace endian

// ============================================================================
// BytecodeHeader Structure
// ============================================================================

/// Bytecode file header (48 bytes)
///
/// Binary layout:
/// Offset  Size  Field
/// 0       4     magic[4]           "DOTV"
/// 4       1     version            26
/// 5       1     arch               Arch32=0, Arch64=1
/// 6       2     flags              DEBUG=0x0001, OPTIMIZED=0x0002
/// 8       8     entry_point        Entry point offset within code section
/// 16      8     const_pool_offset  Constant pool section offset
/// 24      8     const_pool_size    Constant pool section size
/// 32      8     code_offset        Code section offset
/// 40      8     code_size          Code section size
struct BytecodeHeader {
    std::array<std::uint8_t, 4> magic;
    std::uint8_t version;
    Architecture arch;
    std::uint16_t flags;
    std::uint64_t entry_point;
    std::uint64_t const_pool_offset;
    std::uint64_t const_pool_size;
    std::uint64_t code_offset;
    std::uint64_t code_size;

    /// Check if DEBUG flag is set
    [[nodiscard]] constexpr bool is_debug() const noexcept {
        return (flags & bytecode::FLAG_DEBUG) != 0;
    }

    /// Check if OPTIMIZED flag is set
    [[nodiscard]] constexpr bool is_optimized() const noexcept {
        return (flags & bytecode::FLAG_OPTIMIZED) != 0;
    }

    constexpr bool operator==(const BytecodeHeader&) const noexcept = default;
};

static_assert(sizeof(BytecodeHeader) == 48, "BytecodeHeader must be exactly 48 bytes");
static_assert(offsetof(BytecodeHeader, magic) == 0);
static_assert(offsetof(BytecodeHeader, version) == 4);
static_assert(offsetof(BytecodeHeader, arch) == 5);
static_assert(offsetof(BytecodeHeader, flags) == 6);
static_assert(offsetof(BytecodeHeader, entry_point) == 8);
static_assert(offsetof(BytecodeHeader, const_pool_offset) == 16);
static_assert(offsetof(BytecodeHeader, const_pool_size) == 24);
static_assert(offsetof(BytecodeHeader, code_offset) == 32);
static_assert(offsetof(BytecodeHeader, code_size) == 40);

// ============================================================================
// ConstantPoolHeader Structure
// ============================================================================

/// @brief Constant pool header (4 bytes).
///
/// Precedes the constant pool data, specifying the number of entries.
struct ConstantPoolHeader {
    /// @brief Number of constant entries in the pool.
    std::uint32_t entry_count;

    constexpr bool operator==(const ConstantPoolHeader&) const noexcept = default;
};

static_assert(sizeof(ConstantPoolHeader) == 4, "ConstantPoolHeader must be exactly 4 bytes");

// ============================================================================
// Validation Functions
// ============================================================================

/// @brief Validates the magic bytes match "DOTM".
/// @param magic Span of exactly 4 bytes to validate.
/// @return true if magic bytes match, false otherwise.
[[nodiscard]] constexpr bool validate_magic(std::span<const std::uint8_t, 4> magic) noexcept {
    return magic[0] == bytecode::MAGIC_BYTES[0] && magic[1] == bytecode::MAGIC_BYTES[1] &&
           magic[2] == bytecode::MAGIC_BYTES[2] && magic[3] == bytecode::MAGIC_BYTES[3];
}

/// @brief Validates the bytecode version is supported.
/// @param version The version number from the header.
/// @return true if version is within supported range.
[[nodiscard]] constexpr bool validate_version(std::uint8_t version) noexcept {
    return version >= bytecode::MIN_SUPPORTED_VERSION && version <= bytecode::MAX_SUPPORTED_VERSION;
}

/// @brief Validates the architecture value is recognized.
/// @param arch The architecture enum value from the header.
/// @return true if architecture is valid (Arch32 or Arch64).
/// @note Uses is_valid_architecture from arch_types.hpp
[[nodiscard]] constexpr bool validate_architecture(Architecture arch) noexcept {
    return is_valid_architecture(arch);
}

/// @brief Checks if section bounds are valid (no overflow, within file size).
/// @param offset Section start offset in file.
/// @param size Section size in bytes.
/// @param total_file_size Total file size for bounds checking.
/// @return true if the section fits within the file without overflow.
[[nodiscard]] constexpr bool validate_section_bounds(std::uint64_t offset, std::uint64_t size,
                                                     std::size_t total_file_size) noexcept {
    // Check for overflow
    if (offset > static_cast<std::uint64_t>(total_file_size))
        return false;
    if (size > static_cast<std::uint64_t>(total_file_size))
        return false;
    if (offset + size < offset)
        return false;  // Overflow check
    return offset + size <= static_cast<std::uint64_t>(total_file_size);
}

/// @brief Checks if two sections overlap.
/// @param offset1 First section start offset.
/// @param size1 First section size.
/// @param offset2 Second section start offset.
/// @param size2 Second section size.
/// @return true if sections overlap.
/// @note Overflow in offset + size is treated as overlap (conservative approach).
[[nodiscard]] constexpr bool sections_overlap(std::uint64_t offset1, std::uint64_t size1,
                                              std::uint64_t offset2, std::uint64_t size2) noexcept {
    // Empty sections never overlap
    if (size1 == 0 || size2 == 0)
        return false;

    // Check for overflow in end calculations
    // If overflow occurs, treat as overlap (conservative/safe)
    if (offset1 > UINT64_MAX - size1)
        return true;
    if (offset2 > UINT64_MAX - size2)
        return true;

    std::uint64_t end1 = offset1 + size1;
    std::uint64_t end2 = offset2 + size2;

    // Two ranges [a,b) and [c,d) overlap iff a < d && c < b
    return offset1 < end2 && offset2 < end1;
}

/// @brief Mask of valid flag bits (DEBUG | OPTIMIZED).
inline constexpr std::uint16_t VALID_FLAGS_MASK = bytecode::FLAG_DEBUG | bytecode::FLAG_OPTIMIZED;

/// @brief Performs full header validation against file size.
/// @param header The header to validate.
/// @param total_file_size Total file size for bounds checking.
/// @return BytecodeError::Success on valid header, appropriate error otherwise.
[[nodiscard]] constexpr BytecodeError validate_header(const BytecodeHeader& header,
                                                      std::size_t total_file_size) noexcept {
    // Validate file size isn't too large (prevents DoS)
    if (total_file_size > bytecode::MAX_FILE_SIZE) {
        return BytecodeError::FileTooLarge;
    }

    // Validate magic
    if (!validate_magic(std::span<const std::uint8_t, 4>{header.magic})) {
        return BytecodeError::InvalidMagic;
    }

    // Validate version
    if (!validate_version(header.version)) {
        return BytecodeError::UnsupportedVersion;
    }

    // Validate architecture
    if (!validate_architecture(header.arch)) {
        return BytecodeError::InvalidArchitecture;
    }

    // Validate flags (reject unknown/reserved flags)
    if ((header.flags & ~VALID_FLAGS_MASK) != 0) {
        return BytecodeError::InvalidFlags;
    }

    // Validate constant pool bounds
    if (!validate_section_bounds(header.const_pool_offset, header.const_pool_size,
                                 total_file_size)) {
        return BytecodeError::ConstPoolOutOfBounds;
    }

    // Validate code section bounds
    if (!validate_section_bounds(header.code_offset, header.code_size, total_file_size)) {
        return BytecodeError::CodeSectionOutOfBounds;
    }

    // Validate sections don't overlap with header
    if (header.const_pool_size > 0 && header.const_pool_offset < bytecode::HEADER_SIZE) {
        return BytecodeError::ConstPoolOutOfBounds;
    }
    if (header.code_size > 0 && header.code_offset < bytecode::HEADER_SIZE) {
        return BytecodeError::CodeSectionOutOfBounds;
    }

    // Validate entry point (must be within code section and aligned)
    if (header.code_size == 0) {
        if (header.entry_point != 0) {
            return BytecodeError::EntryPointOutOfBounds;
        }
    } else {
        if (header.entry_point >= header.code_size) {
            return BytecodeError::EntryPointOutOfBounds;
        }
        // Validate entry point is instruction-aligned
        if (header.entry_point % bytecode::INSTRUCTION_ALIGNMENT != 0) {
            return BytecodeError::EntryPointNotAligned;
        }
    }

    // Check for section overlap between const_pool and code
    if (sections_overlap(header.const_pool_offset, header.const_pool_size, header.code_offset,
                         header.code_size)) {
        return BytecodeError::SectionsOverlap;
    }

    return BytecodeError::Success;
}

// ============================================================================
// Header Read/Write Functions
// ============================================================================

/// @brief Reads a bytecode header from raw bytes.
/// @param data Span containing at least HEADER_SIZE bytes.
/// @return The parsed header or a BytecodeError.
[[nodiscard]] constexpr BytecodeResult<BytecodeHeader>
read_header(std::span<const std::uint8_t> data) noexcept {
    if (data.size() < bytecode::HEADER_SIZE) {
        return std::unexpected(BytecodeError::FileTooSmall);
    }

    const std::uint8_t* ptr = data.data();

    BytecodeHeader header{};

    // Read magic (offset 0)
    header.magic[0] = ptr[0];
    header.magic[1] = ptr[1];
    header.magic[2] = ptr[2];
    header.magic[3] = ptr[3];

    // Read version (offset 4)
    header.version = ptr[4];

    // Read arch (offset 5)
    header.arch = static_cast<Architecture>(ptr[5]);

    // Read flags (offset 6, 2 bytes LE)
    header.flags = endian::read_u16_le(ptr + 6);

    // Read entry_point (offset 8, 8 bytes LE)
    header.entry_point = endian::read_u64_le(ptr + 8);

    // Read const_pool_offset (offset 16)
    header.const_pool_offset = endian::read_u64_le(ptr + 16);

    // Read const_pool_size (offset 24)
    header.const_pool_size = endian::read_u64_le(ptr + 24);

    // Read code_offset (offset 32)
    header.code_offset = endian::read_u64_le(ptr + 32);

    // Read code_size (offset 40)
    header.code_size = endian::read_u64_le(ptr + 40);

    return header;
}

/// @brief Writes a bytecode header to a byte array.
/// @param header The header to serialize.
/// @return A 48-byte array containing the serialized header.
[[nodiscard]] constexpr std::array<std::uint8_t, bytecode::HEADER_SIZE>
write_header(const BytecodeHeader& header) noexcept {
    std::array<std::uint8_t, bytecode::HEADER_SIZE> data{};
    std::uint8_t* ptr = data.data();

    // Write magic (offset 0)
    ptr[0] = header.magic[0];
    ptr[1] = header.magic[1];
    ptr[2] = header.magic[2];
    ptr[3] = header.magic[3];

    // Write version (offset 4)
    ptr[4] = header.version;

    // Write arch (offset 5)
    ptr[5] = static_cast<std::uint8_t>(header.arch);

    // Write flags (offset 6)
    endian::write_u16_le(ptr + 6, header.flags);

    // Write entry_point (offset 8)
    endian::write_u64_le(ptr + 8, header.entry_point);

    // Write const_pool_offset (offset 16)
    endian::write_u64_le(ptr + 16, header.const_pool_offset);

    // Write const_pool_size (offset 24)
    endian::write_u64_le(ptr + 24, header.const_pool_size);

    // Write code_offset (offset 32)
    endian::write_u64_le(ptr + 32, header.code_offset);

    // Write code_size (offset 40)
    endian::write_u64_le(ptr + 40, header.code_size);

    return data;
}

/// @brief Creates a header with the given parameters and default magic/version.
/// @param arch Target architecture (Arch32 or Arch64).
/// @param flags Bytecode flags (FLAG_DEBUG, FLAG_OPTIMIZED).
/// @param entry_point Entry point offset within code section.
/// @param const_pool_offset Constant pool section offset in file.
/// @param const_pool_size Constant pool section size in bytes.
/// @param code_offset Code section offset in file.
/// @param code_size Code section size in bytes.
/// @return A fully initialized BytecodeHeader.
[[nodiscard]] constexpr BytecodeHeader
make_header(Architecture arch, std::uint16_t flags, std::uint64_t entry_point,
            std::uint64_t const_pool_offset, std::uint64_t const_pool_size,
            std::uint64_t code_offset, std::uint64_t code_size) noexcept {
    return BytecodeHeader{.magic = bytecode::MAGIC_BYTES,
                          .version = bytecode::CURRENT_VERSION,
                          .arch = arch,
                          .flags = flags,
                          .entry_point = entry_point,
                          .const_pool_offset = const_pool_offset,
                          .const_pool_size = const_pool_size,
                          .code_offset = code_offset,
                          .code_size = code_size};
}

// ============================================================================
// VM Configuration Extraction
// ============================================================================

// Forward declaration for VmConfig (full definition in vm_context.hpp)
struct VmConfig;

/// Extract VM configuration from a bytecode header
///
/// Creates a VmConfig suitable for initializing a VmContext from the
/// information contained in a BytecodeHeader. This function extracts:
/// - Target architecture (Arch32 or Arch64)
/// - Debug mode (from FLAG_DEBUG, maps to strict_overflow)
///
/// @param header The bytecode header to extract from
/// @return A VmConfig configured for the bytecode
/// @note This returns a partial VmConfig - the caller should set max_memory
inline auto extract_vm_config(const BytecodeHeader& header) noexcept {
    struct VmConfigPartial {
        Architecture arch;
        bool strict_overflow;
    };
    return VmConfigPartial{.arch = header.arch, .strict_overflow = header.is_debug()};
}

// ============================================================================
// Constant Pool Functions
// ============================================================================

/// @brief Reads the constant pool header from data.
/// @param data Span containing at least 4 bytes.
/// @return The parsed header or a BytecodeError.
[[nodiscard]] constexpr BytecodeResult<ConstantPoolHeader>
read_const_pool_header(std::span<const std::uint8_t> data) noexcept {
    if (data.size() < sizeof(ConstantPoolHeader)) {
        return std::unexpected(BytecodeError::ConstPoolTruncated);
    }

    ConstantPoolHeader header{};
    header.entry_count = endian::read_u32_le(data.data());
    return header;
}

/// @brief Minimum value for 48-bit signed integer (used by NaN-boxed Value).
inline constexpr std::int64_t MIN_VALUE_INT = -(1LL << 47);
/// @brief Maximum value for 48-bit signed integer (used by NaN-boxed Value).
inline constexpr std::int64_t MAX_VALUE_INT = (1LL << 47) - 1;

/// @brief Checks if a 64-bit integer fits in the 48-bit range supported by Value.
/// @param value The integer to validate.
/// @return true if value is within [-2^47, 2^47-1].
[[nodiscard]] inline constexpr bool is_valid_value_int(std::int64_t value) noexcept {
    return value >= MIN_VALUE_INT && value <= MAX_VALUE_INT;
}

/// @brief Reads a single constant entry from data.
/// @param data Span containing the constant entry data.
/// @return A pair of (Value, bytes_consumed) or a BytecodeError.
[[nodiscard]] inline BytecodeResult<std::pair<Value, std::size_t>>
read_constant_entry(std::span<const std::uint8_t> data) noexcept {
    if (data.empty()) {
        return std::unexpected(BytecodeError::ConstPoolTruncated);
    }

    const std::uint8_t type_tag = data[0];
    std::size_t bytes_consumed = 1;

    switch (type_tag) {
        case bytecode::CONST_TYPE_I64: {
            if (data.size() < 9) {
                return std::unexpected(BytecodeError::ConstPoolTruncated);
            }
            std::int64_t value = endian::read_i64_le(data.data() + 1);
            // Validate integer fits in 48-bit range used by Value
            if (!is_valid_value_int(value)) {
                return std::unexpected(BytecodeError::IntegerOutOfRange);
            }
            bytes_consumed = 9;
            return std::pair{Value::from_int(value), bytes_consumed};
        }

        case bytecode::CONST_TYPE_F64: {
            if (data.size() < 9) {
                return std::unexpected(BytecodeError::ConstPoolTruncated);
            }
            double value = endian::read_f64_le(data.data() + 1);
            bytes_consumed = 9;
            return std::pair{Value::from_float(value), bytes_consumed};
        }

        case bytecode::CONST_TYPE_STRING: {
            // String constants are not yet implemented - return explicit error
            // rather than silently returning nil, which could cause subtle bugs
            return std::unexpected(BytecodeError::StringNotSupported);
        }

        default:
            return std::unexpected(BytecodeError::InvalidConstantType);
    }
}

/// @brief Loads all constants from pool data into a vector of Values.
/// @param pool_data Span containing the entire constant pool section.
/// @return A vector of Values or a BytecodeError.
[[nodiscard]] inline BytecodeResult<std::vector<Value>>
load_constant_pool(std::span<const std::uint8_t> pool_data) noexcept {
    if (pool_data.size() < sizeof(ConstantPoolHeader)) {
        if (pool_data.empty()) {
            // Empty pool is valid - return empty vector
            return std::vector<Value>{};
        }
        return std::unexpected(BytecodeError::ConstPoolTruncated);
    }

    auto header_result = read_const_pool_header(pool_data);
    if (!header_result) {
        return std::unexpected(header_result.error());
    }

    // Validate entry count to prevent DoS via massive memory allocation
    if (header_result->entry_count > bytecode::MAX_CONST_POOL_ENTRIES) {
        return std::unexpected(BytecodeError::TooManyConstants);
    }

    // Additional sanity check: entry count should be reasonable given pool size
    // Minimum entry size is 1 (type tag only, though invalid), realistically 9 bytes
    // This prevents allocating huge vectors for small data sizes
    const std::size_t data_size = pool_data.size() - sizeof(ConstantPoolHeader);
    if (header_result->entry_count > data_size) {
        return std::unexpected(BytecodeError::ConstPoolCorrupted);
    }

    std::vector<Value> constants;
    constants.reserve(header_result->entry_count);

    std::size_t offset = sizeof(ConstantPoolHeader);

    for (std::uint32_t i = 0; i < header_result->entry_count; ++i) {
        if (offset >= pool_data.size()) {
            return std::unexpected(BytecodeError::ConstPoolTruncated);
        }

        auto entry_result = read_constant_entry(pool_data.subspan(offset));
        if (!entry_result) {
            return std::unexpected(entry_result.error());
        }

        constants.push_back(entry_result->first);

        // Check for offset overflow before incrementing
        if (offset > SIZE_MAX - entry_result->second) {
            return std::unexpected(BytecodeError::ConstPoolCorrupted);
        }
        offset += entry_result->second;
    }

    return constants;
}

/// @brief Writes the constant pool header to a byte array.
/// @param header The header to serialize.
/// @return A 4-byte array containing the serialized header.
[[nodiscard]] constexpr std::array<std::uint8_t, 4>
write_const_pool_header(const ConstantPoolHeader& header) noexcept {
    std::array<std::uint8_t, 4> data{};
    endian::write_u32_le(data.data(), header.entry_count);
    return data;
}

// ============================================================================
// Execution-Time Validation Functions
// ============================================================================

/// @brief Constraints extracted from bytecode header for execution validation.
///
/// Contains the essential parameters needed to validate execution-time
/// operations like jump targets and program counter bounds.
struct ExecutionConstraints {
    std::uint64_t code_size;    ///< Size of code section in bytes
    std::uint64_t entry_point;  ///< Initial program counter
    Architecture arch;          ///< Target architecture
    bool debug_mode;            ///< Debug mode enabled (strict checks)
};

/// @brief Extracts execution constraints from a validated header.
/// @param header The validated bytecode header.
/// @return ExecutionConstraints for runtime validation.
[[nodiscard]] constexpr ExecutionConstraints
extract_execution_constraints(const BytecodeHeader& header) noexcept {
    return ExecutionConstraints{.code_size = header.code_size,
                                .entry_point = header.entry_point,
                                .arch = header.arch,
                                .debug_mode = header.is_debug()};
}

/// Validate that a program counter is within bounds and properly aligned
///
/// @param pc Program counter (byte offset in code section)
/// @param code_size Total size of code section
/// @return BytecodeError::Success if valid, appropriate error otherwise
[[nodiscard]] constexpr BytecodeError validate_pc(std::uint64_t pc,
                                                  std::uint64_t code_size) noexcept {
    if (pc >= code_size) {
        return BytecodeError::EntryPointOutOfBounds;
    }
    if (pc % bytecode::INSTRUCTION_ALIGNMENT != 0) {
        return BytecodeError::EntryPointNotAligned;
    }
    return BytecodeError::Success;
}

/// Validate that a program counter is valid given execution constraints
///
/// @param pc Program counter (byte offset in code section)
/// @param constraints Execution constraints from bytecode header
/// @return BytecodeError::Success if valid, appropriate error otherwise
[[nodiscard]] constexpr BytecodeError
validate_pc(std::uint64_t pc, const ExecutionConstraints& constraints) noexcept {
    return validate_pc(pc, constraints.code_size);
}

/// Validate that a constant pool index is within bounds
///
/// @param index Index into the constant pool
/// @param pool_size Number of entries in the constant pool
/// @return true if index is valid, false otherwise
[[nodiscard]] constexpr bool is_valid_const_index(std::uint32_t index,
                                                  std::uint32_t pool_size) noexcept {
    return index < pool_size;
}

/// Validate that a jump offset produces a valid target
///
/// @param current_pc Current program counter
/// @param offset Signed offset to add (may be negative for backward jumps)
/// @param code_size Total size of code section
/// @return BytecodeError::Success if valid, appropriate error otherwise
[[nodiscard]] constexpr BytecodeError validate_jump_target(std::uint64_t current_pc,
                                                           std::int32_t offset,
                                                           std::uint64_t code_size) noexcept {
    // Compute target PC, handling signed offset
    std::int64_t target = static_cast<std::int64_t>(current_pc) + offset;

    // Check for underflow (negative target)
    if (target < 0) {
        return BytecodeError::EntryPointOutOfBounds;
    }

    // Delegate to standard PC validation
    return validate_pc(static_cast<std::uint64_t>(target), code_size);
}

/// Validate that a register index is within valid range
///
/// @param reg Register index (0-255)
/// @return true if valid, false otherwise
[[nodiscard]] constexpr bool is_valid_register(std::uint8_t reg) noexcept {
    // All 8-bit values are valid register indices (0-255)
    return true;
}

}  // namespace dotvm::core
