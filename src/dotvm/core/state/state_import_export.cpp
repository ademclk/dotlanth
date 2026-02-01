/// @file state_import_export.cpp
/// @brief STATE-010 Bulk state import/export implementation
///
/// Implements StateExporter and StateImporter for bulk state operations
/// with streaming support and configurable merge strategies.

#include "dotvm/core/state/state_import_export.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>

#include "dotvm/core/state/rlp.hpp"

namespace dotvm::core::state {

namespace {

// ============================================================================
// CRC32 Implementation (IEEE 802.3 polynomial)
// Copied from log_record.cpp for self-containment
// ============================================================================

constexpr std::array<std::uint32_t, 256> generate_crc32_table() {
    std::array<std::uint32_t, 256> table{};
    constexpr std::uint32_t polynomial = 0xEDB88320;

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

[[nodiscard]] std::uint32_t crc32(std::span<const std::byte> data,
                                  std::uint32_t initial = 0xFFFFFFFF) {
    std::uint32_t crc = initial;
    for (std::byte b : data) {
        auto index = static_cast<std::uint8_t>(crc ^ static_cast<std::uint8_t>(b));
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
    return static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(buf[offset]) |
        static_cast<std::uint16_t>(static_cast<std::uint16_t>(buf[offset + 1]) << 8));
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

// ============================================================================
// File Header Structure (32 bytes)
// ============================================================================
//
// Offset  Size  Description
// 0       4     Magic bytes "DVSF"
// 4       2     Version (little-endian)
// 6       2     Reserved (padding)
// 8       4     Total record count (little-endian)
// 12      4     Chunk count (little-endian)
// 16      8     Export timestamp (Unix epoch, microseconds)
// 24      4     Header CRC32
// 28      4     Reserved

struct FileHeader {
    std::array<std::byte, 4> magic{};
    std::uint16_t version{0};
    std::uint32_t record_count{0};
    std::uint32_t chunk_count{0};
    std::uint64_t timestamp{0};
    std::uint32_t header_crc{0};

    [[nodiscard]] std::vector<std::byte> serialize() const {
        std::vector<std::byte> buf;
        buf.reserve(FILE_HEADER_SIZE);

        // Magic
        buf.insert(buf.end(), magic.begin(), magic.end());

        // Version + padding
        write_u16(buf, version);
        write_u16(buf, 0);  // Reserved

        // Counts
        write_u32(buf, record_count);
        write_u32(buf, chunk_count);

        // Timestamp
        write_u64(buf, timestamp);

        // Calculate CRC of header content (first 24 bytes)
        auto content_crc = crc32({buf.data(), buf.size()});
        write_u32(buf, content_crc);

        // Reserved
        write_u32(buf, 0);

        return buf;
    }

    [[nodiscard]] static ::dotvm::core::Result<FileHeader, StateBackendError>
    deserialize(std::span<const std::byte> data) {
        if (data.size() < FILE_HEADER_SIZE) {
            return StateBackendError::TruncatedData;
        }

        FileHeader header;

        // Check magic
        std::copy_n(data.begin(), 4, header.magic.begin());
        if (header.magic != STATE_FILE_MAGIC) {
            return StateBackendError::InvalidMagic;
        }

        // Version
        header.version = read_u16(data, 4);
        if (header.version != STATE_FILE_VERSION) {
            return StateBackendError::InvalidVersion;
        }

        // Counts
        header.record_count = read_u32(data, 8);
        header.chunk_count = read_u32(data, 12);

        // Timestamp
        header.timestamp = read_u64(data, 16);

        // Verify CRC
        header.header_crc = read_u32(data, 24);
        auto computed_crc = crc32(data.subspan(0, 24));
        if (computed_crc != header.header_crc) {
            return StateBackendError::ChecksumMismatch;
        }

        return header;
    }
};

// ============================================================================
// Chunk Header Structure (16 bytes)
// ============================================================================
//
// Offset  Size  Description
// 0       4     Chunk index (little-endian)
// 4       4     Record count in this chunk
// 8       4     Payload size (excluding header and CRC footer)
// 12      4     Reserved

struct ChunkHeader {
    std::uint32_t index{0};
    std::uint32_t record_count{0};
    std::uint32_t payload_size{0};

    [[nodiscard]] std::vector<std::byte> serialize() const {
        std::vector<std::byte> buf;
        buf.reserve(CHUNK_HEADER_SIZE);

        write_u32(buf, index);
        write_u32(buf, record_count);
        write_u32(buf, payload_size);
        write_u32(buf, 0);  // Reserved

        return buf;
    }

    [[nodiscard]] static ::dotvm::core::Result<ChunkHeader, StateBackendError>
    deserialize(std::span<const std::byte> data) {
        if (data.size() < CHUNK_HEADER_SIZE) {
            return StateBackendError::TruncatedData;
        }

        ChunkHeader header;
        header.index = read_u32(data, 0);
        header.record_count = read_u32(data, 4);
        header.payload_size = read_u32(data, 8);

        return header;
    }
};

// ============================================================================
// File Footer Structure (16 bytes)
// ============================================================================
//
// Offset  Size  Description
// 0       4     Total chunk count (verification)
// 4       4     File CRC32 (of all preceding content)
// 8       4     Footer magic "DONE"
// 12      4     Reserved

struct FileFooter {
    std::uint32_t chunk_count{0};
    std::uint32_t file_crc{0};
    std::array<std::byte, 4> magic{};

    [[nodiscard]] std::vector<std::byte> serialize(std::uint32_t content_crc) const {
        std::vector<std::byte> buf;
        buf.reserve(FILE_FOOTER_SIZE);

        write_u32(buf, chunk_count);
        write_u32(buf, content_crc);
        buf.insert(buf.end(), STATE_FILE_FOOTER_MAGIC.begin(), STATE_FILE_FOOTER_MAGIC.end());
        write_u32(buf, 0);  // Reserved

        return buf;
    }

    [[nodiscard]] static ::dotvm::core::Result<FileFooter, StateBackendError>
    deserialize(std::span<const std::byte> data, std::size_t footer_offset) {
        if (data.size() < footer_offset + FILE_FOOTER_SIZE) {
            return StateBackendError::TruncatedData;
        }

        auto footer_data = data.subspan(footer_offset);

        FileFooter footer;
        footer.chunk_count = read_u32(footer_data, 0);
        footer.file_crc = read_u32(footer_data, 4);

        // Check footer magic
        std::copy_n(footer_data.begin() + 8, 4, footer.magic.begin());
        if (footer.magic != STATE_FILE_FOOTER_MAGIC) {
            return StateBackendError::TruncatedData;  // Likely truncated
        }

        return footer;
    }
};

// ============================================================================
// Record Encoding/Decoding
// ============================================================================

/// @brief Encode a key-value pair as RLP list [key, value]
[[nodiscard]] std::vector<std::byte> encode_record(std::span<const std::byte> key,
                                                    std::span<const std::byte> value) {
    std::vector<std::vector<std::byte>> items;
    items.emplace_back(key.begin(), key.end());
    items.emplace_back(value.begin(), value.end());
    return rlp::encode_list(items);
}

/// @brief Decode a record from RLP
[[nodiscard]] ::dotvm::core::Result<std::pair<std::vector<std::byte>, std::vector<std::byte>>,
                                     StateBackendError>
decode_record(std::span<const std::byte> data, std::size_t& bytes_consumed) {
    auto list_result = rlp::decode_list(data);
    if (list_result.is_err()) {
        return StateBackendError::InvalidRecordFormat;
    }

    const auto& items = list_result.value();
    if (items.size() != 2) {
        return StateBackendError::InvalidRecordFormat;
    }

    // Calculate bytes consumed by decoding the item
    auto item_result = rlp::decode_item(data);
    if (item_result.is_err()) {
        return StateBackendError::InvalidRecordFormat;
    }
    bytes_consumed = item_result.value().bytes_consumed;

    return std::make_pair(items[0], items[1]);
}

/// @brief Get current timestamp in microseconds
[[nodiscard]] std::uint64_t current_timestamp_us() {
    auto now = std::chrono::system_clock::now();
    auto epoch = now.time_since_epoch();
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(epoch).count());
}

}  // namespace

// ============================================================================
// StateExporter Implementation
// ============================================================================

StateExporter::StateExporter(const StateBackend& backend, const ExportConfig& config)
    : backend_{backend}, config_{config} {}

StateExporter::Result<void> StateExporter::export_state(StateBackend::Key prefix,
                                                         const ChunkCallback& callback) {
    // Collect all records first to know counts
    struct Record {
        std::vector<std::byte> key;
        std::vector<std::byte> value;
    };
    std::vector<Record> records;

    auto iterate_result = backend_.iterate(prefix, [&records](StateBackend::Key key,
                                                               StateBackend::Value value) {
        records.push_back(Record{
            std::vector<std::byte>(key.begin(), key.end()),
            std::vector<std::byte>(value.begin(), value.end())
        });
        return true;  // Continue iteration
    });

    if (iterate_result.is_err()) {
        return iterate_result.error();
    }

    // Build chunks
    std::vector<std::vector<std::byte>> chunks;
    std::vector<std::byte> current_chunk_payload;
    std::uint32_t current_chunk_record_count = 0;
    std::uint32_t chunk_index = 0;

    auto flush_chunk = [&]() {
        if (current_chunk_payload.empty()) {
            return;
        }

        ChunkHeader header;
        header.index = chunk_index;
        header.record_count = current_chunk_record_count;
        header.payload_size = static_cast<std::uint32_t>(current_chunk_payload.size());

        std::vector<std::byte> chunk_data;
        auto header_bytes = header.serialize();
        chunk_data.insert(chunk_data.end(), header_bytes.begin(), header_bytes.end());
        chunk_data.insert(chunk_data.end(), current_chunk_payload.begin(),
                          current_chunk_payload.end());

        // Add CRC32 footer for chunk
        auto chunk_crc = crc32(chunk_data);
        write_u32(chunk_data, chunk_crc);

        chunks.push_back(std::move(chunk_data));
        current_chunk_payload.clear();
        current_chunk_record_count = 0;
        ++chunk_index;
    };

    for (const auto& record : records) {
        auto encoded = encode_record(record.key, record.value);

        // Check if adding this record would exceed chunk size
        if (!current_chunk_payload.empty() &&
            current_chunk_payload.size() + encoded.size() > config_.target_chunk_size) {
            flush_chunk();
        }

        current_chunk_payload.insert(current_chunk_payload.end(), encoded.begin(), encoded.end());
        ++current_chunk_record_count;
    }

    // Flush remaining data
    flush_chunk();

    // Build file header
    FileHeader file_header;
    file_header.magic = STATE_FILE_MAGIC;
    file_header.version = STATE_FILE_VERSION;
    file_header.record_count = static_cast<std::uint32_t>(records.size());
    file_header.chunk_count = static_cast<std::uint32_t>(chunks.size());
    file_header.timestamp = current_timestamp_us();

    // Calculate content CRC (header + all chunks)
    std::vector<std::byte> all_content;
    auto header_bytes = file_header.serialize();
    all_content.insert(all_content.end(), header_bytes.begin(), header_bytes.end());

    for (const auto& chunk : chunks) {
        all_content.insert(all_content.end(), chunk.begin(), chunk.end());
    }

    auto content_crc = crc32(all_content);

    // Build footer
    FileFooter footer;
    footer.chunk_count = static_cast<std::uint32_t>(chunks.size());
    auto footer_bytes = footer.serialize(content_crc);

    // Emit chunks via callback
    // First emit header as "chunk 0"
    std::uint32_t callback_idx = 0;

    // Combine header + chunks + footer into logical callback chunks
    std::vector<std::byte> output;
    output.insert(output.end(), header_bytes.begin(), header_bytes.end());

    for (const auto& chunk : chunks) {
        output.insert(output.end(), chunk.begin(), chunk.end());
    }

    output.insert(output.end(), footer_bytes.begin(), footer_bytes.end());

    // For streaming, we could emit in smaller pieces
    // For simplicity, emit everything as one callback for small exports
    // or chunk it for larger exports
    constexpr std::size_t CALLBACK_CHUNK_SIZE = 64 * 1024;

    std::size_t offset = 0;
    while (offset < output.size()) {
        std::size_t chunk_size = std::min(CALLBACK_CHUNK_SIZE, output.size() - offset);
        std::span<const std::byte> chunk_span{output.data() + offset, chunk_size};

        if (!callback(callback_idx++, chunk_span)) {
            return StateBackendError::ExportAborted;
        }

        offset += chunk_size;
    }

    return {};
}

StateExporter::Result<std::vector<std::byte>> StateExporter::export_to_bytes(StateBackend::Key prefix) {
    std::vector<std::byte> result;

    auto export_result = export_state(prefix, [&result](std::uint32_t,
                                                         std::span<const std::byte> data) {
        result.insert(result.end(), data.begin(), data.end());
        return true;
    });

    if (export_result.is_err()) {
        return export_result.error();
    }

    return result;
}

// ============================================================================
// StateImporter Implementation
// ============================================================================

StateImporter::StateImporter(StateBackend& backend, const ImportConfig& config)
    : backend_{backend}, config_{config} {}

StateImporter::Result<ImportResult> StateImporter::import_state(std::span<const std::byte> data) {
    ImportResult stats;

    // Minimum valid file: header + footer
    if (data.size() < FILE_HEADER_SIZE + FILE_FOOTER_SIZE) {
        return StateBackendError::TruncatedData;
    }

    // Parse file header
    auto header_result = FileHeader::deserialize(data);
    if (header_result.is_err()) {
        return header_result.error();
    }
    const auto& header = header_result.value();

    // Find and parse footer
    std::size_t footer_offset = data.size() - FILE_FOOTER_SIZE;
    auto footer_result = FileFooter::deserialize(data, footer_offset);
    if (footer_result.is_err()) {
        return footer_result.error();
    }
    const auto& footer = footer_result.value();

    // Verify chunk count matches
    if (header.chunk_count != footer.chunk_count) {
        return StateBackendError::TruncatedData;
    }

    // Verify file CRC if enabled
    if (config_.verify_checksums) {
        auto content_span = data.subspan(0, footer_offset);
        auto computed_crc = crc32(content_span);
        if (computed_crc != footer.file_crc) {
            return StateBackendError::ChecksumMismatch;
        }
    }

    // Process chunks
    std::size_t offset = FILE_HEADER_SIZE;
    std::uint32_t expected_chunk_index = 0;

    while (offset < footer_offset) {
        // Parse chunk header
        if (offset + CHUNK_HEADER_SIZE > footer_offset) {
            return StateBackendError::TruncatedData;
        }

        auto chunk_header_result = ChunkHeader::deserialize(data.subspan(offset));
        if (chunk_header_result.is_err()) {
            return chunk_header_result.error();
        }
        const auto& chunk_header = chunk_header_result.value();

        // Verify chunk sequence
        if (chunk_header.index != expected_chunk_index) {
            return StateBackendError::ChunkSequenceError;
        }

        // Skip to resume point if configured
        if (chunk_header.index < config_.resume_from_chunk) {
            // Calculate chunk size and skip
            std::size_t chunk_total = CHUNK_HEADER_SIZE + chunk_header.payload_size + 4;  // +4 for CRC
            offset += chunk_total;
            ++expected_chunk_index;
            continue;
        }

        // Verify we have enough data for payload + CRC
        std::size_t chunk_total = CHUNK_HEADER_SIZE + chunk_header.payload_size + 4;
        if (offset + chunk_total > footer_offset) {
            return StateBackendError::TruncatedData;
        }

        // Verify chunk CRC if enabled
        if (config_.verify_checksums) {
            auto chunk_content = data.subspan(offset, CHUNK_HEADER_SIZE + chunk_header.payload_size);
            auto stored_crc = read_u32(data, offset + CHUNK_HEADER_SIZE + chunk_header.payload_size);
            auto computed_crc = crc32(chunk_content);
            if (computed_crc != stored_crc) {
                return StateBackendError::ChecksumMismatch;
            }
        }

        // Process records in chunk
        auto payload = data.subspan(offset + CHUNK_HEADER_SIZE, chunk_header.payload_size);
        std::size_t payload_offset = 0;

        while (payload_offset < payload.size()) {
            std::size_t bytes_consumed = 0;
            auto record_result = decode_record(payload.subspan(payload_offset), bytes_consumed);

            if (record_result.is_err()) {
                ++stats.failed_count;
                stats.errors.push_back("Failed to decode record at chunk " +
                                       std::to_string(chunk_header.index));

                if (stats.failed_count >= config_.max_errors) {
                    stats.last_chunk = chunk_header.index;
                    return StateBackendError::TooManyErrors;
                }

                // Try to skip this record - but we don't know its size
                // This is a fatal error for this chunk
                break;
            }

            const auto& [key, value] = record_result.value();

            // Apply merge strategy
            bool key_exists = backend_.exists(key);

            bool should_write = false;
            switch (config_.strategy) {
                case MergeStrategy::Replace:
                    should_write = true;
                    if (key_exists) {
                        ++stats.updated_count;
                    } else {
                        ++stats.inserted_count;
                    }
                    break;

                case MergeStrategy::Merge:
                    should_write = true;
                    if (key_exists) {
                        ++stats.updated_count;
                    } else {
                        ++stats.inserted_count;
                    }
                    break;

                case MergeStrategy::SkipExisting:
                    if (key_exists) {
                        ++stats.skipped_count;
                        should_write = false;
                    } else {
                        ++stats.inserted_count;
                        should_write = true;
                    }
                    break;
            }

            if (should_write) {
                auto put_result = backend_.put(key, value);
                if (put_result.is_err()) {
                    ++stats.failed_count;
                    stats.errors.push_back("Failed to write key at chunk " +
                                           std::to_string(chunk_header.index) + ": " +
                                           std::string(to_string(put_result.error())));

                    // Adjust counts - we counted this as insert/update but it failed
                    if (key_exists) {
                        --stats.updated_count;
                    } else {
                        --stats.inserted_count;
                    }

                    if (stats.failed_count >= config_.max_errors) {
                        stats.last_chunk = chunk_header.index;
                        return StateBackendError::TooManyErrors;
                    }
                }
            }

            payload_offset += bytes_consumed;
        }

        stats.last_chunk = chunk_header.index;
        offset += chunk_total;
        ++expected_chunk_index;
    }

    return stats;
}

}  // namespace dotvm::core::state
