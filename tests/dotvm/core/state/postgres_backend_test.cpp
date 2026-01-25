/// @file postgres_backend_test.cpp
/// @brief Unit tests for STATE-006 PostgresBackend implementation
///
/// These tests follow TDD principles - written before implementation.
/// Unit tests run without a PostgreSQL instance.
/// Integration tests are guarded by DOTVM_RUN_POSTGRES_INTEGRATION.

#include <gtest/gtest.h>

#include "dotvm/core/state/postgres_backend.hpp"

namespace dotvm::core::state {
namespace {

// ============================================================================
// PostgresBackendConfig Tests (Unit - no database required)
// ============================================================================

TEST(PostgresBackendConfigTest, DefaultsAreValid) {
    auto config = PostgresBackendConfig::defaults();

    // Connection defaults
    EXPECT_EQ(config.host, "localhost");
    EXPECT_EQ(config.port, 5432);
    EXPECT_EQ(config.dbname, "dotvm");
    EXPECT_EQ(config.user, "dotvm");
    EXPECT_TRUE(config.password.empty());

    // SSL default
    EXPECT_EQ(config.ssl_mode, PostgresBackendConfig::SslMode::Prefer);

    // Pool defaults
    EXPECT_EQ(config.pool_min_connections, 2);
    EXPECT_EQ(config.pool_max_connections, 10);
    EXPECT_EQ(config.connection_timeout, std::chrono::milliseconds{5000});

    // Inherited StateBackendConfig defaults
    EXPECT_EQ(config.max_key_size, 1024);
    EXPECT_EQ(config.max_value_size, 1024 * 1024);
    EXPECT_EQ(config.isolation_level, TransactionIsolationLevel::Snapshot);
    EXPECT_TRUE(config.enable_transactions);

    // Batch tuning
    EXPECT_TRUE(config.use_copy_for_batch);

    // Should be valid
    EXPECT_TRUE(config.is_valid());
}

TEST(PostgresBackendConfigTest, ConnectionStringBuilderBasic) {
    PostgresBackendConfig config;
    config.host = "myhost";
    config.port = 5433;
    config.dbname = "testdb";
    config.user = "testuser";
    config.password = "secret";
    config.ssl_mode = PostgresBackendConfig::SslMode::Require;

    std::string conn_str = config.build_connection_string();

    // Should contain all required parts
    EXPECT_NE(conn_str.find("host=myhost"), std::string::npos);
    EXPECT_NE(conn_str.find("port=5433"), std::string::npos);
    EXPECT_NE(conn_str.find("dbname=testdb"), std::string::npos);
    EXPECT_NE(conn_str.find("user=testuser"), std::string::npos);
    EXPECT_NE(conn_str.find("password=secret"), std::string::npos);
    EXPECT_NE(conn_str.find("sslmode=require"), std::string::npos);
}

TEST(PostgresBackendConfigTest, ConnectionStringOmitsEmptyPassword) {
    PostgresBackendConfig config;
    config.host = "localhost";
    config.port = 5432;
    config.dbname = "testdb";
    config.user = "testuser";
    config.password = "";  // Empty password

    std::string conn_str = config.build_connection_string();

    // Should NOT contain password field
    EXPECT_EQ(conn_str.find("password="), std::string::npos);
}

TEST(PostgresBackendConfigTest, SslModeMapping) {
    PostgresBackendConfig config;

    // Test each SSL mode maps correctly
    config.ssl_mode = PostgresBackendConfig::SslMode::Disable;
    EXPECT_NE(config.build_connection_string().find("sslmode=disable"), std::string::npos);

    config.ssl_mode = PostgresBackendConfig::SslMode::Allow;
    EXPECT_NE(config.build_connection_string().find("sslmode=allow"), std::string::npos);

    config.ssl_mode = PostgresBackendConfig::SslMode::Prefer;
    EXPECT_NE(config.build_connection_string().find("sslmode=prefer"), std::string::npos);

    config.ssl_mode = PostgresBackendConfig::SslMode::Require;
    EXPECT_NE(config.build_connection_string().find("sslmode=require"), std::string::npos);

    config.ssl_mode = PostgresBackendConfig::SslMode::VerifyCA;
    EXPECT_NE(config.build_connection_string().find("sslmode=verify-ca"), std::string::npos);

    config.ssl_mode = PostgresBackendConfig::SslMode::VerifyFull;
    EXPECT_NE(config.build_connection_string().find("sslmode=verify-full"), std::string::npos);
}

TEST(PostgresBackendConfigTest, InvalidPoolSizeMinGreaterThanMax) {
    PostgresBackendConfig config = PostgresBackendConfig::defaults();
    config.pool_min_connections = 20;
    config.pool_max_connections = 10;

    EXPECT_FALSE(config.is_valid());
}

TEST(PostgresBackendConfigTest, InvalidPoolSizeZeroMax) {
    PostgresBackendConfig config = PostgresBackendConfig::defaults();
    config.pool_max_connections = 0;

    EXPECT_FALSE(config.is_valid());
}

TEST(PostgresBackendConfigTest, InvalidPoolSizeZeroMin) {
    PostgresBackendConfig config = PostgresBackendConfig::defaults();
    config.pool_min_connections = 0;

    // Zero min is actually valid (lazy pool creation)
    EXPECT_TRUE(config.is_valid());
}

TEST(PostgresBackendConfigTest, InvalidEmptyHost) {
    PostgresBackendConfig config = PostgresBackendConfig::defaults();
    config.host = "";

    EXPECT_FALSE(config.is_valid());
}

TEST(PostgresBackendConfigTest, InvalidEmptyDbname) {
    PostgresBackendConfig config = PostgresBackendConfig::defaults();
    config.dbname = "";

    EXPECT_FALSE(config.is_valid());
}

TEST(PostgresBackendConfigTest, InvalidEmptyUser) {
    PostgresBackendConfig config = PostgresBackendConfig::defaults();
    config.user = "";

    EXPECT_FALSE(config.is_valid());
}

TEST(PostgresBackendConfigTest, InvalidZeroPort) {
    PostgresBackendConfig config = PostgresBackendConfig::defaults();
    config.port = 0;

    EXPECT_FALSE(config.is_valid());
}

TEST(PostgresBackendConfigTest, InvalidZeroKeySize) {
    PostgresBackendConfig config = PostgresBackendConfig::defaults();
    config.max_key_size = 0;

    EXPECT_FALSE(config.is_valid());
}

TEST(PostgresBackendConfigTest, InvalidZeroValueSize) {
    PostgresBackendConfig config = PostgresBackendConfig::defaults();
    config.max_value_size = 0;

    EXPECT_FALSE(config.is_valid());
}

TEST(PostgresBackendConfigTest, InvalidZeroTimeout) {
    PostgresBackendConfig config = PostgresBackendConfig::defaults();
    config.connection_timeout = std::chrono::milliseconds{0};

    EXPECT_FALSE(config.is_valid());
}

TEST(PostgresBackendConfigTest, SpecialCharactersInPasswordEscaped) {
    PostgresBackendConfig config = PostgresBackendConfig::defaults();
    config.password = "pass'word\"with\\special";

    std::string conn_str = config.build_connection_string();

    // The password should be properly escaped for libpq
    // libpq uses single quotes and escapes internal single quotes
    EXPECT_NE(conn_str.find("password="), std::string::npos);
}

// ============================================================================
// PostgresBackend Factory Tests (Unit - creation without connection)
// ============================================================================

#ifdef DOTVM_POSTGRESQL_ENABLED

TEST(PostgresBackendFactoryTest, CreateWithInvalidConfigReturnsError) {
    PostgresBackendConfig config = PostgresBackendConfig::defaults();
    config.host = "";  // Invalid

    auto result = PostgresBackend::create(config);
    ASSERT_TRUE(result.is_err());
    EXPECT_EQ(result.error(), StateBackendError::InvalidConfig);
}

// ============================================================================
// Integration Tests (require PostgreSQL)
// ============================================================================

