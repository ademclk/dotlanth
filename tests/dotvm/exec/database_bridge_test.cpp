/// @file database_bridge_test.cpp
/// @brief STATE-005 DatabaseBridge Tests (TDD)

#include <cmath>
#include <cstring>
#include <limits>
#include <vector>

#include <gtest/gtest.h>

#include "dotvm/core/state/state_backend.hpp"
#include "dotvm/core/state/transaction_manager.hpp"
#include "dotvm/core/vm_context.hpp"
#include "dotvm/exec/database_bridge.hpp"

namespace dotvm::exec::test {

// ============================================================================
// Test Helpers
// ============================================================================

/// Helper to discard nodiscard return values in tests
template <typename T>
void discard([[maybe_unused]] T&& value) {}

/// Helper to write a string into VM memory and return the handle
inline core::Handle write_key_to_memory(core::VmContext& ctx, std::string_view key) {
    auto alloc_size = key.size() > 0 ? key.size() : 1;  // Ensure non-zero allocation
    auto result = ctx.memory().allocate(alloc_size);
    if (!result) {
        return core::MemoryManager::invalid_handle();
    }
    auto handle = result.value();
    if (key.size() > 0) {
        auto err = ctx.memory().write_bytes(handle, 0, key.data(), key.size());
        if (err != core::MemoryError::Success) {
            discard(ctx.memory().deallocate(handle));
            return core::MemoryManager::invalid_handle();
        }
    }
    return handle;
}

/// Helper to deallocate a handle (discarding nodiscard)
inline void dealloc(core::VmContext& ctx, core::Handle h) {
    discard(ctx.memory().deallocate(h));
}

// ============================================================================
// Value Serialization Tests
// ============================================================================

class ValueSerializationTest : public ::testing::Test {};

TEST_F(ValueSerializationTest, SerializeNil) {
    auto value = core::Value::nil();
    auto bytes = DatabaseBridge::serialize_value(value);

    ASSERT_EQ(bytes.size(), 1);
    EXPECT_EQ(static_cast<std::uint8_t>(bytes[0]), value_tag::NIL);
}

TEST_F(ValueSerializationTest, DeserializeNil) {
    std::vector<std::byte> data = {std::byte{value_tag::NIL}};

    auto result = DatabaseBridge::deserialize_value(data);
    ASSERT_TRUE(result.is_ok()) << "Error code: " << static_cast<int>(result.error());
    EXPECT_TRUE(result.value().is_nil());
}

TEST_F(ValueSerializationTest, SerializeInteger) {
    auto value = core::Value::from_int(42);
    auto bytes = DatabaseBridge::serialize_value(value);

    ASSERT_EQ(bytes.size(), 9);  // 1 tag + 8 payload
    EXPECT_EQ(static_cast<std::uint8_t>(bytes[0]), value_tag::INTEGER);

    // Verify little-endian encoding
    std::int64_t decoded = 0;
    std::memcpy(&decoded, &bytes[1], 8);
    EXPECT_EQ(decoded, 42);
}

TEST_F(ValueSerializationTest, SerializeNegativeInteger) {
    auto value = core::Value::from_int(-12345);
    auto bytes = DatabaseBridge::serialize_value(value);

    ASSERT_EQ(bytes.size(), 9);
    EXPECT_EQ(static_cast<std::uint8_t>(bytes[0]), value_tag::INTEGER);

    std::int64_t decoded = 0;
    std::memcpy(&decoded, &bytes[1], 8);
    EXPECT_EQ(decoded, -12345);
}

TEST_F(ValueSerializationTest, SerializeMaxInteger) {
    // 48-bit max signed value
    constexpr std::int64_t max_48bit = (1LL << 47) - 1;
    auto value = core::Value::from_int(max_48bit);
    auto bytes = DatabaseBridge::serialize_value(value);

    auto result = DatabaseBridge::deserialize_value(bytes);
    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(result.value().as_integer(), max_48bit);
}

TEST_F(ValueSerializationTest, SerializeMinInteger) {
    // 48-bit min signed value
    constexpr std::int64_t min_48bit = -(1LL << 47);
    auto value = core::Value::from_int(min_48bit);
    auto bytes = DatabaseBridge::serialize_value(value);

    auto result = DatabaseBridge::deserialize_value(bytes);
    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(result.value().as_integer(), min_48bit);
}

TEST_F(ValueSerializationTest, DeserializeInteger) {
    std::vector<std::byte> data(9);
    data[0] = std::byte{value_tag::INTEGER};
    std::int64_t val = 12345;
    std::memcpy(&data[1], &val, 8);

    auto result = DatabaseBridge::deserialize_value(data);
    ASSERT_TRUE(result.is_ok());
    ASSERT_TRUE(result.value().is_integer());
    EXPECT_EQ(result.value().as_integer(), 12345);
}

TEST_F(ValueSerializationTest, SerializeFloat) {
    auto value = core::Value::from_float(3.14159);
    auto bytes = DatabaseBridge::serialize_value(value);

    ASSERT_EQ(bytes.size(), 9);  // 1 tag + 8 payload
    EXPECT_EQ(static_cast<std::uint8_t>(bytes[0]), value_tag::FLOAT);

    double decoded = 0.0;
    std::memcpy(&decoded, &bytes[1], 8);
    EXPECT_DOUBLE_EQ(decoded, 3.14159);
}

TEST_F(ValueSerializationTest, SerializeNegativeFloat) {
    auto value = core::Value::from_float(-2.71828);
    auto bytes = DatabaseBridge::serialize_value(value);

    auto result = DatabaseBridge::deserialize_value(bytes);
    ASSERT_TRUE(result.is_ok());
    EXPECT_DOUBLE_EQ(result.value().as_float(), -2.71828);
}

TEST_F(ValueSerializationTest, SerializeFloatInfinity) {
    auto value = core::Value::from_float(std::numeric_limits<double>::infinity());
    auto bytes = DatabaseBridge::serialize_value(value);

    auto result = DatabaseBridge::deserialize_value(bytes);
    ASSERT_TRUE(result.is_ok());
    EXPECT_TRUE(std::isinf(result.value().as_float()));
    EXPECT_GT(result.value().as_float(), 0);
}

TEST_F(ValueSerializationTest, SerializeFloatNegativeInfinity) {
    auto value = core::Value::from_float(-std::numeric_limits<double>::infinity());
    auto bytes = DatabaseBridge::serialize_value(value);

    auto result = DatabaseBridge::deserialize_value(bytes);
    ASSERT_TRUE(result.is_ok());
    EXPECT_TRUE(std::isinf(result.value().as_float()));
    EXPECT_LT(result.value().as_float(), 0);
}

TEST_F(ValueSerializationTest, SerializeFloatZero) {
    auto value = core::Value::from_float(0.0);
    auto bytes = DatabaseBridge::serialize_value(value);

    auto result = DatabaseBridge::deserialize_value(bytes);
    ASSERT_TRUE(result.is_ok());
    EXPECT_DOUBLE_EQ(result.value().as_float(), 0.0);
}

TEST_F(ValueSerializationTest, DeserializeFloat) {
    std::vector<std::byte> data(9);
    data[0] = std::byte{value_tag::FLOAT};
    double val = 1.23456789;
    std::memcpy(&data[1], &val, 8);

    auto result = DatabaseBridge::deserialize_value(data);
    ASSERT_TRUE(result.is_ok());
    ASSERT_TRUE(result.value().is_float());
    EXPECT_DOUBLE_EQ(result.value().as_float(), 1.23456789);
}

TEST_F(ValueSerializationTest, SerializeBoolTrue) {
    auto value = core::Value::from_bool(true);
    auto bytes = DatabaseBridge::serialize_value(value);

    ASSERT_EQ(bytes.size(), 2);  // 1 tag + 1 payload
    EXPECT_EQ(static_cast<std::uint8_t>(bytes[0]), value_tag::BOOL);
    EXPECT_EQ(static_cast<std::uint8_t>(bytes[1]), 1);
}

TEST_F(ValueSerializationTest, SerializeBoolFalse) {
    auto value = core::Value::from_bool(false);
    auto bytes = DatabaseBridge::serialize_value(value);

    ASSERT_EQ(bytes.size(), 2);
    EXPECT_EQ(static_cast<std::uint8_t>(bytes[0]), value_tag::BOOL);
    EXPECT_EQ(static_cast<std::uint8_t>(bytes[1]), 0);
}

TEST_F(ValueSerializationTest, DeserializeBoolTrue) {
    std::vector<std::byte> data = {std::byte{value_tag::BOOL}, std::byte{1}};

    auto result = DatabaseBridge::deserialize_value(data);
    ASSERT_TRUE(result.is_ok());
    ASSERT_TRUE(result.value().is_bool());
    EXPECT_TRUE(result.value().as_bool());
}

TEST_F(ValueSerializationTest, DeserializeBoolFalse) {
    std::vector<std::byte> data = {std::byte{value_tag::BOOL}, std::byte{0}};

    auto result = DatabaseBridge::deserialize_value(data);
    ASSERT_TRUE(result.is_ok());
    ASSERT_TRUE(result.value().is_bool());
    EXPECT_FALSE(result.value().as_bool());
}

TEST_F(ValueSerializationTest, SerializeHandle) {
    core::Handle h{.index = 42, .generation = 7};
    auto value = core::Value::from_handle(h);
    auto bytes = DatabaseBridge::serialize_value(value);

    ASSERT_EQ(bytes.size(), 9);  // 1 tag + 4 index + 4 generation
    EXPECT_EQ(static_cast<std::uint8_t>(bytes[0]), value_tag::HANDLE);

    std::uint32_t index = 0, gen = 0;
    std::memcpy(&index, &bytes[1], 4);
    std::memcpy(&gen, &bytes[5], 4);
    EXPECT_EQ(index, 42u);
    EXPECT_EQ(gen, 7u);
}

TEST_F(ValueSerializationTest, DeserializeHandle) {
    std::vector<std::byte> data(9);
    data[0] = std::byte{value_tag::HANDLE};
    std::uint32_t index = 100, gen = 5;
    std::memcpy(&data[1], &index, 4);
    std::memcpy(&data[5], &gen, 4);

    auto result = DatabaseBridge::deserialize_value(data);
    ASSERT_TRUE(result.is_ok());
    ASSERT_TRUE(result.value().is_handle());

    auto handle = result.value().as_handle();
    EXPECT_EQ(handle.index, 100u);
    // Note: NaN-boxing truncates generation to 16 bits
    EXPECT_EQ(handle.generation, 5u);
}

TEST_F(ValueSerializationTest, SerializePointer) {
    void* ptr = reinterpret_cast<void*>(0x7FFE'DEAD'BEEF);
    auto value = core::Value{ptr};
    auto bytes = DatabaseBridge::serialize_value(value);

    ASSERT_EQ(bytes.size(), 9);  // 1 tag + 8 address
    EXPECT_EQ(static_cast<std::uint8_t>(bytes[0]), value_tag::POINTER);

    std::uint64_t addr = 0;
    std::memcpy(&addr, &bytes[1], 8);
    EXPECT_EQ(addr, 0x7FFE'DEAD'BEEF);
}

TEST_F(ValueSerializationTest, DeserializePointer) {
    std::vector<std::byte> data(9);
    data[0] = std::byte{value_tag::POINTER};
    std::uint64_t addr = 0x1234'5678'9ABC;
    std::memcpy(&data[1], &addr, 8);

    auto result = DatabaseBridge::deserialize_value(data);
    ASSERT_TRUE(result.is_ok());
    ASSERT_TRUE(result.value().is_pointer());

    auto ptr = result.value().as_pointer();
    EXPECT_EQ(reinterpret_cast<std::uint64_t>(ptr), 0x1234'5678'9ABC);
}

TEST_F(ValueSerializationTest, RoundTripAllTypes) {
    std::vector<core::Value> values = {
        core::Value::nil(),
        core::Value::from_int(0),
        core::Value::from_int(-1),
        core::Value::from_int(1000000),
        core::Value::from_float(0.0),
        core::Value::from_float(-1.5),
        core::Value::from_float(1e100),
        core::Value::from_bool(true),
        core::Value::from_bool(false),
        core::Value::from_handle({.index = 0, .generation = 0}),
        core::Value::from_handle({.index = 0xFFFFFFFF, .generation = 0xFFFF}),
    };

    for (const auto& original : values) {
        auto bytes = DatabaseBridge::serialize_value(original);
        auto result = DatabaseBridge::deserialize_value(bytes);

        ASSERT_TRUE(result.is_ok()) << "Failed to deserialize value of type "
            << static_cast<int>(original.type());
        EXPECT_EQ(result.value().type(), original.type());

        // Type-specific comparisons
        if (original.is_nil()) {
            EXPECT_TRUE(result.value().is_nil());
        } else if (original.is_integer()) {
            EXPECT_EQ(result.value().as_integer(), original.as_integer());
        } else if (original.is_float()) {
            EXPECT_DOUBLE_EQ(result.value().as_float(), original.as_float());
        } else if (original.is_bool()) {
            EXPECT_EQ(result.value().as_bool(), original.as_bool());
        } else if (original.is_handle()) {
            auto h1 = original.as_handle();
            auto h2 = result.value().as_handle();
            EXPECT_EQ(h1.index, h2.index);
            // Generation is truncated to 16 bits in NaN-boxing
            EXPECT_EQ(h1.generation & 0xFFFF, h2.generation & 0xFFFF);
        }
    }
}

TEST_F(ValueSerializationTest, DeserializeEmptyData) {
    std::vector<std::byte> data;
    auto result = DatabaseBridge::deserialize_value(data);
    EXPECT_TRUE(result.is_err());
    EXPECT_EQ(result.error(), DatabaseBridgeError::SerializationError);
}

TEST_F(ValueSerializationTest, DeserializeInvalidTag) {
    std::vector<std::byte> data = {std::byte{0xFF}};  // Invalid tag
    auto result = DatabaseBridge::deserialize_value(data);
    EXPECT_TRUE(result.is_err());
    EXPECT_EQ(result.error(), DatabaseBridgeError::SerializationError);
}

TEST_F(ValueSerializationTest, DeserializeTruncatedInteger) {
    std::vector<std::byte> data = {std::byte{value_tag::INTEGER}, std::byte{0}, std::byte{0}};
    auto result = DatabaseBridge::deserialize_value(data);
    EXPECT_TRUE(result.is_err());
    EXPECT_EQ(result.error(), DatabaseBridgeError::SerializationError);
}

TEST_F(ValueSerializationTest, DeserializeTruncatedFloat) {
    std::vector<std::byte> data = {std::byte{value_tag::FLOAT}, std::byte{0}};
    auto result = DatabaseBridge::deserialize_value(data);
    EXPECT_TRUE(result.is_err());
    EXPECT_EQ(result.error(), DatabaseBridgeError::SerializationError);
}

TEST_F(ValueSerializationTest, DeserializeTruncatedBool) {
    std::vector<std::byte> data = {std::byte{value_tag::BOOL}};  // Missing payload
    auto result = DatabaseBridge::deserialize_value(data);
    EXPECT_TRUE(result.is_err());
    EXPECT_EQ(result.error(), DatabaseBridgeError::SerializationError);
}

// ============================================================================
// DatabaseBridge Infrastructure Tests
// ============================================================================

class DatabaseBridgeTest : public ::testing::Test {
protected:
    void SetUp() override {
        core::state::StateBackendConfig backend_config;
        backend_config.enable_transactions = true;
        backend_config.isolation_level = core::state::TransactionIsolationLevel::Snapshot;

        core::state::TransactionManagerConfig tm_config;
        tm_config.max_concurrent_transactions = 100;
        tm_config.default_timeout = std::chrono::milliseconds{5000};
        tm_config.default_isolation = core::state::TransactionIsolationLevel::Snapshot;

        auto backend = core::state::create_state_backend(backend_config);
        tx_mgr_ = std::make_unique<core::state::TransactionManager>(std::move(backend), tm_config);

        ctx_ = std::make_unique<core::VmContext>(core::Architecture::Arch64);
        ctx_->enable_state(tx_mgr_.get());
    }

