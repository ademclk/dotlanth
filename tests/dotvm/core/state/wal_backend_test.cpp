/// @file wal_backend_test.cpp
/// @brief Integration tests for STATE-007 WalBackend decorator
///
/// TDD tests for WalBackend wrapping InMemoryBackend with WAL.

#include <gtest/gtest.h>

#include <cstring>
#include <filesystem>

#include "dotvm/core/state/state_backend.hpp"
#include "dotvm/core/state/wal_backend.hpp"

namespace dotvm::core::state {
namespace {

/// @brief Create a byte span from a string literal
[[nodiscard]] std::span<const std::byte> to_bytes(std::string_view str) {
    return {reinterpret_cast<const std::byte*>(str.data()), str.size()};
}


/// @brief Convert byte vector to string for comparison
[[nodiscard]] std::string to_string(std::span<const std::byte> bytes) {
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

/// @brief Test fixture that creates a temporary WAL directory
class WalBackendTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_dir_ = std::filesystem::temp_directory_path() /
                    ("wal_backend_test_" + std::to_string(test_counter_++));
        std::filesystem::create_directories(test_dir_);
    }

    void TearDown() override {
        std::filesystem::remove_all(test_dir_);
    }

    WalBackendConfig default_config() {
        WalBackendConfig config;
        config.wal_config.wal_directory = test_dir_;
        config.wal_config.segment_size = 64 * 1024;  // 64KB for tests
        config.wal_config.buffer_size = 4096;
        config.wal_config.sync_policy = WalSyncPolicy::EveryCommit;
        return config;
    }