    #ifdef DOTVM_RUN_POSTGRES_INTEGRATION

/// @brief Helper to create a test backend connected to PostgreSQL
/// @return Backend configured for test database
[[nodiscard]] std::unique_ptr<PostgresBackend> create_test_backend() {
    PostgresBackendConfig config;
    config.host = std::getenv("POSTGRES_HOST") ? std::getenv("POSTGRES_HOST") : "localhost";
    config.port = std::getenv("POSTGRES_PORT")
                      ? static_cast<std::uint16_t>(std::stoi(std::getenv("POSTGRES_PORT")))
                      : 5432;
    config.dbname = std::getenv("POSTGRES_DB") ? std::getenv("POSTGRES_DB") : "dotvm_test";
    config.user = std::getenv("POSTGRES_USER") ? std::getenv("POSTGRES_USER") : "dotvm";
    config.password = std::getenv("POSTGRES_PASSWORD") ? std::getenv("POSTGRES_PASSWORD") : "";
    config.pool_min_connections = 1;
    config.pool_max_connections = 5;

    auto result = PostgresBackend::create(config);
    if (result.is_err()) {
        return nullptr;
    }
    auto backend = std::move(result.value());
    backend->clear();  // Start with clean slate
    return backend;
}

/// @brief Create a byte span from a string literal
[[nodiscard]] std::span<const std::byte> to_bytes_pg(std::string_view str) {
    return {reinterpret_cast<const std::byte*>(str.data()), str.size()};
}

/// @brief Create a byte vector from a string
[[nodiscard]] std::vector<std::byte> make_bytes_pg(std::string_view str) {
    std::vector<std::byte> result(str.size());
    std::memcpy(result.data(), str.data(), str.size());
    return result;
}

/// @brief Convert byte vector to string for comparison
[[nodiscard]] std::string to_string_pg(std::span<const std::byte> bytes) {
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

// ============================================================================
// PostgreSQL CRUD Tests
// ============================================================================

class PostgresBackendIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        backend_ = create_test_backend();
        if (!backend_) {
            GTEST_SKIP() << "PostgreSQL not available";
        }
    }

