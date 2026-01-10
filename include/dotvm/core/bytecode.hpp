#pragma once

#include <array>
#include <bit>
#include <cstdint>
#include <cstring>
#include <expected>
#include <span>
#include <string_view>
#include <vector>

#include "value.hpp"

namespace dotvm::core {

// ============================================================================
// Bytecode Format Constants
// ============================================================================

namespace bytecode {
    // Magic bytes "DOTV" as little-endian u32
    inline constexpr std::uint32_t MAGIC = 0x5654'4F44U;  // "DOTV" LE
    inline constexpr std::array<std::uint8_t, 4> MAGIC_BYTES = {'D', 'O', 'T', 'V'};

    // Version
    inline constexpr std::uint8_t CURRENT_VERSION = 26;
    inline constexpr std::uint8_t MIN_SUPPORTED_VERSION = 26;
    inline constexpr std::uint8_t MAX_SUPPORTED_VERSION = 26;

    // Header size
    inline constexpr std::size_t HEADER_SIZE = 48;

    // Flags (bitfield)
    inline constexpr std::uint16_t FLAG_NONE = 0x0000;
    inline constexpr std::uint16_t FLAG_DEBUG = 0x0001;
    inline constexpr std::uint16_t FLAG_OPTIMIZED = 0x0002;

    // Constant pool type tags
    inline constexpr std::uint8_t CONST_TYPE_I64 = 0x01;
    inline constexpr std::uint8_t CONST_TYPE_F64 = 0x02;
    inline constexpr std::uint8_t CONST_TYPE_STRING = 0x03;

    // Maximum string length in constant pool (16 MB)
    inline constexpr std::uint32_t MAX_STRING_LENGTH = 0x01'00'00'00U;
} // namespace bytecode

// ============================================================================
// Enumerations
// ============================================================================

/// Target architecture for bytecode
enum class Architecture : std::uint8_t {
    Arch32 = 0,
    Arch64 = 1
};

/// Constant entry type tag
enum class ConstantType : std::uint8_t {
    Int64 = 0x01,
    Float64 = 0x02,
    String = 0x03
};

/// Error codes for bytecode validation and parsing
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
    UnexpectedEof = 14
};

/// Returns a human-readable error message for a BytecodeError
[[nodiscard]] constexpr std::string_view to_string(BytecodeError err) noexcept {
    switch (err) {
        case BytecodeError::Success:
            return "";
        case BytecodeError::InvalidMagic:
            return "Invalid magic bytes - expected 'DOTV'";
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
    }
    return "Unknown error";
}

/// Result type alias for bytecode operations
template<typename T>
using BytecodeResult = std::expected<T, BytecodeError>;

// ============================================================================
// Little-Endian Read/Write Helpers
// ============================================================================

namespace endian {

[[nodiscard]] constexpr std::uint16_t read_u16_le(
    const std::uint8_t* data) noexcept {
    return static_cast<std::uint16_t>(
        static_cast<unsigned>(data[0]) |
        (static_cast<unsigned>(data[1]) << 8));
}

[[nodiscard]] constexpr std::uint32_t read_u32_le(
    const std::uint8_t* data) noexcept {
    return static_cast<std::uint32_t>(data[0]) |
           (static_cast<std::uint32_t>(data[1]) << 8) |
           (static_cast<std::uint32_t>(data[2]) << 16) |
           (static_cast<std::uint32_t>(data[3]) << 24);
}

[[nodiscard]] constexpr std::uint64_t read_u64_le(
    const std::uint8_t* data) noexcept {
    return static_cast<std::uint64_t>(data[0]) |
           (static_cast<std::uint64_t>(data[1]) << 8) |
           (static_cast<std::uint64_t>(data[2]) << 16) |
           (static_cast<std::uint64_t>(data[3]) << 24) |
           (static_cast<std::uint64_t>(data[4]) << 32) |
           (static_cast<std::uint64_t>(data[5]) << 40) |
           (static_cast<std::uint64_t>(data[6]) << 48) |
           (static_cast<std::uint64_t>(data[7]) << 56);
}

[[nodiscard]] constexpr std::int64_t read_i64_le(
    const std::uint8_t* data) noexcept {
    return static_cast<std::int64_t>(read_u64_le(data));
}

[[nodiscard]] constexpr double read_f64_le(const std::uint8_t* data) noexcept {
    return std::bit_cast<double>(read_u64_le(data));
}

constexpr void write_u16_le(std::uint8_t* data, std::uint16_t value) noexcept {
    data[0] = static_cast<std::uint8_t>(value);
    data[1] = static_cast<std::uint8_t>(value >> 8);
}

constexpr void write_u32_le(std::uint8_t* data, std::uint32_t value) noexcept {
    data[0] = static_cast<std::uint8_t>(value);
    data[1] = static_cast<std::uint8_t>(value >> 8);
    data[2] = static_cast<std::uint8_t>(value >> 16);
    data[3] = static_cast<std::uint8_t>(value >> 24);
}

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

constexpr void write_i64_le(std::uint8_t* data, std::int64_t value) noexcept {
    write_u64_le(data, static_cast<std::uint64_t>(value));
}

constexpr void write_f64_le(std::uint8_t* data, double value) noexcept {
    write_u64_le(data, std::bit_cast<std::uint64_t>(value));
}

} // namespace endian

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

