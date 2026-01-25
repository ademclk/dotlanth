/// @file log_record_test.cpp
/// @brief Unit tests for STATE-007 LogRecord types and serialization
///
/// TDD tests for LSN, LogRecordType, LogRecord, and serialization.

#include <vector>

#include <gtest/gtest.h>

#include "dotvm/core/state/log_record.hpp"

namespace dotvm::core::state {
namespace {

// ============================================================================
// LSN Tests
// ============================================================================

TEST(LSNTest, InvalidLsnHasZeroValue) {
    LSN lsn = LSN::invalid();
    EXPECT_EQ(lsn.value, 0);
}

TEST(LSNTest, FirstLsnHasValueOne) {
    LSN lsn = LSN::first();
    EXPECT_EQ(lsn.value, 1);
}

TEST(LSNTest, LsnsAreComparable) {
    LSN a{10};
    LSN b{20};
    LSN c{10};

    EXPECT_TRUE(a < b);
    EXPECT_FALSE(b < a);
    EXPECT_TRUE(a == c);
    EXPECT_FALSE(a == b);
    EXPECT_TRUE(a != b);
    EXPECT_TRUE(a <= b);
    EXPECT_TRUE(a <= c);
    EXPECT_TRUE(b >= a);
}

TEST(LSNTest, NextIncrementsValue) {
    LSN lsn{42};
    LSN next = lsn.next();
    EXPECT_EQ(next.value, 43);
}

// ============================================================================
// LogRecordType Tests
// ============================================================================

TEST(LogRecordTypeTest, EnumValues) {
    EXPECT_EQ(static_cast<std::uint8_t>(LogRecordType::Put), 0);
    EXPECT_EQ(static_cast<std::uint8_t>(LogRecordType::Delete), 1);
    EXPECT_EQ(static_cast<std::uint8_t>(LogRecordType::TxBegin), 2);
    EXPECT_EQ(static_cast<std::uint8_t>(LogRecordType::TxCommit), 3);
    EXPECT_EQ(static_cast<std::uint8_t>(LogRecordType::TxAbort), 4);
    EXPECT_EQ(static_cast<std::uint8_t>(LogRecordType::Checkpoint), 5);
}

TEST(LogRecordTypeTest, ToStringReturnsCorrectNames) {
    EXPECT_STREQ(to_string(LogRecordType::Put), "Put");
    EXPECT_STREQ(to_string(LogRecordType::Delete), "Delete");
    EXPECT_STREQ(to_string(LogRecordType::TxBegin), "TxBegin");
    EXPECT_STREQ(to_string(LogRecordType::TxCommit), "TxCommit");
    EXPECT_STREQ(to_string(LogRecordType::TxAbort), "TxAbort");
    EXPECT_STREQ(to_string(LogRecordType::Checkpoint), "Checkpoint");
}

// ============================================================================
// LogRecord Construction Tests
// ============================================================================

TEST(LogRecordTest, CreatePutRecord) {
    std::vector<std::byte> key = {std::byte{0x01}, std::byte{0x02}};
    std::vector<std::byte> value = {std::byte{0x03}, std::byte{0x04}, std::byte{0x05}};
    TxId tx{.id = 42, .generation = 1};

    LogRecord record = LogRecord::create_put(LSN{100}, key, value, tx);

    EXPECT_EQ(record.lsn.value, 100);
    EXPECT_EQ(record.type, LogRecordType::Put);
    EXPECT_EQ(record.key, key);
    EXPECT_EQ(record.value, value);
    EXPECT_EQ(record.tx_id.id, 42);
    EXPECT_EQ(record.tx_id.generation, 1);
}

TEST(LogRecordTest, CreateDeleteRecord) {
    std::vector<std::byte> key = {std::byte{0x01}, std::byte{0x02}};
    TxId tx{.id = 10, .generation = 2};

    LogRecord record = LogRecord::create_delete(LSN{200}, key, tx);

    EXPECT_EQ(record.lsn.value, 200);
    EXPECT_EQ(record.type, LogRecordType::Delete);
    EXPECT_EQ(record.key, key);
    EXPECT_TRUE(record.value.empty());
    EXPECT_EQ(record.tx_id.id, 10);
}

TEST(LogRecordTest, CreateTxBeginRecord) {
    TxId tx{.id = 5, .generation = 0};

    LogRecord record = LogRecord::create_tx_begin(LSN{50}, tx);

    EXPECT_EQ(record.lsn.value, 50);
    EXPECT_EQ(record.type, LogRecordType::TxBegin);
    EXPECT_TRUE(record.key.empty());
    EXPECT_TRUE(record.value.empty());
    EXPECT_EQ(record.tx_id.id, 5);
}

TEST(LogRecordTest, CreateTxCommitRecord) {
    TxId tx{.id = 5, .generation = 0};

    LogRecord record = LogRecord::create_tx_commit(LSN{60}, tx);

    EXPECT_EQ(record.type, LogRecordType::TxCommit);
    EXPECT_EQ(record.tx_id.id, 5);
}

TEST(LogRecordTest, CreateTxAbortRecord) {
    TxId tx{.id = 5, .generation = 0};

    LogRecord record = LogRecord::create_tx_abort(LSN{70}, tx);

    EXPECT_EQ(record.type, LogRecordType::TxAbort);
    EXPECT_EQ(record.tx_id.id, 5);
}

TEST(LogRecordTest, CreateCheckpointRecord) {
    LSN checkpoint_lsn{500};

    LogRecord record = LogRecord::create_checkpoint(LSN{1000}, checkpoint_lsn);

    EXPECT_EQ(record.type, LogRecordType::Checkpoint);
    EXPECT_EQ(record.lsn.value, 1000);
    // Checkpoint stores the checkpoint LSN in value as 8 bytes
    EXPECT_EQ(record.value.size(), sizeof(std::uint64_t));
}

// ============================================================================
// Serialization Round-Trip Tests
// ============================================================================

TEST(LogRecordSerializationTest, PutRecordRoundTrip) {
    std::vector<std::byte> key = {std::byte{'k'}, std::byte{'e'}, std::byte{'y'}};
    std::vector<std::byte> value = {std::byte{'v'}, std::byte{'a'}, std::byte{'l'}};
    TxId tx{.id = 42, .generation = 1};

    LogRecord original = LogRecord::create_put(LSN{100}, key, value, tx);
    std::vector<std::byte> serialized = original.serialize();

    auto result = LogRecord::deserialize(serialized);
    ASSERT_TRUE(result.is_ok()) << "Deserialization failed: " << to_string(result.error());

    LogRecord deserialized = std::move(result.value());

    EXPECT_EQ(deserialized.lsn.value, original.lsn.value);
    EXPECT_EQ(deserialized.type, original.type);
    EXPECT_EQ(deserialized.key, original.key);
    EXPECT_EQ(deserialized.value, original.value);
    EXPECT_EQ(deserialized.tx_id.id, original.tx_id.id);
    EXPECT_EQ(deserialized.tx_id.generation, original.tx_id.generation);
}

TEST(LogRecordSerializationTest, DeleteRecordRoundTrip) {
    std::vector<std::byte> key = {std::byte{'d'}, std::byte{'e'}, std::byte{'l'}};
    TxId tx{.id = 99, .generation = 5};

    LogRecord original = LogRecord::create_delete(LSN{200}, key, tx);
    std::vector<std::byte> serialized = original.serialize();

    auto result = LogRecord::deserialize(serialized);
    ASSERT_TRUE(result.is_ok());

    LogRecord deserialized = std::move(result.value());

    EXPECT_EQ(deserialized.type, LogRecordType::Delete);
    EXPECT_EQ(deserialized.key, original.key);
    EXPECT_TRUE(deserialized.value.empty());
}

TEST(LogRecordSerializationTest, TxBeginRoundTrip) {
    TxId tx{.id = 7, .generation = 0};

    LogRecord original = LogRecord::create_tx_begin(LSN{300}, tx);
    std::vector<std::byte> serialized = original.serialize();

    auto result = LogRecord::deserialize(serialized);
    ASSERT_TRUE(result.is_ok());

    LogRecord deserialized = std::move(result.value());
    EXPECT_EQ(deserialized.type, LogRecordType::TxBegin);
}

TEST(LogRecordSerializationTest, TxCommitRoundTrip) {
    TxId tx{.id = 7, .generation = 0};

    LogRecord original = LogRecord::create_tx_commit(LSN{400}, tx);
    std::vector<std::byte> serialized = original.serialize();

    auto result = LogRecord::deserialize(serialized);
    ASSERT_TRUE(result.is_ok());

    LogRecord deserialized = std::move(result.value());
    EXPECT_EQ(deserialized.type, LogRecordType::TxCommit);
}

TEST(LogRecordSerializationTest, EmptyKeyAndValue) {
    LogRecord original = LogRecord::create_tx_abort(LSN{500}, TxId{.id = 1, .generation = 0});
    std::vector<std::byte> serialized = original.serialize();

    auto result = LogRecord::deserialize(serialized);
    ASSERT_TRUE(result.is_ok());

    LogRecord deserialized = std::move(result.value());
    EXPECT_TRUE(deserialized.key.empty());
    EXPECT_TRUE(deserialized.value.empty());
}

TEST(LogRecordSerializationTest, LargeKeyAndValue) {
    std::vector<std::byte> key(1000, std::byte{0xAB});
    std::vector<std::byte> value(10000, std::byte{0xCD});
    TxId tx{.id = 123, .generation = 456};

    LogRecord original = LogRecord::create_put(LSN{999}, key, value, tx);
    std::vector<std::byte> serialized = original.serialize();

    auto result = LogRecord::deserialize(serialized);
    ASSERT_TRUE(result.is_ok());

    LogRecord deserialized = std::move(result.value());
    EXPECT_EQ(deserialized.key.size(), 1000);
    EXPECT_EQ(deserialized.value.size(), 10000);
    EXPECT_EQ(deserialized.key, key);
    EXPECT_EQ(deserialized.value, value);
}

// ============================================================================
// CRC32 Checksum Tests
// ============================================================================

TEST(LogRecordSerializationTest, ChecksumIsValidAfterSerialization) {
    std::vector<std::byte> key = {std::byte{'k'}};
    std::vector<std::byte> value = {std::byte{'v'}};
    TxId tx{.id = 1, .generation = 0};

    LogRecord original = LogRecord::create_put(LSN{1}, key, value, tx);
    std::vector<std::byte> serialized = original.serialize();

    // Checksum should be non-zero
    EXPECT_NE(original.checksum, 0);

    // Deserialization should succeed with valid checksum
    auto result = LogRecord::deserialize(serialized);
    ASSERT_TRUE(result.is_ok());
}

TEST(LogRecordSerializationTest, CorruptedDataDetected) {
    std::vector<std::byte> key = {std::byte{'k'}};
    std::vector<std::byte> value = {std::byte{'v'}};
    TxId tx{.id = 1, .generation = 0};

    LogRecord original = LogRecord::create_put(LSN{1}, key, value, tx);
    std::vector<std::byte> serialized = original.serialize();

    // Corrupt a byte in the middle of the data
    if (serialized.size() > 20) {
        serialized[15] =
            std::byte{static_cast<unsigned char>(~static_cast<unsigned char>(serialized[15]))};
    }

    auto result = LogRecord::deserialize(serialized);
    ASSERT_TRUE(result.is_err());
    EXPECT_EQ(result.error(), WalError::WalCorrupted);
}

TEST(LogRecordSerializationTest, CorruptedChecksumDetected) {
    std::vector<std::byte> key = {std::byte{'k'}};
    std::vector<std::byte> value = {std::byte{'v'}};
    TxId tx{.id = 1, .generation = 0};

    LogRecord original = LogRecord::create_put(LSN{1}, key, value, tx);
    std::vector<std::byte> serialized = original.serialize();

    // Corrupt the last byte (part of checksum)
    serialized.back() =
        std::byte{static_cast<unsigned char>(~static_cast<unsigned char>(serialized.back()))};

    auto result = LogRecord::deserialize(serialized);
    ASSERT_TRUE(result.is_err());
    EXPECT_EQ(result.error(), WalError::WalCorrupted);
}

TEST(LogRecordSerializationTest, TruncatedDataRejected) {
    std::vector<std::byte> key = {std::byte{'k'}};
    std::vector<std::byte> value = {std::byte{'v'}};
    TxId tx{.id = 1, .generation = 0};

    LogRecord original = LogRecord::create_put(LSN{1}, key, value, tx);
    std::vector<std::byte> serialized = original.serialize();

    // Truncate the data
    serialized.resize(serialized.size() / 2);

    auto result = LogRecord::deserialize(serialized);
    ASSERT_TRUE(result.is_err());
    // Could be WalCorrupted or WalReadFailed depending on implementation
    EXPECT_TRUE(result.error() == WalError::WalCorrupted ||
                result.error() == WalError::WalReadFailed);
}

TEST(LogRecordSerializationTest, EmptyBufferRejected) {
    std::vector<std::byte> empty;

    auto result = LogRecord::deserialize(empty);
    ASSERT_TRUE(result.is_err());
}

// ============================================================================
// Binary Format Tests
// ============================================================================

TEST(LogRecordSerializationTest, SerializedSizeIsCorrect) {
    std::vector<std::byte> key = {std::byte{'k'}, std::byte{'e'}, std::byte{'y'}};    // 3 bytes
    std::vector<std::byte> value = {std::byte{'v'}, std::byte{'a'}, std::byte{'l'}};  // 3 bytes
    TxId tx{.id = 1, .generation = 0};

    LogRecord record = LogRecord::create_put(LSN{1}, key, value, tx);
    std::vector<std::byte> serialized = record.serialize();

    // Use the record's own size calculation
    std::size_t expected_size = record.serialized_size();
    EXPECT_EQ(serialized.size(), expected_size);
}

TEST(LogRecordSerializationTest, HeaderSizeIsCorrect) {
    // Header: LSN(8) + Type(1) + Reserved(1) + KeyLen(2) + ValueLen(4) = 16 bytes
    EXPECT_EQ(LogRecord::HEADER_SIZE, 16);
}

}  // namespace
}  // namespace dotvm::core::state