    std::unique_ptr<core::state::TransactionManager> tx_mgr_;
    std::unique_ptr<core::VmContext> ctx_;
};

TEST_F(DatabaseBridgeTest, ConstructionWithDefaultNamespace) {
    DatabaseBridge bridge{*ctx_};
    EXPECT_EQ(bridge.namespace_id(), 0);
}

TEST_F(DatabaseBridgeTest, ConstructionWithCustomNamespace) {
    DatabaseBridge bridge{*ctx_, 12345};
    EXPECT_EQ(bridge.namespace_id(), 12345);
}

TEST_F(DatabaseBridgeTest, SetNamespaceId) {
    DatabaseBridge bridge{*ctx_};
    bridge.set_namespace_id(999);
    EXPECT_EQ(bridge.namespace_id(), 999);
}

TEST_F(DatabaseBridgeTest, InitialStatsAreZero) {
    DatabaseBridge bridge{*ctx_};
    const auto& stats = bridge.stats();

    EXPECT_EQ(stats.reads, 0);
    EXPECT_EQ(stats.writes, 0);
    EXPECT_EQ(stats.deletes, 0);
    EXPECT_EQ(stats.exists_checks, 0);
    EXPECT_EQ(stats.bytes_read, 0);
    EXPECT_EQ(stats.bytes_written, 0);
}

TEST_F(DatabaseBridgeTest, ResetStats) {
    DatabaseBridge bridge{*ctx_};

    // Write something to increment stats
    auto key_handle = write_key_to_memory(*ctx_, "test_key");
    ASSERT_NE(key_handle.index, core::mem_config::INVALID_INDEX);

    auto write_result = bridge.write_key(0, key_handle, core::Value::from_int(42));
    ASSERT_TRUE(write_result.is_ok());
    EXPECT_GT(bridge.stats().writes, 0);

    bridge.reset_stats();

    EXPECT_EQ(bridge.stats().reads, 0);
    EXPECT_EQ(bridge.stats().writes, 0);
    EXPECT_EQ(bridge.stats().bytes_read, 0);
    EXPECT_EQ(bridge.stats().bytes_written, 0);

    dealloc(*ctx_, key_handle);
}

// ============================================================================
// Namespace Isolation Tests
// ============================================================================

TEST_F(DatabaseBridgeTest, NamespaceIsolation) {
    DatabaseBridge bridge1{*ctx_, 1};
    DatabaseBridge bridge2{*ctx_, 2};

    // Write same key with different namespaces
    auto key_handle = write_key_to_memory(*ctx_, "shared_key");
    ASSERT_NE(key_handle.index, core::mem_config::INVALID_INDEX);

    auto result1 = bridge1.write_key(0, key_handle, core::Value::from_int(100));
    ASSERT_TRUE(result1.is_ok());

    auto result2 = bridge2.write_key(0, key_handle, core::Value::from_int(200));
    ASSERT_TRUE(result2.is_ok());

    // Read back - each namespace should see its own value
    auto read1 = bridge1.read_key(0, key_handle);
    ASSERT_TRUE(read1.is_ok());
    EXPECT_EQ(read1.value().as_integer(), 100);

    auto read2 = bridge2.read_key(0, key_handle);
    ASSERT_TRUE(read2.is_ok());
    EXPECT_EQ(read2.value().as_integer(), 200);

    dealloc(*ctx_, key_handle);
}

// ============================================================================
// State Operations Tests
// ============================================================================

TEST_F(DatabaseBridgeTest, WriteAndReadKey) {
    DatabaseBridge bridge{*ctx_};

    auto key_handle = write_key_to_memory(*ctx_, "my_key");
    ASSERT_NE(key_handle.index, core::mem_config::INVALID_INDEX);

    // Write
    auto write_result = bridge.write_key(0, key_handle, core::Value::from_int(42));
    ASSERT_TRUE(write_result.is_ok());

    // Read
    auto read_result = bridge.read_key(0, key_handle);
    ASSERT_TRUE(read_result.is_ok());
    EXPECT_TRUE(read_result.value().is_integer());
    EXPECT_EQ(read_result.value().as_integer(), 42);

    dealloc(*ctx_, key_handle);
}

TEST_F(DatabaseBridgeTest, WriteAndReadFloat) {
    DatabaseBridge bridge{*ctx_};

    auto key_handle = write_key_to_memory(*ctx_, "float_key");
    ASSERT_NE(key_handle.index, core::mem_config::INVALID_INDEX);

    auto write_result = bridge.write_key(0, key_handle, core::Value::from_float(3.14));
    ASSERT_TRUE(write_result.is_ok());

    auto read_result = bridge.read_key(0, key_handle);
    ASSERT_TRUE(read_result.is_ok());
    EXPECT_TRUE(read_result.value().is_float());
    EXPECT_DOUBLE_EQ(read_result.value().as_float(), 3.14);

    dealloc(*ctx_, key_handle);
}

TEST_F(DatabaseBridgeTest, WriteAndReadBool) {
    DatabaseBridge bridge{*ctx_};

    auto key_handle = write_key_to_memory(*ctx_, "bool_key");
    ASSERT_NE(key_handle.index, core::mem_config::INVALID_INDEX);

    auto write_result = bridge.write_key(0, key_handle, core::Value::from_bool(true));
    ASSERT_TRUE(write_result.is_ok());

    auto read_result = bridge.read_key(0, key_handle);
    ASSERT_TRUE(read_result.is_ok());
    EXPECT_TRUE(read_result.value().is_bool());
    EXPECT_TRUE(read_result.value().as_bool());

    dealloc(*ctx_, key_handle);
}

TEST_F(DatabaseBridgeTest, WriteAndReadNil) {
    DatabaseBridge bridge{*ctx_};

    auto key_handle = write_key_to_memory(*ctx_, "nil_key");
    ASSERT_NE(key_handle.index, core::mem_config::INVALID_INDEX);

    auto write_result = bridge.write_key(0, key_handle, core::Value::nil());
    ASSERT_TRUE(write_result.is_ok());

    auto read_result = bridge.read_key(0, key_handle);
    ASSERT_TRUE(read_result.is_ok());
    EXPECT_TRUE(read_result.value().is_nil());

    dealloc(*ctx_, key_handle);
}

TEST_F(DatabaseBridgeTest, ReadMissingKey) {
    DatabaseBridge bridge{*ctx_};

    auto key_handle = write_key_to_memory(*ctx_, "nonexistent");
    ASSERT_NE(key_handle.index, core::mem_config::INVALID_INDEX);

    auto read_result = bridge.read_key(0, key_handle);
    EXPECT_TRUE(read_result.is_err());
    EXPECT_EQ(read_result.error(), DatabaseBridgeError::KeyNotFound);

    dealloc(*ctx_, key_handle);
}

TEST_F(DatabaseBridgeTest, DeleteKey) {
    DatabaseBridge bridge{*ctx_};

    auto key_handle = write_key_to_memory(*ctx_, "delete_me");
    ASSERT_NE(key_handle.index, core::mem_config::INVALID_INDEX);

    // Write first
    auto write_result = bridge.write_key(0, key_handle, core::Value::from_int(123));
    ASSERT_TRUE(write_result.is_ok());

    // Verify exists
    auto exists_result = bridge.key_exists(0, key_handle);
    ASSERT_TRUE(exists_result.is_ok());
    EXPECT_TRUE(exists_result.value());

    // Delete
    auto delete_result = bridge.delete_key(0, key_handle);
    ASSERT_TRUE(delete_result.is_ok());

    // Verify no longer exists
    exists_result = bridge.key_exists(0, key_handle);
    ASSERT_TRUE(exists_result.is_ok());
    EXPECT_FALSE(exists_result.value());

    dealloc(*ctx_, key_handle);
}

TEST_F(DatabaseBridgeTest, KeyExists) {
    DatabaseBridge bridge{*ctx_};

    auto key_handle = write_key_to_memory(*ctx_, "exists_test");
    ASSERT_NE(key_handle.index, core::mem_config::INVALID_INDEX);

    // Initially should not exist
    auto exists_result = bridge.key_exists(0, key_handle);
    ASSERT_TRUE(exists_result.is_ok());
    EXPECT_FALSE(exists_result.value());

    // Write
    auto write_result = bridge.write_key(0, key_handle, core::Value::from_int(1));
    ASSERT_TRUE(write_result.is_ok());

    // Now should exist
    exists_result = bridge.key_exists(0, key_handle);
    ASSERT_TRUE(exists_result.is_ok());
    EXPECT_TRUE(exists_result.value());

    dealloc(*ctx_, key_handle);
}

TEST_F(DatabaseBridgeTest, OverwriteKey) {
    DatabaseBridge bridge{*ctx_};

    auto key_handle = write_key_to_memory(*ctx_, "overwrite_key");
    ASSERT_NE(key_handle.index, core::mem_config::INVALID_INDEX);

    // Write initial value
    auto write1 = bridge.write_key(0, key_handle, core::Value::from_int(1));
    ASSERT_TRUE(write1.is_ok());

    auto read1 = bridge.read_key(0, key_handle);
    ASSERT_TRUE(read1.is_ok());
    EXPECT_EQ(read1.value().as_integer(), 1);

    // Overwrite with new value
    auto write2 = bridge.write_key(0, key_handle, core::Value::from_int(999));
    ASSERT_TRUE(write2.is_ok());

    auto read2 = bridge.read_key(0, key_handle);
    ASSERT_TRUE(read2.is_ok());
    EXPECT_EQ(read2.value().as_integer(), 999);

    dealloc(*ctx_, key_handle);
}

TEST_F(DatabaseBridgeTest, InvalidKeyHandle) {
    DatabaseBridge bridge{*ctx_};

    core::Handle invalid_handle = core::MemoryManager::invalid_handle();

    auto read_result = bridge.read_key(0, invalid_handle);
    EXPECT_TRUE(read_result.is_err());
    EXPECT_EQ(read_result.error(), DatabaseBridgeError::InvalidKeyHandle);

    auto write_result = bridge.write_key(0, invalid_handle, core::Value::from_int(1));
    EXPECT_TRUE(write_result.is_err());
    EXPECT_EQ(write_result.error(), DatabaseBridgeError::InvalidKeyHandle);

    auto exists_result = bridge.key_exists(0, invalid_handle);
    EXPECT_TRUE(exists_result.is_err());
    EXPECT_EQ(exists_result.error(), DatabaseBridgeError::InvalidKeyHandle);

    auto delete_result = bridge.delete_key(0, invalid_handle);
    EXPECT_TRUE(delete_result.is_err());
    EXPECT_EQ(delete_result.error(), DatabaseBridgeError::InvalidKeyHandle);
}

TEST_F(DatabaseBridgeTest, StateNotEnabled) {
    // Create context without enabling state
    core::VmContext ctx_no_state{core::Architecture::Arch64};
    DatabaseBridge bridge{ctx_no_state};

    auto key_handle = write_key_to_memory(ctx_no_state, "key");
    ASSERT_NE(key_handle.index, core::mem_config::INVALID_INDEX);

    auto read_result = bridge.read_key(0, key_handle);
    EXPECT_TRUE(read_result.is_err());
    EXPECT_EQ(read_result.error(), DatabaseBridgeError::StateNotEnabled);

    auto write_result = bridge.write_key(0, key_handle, core::Value::from_int(1));
    EXPECT_TRUE(write_result.is_err());
    EXPECT_EQ(write_result.error(), DatabaseBridgeError::StateNotEnabled);

    dealloc(ctx_no_state, key_handle);
}

// ============================================================================
// Statistics Tracking Tests
// ============================================================================

TEST_F(DatabaseBridgeTest, WriteIncrementsStats) {
    DatabaseBridge bridge{*ctx_};

    auto key_handle = write_key_to_memory(*ctx_, "stat_write");
    ASSERT_NE(key_handle.index, core::mem_config::INVALID_INDEX);

    auto write_result = bridge.write_key(0, key_handle, core::Value::from_int(42));
    ASSERT_TRUE(write_result.is_ok());

    EXPECT_EQ(bridge.stats().writes, 1);
    EXPECT_GT(bridge.stats().bytes_written, 0);

    dealloc(*ctx_, key_handle);
}

TEST_F(DatabaseBridgeTest, ReadIncrementsStats) {
    DatabaseBridge bridge{*ctx_};

    auto key_handle = write_key_to_memory(*ctx_, "stat_read");
    ASSERT_NE(key_handle.index, core::mem_config::INVALID_INDEX);

    auto write_result = bridge.write_key(0, key_handle, core::Value::from_int(42));
    ASSERT_TRUE(write_result.is_ok());

    bridge.reset_stats();  // Reset to only count the read

    auto read_result = bridge.read_key(0, key_handle);
    ASSERT_TRUE(read_result.is_ok());

    EXPECT_EQ(bridge.stats().reads, 1);
    EXPECT_GT(bridge.stats().bytes_read, 0);

    dealloc(*ctx_, key_handle);
}

TEST_F(DatabaseBridgeTest, DeleteIncrementsStats) {
    DatabaseBridge bridge{*ctx_};

    auto key_handle = write_key_to_memory(*ctx_, "stat_delete");
    ASSERT_NE(key_handle.index, core::mem_config::INVALID_INDEX);

    auto write_result = bridge.write_key(0, key_handle, core::Value::from_int(42));
    ASSERT_TRUE(write_result.is_ok());

    bridge.reset_stats();

    auto delete_result = bridge.delete_key(0, key_handle);
    ASSERT_TRUE(delete_result.is_ok());

    EXPECT_EQ(bridge.stats().deletes, 1);

    dealloc(*ctx_, key_handle);
}

TEST_F(DatabaseBridgeTest, ExistsIncrementsStats) {
    DatabaseBridge bridge{*ctx_};

    auto key_handle = write_key_to_memory(*ctx_, "stat_exists");
    ASSERT_NE(key_handle.index, core::mem_config::INVALID_INDEX);

    auto exists_result = bridge.key_exists(0, key_handle);
    ASSERT_TRUE(exists_result.is_ok());

    EXPECT_EQ(bridge.stats().exists_checks, 1);

    dealloc(*ctx_, key_handle);
}

TEST_F(DatabaseBridgeTest, MultipleOperationsAccumulateStats) {
    DatabaseBridge bridge{*ctx_};

    auto key1 = write_key_to_memory(*ctx_, "key1");
    auto key2 = write_key_to_memory(*ctx_, "key2");
    auto key3 = write_key_to_memory(*ctx_, "key3");

    // 3 writes
    discard(bridge.write_key(0, key1, core::Value::from_int(1)));
    discard(bridge.write_key(0, key2, core::Value::from_int(2)));
    discard(bridge.write_key(0, key3, core::Value::from_int(3)));

    // 2 reads
    discard(bridge.read_key(0, key1));
    discard(bridge.read_key(0, key2));

    // 1 exists check
    discard(bridge.key_exists(0, key3));

    // 1 delete
    discard(bridge.delete_key(0, key1));

    EXPECT_EQ(bridge.stats().writes, 3);
    EXPECT_EQ(bridge.stats().reads, 2);
    EXPECT_EQ(bridge.stats().exists_checks, 1);
    EXPECT_EQ(bridge.stats().deletes, 1);

    dealloc(*ctx_, key1);
    dealloc(*ctx_, key2);
    dealloc(*ctx_, key3);
}

// ============================================================================
// Transaction Integration Tests
// ============================================================================

TEST_F(DatabaseBridgeTest, WriteWithTransaction) {
    DatabaseBridge bridge{*ctx_};

    auto key_handle = write_key_to_memory(*ctx_, "tx_write");
    ASSERT_NE(key_handle.index, core::mem_config::INVALID_INDEX);

    // Begin transaction
    auto tx_handle = ctx_->state_context().begin_transaction();
    ASSERT_NE(tx_handle, 0);

    // Write within transaction
    auto write_result = bridge.write_key(tx_handle, key_handle, core::Value::from_int(100));
    ASSERT_TRUE(write_result.is_ok());

    // Read within same transaction should see the value
    auto read_result = bridge.read_key(tx_handle, key_handle);
    ASSERT_TRUE(read_result.is_ok());
    EXPECT_EQ(read_result.value().as_integer(), 100);

    // Commit
    auto commit_result = ctx_->state_context().commit(tx_handle);
    EXPECT_EQ(commit_result, StateExecError::Success);

    // After commit, direct read should also see the value
    auto direct_read = bridge.read_key(0, key_handle);
    ASSERT_TRUE(direct_read.is_ok());
    EXPECT_EQ(direct_read.value().as_integer(), 100);

    dealloc(*ctx_, key_handle);
}

TEST_F(DatabaseBridgeTest, RollbackDiscardsWrites) {
    DatabaseBridge bridge{*ctx_};

    auto key_handle = write_key_to_memory(*ctx_, "tx_rollback");
    ASSERT_NE(key_handle.index, core::mem_config::INVALID_INDEX);

    // Begin transaction
    auto tx_handle = ctx_->state_context().begin_transaction();
    ASSERT_NE(tx_handle, 0);

    // Write within transaction
    auto write_result = bridge.write_key(tx_handle, key_handle, core::Value::from_int(999));
    ASSERT_TRUE(write_result.is_ok());

    // Rollback
    auto rollback_result = ctx_->state_context().rollback(tx_handle);
    EXPECT_EQ(rollback_result, StateExecError::Success);

    // After rollback, the key should not exist
    auto read_result = bridge.read_key(0, key_handle);
    EXPECT_TRUE(read_result.is_err());
    EXPECT_EQ(read_result.error(), DatabaseBridgeError::KeyNotFound);

    dealloc(*ctx_, key_handle);
}

TEST_F(DatabaseBridgeTest, TransactionIsolationWithBridge) {
    DatabaseBridge bridge{*ctx_};

    auto key_handle = write_key_to_memory(*ctx_, "tx_isolation");
    ASSERT_NE(key_handle.index, core::mem_config::INVALID_INDEX);

    // Pre-populate
    auto write0 = bridge.write_key(0, key_handle, core::Value::from_int(0));
    ASSERT_TRUE(write0.is_ok());

    // Start transaction
    auto tx_handle = ctx_->state_context().begin_transaction();
    ASSERT_NE(tx_handle, 0);

    // Read initial value
    auto read1 = bridge.read_key(tx_handle, key_handle);
    ASSERT_TRUE(read1.is_ok());
    EXPECT_EQ(read1.value().as_integer(), 0);

    // Modify via direct access (outside transaction)
    auto direct_write = bridge.write_key(0, key_handle, core::Value::from_int(999));
    ASSERT_TRUE(direct_write.is_ok());

    // Transaction should still see snapshot value (isolation)
    auto read2 = bridge.read_key(tx_handle, key_handle);
    ASSERT_TRUE(read2.is_ok());
    EXPECT_EQ(read2.value().as_integer(), 0);  // Still sees old value

    discard(ctx_->state_context().rollback(tx_handle));
    dealloc(*ctx_, key_handle);
}

TEST_F(DatabaseBridgeTest, EmptyKey) {
    DatabaseBridge bridge{*ctx_};

    auto key_handle = write_key_to_memory(*ctx_, "");
    ASSERT_NE(key_handle.index, core::mem_config::INVALID_INDEX);

    auto write_result = bridge.write_key(0, key_handle, core::Value::from_int(42));
    ASSERT_TRUE(write_result.is_ok());

    auto read_result = bridge.read_key(0, key_handle);
    ASSERT_TRUE(read_result.is_ok());
    EXPECT_EQ(read_result.value().as_integer(), 42);

    dealloc(*ctx_, key_handle);
}

TEST_F(DatabaseBridgeTest, LongKey) {
    DatabaseBridge bridge{*ctx_};

    std::string long_key(256, 'x');
    auto key_handle = write_key_to_memory(*ctx_, long_key);
    ASSERT_NE(key_handle.index, core::mem_config::INVALID_INDEX);

    auto write_result = bridge.write_key(0, key_handle, core::Value::from_int(12345));
    ASSERT_TRUE(write_result.is_ok());

    auto read_result = bridge.read_key(0, key_handle);
    ASSERT_TRUE(read_result.is_ok());
    EXPECT_EQ(read_result.value().as_integer(), 12345);

    dealloc(*ctx_, key_handle);
}

}  // namespace dotvm::exec::test