/// Constant pool header (4 bytes)
struct ConstantPoolHeader {
    std::uint32_t entry_count;

    constexpr bool operator==(const ConstantPoolHeader&) const noexcept = default;
};

static_assert(sizeof(ConstantPoolHeader) == 4, "ConstantPoolHeader must be exactly 4 bytes");

// ============================================================================
// Validation Functions
// ============================================================================

/// Validate magic bytes
[[nodiscard]] constexpr bool validate_magic(
    std::span<const std::uint8_t, 4> magic) noexcept {
    return magic[0] == bytecode::MAGIC_BYTES[0] &&
           magic[1] == bytecode::MAGIC_BYTES[1] &&
           magic[2] == bytecode::MAGIC_BYTES[2] &&
           magic[3] == bytecode::MAGIC_BYTES[3];
}

/// Validate version compatibility
[[nodiscard]] constexpr bool validate_version(std::uint8_t version) noexcept {
    return version >= bytecode::MIN_SUPPORTED_VERSION &&
           version <= bytecode::MAX_SUPPORTED_VERSION;
}

/// Validate architecture value
[[nodiscard]] constexpr bool validate_architecture(Architecture arch) noexcept {
    return arch == Architecture::Arch32 || arch == Architecture::Arch64;
}

/// Check if section bounds are valid (no overflow, within file size)
[[nodiscard]] constexpr bool validate_section_bounds(
    std::uint64_t offset,
    std::uint64_t size,
    std::size_t total_file_size) noexcept {
    // Check for overflow
    if (offset > static_cast<std::uint64_t>(total_file_size)) return false;
    if (size > static_cast<std::uint64_t>(total_file_size)) return false;
    if (offset + size < offset) return false;  // Overflow check
    return offset + size <= static_cast<std::uint64_t>(total_file_size);
}

/// Check if two sections overlap
[[nodiscard]] constexpr bool sections_overlap(
    std::uint64_t offset1, std::uint64_t size1,
    std::uint64_t offset2, std::uint64_t size2) noexcept {
    // Empty sections never overlap
    if (size1 == 0 || size2 == 0) return false;

    std::uint64_t end1 = offset1 + size1;
    std::uint64_t end2 = offset2 + size2;

    // Two ranges [a,b) and [c,d) overlap iff a < d && c < b
    return offset1 < end2 && offset2 < end1;
}

/// Mask of valid flag bits
inline constexpr std::uint16_t VALID_FLAGS_MASK =
    bytecode::FLAG_DEBUG | bytecode::FLAG_OPTIMIZED;

