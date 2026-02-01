#pragma once

/// @file state_import_export.hpp
/// @brief STATE-010 Bulk state import/export with streaming support
///
/// Provides StateExporter and StateImporter for bulk state operations
/// with configurable merge strategies and streaming support for large
/// datasets.
///
/// @par Binary Format
/// The export format uses a custom binary structure:
/// - File Header: 32 bytes (magic "DVSF", version, counts, CRC32)
/// - Chunks: ~64KB each with header, RLP-encoded records, CRC32 footer
/// - File Footer: 16 bytes (chunk count, file CRC32, "DONE")
///
/// @par Merge Strategies
/// - Replace: Overwrite all existing keys
/// - Merge: Update existing keys, insert new ones
/// - SkipExisting: Only insert non-existing keys

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "dotvm/core/result.hpp"
#include "dotvm/core/state/state_backend.hpp"

namespace dotvm::core::state {

// ============================================================================
// Constants
// ============================================================================

/// @brief Magic bytes identifying a DotVM State File ("DVSF")
inline constexpr std::array<std::byte, 4> STATE_FILE_MAGIC = {
    std::byte{'D'}, std::byte{'V'}, std::byte{'S'}, std::byte{'F'}};

/// @brief Current file format version
inline constexpr std::uint16_t STATE_FILE_VERSION = 1;

/// @brief Footer magic bytes ("DONE")
inline constexpr std::array<std::byte, 4> STATE_FILE_FOOTER_MAGIC = {
    std::byte{'D'}, std::byte{'O'}, std::byte{'N'}, std::byte{'E'}};

/// @brief Default target chunk size (~64KB)
inline constexpr std::size_t DEFAULT_CHUNK_SIZE = 64 * 1024;

/// @brief File header size in bytes
inline constexpr std::size_t FILE_HEADER_SIZE = 32;

/// @brief Chunk header size in bytes
inline constexpr std::size_t CHUNK_HEADER_SIZE = 16;

/// @brief File footer size in bytes
inline constexpr std::size_t FILE_FOOTER_SIZE = 16;

// ============================================================================
// Merge Strategy
// ============================================================================

/// @brief Strategy for handling key conflicts during import
enum class MergeStrategy : std::uint8_t {
    Replace = 0,      ///< Overwrite existing keys with imported values
    Merge = 1,        ///< Update existing keys, insert new keys
    SkipExisting = 2, ///< Only insert keys that don't already exist
};

/// @brief Convert merge strategy to string
[[nodiscard]] constexpr std::string_view to_string(MergeStrategy strategy) noexcept {
    switch (strategy) {
        case MergeStrategy::Replace:
            return "Replace";
        case MergeStrategy::Merge:
            return "Merge";
        case MergeStrategy::SkipExisting:
            return "SkipExisting";
    }
    return "Unknown";
}

// ============================================================================
// Import Result
// ============================================================================

/// @brief Statistics and status from an import operation
struct ImportResult {
    std::size_t inserted_count{0};  ///< Number of new keys inserted
    std::size_t updated_count{0};   ///< Number of existing keys updated
    std::size_t skipped_count{0};   ///< Number of keys skipped (SkipExisting strategy)
    std::size_t failed_count{0};    ///< Number of records that failed to import
    std::uint32_t last_chunk{0};    ///< Last successfully imported chunk (for resume)
    std::vector<std::string> errors;  ///< Detailed error messages

    /// @brief Check if import completed successfully
    [[nodiscard]] bool is_success() const noexcept {
        return failed_count == 0 && errors.empty();
    }

    /// @brief Get total number of records processed
    [[nodiscard]] std::size_t total_processed() const noexcept {
        return inserted_count + updated_count + skipped_count + failed_count;
    }
};

// ============================================================================
// Export Configuration
// ============================================================================

/// @brief Configuration for state export operations
struct ExportConfig {
    std::size_t target_chunk_size{DEFAULT_CHUNK_SIZE};  ///< Target size per chunk
    bool include_metadata{true};  ///< Include export metadata in header