    std::filesystem::path test_dir_;
    static inline int test_counter_ = 0;
};

// ============================================================================
// WalBackendConfig Tests
// ============================================================================

TEST(WalBackendConfigTest, DefaultsAreValid) {
    auto config = WalBackendConfig::defaults();

    EXPECT_TRUE(config.is_valid());
    EXPECT_TRUE(config.wal_config.is_valid());
}

// ============================================================================
// WalBackend Creation Tests
// ============================================================================

TEST_F(WalBackendTest, CreateWithInMemoryBackend) {
    auto inner = create_state_backend();
    auto result = WalBackend::create(std::move(inner), default_config());

    ASSERT_TRUE(result.is_ok()) << "Failed: " << to_string(result.error());
    EXPECT_NE(result.value(), nullptr);
}

TEST_F(WalBackendTest, CreateWithInvalidConfigReturnsError) {
    auto inner = create_state_backend();
    WalBackendConfig config;
    config.wal_config.wal_directory = "";  // Invalid

    auto result = WalBackend::create(std::move(inner), config);

    EXPECT_TRUE(result.is_err());
}

// ============================================================================
// CRUD Operations with WAL
// ============================================================================

TEST_F(WalBackendTest, PutAndGetRoundtrip) {
    auto inner = create_state_backend();
    auto result = WalBackend::create(std::move(inner), default_config());
    ASSERT_TRUE(result.is_ok());
    auto& backend = result.value();

    auto key = to_bytes("test_key");
    auto value = to_bytes("test_value");

    auto put_result = backend->put(key, value);
    ASSERT_TRUE(put_result.is_ok());

    auto get_result = backend->get(key);
    ASSERT_TRUE(get_result.is_ok());
    EXPECT_EQ(to_string(get_result.value()), "test_value");
}

TEST_F(WalBackendTest, PutOverwritesExistingValue) {
    auto inner = create_state_backend();
    auto result = WalBackend::create(std::move(inner), default_config());
    ASSERT_TRUE(result.is_ok());
    auto& backend = result.value();

    auto key = to_bytes("key");
    auto value1 = to_bytes("value1");
    auto value2 = to_bytes("value2");

    ASSERT_TRUE(backend->put(key, value1).is_ok());
    ASSERT_TRUE(backend->put(key, value2).is_ok());

    auto get_result = backend->get(key);
    ASSERT_TRUE(get_result.is_ok());
    EXPECT_EQ(to_string(get_result.value()), "value2");
}

TEST_F(WalBackendTest, RemoveDeletesKey) {
    auto inner = create_state_backend();
    auto result = WalBackend::create(std::move(inner), default_config());
    ASSERT_TRUE(result.is_ok());
    auto& backend = result.value();

    auto key = to_bytes("key");
    auto value = to_bytes("value");

    ASSERT_TRUE(backend->put(key, value).is_ok());
    EXPECT_TRUE(backend->exists(key));

    ASSERT_TRUE(backend->remove(key).is_ok());
    EXPECT_FALSE(backend->exists(key));
}

TEST_F(WalBackendTest, GetNonExistentKeyReturnsError) {
    auto inner = create_state_backend();
    auto result = WalBackend::create(std::move(inner), default_config());
    ASSERT_TRUE(result.is_ok());
    auto& backend = result.value();

    auto get_result = backend->get(to_bytes("nonexistent"));
    EXPECT_TRUE(get_result.is_err());
    EXPECT_EQ(get_result.error(), StateBackendError::KeyNotFound);
}

// ============================================================================
// Recovery Tests
// ============================================================================

TEST_F(WalBackendTest, RecoveryRestoresData) {
    auto key = to_bytes("key");
    auto value = to_bytes("value");

    // Write data and close
    {
        auto inner = create_state_backend();
        auto result = WalBackend::create(std::move(inner), default_config());
        ASSERT_TRUE(result.is_ok());
        auto& backend = result.value();

        ASSERT_TRUE(backend->put(key, value).is_ok());
        ASSERT_TRUE(backend->sync_wal().is_ok());
    }

    // Re-open with fresh inner backend and recover
    {
        auto inner = create_state_backend();
        auto result = WalBackend::open(std::move(inner), test_dir_);
        ASSERT_TRUE(result.is_ok());
        auto& backend = result.value();

        ASSERT_TRUE(backend->recover().is_ok());

        auto get_result = backend->get(key);
        ASSERT_TRUE(get_result.is_ok());
        EXPECT_EQ(to_string(get_result.value()), "value");
    }
}

TEST_F(WalBackendTest, RecoveryRestoresMultipleKeys) {
    constexpr int NUM_KEYS = 100;

    // Write data
    {
        auto inner = create_state_backend();
        auto result = WalBackend::create(std::move(inner), default_config());
        ASSERT_TRUE(result.is_ok());
        auto& backend = result.value();

        for (int i = 0; i < NUM_KEYS; ++i) {
            std::string key_str = "key" + std::to_string(i);
            std::string value_str = "value" + std::to_string(i);
            ASSERT_TRUE(backend->put(to_bytes(key_str), to_bytes(value_str)).is_ok());
        }
        ASSERT_TRUE(backend->sync_wal().is_ok());
    }

    // Recover
    {
        auto inner = create_state_backend();
        auto result = WalBackend::open(std::move(inner), test_dir_);
        ASSERT_TRUE(result.is_ok());
        auto& backend = result.value();

        ASSERT_TRUE(backend->recover().is_ok());

        for (int i = 0; i < NUM_KEYS; ++i) {
            std::string key_str = "key" + std::to_string(i);
            std::string expected_value = "value" + std::to_string(i);

            auto get_result = backend->get(to_bytes(key_str));
            ASSERT_TRUE(get_result.is_ok()) << "Failed for key " << i;
            EXPECT_EQ(to_string(get_result.value()), expected_value);
        }
    }
}

TEST_F(WalBackendTest, RecoveryHandlesDeletes) {
    // Write and delete
    {
        auto inner = create_state_backend();
        auto result = WalBackend::create(std::move(inner), default_config());
        ASSERT_TRUE(result.is_ok());
        auto& backend = result.value();

        ASSERT_TRUE(backend->put(to_bytes("key1"), to_bytes("value1")).is_ok());
        ASSERT_TRUE(backend->put(to_bytes("key2"), to_bytes("value2")).is_ok());
        ASSERT_TRUE(backend->remove(to_bytes("key1")).is_ok());
        ASSERT_TRUE(backend->sync_wal().is_ok());
    }

    // Recover
    {
        auto inner = create_state_backend();
        auto result = WalBackend::open(std::move(inner), test_dir_);
        ASSERT_TRUE(result.is_ok());
        auto& backend = result.value();

        ASSERT_TRUE(backend->recover().is_ok());

        EXPECT_FALSE(backend->exists(to_bytes("key1")));
        EXPECT_TRUE(backend->exists(to_bytes("key2")));
    }
}

// ============================================================================
// Transaction Tests
// ============================================================================

TEST_F(WalBackendTest, TransactionCommitPersistsData) {
    auto key = to_bytes("tx_key");
    auto value = to_bytes("tx_value");

    // Write with transaction
    {
        auto inner = create_state_backend();
        auto result = WalBackend::create(std::move(inner), default_config());
        ASSERT_TRUE(result.is_ok());
        auto& backend = result.value();

        auto tx_result = backend->begin_transaction();
        ASSERT_TRUE(tx_result.is_ok());
        TxHandle tx = std::move(tx_result.value());

        ASSERT_TRUE(backend->put(key, value).is_ok());

        auto commit_result = backend->commit(std::move(tx));
        ASSERT_TRUE(commit_result.is_ok());

        ASSERT_TRUE(backend->sync_wal().is_ok());
    }

    // Recover and verify
    {
        auto inner = create_state_backend();
        auto result = WalBackend::open(std::move(inner), test_dir_);
        ASSERT_TRUE(result.is_ok());
        auto& backend = result.value();

        ASSERT_TRUE(backend->recover().is_ok());

        auto get_result = backend->get(key);
        ASSERT_TRUE(get_result.is_ok());
        EXPECT_EQ(to_string(get_result.value()), "tx_value");
    }
}

TEST_F(WalBackendTest, TransactionRollbackDoesNotPersist) {
    auto key = to_bytes("rollback_key");
    auto value = to_bytes("rollback_value");

    // Write with rollback
    {
        auto inner = create_state_backend();
        auto result = WalBackend::create(std::move(inner), default_config());
        ASSERT_TRUE(result.is_ok());
        auto& backend = result.value();

        auto tx_result = backend->begin_transaction();
        ASSERT_TRUE(tx_result.is_ok());
        TxHandle tx = std::move(tx_result.value());

        ASSERT_TRUE(backend->put(key, value).is_ok());

        auto rollback_result = backend->rollback(std::move(tx));
        ASSERT_TRUE(rollback_result.is_ok());

        ASSERT_TRUE(backend->sync_wal().is_ok());
    }

    // Recover and verify key doesn't exist
    {
        auto inner = create_state_backend();
        auto result = WalBackend::open(std::move(inner), test_dir_);
        ASSERT_TRUE(result.is_ok());
        auto& backend = result.value();

        ASSERT_TRUE(backend->recover().is_ok());

        EXPECT_FALSE(backend->exists(key));
    }
}

// ============================================================================
// Statistics Tests
// ============================================================================

TEST_F(WalBackendTest, KeyCountReflectsData) {
    auto inner = create_state_backend();
    auto result = WalBackend::create(std::move(inner), default_config());
    ASSERT_TRUE(result.is_ok());
    auto& backend = result.value();

    EXPECT_EQ(backend->key_count(), 0);

    ASSERT_TRUE(backend->put(to_bytes("k1"), to_bytes("v1")).is_ok());
    EXPECT_EQ(backend->key_count(), 1);

    ASSERT_TRUE(backend->put(to_bytes("k2"), to_bytes("v2")).is_ok());
    EXPECT_EQ(backend->key_count(), 2);
}

TEST_F(WalBackendTest, SupportsTransactions) {
    auto inner = create_state_backend();
    auto result = WalBackend::create(std::move(inner), default_config());
    ASSERT_TRUE(result.is_ok());
    auto& backend = result.value();

    EXPECT_TRUE(backend->supports_transactions());
}

}  // namespace
}  // namespace dotvm::core::state
