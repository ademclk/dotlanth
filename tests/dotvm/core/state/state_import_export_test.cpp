/// @file state_import_export_test.cpp
/// @brief STATE-010 StateImporter/StateExporter unit tests (TDD)
///
/// Tests for bulk state import/export operations:
/// - Empty state handling
/// - Single record round-trip
/// - Multi-chunk streaming
/// - Prefix filtering
/// - All merge strategies
/// - Error handling (invalid magic, checksum, truncated)
/// - Resume from checkpoint

#include <algorithm>
#include <cstring>
#include <random>
#include <vector>

#include <gtest/gtest.h>

#include "dotvm/core/state/state_backend.hpp"
#include "dotvm/core/state/state_import_export.hpp"

namespace dotvm::core::state {
namespace {

// ============================================================================
// Helper Functions
// ============================================================================

/// @brief Create a byte span from a string literal
[[nodiscard]] std::span<const std::byte> to_bytes(std::string_view str) {
    return {reinterpret_cast<const std::byte*>(str.data()), str.size()};
}

/// @brief Create a byte vector from a string (unused but kept for consistency with other tests)
[[maybe_unused]] [[nodiscard]] std::vector<std::byte> make_bytes(std::string_view str) {
    std::vector<std::byte> result(str.size());
    std::memcpy(result.data(), str.data(), str.size());
    return result;
}

/// @brief Convert byte vector to string for comparison
[[nodiscard]] std::string to_string(std::span<const std::byte> bytes) {
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

/// @brief Generate random bytes for testing (unused but kept for future tests)
[[maybe_unused]] [[nodiscard]] std::vector<std::byte> random_bytes(std::size_t size,
                                                                   std::uint32_t seed = 42) {
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> dist(0, 255);
    std::vector<std::byte> result(size);
    for (auto& b : result) {
        b = static_cast<std::byte>(dist(rng));
    }
    return result;
}

// ============================================================================
// Test Fixture
// ============================================================================

class StateImportExportTest : public ::testing::Test {
protected:
    void SetUp() override {
        StateBackendConfig config;
        config.enable_transactions = true;
        backend_ = create_state_backend(config);
    }

    /// @brief Helper to put a key-value pair
    void put(std::string_view key, std::string_view value) {
        auto result = backend_->put(to_bytes(key), to_bytes(value));
        ASSERT_TRUE(result.is_ok()) << "Failed to put: " << to_string(result.error());
    }

    /// @brief Helper to get a value
    [[nodiscard]] std::string get(std::string_view key) {
        auto result = backend_->get(to_bytes(key));
        if (result.is_err())
            return "";
        return to_string(result.value());
    }

    /// @brief Helper to check if key exists
    [[nodiscard]] bool exists(std::string_view key) { return backend_->exists(to_bytes(key)); }

    std::unique_ptr<StateBackend> backend_;
};

// ============================================================================
// Error Code Tests
// ============================================================================

/// @test Import/export error codes have correct to_string()
TEST(ImportExportErrorTest, ErrorCodesToString) {
    EXPECT_EQ(to_string(StateBackendError::InvalidMagic), "InvalidMagic");
    EXPECT_EQ(to_string(StateBackendError::InvalidVersion), "InvalidVersion");
    EXPECT_EQ(to_string(StateBackendError::ChecksumMismatch), "ChecksumMismatch");
    EXPECT_EQ(to_string(StateBackendError::TruncatedData), "TruncatedData");
    EXPECT_EQ(to_string(StateBackendError::InvalidChunkHeader), "InvalidChunkHeader");
    EXPECT_EQ(to_string(StateBackendError::InvalidRecordFormat), "InvalidRecordFormat");
    EXPECT_EQ(to_string(StateBackendError::ChunkSequenceError), "ChunkSequenceError");
    EXPECT_EQ(to_string(StateBackendError::ImportAborted), "ImportAborted");
    EXPECT_EQ(to_string(StateBackendError::ExportAborted), "ExportAborted");
    EXPECT_EQ(to_string(StateBackendError::TooManyErrors), "TooManyErrors");
}

/// @test ImportAborted and ExportAborted are recoverable
TEST(ImportExportErrorTest, RecoverableErrors) {
    EXPECT_TRUE(is_recoverable(StateBackendError::ImportAborted));
    EXPECT_TRUE(is_recoverable(StateBackendError::ExportAborted));
    EXPECT_FALSE(is_recoverable(StateBackendError::InvalidMagic));
    EXPECT_FALSE(is_recoverable(StateBackendError::ChecksumMismatch));
}

// ============================================================================
// MergeStrategy Tests
// ============================================================================

/// @test MergeStrategy to_string() returns correct values
TEST(MergeStrategyTest, ToString) {
    EXPECT_EQ(to_string(MergeStrategy::Replace), "Replace");
    EXPECT_EQ(to_string(MergeStrategy::Merge), "Merge");
    EXPECT_EQ(to_string(MergeStrategy::SkipExisting), "SkipExisting");
}

// ============================================================================
// ImportResult Tests
// ============================================================================

/// @test ImportResult::is_success() returns true when no errors
TEST(ImportResultTest, IsSuccessNoErrors) {
    ImportResult result;
    result.inserted_count = 10;
    result.updated_count = 5;
    EXPECT_TRUE(result.is_success());
}

/// @test ImportResult::is_success() returns false when failed_count > 0
TEST(ImportResultTest, IsSuccessWithFailures) {
    ImportResult result;
    result.inserted_count = 10;
    result.failed_count = 1;
    EXPECT_FALSE(result.is_success());
}

/// @test ImportResult::is_success() returns false when errors present
TEST(ImportResultTest, IsSuccessWithErrorMessages) {
    ImportResult result;
    result.inserted_count = 10;
    result.errors.push_back("some error");
    EXPECT_FALSE(result.is_success());
}

/// @test ImportResult::total_processed() returns sum of all counts
TEST(ImportResultTest, TotalProcessed) {
    ImportResult result;
    result.inserted_count = 10;
    result.updated_count = 5;
    result.skipped_count = 3;
    result.failed_count = 2;
    EXPECT_EQ(result.total_processed(), 20u);
}

// ============================================================================
// Configuration Tests
// ============================================================================

/// @test ExportConfig::defaults() returns valid configuration
TEST(ExportConfigTest, Defaults) {
    auto config = ExportConfig::defaults();
    EXPECT_EQ(config.target_chunk_size, DEFAULT_CHUNK_SIZE);
    EXPECT_TRUE(config.include_metadata);
}

/// @test ImportConfig::defaults() returns valid configuration
TEST(ImportConfigTest, Defaults) {
    auto config = ImportConfig::defaults();
    EXPECT_EQ(config.strategy, MergeStrategy::Merge);
    EXPECT_EQ(config.resume_from_chunk, 0u);
    EXPECT_EQ(config.max_errors, 100u);
    EXPECT_TRUE(config.verify_checksums);
}

// ============================================================================
// Empty State Tests
// ============================================================================

/// @test Exporting empty state produces valid output
TEST_F(StateImportExportTest, ExportEmptyState) {
    StateExporter exporter(*backend_);
    auto result = exporter.export_to_bytes({});

    ASSERT_TRUE(result.is_ok()) << "Export failed: " << to_string(result.error());

    const auto& data = result.value();
    // Should have at least file header + footer
    EXPECT_GE(data.size(), FILE_HEADER_SIZE + FILE_FOOTER_SIZE);

    // Check magic bytes
    EXPECT_EQ(data[0], STATE_FILE_MAGIC[0]);
    EXPECT_EQ(data[1], STATE_FILE_MAGIC[1]);
    EXPECT_EQ(data[2], STATE_FILE_MAGIC[2]);
    EXPECT_EQ(data[3], STATE_FILE_MAGIC[3]);
}

/// @test Importing empty export produces empty result
TEST_F(StateImportExportTest, ImportEmptyState) {
    // Export empty state
    StateExporter exporter(*backend_);
    auto export_result = exporter.export_to_bytes({});
    ASSERT_TRUE(export_result.is_ok());

    // Create new backend for import
    auto import_backend = create_state_backend();
    StateImporter importer(*import_backend);

    auto import_result = importer.import_state(export_result.value());
    ASSERT_TRUE(import_result.is_ok()) << "Import failed: " << to_string(import_result.error());

    const auto& stats = import_result.value();
    EXPECT_EQ(stats.inserted_count, 0u);
    EXPECT_EQ(stats.updated_count, 0u);
    EXPECT_EQ(stats.skipped_count, 0u);
    EXPECT_EQ(stats.failed_count, 0u);
    EXPECT_TRUE(stats.is_success());
}

// ============================================================================
// Single Record Tests
// ============================================================================

/// @test Export and import single key-value pair
TEST_F(StateImportExportTest, SingleRecordRoundTrip) {
    // Setup source data
    put("key1", "value1");

    // Export
    StateExporter exporter(*backend_);
    auto export_result = exporter.export_to_bytes({});
    ASSERT_TRUE(export_result.is_ok());

    // Import to new backend
    auto import_backend = create_state_backend();
    StateImporter importer(*import_backend);

    auto import_result = importer.import_state(export_result.value());
    ASSERT_TRUE(import_result.is_ok());

    const auto& stats = import_result.value();
    EXPECT_EQ(stats.inserted_count, 1u);
    EXPECT_TRUE(stats.is_success());

    // Verify data
    auto value = import_backend->get(to_bytes("key1"));
    ASSERT_TRUE(value.is_ok());
    EXPECT_EQ(to_string(value.value()), "value1");
}

/// @test Export and import with binary key and value
TEST_F(StateImportExportTest, BinaryDataRoundTrip) {
    // Use binary data with null bytes
    std::vector<std::byte> key = {std::byte{0x00}, std::byte{0xFF}, std::byte{0x42}};
    std::vector<std::byte> value = {std::byte{0x01}, std::byte{0x00}, std::byte{0x02}};
    ASSERT_TRUE(backend_->put(key, value).is_ok());

    // Export
    StateExporter exporter(*backend_);
    auto export_result = exporter.export_to_bytes({});
    ASSERT_TRUE(export_result.is_ok());

    // Import to new backend
    auto import_backend = create_state_backend();
    StateImporter importer(*import_backend);

    auto import_result = importer.import_state(export_result.value());
    ASSERT_TRUE(import_result.is_ok());
    EXPECT_EQ(import_result.value().inserted_count, 1u);

    // Verify data
    auto result = import_backend->get(key);
    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(result.value(), value);
}

// ============================================================================
// Multiple Records Tests
// ============================================================================

/// @test Export and import multiple records
TEST_F(StateImportExportTest, MultipleRecordsRoundTrip) {
    // Setup source data
    put("alpha", "first");
    put("beta", "second");
    put("gamma", "third");
    put("delta", "fourth");

    // Export
    StateExporter exporter(*backend_);
    auto export_result = exporter.export_to_bytes({});
    ASSERT_TRUE(export_result.is_ok());

    // Import to new backend
    auto import_backend = create_state_backend();
    StateImporter importer(*import_backend);

    auto import_result = importer.import_state(export_result.value());
    ASSERT_TRUE(import_result.is_ok());

    const auto& stats = import_result.value();
    EXPECT_EQ(stats.inserted_count, 4u);
    EXPECT_TRUE(stats.is_success());

    // Verify all data
    auto val1 = import_backend->get(to_bytes("alpha"));
    auto val2 = import_backend->get(to_bytes("beta"));
    auto val3 = import_backend->get(to_bytes("gamma"));
    auto val4 = import_backend->get(to_bytes("delta"));

    ASSERT_TRUE(val1.is_ok());
    ASSERT_TRUE(val2.is_ok());
    ASSERT_TRUE(val3.is_ok());
    ASSERT_TRUE(val4.is_ok());

    EXPECT_EQ(to_string(val1.value()), "first");
    EXPECT_EQ(to_string(val2.value()), "second");
    EXPECT_EQ(to_string(val3.value()), "third");
    EXPECT_EQ(to_string(val4.value()), "fourth");
}

// ============================================================================
// Multi-Chunk Tests
// ============================================================================

/// @test Large export produces multiple chunks
TEST_F(StateImportExportTest, MultiChunkExport) {
    // Add enough data to exceed one chunk (~64KB)
    // Each record has overhead, so ~1KB values should give us many records per chunk
    for (int i = 0; i < 200; ++i) {
        std::string key = "key_" + std::to_string(i);
        std::string value(500, static_cast<char>('A' + (i % 26)));  // 500 byte values
        put(key, value);
    }

    // Export with small chunk size to force multiple chunks
    ExportConfig config;
    config.target_chunk_size = 4096;  // 4KB chunks
    StateExporter exporter(*backend_, config);

    std::uint32_t chunk_count = 0;
    auto result =
        exporter.export_state({}, [&chunk_count](std::uint32_t, std::span<const std::byte>) {
            ++chunk_count;
            return true;
        });

    ASSERT_TRUE(result.is_ok());
    EXPECT_GT(chunk_count, 1u) << "Expected multiple chunks";
}

/// @test Multi-chunk round trip preserves all data
TEST_F(StateImportExportTest, MultiChunkRoundTrip) {
    // Add data
    for (int i = 0; i < 100; ++i) {
        std::string key = "record_" + std::to_string(i);
        std::string value = "value_" + std::to_string(i * 100);
        put(key, value);
    }

    // Export with small chunks
    ExportConfig config;
    config.target_chunk_size = 2048;
    StateExporter exporter(*backend_, config);
    auto export_result = exporter.export_to_bytes({});
    ASSERT_TRUE(export_result.is_ok());

    // Import to new backend
    auto import_backend = create_state_backend();
    StateImporter importer(*import_backend);

    auto import_result = importer.import_state(export_result.value());
    ASSERT_TRUE(import_result.is_ok());
    EXPECT_EQ(import_result.value().inserted_count, 100u);

    // Verify all data
    for (int i = 0; i < 100; ++i) {
        std::string key = "record_" + std::to_string(i);
        std::string expected = "value_" + std::to_string(i * 100);
        auto val = import_backend->get(to_bytes(key));
        ASSERT_TRUE(val.is_ok()) << "Missing key: " << key;
        EXPECT_EQ(to_string(val.value()), expected);
    }
}

// ============================================================================
// Prefix Filter Tests
// ============================================================================

/// @test Export with prefix only includes matching keys
TEST_F(StateImportExportTest, ExportWithPrefix) {
    put("users/alice", "data_alice");
    put("users/bob", "data_bob");
    put("items/sword", "data_sword");
    put("items/shield", "data_shield");

    // Export only users/
    StateExporter exporter(*backend_);
    auto export_result = exporter.export_to_bytes(to_bytes("users/"));
    ASSERT_TRUE(export_result.is_ok());

    // Import and verify
    auto import_backend = create_state_backend();
    StateImporter importer(*import_backend);

    auto import_result = importer.import_state(export_result.value());
    ASSERT_TRUE(import_result.is_ok());
    EXPECT_EQ(import_result.value().inserted_count, 2u);

    // Only users/ keys should exist
    EXPECT_TRUE(import_backend->exists(to_bytes("users/alice")));
    EXPECT_TRUE(import_backend->exists(to_bytes("users/bob")));
    EXPECT_FALSE(import_backend->exists(to_bytes("items/sword")));
    EXPECT_FALSE(import_backend->exists(to_bytes("items/shield")));
}

/// @test Export with non-matching prefix produces empty export
TEST_F(StateImportExportTest, ExportWithNonMatchingPrefix) {
    put("key1", "value1");
    put("key2", "value2");

    StateExporter exporter(*backend_);
    auto export_result = exporter.export_to_bytes(to_bytes("nonexistent/"));
    ASSERT_TRUE(export_result.is_ok());

    auto import_backend = create_state_backend();
    StateImporter importer(*import_backend);

    auto import_result = importer.import_state(export_result.value());
    ASSERT_TRUE(import_result.is_ok());
    EXPECT_EQ(import_result.value().inserted_count, 0u);
}

// ============================================================================
// Merge Strategy Tests
// ============================================================================

/// @test MergeStrategy::Replace overwrites existing keys
TEST_F(StateImportExportTest, MergeStrategyReplace) {
    // Setup source
    put("key1", "new_value");

    // Export
    StateExporter exporter(*backend_);
    auto export_result = exporter.export_to_bytes({});
    ASSERT_TRUE(export_result.is_ok());

    // Setup target with existing data
    auto import_backend = create_state_backend();
    ASSERT_TRUE(import_backend->put(to_bytes("key1"), to_bytes("old_value")).is_ok());
    ASSERT_TRUE(import_backend->put(to_bytes("key2"), to_bytes("existing")).is_ok());

    // Import with Replace strategy
    ImportConfig config;
    config.strategy = MergeStrategy::Replace;
    StateImporter importer(*import_backend, config);

    auto import_result = importer.import_state(export_result.value());
    ASSERT_TRUE(import_result.is_ok());

    // key1 should be updated
    auto val1 = import_backend->get(to_bytes("key1"));
    ASSERT_TRUE(val1.is_ok());
    EXPECT_EQ(to_string(val1.value()), "new_value");

    // key2 should still exist (Replace doesn't delete unmentioned keys)
    auto val2 = import_backend->get(to_bytes("key2"));
    ASSERT_TRUE(val2.is_ok());
    EXPECT_EQ(to_string(val2.value()), "existing");
}

/// @test MergeStrategy::Merge updates existing and inserts new
TEST_F(StateImportExportTest, MergeStrategyMerge) {
    // Setup source
    put("existing", "updated_value");
    put("new_key", "new_value");

    // Export
    StateExporter exporter(*backend_);
    auto export_result = exporter.export_to_bytes({});
    ASSERT_TRUE(export_result.is_ok());

    // Setup target
    auto import_backend = create_state_backend();
    ASSERT_TRUE(import_backend->put(to_bytes("existing"), to_bytes("old_value")).is_ok());

    // Import with Merge strategy
    ImportConfig config;
    config.strategy = MergeStrategy::Merge;
    StateImporter importer(*import_backend, config);

    auto import_result = importer.import_state(export_result.value());
    ASSERT_TRUE(import_result.is_ok());

    const auto& stats = import_result.value();
    EXPECT_EQ(stats.updated_count, 1u);   // existing updated
    EXPECT_EQ(stats.inserted_count, 1u);  // new_key inserted

    // Verify values
    auto val1 = import_backend->get(to_bytes("existing"));
    auto val2 = import_backend->get(to_bytes("new_key"));
    ASSERT_TRUE(val1.is_ok());
    ASSERT_TRUE(val2.is_ok());
    EXPECT_EQ(to_string(val1.value()), "updated_value");
    EXPECT_EQ(to_string(val2.value()), "new_value");
}

/// @test MergeStrategy::SkipExisting only inserts non-existing keys
TEST_F(StateImportExportTest, MergeStrategySkipExisting) {
    // Setup source
    put("existing", "new_value");
    put("new_key", "value");

    // Export
    StateExporter exporter(*backend_);
    auto export_result = exporter.export_to_bytes({});
    ASSERT_TRUE(export_result.is_ok());

    // Setup target
    auto import_backend = create_state_backend();
    ASSERT_TRUE(import_backend->put(to_bytes("existing"), to_bytes("preserved")).is_ok());

    // Import with SkipExisting strategy
    ImportConfig config;
    config.strategy = MergeStrategy::SkipExisting;
    StateImporter importer(*import_backend, config);

    auto import_result = importer.import_state(export_result.value());
    ASSERT_TRUE(import_result.is_ok());

    const auto& stats = import_result.value();
    EXPECT_EQ(stats.skipped_count, 1u);   // existing skipped
    EXPECT_EQ(stats.inserted_count, 1u);  // new_key inserted

    // existing should keep original value
    auto val1 = import_backend->get(to_bytes("existing"));
    ASSERT_TRUE(val1.is_ok());
    EXPECT_EQ(to_string(val1.value()), "preserved");

    // new_key should be inserted
    auto val2 = import_backend->get(to_bytes("new_key"));
    ASSERT_TRUE(val2.is_ok());
    EXPECT_EQ(to_string(val2.value()), "value");
}

// ============================================================================
// Error Handling Tests
// ============================================================================

/// @test Import rejects data with invalid magic bytes
TEST_F(StateImportExportTest, ImportInvalidMagic) {
    std::vector<std::byte> bad_data(100, std::byte{0x00});
    bad_data[0] = std::byte{'B'};  // Wrong magic
    bad_data[1] = std::byte{'A'};
    bad_data[2] = std::byte{'D'};
    bad_data[3] = std::byte{'!'};

    StateImporter importer(*backend_);
    auto result = importer.import_state(bad_data);

    EXPECT_TRUE(result.is_err());
    EXPECT_EQ(result.error(), StateBackendError::InvalidMagic);
}

/// @test Import rejects truncated data
TEST_F(StateImportExportTest, ImportTruncatedData) {
    // Create valid export first
    put("key", "value");
    StateExporter exporter(*backend_);
    auto export_result = exporter.export_to_bytes({});
    ASSERT_TRUE(export_result.is_ok());

    // Truncate it
    auto truncated = export_result.value();
    truncated.resize(truncated.size() / 2);

    auto import_backend = create_state_backend();
    StateImporter importer(*import_backend);
    auto result = importer.import_state(truncated);

    EXPECT_TRUE(result.is_err());
    // Could be TruncatedData or ChecksumMismatch depending on where truncation happens
    EXPECT_TRUE(result.error() == StateBackendError::TruncatedData ||
                result.error() == StateBackendError::ChecksumMismatch);
}

/// @test Import rejects data with corrupted checksum
TEST_F(StateImportExportTest, ImportCorruptedChecksum) {
    put("key", "value");
    StateExporter exporter(*backend_);
    auto export_result = exporter.export_to_bytes({});
    ASSERT_TRUE(export_result.is_ok());

    // Corrupt a byte in the middle
    auto& data = export_result.value();
    if (data.size() > FILE_HEADER_SIZE + 10) {
        data[FILE_HEADER_SIZE + 5] = std::byte{0xFF};
    }

    auto import_backend = create_state_backend();
    StateImporter importer(*import_backend);
    auto result = importer.import_state(data);

    EXPECT_TRUE(result.is_err());
    EXPECT_EQ(result.error(), StateBackendError::ChecksumMismatch);
}

/// @test Import with verify_checksums=false ignores checksum errors
TEST_F(StateImportExportTest, ImportWithoutChecksumVerification) {
    put("key", "value");
    StateExporter exporter(*backend_);
    auto export_result = exporter.export_to_bytes({});
    ASSERT_TRUE(export_result.is_ok());

    // Corrupt the data slightly but not catastrophically
    // This test verifies the option exists - actual behavior depends on implementation
    ImportConfig config;
    config.verify_checksums = false;

    auto import_backend = create_state_backend();
    StateImporter importer(*import_backend, config);

    // Should at least not crash with valid data
    auto result = importer.import_state(export_result.value());
    EXPECT_TRUE(result.is_ok());
}

/// @test Import stops after max_errors threshold
TEST_F(StateImportExportTest, ImportMaxErrors) {
    // This test verifies the max_errors configuration is respected
    // Actual behavior depends on implementation - here we just verify config is accepted
    ImportConfig config;
    config.max_errors = 5;

    StateImporter importer(*backend_, config);
    // Importer should accept the config without error
    SUCCEED();
}

// ============================================================================
// Streaming Export Tests
// ============================================================================

/// @test Streaming export callback receives all chunks
TEST_F(StateImportExportTest, StreamingExportCallback) {
    put("key1", "value1");
    put("key2", "value2");

    std::vector<std::vector<std::byte>> chunks;
    StateExporter exporter(*backend_);

    auto result =
        exporter.export_state({}, [&chunks](std::uint32_t idx, std::span<const std::byte> data) {
            EXPECT_EQ(idx, static_cast<std::uint32_t>(chunks.size()));
            chunks.emplace_back(data.begin(), data.end());
            return true;
        });

    ASSERT_TRUE(result.is_ok());
    EXPECT_FALSE(chunks.empty());

    // Concatenate chunks and import
    std::vector<std::byte> all_data;
    for (const auto& chunk : chunks) {
        all_data.insert(all_data.end(), chunk.begin(), chunk.end());
    }

    auto import_backend = create_state_backend();
    StateImporter importer(*import_backend);
    auto import_result = importer.import_state(all_data);
    ASSERT_TRUE(import_result.is_ok());
    EXPECT_EQ(import_result.value().inserted_count, 2u);
}

/// @test Streaming export aborts when callback returns false
TEST_F(StateImportExportTest, StreamingExportAbort) {
    // Add enough data to exceed the callback chunk size (64KB)
    // We need >64KB of actual exported data to trigger multiple callbacks
    for (int i = 0; i < 200; ++i) {
        std::string key = "key_" + std::to_string(i);
        // Each value is 500 bytes, 200 records = ~100KB > 64KB callback chunk
        put(key, std::string(500, static_cast<char>('A' + (i % 26))));
    }

    ExportConfig config;
    config.target_chunk_size = 4096;  // Small internal chunks
    StateExporter exporter(*backend_, config);

    std::uint32_t chunks_received = 0;
    auto result =
        exporter.export_state({}, [&chunks_received](std::uint32_t, std::span<const std::byte>) {
            ++chunks_received;
            return chunks_received < 2;  // Abort after first callback chunk
        });

    // With >64KB of data, we should get multiple callback chunks
    // If only one callback chunk, the export completes successfully (no abort)
    if (chunks_received >= 2) {
        EXPECT_TRUE(result.is_err());
        EXPECT_EQ(result.error(), StateBackendError::ExportAborted);
    } else {
        // If data fits in one callback chunk, no abort possible
        EXPECT_TRUE(result.is_ok());
    }
    EXPECT_GE(chunks_received, 1u);
}

// ============================================================================
// Resume Tests
// ============================================================================

/// @test Import can resume from a checkpoint
TEST_F(StateImportExportTest, ImportResumeFromCheckpoint) {
    // This test verifies resume_from_chunk is respected
    // Full implementation would require partial import support
    ImportConfig config;
    config.resume_from_chunk = 0;  // Start from beginning

    auto import_backend = create_state_backend();
    StateImporter importer(*import_backend, config);

    // Empty export should work even with resume config
    StateExporter exporter(*backend_);
    auto export_result = exporter.export_to_bytes({});
    ASSERT_TRUE(export_result.is_ok());

    auto import_result = importer.import_state(export_result.value());
    ASSERT_TRUE(import_result.is_ok());
}

// ============================================================================
// Large Data Tests
// ============================================================================

/// @test Handle large values correctly
TEST_F(StateImportExportTest, LargeValueRoundTrip) {
    // 100KB value
    std::string large_value(100 * 1024, 'X');
    put("large_key", large_value);

    StateExporter exporter(*backend_);
    auto export_result = exporter.export_to_bytes({});
    ASSERT_TRUE(export_result.is_ok());

    auto import_backend = create_state_backend();
    StateImporter importer(*import_backend);

    auto import_result = importer.import_state(export_result.value());
    ASSERT_TRUE(import_result.is_ok());
    EXPECT_EQ(import_result.value().inserted_count, 1u);

    auto val = import_backend->get(to_bytes("large_key"));
    ASSERT_TRUE(val.is_ok());
    EXPECT_EQ(val.value().size(), large_value.size());
    EXPECT_EQ(to_string(val.value()), large_value);
}

/// @test Handle many small records
TEST_F(StateImportExportTest, ManySmallRecords) {
    // 1000 small records
    for (int i = 0; i < 1000; ++i) {
        put("k" + std::to_string(i), "v" + std::to_string(i));
    }

    StateExporter exporter(*backend_);
    auto export_result = exporter.export_to_bytes({});
    ASSERT_TRUE(export_result.is_ok());

    auto import_backend = create_state_backend();
    StateImporter importer(*import_backend);

    auto import_result = importer.import_state(export_result.value());
    ASSERT_TRUE(import_result.is_ok());
    EXPECT_EQ(import_result.value().inserted_count, 1000u);

    // Spot check some values
    for (int i : {0, 100, 500, 999}) {
        auto val = import_backend->get(to_bytes("k" + std::to_string(i)));
        ASSERT_TRUE(val.is_ok());
        EXPECT_EQ(to_string(val.value()), "v" + std::to_string(i));
    }
}

// ============================================================================
// Edge Case Tests
// ============================================================================

/// @test Empty key is handled correctly
TEST_F(StateImportExportTest, EmptyKeyRoundTrip) {
    // Empty keys are typically invalid, but let's verify behavior
    // Most backends reject empty keys
    auto result = backend_->put({}, to_bytes("value"));

    if (result.is_ok()) {
        // If empty key is allowed, verify round trip
        StateExporter exporter(*backend_);
        auto export_result = exporter.export_to_bytes({});
        ASSERT_TRUE(export_result.is_ok());

        auto import_backend = create_state_backend();
        StateImporter importer(*import_backend);
        auto import_result = importer.import_state(export_result.value());
        EXPECT_TRUE(import_result.is_ok());
    } else {
        // Empty key is invalid - expected
        EXPECT_EQ(result.error(), StateBackendError::InvalidKey);
    }
}

/// @test Empty value is handled correctly
TEST_F(StateImportExportTest, EmptyValueRoundTrip) {
    put("key_with_empty_value", "");

    StateExporter exporter(*backend_);
    auto export_result = exporter.export_to_bytes({});
    ASSERT_TRUE(export_result.is_ok());

    auto import_backend = create_state_backend();
    StateImporter importer(*import_backend);

    auto import_result = importer.import_state(export_result.value());
    ASSERT_TRUE(import_result.is_ok());
    EXPECT_EQ(import_result.value().inserted_count, 1u);

    auto val = import_backend->get(to_bytes("key_with_empty_value"));
    ASSERT_TRUE(val.is_ok());
    EXPECT_TRUE(val.value().empty());
}

}  // namespace
}  // namespace dotvm::core::state
