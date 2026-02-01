/// @file version_constraint_test.cpp
/// @brief Unit tests for PRD-007 version constraints

#include <gtest/gtest.h>

#include "dotvm/pkg/version_constraint.hpp"

namespace dotvm::pkg {
namespace {

// ============================================================================
// Constraint Parsing Tests
// ============================================================================

TEST(VersionConstraintTest, ParseExactVersion) {
    auto result = VersionConstraint::parse("1.2.3");
    ASSERT_TRUE(result.is_ok());

    const auto& constraint = result.value();
    EXPECT_EQ(constraint.op, ConstraintOp::Exact);
    EXPECT_EQ(constraint.version.major, 1);
    EXPECT_EQ(constraint.version.minor, 2);
    EXPECT_EQ(constraint.version.patch, 3);
}

TEST(VersionConstraintTest, ParseCaretConstraint) {
    auto result = VersionConstraint::parse("^1.2.3");
    ASSERT_TRUE(result.is_ok());

    const auto& constraint = result.value();
    EXPECT_EQ(constraint.op, ConstraintOp::Caret);
    EXPECT_EQ(constraint.version.major, 1);
    EXPECT_EQ(constraint.version.minor, 2);
    EXPECT_EQ(constraint.version.patch, 3);
}

TEST(VersionConstraintTest, ParseTildeConstraint) {
    auto result = VersionConstraint::parse("~1.2.3");
    ASSERT_TRUE(result.is_ok());

    const auto& constraint = result.value();
    EXPECT_EQ(constraint.op, ConstraintOp::Tilde);
    EXPECT_EQ(constraint.version.major, 1);
}

TEST(VersionConstraintTest, ParseGreaterThanConstraint) {
    auto result = VersionConstraint::parse(">1.0.0");
    ASSERT_TRUE(result.is_ok());

    const auto& constraint = result.value();
    EXPECT_EQ(constraint.op, ConstraintOp::Greater);
}

TEST(VersionConstraintTest, ParseGreaterThanOrEqualConstraint) {
    auto result = VersionConstraint::parse(">=2.0.0");
    ASSERT_TRUE(result.is_ok());

    const auto& constraint = result.value();
    EXPECT_EQ(constraint.op, ConstraintOp::GreaterEq);
    EXPECT_EQ(constraint.version.major, 2);
}

TEST(VersionConstraintTest, ParseLessThanConstraint) {
    auto result = VersionConstraint::parse("<3.0.0");
    ASSERT_TRUE(result.is_ok());

    const auto& constraint = result.value();
    EXPECT_EQ(constraint.op, ConstraintOp::Less);
    EXPECT_EQ(constraint.version.major, 3);
}

TEST(VersionConstraintTest, ParseLessThanOrEqualConstraint) {
    auto result = VersionConstraint::parse("<=3.0.0");
    ASSERT_TRUE(result.is_ok());

    const auto& constraint = result.value();
    EXPECT_EQ(constraint.op, ConstraintOp::LessEq);
}

TEST(VersionConstraintTest, ParseWithSpaces) {
    auto result = VersionConstraint::parse(">= 1.0.0");
    ASSERT_TRUE(result.is_ok());

    const auto& constraint = result.value();
    EXPECT_EQ(constraint.op, ConstraintOp::GreaterEq);
    EXPECT_EQ(constraint.version.major, 1);
}

// ============================================================================
// Constraint Parsing Error Tests
// ============================================================================

TEST(VersionConstraintTest, ParseEmptyFails) {
    auto result = VersionConstraint::parse("");
    ASSERT_TRUE(result.is_err());
    EXPECT_EQ(result.error(), PackageError::EmptyConstraint);
}

TEST(VersionConstraintTest, ParseInvalidVersionFails) {
    auto result = VersionConstraint::parse("^not.a.version");
    ASSERT_TRUE(result.is_err());
}

TEST(VersionConstraintTest, ParseOperatorOnlyFails) {
    auto result = VersionConstraint::parse(">=");
    ASSERT_TRUE(result.is_err());
}

// ============================================================================
// Constraint Satisfaction Tests - Exact
// ============================================================================

TEST(VersionConstraintTest, ExactSatisfiesMatch) {
    auto constraint = VersionConstraint::parse("1.2.3").value();
    auto version = Version::parse("1.2.3").value();

    EXPECT_TRUE(constraint.satisfies(version));
}

TEST(VersionConstraintTest, ExactDoesNotSatisfyHigher) {
    auto constraint = VersionConstraint::parse("1.2.3").value();
    auto version = Version::parse("1.2.4").value();

    EXPECT_FALSE(constraint.satisfies(version));
}

TEST(VersionConstraintTest, ExactDoesNotSatisfyLower) {
    auto constraint = VersionConstraint::parse("1.2.3").value();
    auto version = Version::parse("1.2.2").value();

    EXPECT_FALSE(constraint.satisfies(version));
}

// ============================================================================
// Constraint Satisfaction Tests - Caret (^)
// ============================================================================

// Caret allows changes that do not modify the left-most non-zero element
// ^1.2.3 := >=1.2.3 <2.0.0
// ^0.2.3 := >=0.2.3 <0.3.0
// ^0.0.3 := >=0.0.3 <0.0.4

TEST(VersionConstraintTest, CaretSatisfiesSameVersion) {
    auto constraint = VersionConstraint::parse("^1.2.3").value();
    auto version = Version::parse("1.2.3").value();

    EXPECT_TRUE(constraint.satisfies(version));
}

TEST(VersionConstraintTest, CaretSatisfiesPatchUpdate) {
    auto constraint = VersionConstraint::parse("^1.2.3").value();
    auto version = Version::parse("1.2.4").value();

    EXPECT_TRUE(constraint.satisfies(version));
}

TEST(VersionConstraintTest, CaretSatisfiesMinorUpdate) {
    auto constraint = VersionConstraint::parse("^1.2.3").value();
    auto version = Version::parse("1.3.0").value();

    EXPECT_TRUE(constraint.satisfies(version));
}

TEST(VersionConstraintTest, CaretDoesNotSatisfyMajorUpdate) {
    auto constraint = VersionConstraint::parse("^1.2.3").value();
    auto version = Version::parse("2.0.0").value();

    EXPECT_FALSE(constraint.satisfies(version));
}

TEST(VersionConstraintTest, CaretDoesNotSatisfyLower) {
    auto constraint = VersionConstraint::parse("^1.2.3").value();
    auto version = Version::parse("1.2.2").value();

    EXPECT_FALSE(constraint.satisfies(version));
}

TEST(VersionConstraintTest, CaretZeroMajorSatisfiesMinorUpdate) {
    // ^0.2.3 allows >= 0.2.3 < 0.3.0
    auto constraint = VersionConstraint::parse("^0.2.3").value();
    auto version = Version::parse("0.2.9").value();

    EXPECT_TRUE(constraint.satisfies(version));
}

TEST(VersionConstraintTest, CaretZeroMajorDoesNotSatisfyMinorUpdate) {
    // ^0.2.3 does NOT allow 0.3.0
    auto constraint = VersionConstraint::parse("^0.2.3").value();
    auto version = Version::parse("0.3.0").value();

    EXPECT_FALSE(constraint.satisfies(version));
}

// ============================================================================
// Constraint Satisfaction Tests - Tilde (~)
// ============================================================================

// Tilde allows patch-level changes
// ~1.2.3 := >=1.2.3 <1.3.0
// ~1.2 := >=1.2.0 <1.3.0

TEST(VersionConstraintTest, TildeSatisfiesSameVersion) {
    auto constraint = VersionConstraint::parse("~1.2.3").value();
    auto version = Version::parse("1.2.3").value();

    EXPECT_TRUE(constraint.satisfies(version));
}

TEST(VersionConstraintTest, TildeSatisfiesPatchUpdate) {
    auto constraint = VersionConstraint::parse("~1.2.3").value();
    auto version = Version::parse("1.2.9").value();

    EXPECT_TRUE(constraint.satisfies(version));
}

TEST(VersionConstraintTest, TildeDoesNotSatisfyMinorUpdate) {
    auto constraint = VersionConstraint::parse("~1.2.3").value();
    auto version = Version::parse("1.3.0").value();

    EXPECT_FALSE(constraint.satisfies(version));
}

TEST(VersionConstraintTest, TildeDoesNotSatisfyLower) {
    auto constraint = VersionConstraint::parse("~1.2.3").value();
    auto version = Version::parse("1.2.2").value();

    EXPECT_FALSE(constraint.satisfies(version));
}

// ============================================================================
// Constraint Satisfaction Tests - Comparison Operators
// ============================================================================

TEST(VersionConstraintTest, GreaterSatisfies) {
    auto constraint = VersionConstraint::parse(">1.0.0").value();

    EXPECT_TRUE(constraint.satisfies(Version::parse("1.0.1").value()));
    EXPECT_TRUE(constraint.satisfies(Version::parse("2.0.0").value()));
    EXPECT_FALSE(constraint.satisfies(Version::parse("1.0.0").value()));
    EXPECT_FALSE(constraint.satisfies(Version::parse("0.9.9").value()));
}

TEST(VersionConstraintTest, GreaterEqSatisfies) {
    auto constraint = VersionConstraint::parse(">=1.0.0").value();

    EXPECT_TRUE(constraint.satisfies(Version::parse("1.0.0").value()));
    EXPECT_TRUE(constraint.satisfies(Version::parse("1.0.1").value()));
    EXPECT_TRUE(constraint.satisfies(Version::parse("2.0.0").value()));
    EXPECT_FALSE(constraint.satisfies(Version::parse("0.9.9").value()));
}

TEST(VersionConstraintTest, LessSatisfies) {
    auto constraint = VersionConstraint::parse("<2.0.0").value();

    EXPECT_TRUE(constraint.satisfies(Version::parse("1.9.9").value()));
    EXPECT_TRUE(constraint.satisfies(Version::parse("1.0.0").value()));
    EXPECT_FALSE(constraint.satisfies(Version::parse("2.0.0").value()));
    EXPECT_FALSE(constraint.satisfies(Version::parse("2.0.1").value()));
}

TEST(VersionConstraintTest, LessEqSatisfies) {
    auto constraint = VersionConstraint::parse("<=2.0.0").value();

    EXPECT_TRUE(constraint.satisfies(Version::parse("2.0.0").value()));
    EXPECT_TRUE(constraint.satisfies(Version::parse("1.9.9").value()));
    EXPECT_FALSE(constraint.satisfies(Version::parse("2.0.1").value()));
}

// ============================================================================
// Constraint String Conversion Tests
// ============================================================================

TEST(VersionConstraintTest, ToStringExact) {
    auto constraint = VersionConstraint::parse("1.2.3").value();
    EXPECT_EQ(constraint.to_string(), "1.2.3");
}

TEST(VersionConstraintTest, ToStringCaret) {
    auto constraint = VersionConstraint::parse("^1.2.3").value();
    EXPECT_EQ(constraint.to_string(), "^1.2.3");
}

TEST(VersionConstraintTest, ToStringTilde) {
    auto constraint = VersionConstraint::parse("~1.2.3").value();
    EXPECT_EQ(constraint.to_string(), "~1.2.3");
}

TEST(VersionConstraintTest, ToStringGreater) {
    auto constraint = VersionConstraint::parse(">1.0.0").value();
    EXPECT_EQ(constraint.to_string(), ">1.0.0");
}

TEST(VersionConstraintTest, ToStringGreaterEq) {
    auto constraint = VersionConstraint::parse(">=1.0.0").value();
    EXPECT_EQ(constraint.to_string(), ">=1.0.0");
}

TEST(VersionConstraintTest, ToStringLess) {
    auto constraint = VersionConstraint::parse("<2.0.0").value();
    EXPECT_EQ(constraint.to_string(), "<2.0.0");
}

TEST(VersionConstraintTest, ToStringLessEq) {
    auto constraint = VersionConstraint::parse("<=2.0.0").value();
    EXPECT_EQ(constraint.to_string(), "<=2.0.0");
}

}  // namespace
}  // namespace dotvm::pkg