    /// @brief Create default export configuration
    [[nodiscard]] static constexpr ExportConfig defaults() noexcept {
        return ExportConfig{};
    }
};

// ============================================================================
// Import Configuration
// ============================================================================

/// @brief Configuration for state import operations
struct ImportConfig {
    MergeStrategy strategy{MergeStrategy::Merge};  ///< How to handle conflicts
    std::uint32_t resume_from_chunk{0};  ///< Chunk to resume from (0 = start)
    std::size_t max_errors{100};  ///< Stop import after this many errors
    bool verify_checksums{true};  ///< Verify CRC32 checksums

    /// @brief Create default import configuration
    [[nodiscard]] static constexpr ImportConfig defaults() noexcept {
        return ImportConfig{};
    }
};

// ============================================================================
// Chunk Callback
// ============================================================================

/// @brief Callback for streaming export chunks
///
/// Called for each chunk during export. Return false to abort export.
///
/// @param chunk_index Zero-based chunk index
/// @param data Chunk data bytes
/// @return true to continue, false to abort
using ChunkCallback = std::function<bool(std::uint32_t chunk_index,
                                          std::span<const std::byte> data)>;

// ============================================================================
// StateExporter
// ============================================================================

/// @brief Exports state data to binary format with streaming support
///
/// StateExporter iterates over a StateBackend and produces a binary
/// representation suitable for transfer or backup. Data is emitted in
/// chunks for memory efficiency.
///
/// @par Thread Safety
/// NOT thread-safe. The backend should not be modified during export.
///
/// @par Example
/// ```cpp
/// StateExporter exporter(backend, ExportConfig::defaults());
///
/// // Stream to file
/// std::ofstream file("state.dvsf", std::ios::binary);
/// exporter.export_state({}, [&file](uint32_t, span<const byte> data) {
///     file.write(reinterpret_cast<const char*>(data.data()), data.size());
///     return true;
/// });
///
/// // Or export to memory
/// auto result = exporter.export_to_bytes({});
/// ```
class StateExporter {
public:
    template <typename T>
    using Result = ::dotvm::core::Result<T, StateBackendError>;

    /// @brief Construct an exporter for a backend
    ///
    /// @param backend State backend to export from (must outlive exporter)
    /// @param config Export configuration
    explicit StateExporter(const StateBackend& backend,
                           const ExportConfig& config = ExportConfig::defaults());

    /// @brief Export state with prefix filter, streaming via callback
    ///
    /// @param prefix Key prefix filter (empty = all keys)
    /// @param callback Called for each chunk; return false to abort
    /// @return Success, or error if export failed
    [[nodiscard]] Result<void> export_state(StateBackend::Key prefix,
                                            const ChunkCallback& callback);

    /// @brief Export state to a single byte vector
    ///
    /// Convenience method for small exports. For large datasets,
    /// use export_state() with a streaming callback.
    ///
    /// @param prefix Key prefix filter (empty = all keys)
    /// @return Exported bytes, or error
    [[nodiscard]] Result<std::vector<std::byte>> export_to_bytes(StateBackend::Key prefix);

private:
    const StateBackend& backend_;
    ExportConfig config_;
};

// ============================================================================
// StateImporter
// ============================================================================

/// @brief Imports state data from binary format
///
/// StateImporter reads the binary export format and inserts records
/// into a StateBackend according to the configured merge strategy.
///
/// @par Thread Safety
/// NOT thread-safe. The backend should not be modified during import.
///
/// @par Example
/// ```cpp
/// StateImporter importer(backend, ImportConfig::defaults());
///
/// // Import from memory
/// auto result = importer.import_state(data);
/// if (result.is_ok()) {
///     auto& stats = result.value();
///     std::cout << "Inserted: " << stats.inserted_count << "\n";
/// }
/// ```
class StateImporter {
public:
    template <typename T>
    using Result = ::dotvm::core::Result<T, StateBackendError>;

    /// @brief Construct an importer for a backend
    ///
    /// @param backend State backend to import into (must outlive importer)
    /// @param config Import configuration
    explicit StateImporter(StateBackend& backend,
                           const ImportConfig& config = ImportConfig::defaults());

    /// @brief Import state from binary data
    ///
    /// @param data Binary data in export format
    /// @return Import statistics, or error if import failed completely
    [[nodiscard]] Result<ImportResult> import_state(std::span<const std::byte> data);

private:
    StateBackend& backend_;
    ImportConfig config_;
};

}  // namespace dotvm::core::state
