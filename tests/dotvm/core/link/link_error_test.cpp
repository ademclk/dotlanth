/// @file link_error_test.cpp
/// @brief Unit tests for LinkError enum

#include <cstdint>
#include <format>
#include <string>

#include <gtest/gtest.h>

#include "dotvm/core/link/link_error.hpp"

namespace dotvm::core::link {
namespace {

// ============================================================================
// Enum Value Tests
// ============================================================================

TEST(LinkErrorTest, EnumValues) {
    // Link existence (192-195)
    EXPECT_EQ(static_cast<std::uint8_t>(LinkError::LinkInstanceNotFound), 192);
    EXPECT_EQ(static_cast<std::uint8_t>(LinkError::LinkInstanceAlreadyExists), 193);
    EXPECT_EQ(static_cast<std::uint8_t>(LinkError::InvalidObjectId), 194);
    EXPECT_EQ(static_cast<std::uint8_t>(LinkError::ObjectNotFound), 195);

    // Cardinality (196-199)
    EXPECT_EQ(static_cast<std::uint8_t>(LinkError::CardinalityViolation), 196);
    EXPECT_EQ(static_cast<std::uint8_t>(LinkError::RequiredLinkMissing), 197);
    EXPECT_EQ(static_cast<std::uint8_t>(LinkError::InverseLinkMismatch), 198);
    EXPECT_EQ(static_cast<std::uint8_t>(LinkError::LinkDefinitionNotFound), 199);

    // Traversal (200-203)
    EXPECT_EQ(static_cast<std::uint8_t>(LinkError::TraversalPathEmpty), 200);
    EXPECT_EQ(static_cast<std::uint8_t>(LinkError::TraversalPathInvalid), 201);
    EXPECT_EQ(static_cast<std::uint8_t>(LinkError::TraversalDepthExceeded), 202);
    EXPECT_EQ(static_cast<std::uint8_t>(LinkError::TraversalCycleDetected), 203);

    // Cascade/Storage (204-207)
    EXPECT_EQ(static_cast<std::uint8_t>(LinkError::CascadeDeleteFailed), 204);
    EXPECT_EQ(static_cast<std::uint8_t>(LinkError::TransactionRequired), 205);
    EXPECT_EQ(static_cast<std::uint8_t>(LinkError::StorageError), 206);
    EXPECT_EQ(static_cast<std::uint8_t>(LinkError::IndexCorruption), 207);
}

// ============================================================================
// to_string Tests
// ============================================================================

TEST(LinkErrorTest, ToStringAllValues) {
    EXPECT_EQ(to_string(LinkError::LinkInstanceNotFound), "LinkInstanceNotFound");
    EXPECT_EQ(to_string(LinkError::LinkInstanceAlreadyExists), "LinkInstanceAlreadyExists");
    EXPECT_EQ(to_string(LinkError::InvalidObjectId), "InvalidObjectId");
    EXPECT_EQ(to_string(LinkError::ObjectNotFound), "ObjectNotFound");
    EXPECT_EQ(to_string(LinkError::CardinalityViolation), "CardinalityViolation");
    EXPECT_EQ(to_string(LinkError::RequiredLinkMissing), "RequiredLinkMissing");
    EXPECT_EQ(to_string(LinkError::InverseLinkMismatch), "InverseLinkMismatch");
    EXPECT_EQ(to_string(LinkError::LinkDefinitionNotFound), "LinkDefinitionNotFound");
    EXPECT_EQ(to_string(LinkError::TraversalPathEmpty), "TraversalPathEmpty");
    EXPECT_EQ(to_string(LinkError::TraversalPathInvalid), "TraversalPathInvalid");
    EXPECT_EQ(to_string(LinkError::TraversalDepthExceeded), "TraversalDepthExceeded");
    EXPECT_EQ(to_string(LinkError::TraversalCycleDetected), "TraversalCycleDetected");
    EXPECT_EQ(to_string(LinkError::CascadeDeleteFailed), "CascadeDeleteFailed");
    EXPECT_EQ(to_string(LinkError::TransactionRequired), "TransactionRequired");
    EXPECT_EQ(to_string(LinkError::StorageError), "StorageError");
    EXPECT_EQ(to_string(LinkError::IndexCorruption), "IndexCorruption");
}

// ============================================================================
// is_recoverable Tests
// ============================================================================

TEST(LinkErrorTest, IsRecoverable) {
    EXPECT_TRUE(is_recoverable(LinkError::LinkInstanceNotFound));
    EXPECT_TRUE(is_recoverable(LinkError::LinkInstanceAlreadyExists));
    EXPECT_TRUE(is_recoverable(LinkError::TraversalCycleDetected));

    EXPECT_FALSE(is_recoverable(LinkError::IndexCorruption));
    EXPECT_FALSE(is_recoverable(LinkError::StorageError));
}

// ============================================================================
// std::formatter Tests
// ============================================================================

TEST(LinkErrorTest, Formatter) {
    EXPECT_EQ(std::format("{}", LinkError::LinkInstanceNotFound), "LinkInstanceNotFound");
    EXPECT_EQ(std::format("{}", LinkError::TraversalCycleDetected), "TraversalCycleDetected");
    EXPECT_EQ(std::format("Error: {}", LinkError::StorageError), "Error: StorageError");
}

}  // namespace
}  // namespace dotvm::core::link
