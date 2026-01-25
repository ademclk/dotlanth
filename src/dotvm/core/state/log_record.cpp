/// @file log_record.cpp
/// @brief STATE-007 Log record serialization implementation
///
/// Implements binary serialization with CRC32 checksum for WAL records.

#include "dotvm/core/state/log_record.hpp"

#include <array>
#include <cstring>

namespace dotvm::core::state {

namespace {

// ============================================================================
// CRC32 Implementation (IEEE 802.3 polynomial)
// ============================================================================

/// @brief Precomputed CRC32 lookup table
constexpr std::array<std::uint32_t, 256> generate_crc32_table() {
    std::array<std::uint32_t, 256> table{};
    constexpr std::uint32_t polynomial = 0xEDB88320;  // IEEE 802.3 polynomial (reversed)

    for (std::uint32_t i = 0; i < 256; ++i) {
        std::uint32_t crc = i;
        for (int j = 0; j < 8; ++j) {
            if (crc & 1) {
                crc = (crc >> 1) ^ polynomial;
            } else {
                crc >>= 1;
            }
        }
        table[i] = crc;
    }
    return table;
}

constexpr std::array<std::uint32_t, 256> CRC32_TABLE = generate_crc32_table();

/// @brief Calculate CRC32 for a byte buffer
[[nodiscard]] std::uint32_t crc32(std::span<const std::byte> data, std::uint32_t initial = 0xFFFFFFFF) {
    std::uint32_t crc = initial;
    for (std::byte b : data) {
        std::uint8_t index = static_cast<std::uint8_t>(crc ^ static_cast<std::uint8_t>(b));
        crc = (crc >> 8) ^ CRC32_TABLE[index];
    }
    return crc ^ 0xFFFFFFFF;
}

// ============================================================================
// Little-Endian Encoding Helpers
// ============================================================================

void write_u16(std::vector<std::byte>& buf, std::uint16_t value) {
    buf.push_back(static_cast<std::byte>(value & 0xFF));
    buf.push_back(static_cast<std::byte>((value >> 8) & 0xFF));
}

void write_u32(std::vector<std::byte>& buf, std::uint32_t value) {
    buf.push_back(static_cast<std::byte>(value & 0xFF));
    buf.push_back(static_cast<std::byte>((value >> 8) & 0xFF));
    buf.push_back(static_cast<std::byte>((value >> 16) & 0xFF));
    buf.push_back(static_cast<std::byte>((value >> 24) & 0xFF));
}

void write_u64(std::vector<std::byte>& buf, std::uint64_t value) {
    for (int i = 0; i < 8; ++i) {
        buf.push_back(static_cast<std::byte>((value >> (i * 8)) & 0xFF));
    }
}

[[nodiscard]] std::uint16_t read_u16(std::span<const std::byte> buf, std::size_t offset) {
    return static_cast<std::uint16_t>(buf[offset]) |
           (static_cast<std::uint16_t>(buf[offset + 1]) << 8);
}

[[nodiscard]] std::uint32_t read_u32(std::span<const std::byte> buf, std::size_t offset) {
    return static_cast<std::uint32_t>(buf[offset]) |
           (static_cast<std::uint32_t>(buf[offset + 1]) << 8) |
           (static_cast<std::uint32_t>(buf[offset + 2]) << 16) |
           (static_cast<std::uint32_t>(buf[offset + 3]) << 24);
}

[[nodiscard]] std::uint64_t read_u64(std::span<const std::byte> buf, std::size_t offset) {
    std::uint64_t value = 0;
    for (int i = 0; i < 8; ++i) {
        value |= static_cast<std::uint64_t>(buf[offset + static_cast<std::size_t>(i)]) << (i * 8);
    }
    return value;
}

}  // namespace

// ============================================================================
// Factory Methods
// ============================================================================

LogRecord LogRecord::create_put(
    LSN lsn,
    std::vector<std::byte> key,
    std::vector<std::byte> value,
    TxId tx_id) {
    LogRecord record;
    record.lsn = lsn;
    record.type = LogRecordType::Put;
    record.key = std::move(key);
    record.value = std::move(value);
    record.tx_id = tx_id;
    record.checksum = record.calculate_checksum();
    return record;
}

LogRecord LogRecord::create_delete(
    LSN lsn,
    std::vector<std::byte> key,
    TxId tx_id) {
    LogRecord record;
    record.lsn = lsn;
    record.type = LogRecordType::Delete;
    record.key = std::move(key);
    record.value = {};
    record.tx_id = tx_id;
    record.checksum = record.calculate_checksum();
    return record;
}

LogRecord LogRecord::create_tx_begin(LSN lsn, TxId tx_id) {
    LogRecord record;
    record.lsn = lsn;
    record.type = LogRecordType::TxBegin;
    record.key = {};
    record.value = {};
    record.tx_id = tx_id;
    record.checksum = record.calculate_checksum();
    return record;
}

LogRecord LogRecord::create_tx_commit(LSN lsn, TxId tx_id) {
    LogRecord record;
    record.lsn = lsn;
    record.type = LogRecordType::TxCommit;
    record.key = {};
    record.value = {};
    record.tx_id = tx_id;
    record.checksum = record.calculate_checksum();
    return record;
}

LogRecord LogRecord::create_tx_abort(LSN lsn, TxId tx_id) {
    LogRecord record;
    record.lsn = lsn;
    record.type = LogRecordType::TxAbort;
    record.key = {};
    record.value = {};
    record.tx_id = tx_id;
    record.checksum = record.calculate_checksum();
    return record;
}

LogRecord LogRecord::create_checkpoint(LSN lsn, LSN checkpoint_lsn) {
    LogRecord record;
    record.lsn = lsn;
    record.type = LogRecordType::Checkpoint;
    record.key = {};
    // Store checkpoint LSN in value as 8 bytes
    record.value.resize(sizeof(std::uint64_t));
    for (int i = 0; i < 8; ++i) {
        record.value[static_cast<std::size_t>(i)] = static_cast<std::byte>((checkpoint_lsn.value >> (i * 8)) & 0xFF);
    }
    record.tx_id = TxId{};  // No transaction for checkpoint
    record.checksum = record.calculate_checksum();
    return record;
}

// ============================================================================
// Serialization
// ============================================================================

std::vector<std::byte> LogRecord::serialize() const {
    std::vector<std::byte> buf;
    buf.reserve(serialized_size());

    // Header (16 bytes)
    write_u64(buf, lsn.value);                                      // [0-7] LSN
    buf.push_back(static_cast<std::byte>(type));                    // [8] Type
    buf.push_back(std::byte{0x00});                                 // [9] Reserved
    write_u16(buf, static_cast<std::uint16_t>(key.size()));         // [10-11] Key length
    write_u32(buf, static_cast<std::uint32_t>(value.size()));       // [12-15] Value length

    // TxId (12 bytes)
    write_u64(buf, tx_id.id);                                       // [16-23] tx_id.id
    write_u32(buf, tx_id.generation);                               // [24-27] tx_id.generation

    // Body (variable)
    buf.insert(buf.end(), key.begin(), key.end());                  // Key bytes
    buf.insert(buf.end(), value.begin(), value.end());              // Value bytes

    // Footer (4 bytes) - CRC32 checksum
    write_u32(buf, checksum);

    return buf;
}

::dotvm::core::Result<LogRecord, WalError> LogRecord::deserialize(std::span<const std::byte> data) {
    // Minimum size: Header(16) + TxId(12) + Checksum(4) = 32 bytes
    constexpr std::size_t MIN_SIZE = HEADER_SIZE + TXID_SERIALIZED_SIZE + sizeof(std::uint32_t);

    if (data.size() < MIN_SIZE) {
        return WalError::WalCorrupted;
    }

    // Read header
    std::uint64_t lsn_value = read_u64(data, 0);
    auto type_byte = static_cast<std::uint8_t>(data[8]);
    // data[9] is reserved
    std::uint16_t key_len = read_u16(data, 10);
    std::uint32_t value_len = read_u32(data, 12);

    // Validate type
    if (type_byte > static_cast<std::uint8_t>(LogRecordType::Checkpoint)) {
        return WalError::WalCorrupted;
    }

    // Read TxId
    std::uint64_t tx_id_id = read_u64(data, 16);
    std::uint32_t tx_id_gen = read_u32(data, 24);

    // Calculate expected size (use TXID_SERIALIZED_SIZE, not sizeof)
    std::size_t expected_size = HEADER_SIZE + TXID_SERIALIZED_SIZE + key_len + value_len + sizeof(std::uint32_t);
    if (data.size() < expected_size) {
        return WalError::WalCorrupted;
    }

    // Read key and value
    std::size_t key_offset = HEADER_SIZE + TXID_SERIALIZED_SIZE;
    std::size_t value_offset = key_offset + key_len;
    std::size_t checksum_offset = value_offset + value_len;

    std::vector<std::byte> key_data(data.begin() + static_cast<std::ptrdiff_t>(key_offset),
                                     data.begin() + static_cast<std::ptrdiff_t>(value_offset));
    std::vector<std::byte> value_data(data.begin() + static_cast<std::ptrdiff_t>(value_offset),
                                       data.begin() + static_cast<std::ptrdiff_t>(checksum_offset));

    // Read stored checksum
    std::uint32_t stored_checksum = read_u32(data, checksum_offset);

    // Build record
    LogRecord record;
    record.lsn = LSN{lsn_value};
    record.type = static_cast<LogRecordType>(type_byte);
    record.key = std::move(key_data);
    record.value = std::move(value_data);
    record.tx_id = TxId{.id = tx_id_id, .generation = tx_id_gen};

    // Verify checksum
    std::uint32_t computed_checksum = record.calculate_checksum();
    if (computed_checksum != stored_checksum) {
        return WalError::WalCorrupted;
    }

    record.checksum = stored_checksum;
    return record;
}

// ============================================================================
// Utilities
// ============================================================================

std::uint32_t LogRecord::calculate_checksum() const {
    // Build buffer without checksum for CRC calculation
    std::vector<std::byte> buf;
    buf.reserve(HEADER_SIZE + TXID_SERIALIZED_SIZE + key.size() + value.size());

    // Header
    write_u64(buf, lsn.value);
    buf.push_back(static_cast<std::byte>(type));
    buf.push_back(std::byte{0x00});
    write_u16(buf, static_cast<std::uint16_t>(key.size()));
    write_u32(buf, static_cast<std::uint32_t>(value.size()));

    // TxId
    write_u64(buf, tx_id.id);
    write_u32(buf, tx_id.generation);

    // Body
    buf.insert(buf.end(), key.begin(), key.end());
    buf.insert(buf.end(), value.begin(), value.end());

    return crc32(buf);
}

}  // namespace dotvm::core::state