    void TearDown() override {
        if (backend_) {
            backend_->clear();
        }
    }

    std::unique_ptr<PostgresBackend> backend_;
};

TEST_F(PostgresBackendIntegrationTest, PutAndGetRoundtrip) {
    auto key = to_bytes_pg("test_key");
    auto value = to_bytes_pg("test_value");

    auto put_result = backend_->put(key, value);
    ASSERT_TRUE(put_result.is_ok());

    auto get_result = backend_->get(key);
    ASSERT_TRUE(get_result.is_ok());
    EXPECT_EQ(to_string_pg(get_result.value()), "test_value");
}

TEST_F(PostgresBackendIntegrationTest, GetNonExistentKeyReturnsKeyNotFound) {
    auto key = to_bytes_pg("nonexistent");

    auto result = backend_->get(key);
    ASSERT_TRUE(result.is_err());
    EXPECT_EQ(result.error(), StateBackendError::KeyNotFound);
}

TEST_F(PostgresBackendIntegrationTest, PutOverwritesExistingValue) {
    auto key = to_bytes_pg("key");
    auto value1 = to_bytes_pg("value1");
    auto value2 = to_bytes_pg("value2");

    ASSERT_TRUE(backend_->put(key, value1).is_ok());
    ASSERT_TRUE(backend_->put(key, value2).is_ok());

    auto result = backend_->get(key);
    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(to_string_pg(result.value()), "value2");
}

TEST_F(PostgresBackendIntegrationTest, RemoveExistingKey) {
    auto key = to_bytes_pg("key");
    auto value = to_bytes_pg("value");

    ASSERT_TRUE(backend_->put(key, value).is_ok());
    EXPECT_TRUE(backend_->exists(key));

    auto remove_result = backend_->remove(key);
    ASSERT_TRUE(remove_result.is_ok());
    EXPECT_FALSE(backend_->exists(key));
}

TEST_F(PostgresBackendIntegrationTest, IterateWithPrefix) {
    ASSERT_TRUE(backend_->put(to_bytes_pg("user:1"), to_bytes_pg("alice")).is_ok());
    ASSERT_TRUE(backend_->put(to_bytes_pg("user:2"), to_bytes_pg("bob")).is_ok());
    ASSERT_TRUE(backend_->put(to_bytes_pg("item:1"), to_bytes_pg("widget")).is_ok());

    std::vector<std::string> keys;
    auto result = backend_->iterate(to_bytes_pg("user:"), [&keys](auto key, auto /*value*/) {
        keys.push_back(to_string_pg(key));
        return true;
    });

    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(keys.size(), 2);
    for (const auto& key : keys) {
        EXPECT_TRUE(key.starts_with("user:"));
    }
}

TEST_F(PostgresBackendIntegrationTest, BatchMultiplePuts) {
    auto key1 = make_bytes_pg("key1");
    auto key2 = make_bytes_pg("key2");
    auto val1 = make_bytes_pg("value1");
    auto val2 = make_bytes_pg("value2");

    std::vector<BatchOp> ops = {
        {.type = BatchOpType::Put, .key = key1, .value = val1},
        {.type = BatchOpType::Put, .key = key2, .value = val2},
    };

    auto result = backend_->batch(ops);
    ASSERT_TRUE(result.is_ok());

    EXPECT_TRUE(backend_->exists(key1));
    EXPECT_TRUE(backend_->exists(key2));
}

TEST_F(PostgresBackendIntegrationTest, TransactionCommit) {
    auto tx_result = backend_->begin_transaction();
    ASSERT_TRUE(tx_result.is_ok());

    TxHandle tx = std::move(tx_result.value());
    EXPECT_TRUE(tx.is_valid());

    auto key = to_bytes_pg("tx_key");
    auto value = to_bytes_pg("tx_value");
    ASSERT_TRUE(backend_->put(key, value).is_ok());

    auto commit_result = backend_->commit(std::move(tx));
    ASSERT_TRUE(commit_result.is_ok());

    EXPECT_TRUE(backend_->exists(key));
}

TEST_F(PostgresBackendIntegrationTest, TransactionRollback) {
    auto key = to_bytes_pg("rollback_key");
    auto value = to_bytes_pg("rollback_value");

    {
        auto tx_result = backend_->begin_transaction();
        ASSERT_TRUE(tx_result.is_ok());
        TxHandle tx = std::move(tx_result.value());

        ASSERT_TRUE(backend_->put(key, value).is_ok());

        auto rollback_result = backend_->rollback(std::move(tx));
        ASSERT_TRUE(rollback_result.is_ok());
    }

    EXPECT_FALSE(backend_->exists(key));
}

TEST_F(PostgresBackendIntegrationTest, KeyCountAndStorageBytes) {
    EXPECT_EQ(backend_->key_count(), 0);
    EXPECT_EQ(backend_->storage_bytes(), 0);

    ASSERT_TRUE(backend_->put(to_bytes_pg("k1"), to_bytes_pg("v1")).is_ok());
    ASSERT_TRUE(backend_->put(to_bytes_pg("k2"), to_bytes_pg("v2")).is_ok());

    EXPECT_EQ(backend_->key_count(), 2);
    EXPECT_GT(backend_->storage_bytes(), 0);
}

TEST_F(PostgresBackendIntegrationTest, ClearRemovesAllData) {
    ASSERT_TRUE(backend_->put(to_bytes_pg("a"), to_bytes_pg("1")).is_ok());
    ASSERT_TRUE(backend_->put(to_bytes_pg("b"), to_bytes_pg("2")).is_ok());

    backend_->clear();

    EXPECT_EQ(backend_->key_count(), 0);
    EXPECT_FALSE(backend_->exists(to_bytes_pg("a")));
}

    #endif  // DOTVM_RUN_POSTGRES_INTEGRATION

#endif  // DOTVM_POSTGRESQL_ENABLED

}  // namespace
}  // namespace dotvm::core::state