/// Full header validation against file size
[[nodiscard]] constexpr BytecodeError validate_header(
    const BytecodeHeader& header,
    std::size_t total_file_size) noexcept {
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
    if (!validate_section_bounds(header.const_pool_offset,
                                  header.const_pool_size,
                                  total_file_size)) {
        return BytecodeError::ConstPoolOutOfBounds;
    }

    // Validate code section bounds
    if (!validate_section_bounds(header.code_offset,
                                  header.code_size,
                                  total_file_size)) {
        return BytecodeError::CodeSectionOutOfBounds;
    }

    // Validate sections don't overlap with header
    if (header.const_pool_size > 0 &&
        header.const_pool_offset < bytecode::HEADER_SIZE) {
        return BytecodeError::ConstPoolOutOfBounds;
    }
    if (header.code_size > 0 &&
        header.code_offset < bytecode::HEADER_SIZE) {
        return BytecodeError::CodeSectionOutOfBounds;
    }

    // Validate entry point (must be within code section)
    if (header.code_size == 0) {
        if (header.entry_point != 0) {
            return BytecodeError::EntryPointOutOfBounds;
        }
    } else if (header.entry_point >= header.code_size) {
        return BytecodeError::EntryPointOutOfBounds;
    }

    // Check for section overlap between const_pool and code
    if (sections_overlap(header.const_pool_offset, header.const_pool_size,
                         header.code_offset, header.code_size)) {
        return BytecodeError::SectionsOverlap;
    }

    return BytecodeError::Success;
}

// ============================================================================
// Header Read/Write Functions
// ============================================================================

/// Read header from raw bytes
[[nodiscard]] constexpr BytecodeResult<BytecodeHeader> read_header(
    std::span<const std::uint8_t> data) noexcept {
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

/// Write header to byte array
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

/// Create a header with the given parameters and default magic/version
[[nodiscard]] constexpr BytecodeHeader make_header(
    Architecture arch,
    std::uint16_t flags,
    std::uint64_t entry_point,
    std::uint64_t const_pool_offset,
    std::uint64_t const_pool_size,
    std::uint64_t code_offset,
    std::uint64_t code_size) noexcept {
    return BytecodeHeader{
        .magic = bytecode::MAGIC_BYTES,
        .version = bytecode::CURRENT_VERSION,
        .arch = arch,
        .flags = flags,
        .entry_point = entry_point,
        .const_pool_offset = const_pool_offset,
        .const_pool_size = const_pool_size,
        .code_offset = code_offset,
        .code_size = code_size
    };
}

// ============================================================================
// Constant Pool Functions
// ============================================================================

/// Read constant pool header
[[nodiscard]] constexpr BytecodeResult<ConstantPoolHeader>
read_const_pool_header(std::span<const std::uint8_t> data) noexcept {
    if (data.size() < sizeof(ConstantPoolHeader)) {
        return std::unexpected(BytecodeError::ConstPoolTruncated);
    }

    ConstantPoolHeader header{};
    header.entry_count = endian::read_u32_le(data.data());
    return header;
}

/// Read a single constant entry from data
/// Returns the Value and number of bytes consumed
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
            if (data.size() < 5) {
                return std::unexpected(BytecodeError::ConstPoolTruncated);
            }
            std::uint32_t length = endian::read_u32_le(data.data() + 1);

            if (length > bytecode::MAX_STRING_LENGTH) {
                return std::unexpected(BytecodeError::StringTooLong);
            }

            if (data.size() < 5 + length) {
                return std::unexpected(BytecodeError::ConstPoolTruncated);
            }

            bytes_consumed = 5 + length;
            // Strings are stored as handles (index into string table)
            // For now, return nil as a placeholder
            // The VM will manage string storage separately
            return std::pair{Value::nil(), bytes_consumed};
        }

        default:
            return std::unexpected(BytecodeError::InvalidConstantType);
    }
}

/// Load all constants from pool data into a vector of Values
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
        offset += entry_result->second;
    }

    return constants;
}

/// Write constant pool header
[[nodiscard]] constexpr std::array<std::uint8_t, 4>
write_const_pool_header(const ConstantPoolHeader& header) noexcept {
    std::array<std::uint8_t, 4> data{};
    endian::write_u32_le(data.data(), header.entry_count);
    return data;
}

} // namespace dotvm::core
