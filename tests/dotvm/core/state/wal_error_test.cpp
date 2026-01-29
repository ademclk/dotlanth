/// @file wal_error_test.cpp
/// @brief Unit tests for STATE-007 WAL error types
///
/// TDD tests for WalError enum - written before implementation.

#include <gtest/gtest.h>

#include "dotvm/core/state/wal_error.hpp"

namespace dotvm::core::state {
namespace {

// ============================================================================
// WalError Value Tests
// ============================================================================

TEST(WalErrorTest, ErrorCodesInCorrectRange) {
    // WAL errors use range 80-95
    EXPECT_EQ(static_cast<std::uint8_t>(WalError::WalWriteFailed), 80);
    EXPECT_EQ(static_cast<std::uint8_t>(WalError::WalReadFailed), 81);
    EXPECT_EQ(static_cast<std::uint8_t>(WalError::WalSyncFailed), 82);
    EXPECT_EQ(static_cast<std::uint8_t>(WalError::WalCorrupted), 83);
    EXPECT_EQ(static_cast<std::uint8_t>(WalError::WalTruncateFailed), 84);

    // Checkpoint errors (88-91)
    EXPECT_EQ(static_cast<std::uint8_t>(WalError::CheckpointFailed), 88);
    EXPECT_EQ(static_cast<std::uint8_t>(WalError::CheckpointCorrupted), 89);

    // Recovery errors (92-95)
    EXPECT_EQ(static_cast<std::uint8_t>(WalError::RecoveryFailed), 92);
    EXPECT_EQ(static_cast<std::uint8_t>(WalError::PartialRecovery), 93);
}

// ============================================================================
// to_string Tests
// ============================================================================

TEST(WalErrorTest, ToStringReturnsCorrectNames) {
    EXPECT_EQ(to_string(WalError::WalWriteFailed), "WalWriteFailed");
    EXPECT_EQ(to_string(WalError::WalReadFailed), "WalReadFailed");
    EXPECT_EQ(to_string(WalError::WalSyncFailed), "WalSyncFailed");
    EXPECT_EQ(to_string(WalError::WalCorrupted), "WalCorrupted");
    EXPECT_EQ(to_string(WalError::WalTruncateFailed), "WalTruncateFailed");
    EXPECT_EQ(to_string(WalError::CheckpointFailed), "CheckpointFailed");
    EXPECT_EQ(to_string(WalError::CheckpointCorrupted), "CheckpointCorrupted");
    EXPECT_EQ(to_string(WalError::RecoveryFailed), "RecoveryFailed");
    EXPECT_EQ(to_string(WalError::PartialRecovery), "PartialRecovery");
}

// ============================================================================
// is_recoverable Tests
// ============================================================================

TEST(WalErrorTest, PartialRecoveryIsRecoverable) {
    // Partial recovery means some records were recovered - operation can continue
    EXPECT_TRUE(is_recoverable(WalError::PartialRecovery));
}

TEST(WalErrorTest, WalCorruptedIsNotRecoverable) {
    // Corrupted WAL requires manual intervention
    EXPECT_FALSE(is_recoverable(WalError::WalCorrupted));
}

TEST(WalErrorTest, IoErrorsAreNotRecoverable) {
    // I/O errors are typically not recoverable without intervention
    EXPECT_FALSE(is_recoverable(WalError::WalWriteFailed));
    EXPECT_FALSE(is_recoverable(WalError::WalReadFailed));
    EXPECT_FALSE(is_recoverable(WalError::WalSyncFailed));
    EXPECT_FALSE(is_recoverable(WalError::WalTruncateFailed));
}

TEST(WalErrorTest, CheckpointErrorsAreNotRecoverable) {
    EXPECT_FALSE(is_recoverable(WalError::CheckpointFailed));
    EXPECT_FALSE(is_recoverable(WalError::CheckpointCorrupted));
}

TEST(WalErrorTest, RecoveryFailedIsNotRecoverable) {
    EXPECT_FALSE(is_recoverable(WalError::RecoveryFailed));
}

}  // namespace
}  // namespace dotvm::core::state
