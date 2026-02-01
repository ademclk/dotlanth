/// @file version_test.cpp
/// @brief Unit tests for PRD-007 SemVer version parsing and comparison

#include <gtest/gtest.h>

#include "dotvm/pkg/version.hpp"

namespace dotvm::pkg {
namespace {

// ============================================================================
// Version Parsing Tests
// ============================================================================

TEST(VersionTest, ParseValidSimpleVersion) {
    auto result = Version::parse("1.2.3");
    ASSERT_TRUE(result.is_ok());

    const auto& version = result.value();
    EXPECT_EQ(version.major, 1);
    EXPECT_EQ(version.minor, 2);
    EXPECT_EQ(version.patch, 3);
    EXPECT_TRUE(version.prerelease.empty());
}

TEST(VersionTest, ParseValidZeroVersion) {
    auto result = Version::parse("0.0.0");
    ASSERT_TRUE(result.is_ok());

    const auto& version = result.value();
    EXPECT_EQ(version.major, 0);
    EXPECT_EQ(version.minor, 0);
    EXPECT_EQ(version.patch, 0);
}

TEST(VersionTest, ParseValidLargeVersion) {
    auto result = Version::parse("100.200.300");
    ASSERT_TRUE(result.is_ok());

    const auto& version = result.value();
    EXPECT_EQ(version.major, 100);
    EXPECT_EQ(version.minor, 200);
    EXPECT_EQ(version.patch, 300);
}

TEST(VersionTest, ParseValidPrereleaseAlpha) {
    auto result = Version::parse("1.0.0-alpha");
    ASSERT_TRUE(result.is_ok());

    const auto& version = result.value();
    EXPECT_EQ(version.major, 1);
    EXPECT_EQ(version.minor, 0);
    EXPECT_EQ(version.patch, 0);
    EXPECT_EQ(version.prerelease, "alpha");
}

TEST(VersionTest, ParseValidPrereleaseBeta1) {
    auto result = Version::parse("2.1.0-beta.1");
    ASSERT_TRUE(result.is_ok());

    const auto& version = result.value();
    EXPECT_EQ(version.major, 2);
    EXPECT_EQ(version.minor, 1);
    EXPECT_EQ(version.patch, 0);
    EXPECT_EQ(version.prerelease, "beta.1");
}

TEST(VersionTest, ParseValidPrereleaseRc) {
    auto result = Version::parse("3.0.0-rc.2");
    ASSERT_TRUE(result.is_ok());

    const auto& version = result.value();
    EXPECT_EQ(version.prerelease, "rc.2");
}

// ============================================================================
// Version Parsing Error Tests
// ============================================================================

TEST(VersionTest, ParseEmptyStringFails) {
    auto result = Version::parse("");
    ASSERT_TRUE(result.is_err());
    EXPECT_EQ(result.error(), PackageError::EmptyVersion);
}

TEST(VersionTest, ParseSingleNumberFails) {
    auto result = Version::parse("1");
    ASSERT_TRUE(result.is_err());
    EXPECT_EQ(result.error(), PackageError::InvalidVersionFormat);
}

TEST(VersionTest, ParseTwoNumbersFails) {
    auto result = Version::parse("1.2");
    ASSERT_TRUE(result.is_err());
    EXPECT_EQ(result.error(), PackageError::InvalidVersionFormat);
}

TEST(VersionTest, ParseNonNumericMajorFails) {
    auto result = Version::parse("a.2.3");
    ASSERT_TRUE(result.is_err());
    EXPECT_EQ(result.error(), PackageError::InvalidMajorVersion);
}

TEST(VersionTest, ParseNonNumericMinorFails) {
    auto result = Version::parse("1.b.3");
    ASSERT_TRUE(result.is_err());
    EXPECT_EQ(result.error(), PackageError::InvalidMinorVersion);
}

TEST(VersionTest, ParseNonNumericPatchFails) {
    auto result = Version::parse("1.2.c");
    ASSERT_TRUE(result.is_err());
    EXPECT_EQ(result.error(), PackageError::InvalidPatchVersion);
}

TEST(VersionTest, ParseNegativeVersionFails) {
    auto result = Version::parse("-1.2.3");
    ASSERT_TRUE(result.is_err());
    EXPECT_EQ(result.error(), PackageError::InvalidMajorVersion);
}

TEST(VersionTest, ParseLeadingZerosFails) {
    // Leading zeros are not valid in SemVer
    auto result = Version::parse("01.2.3");
    ASSERT_TRUE(result.is_err());
    EXPECT_EQ(result.error(), PackageError::InvalidMajorVersion);
}

TEST(VersionTest, ParseWithSpacesFails) {
    auto result = Version::parse("1 . 2 . 3");
    ASSERT_TRUE(result.is_err());
}

TEST(VersionTest, ParseTrailingDotFails) {
    auto result = Version::parse("1.2.3.");
    ASSERT_TRUE(result.is_err());
}

TEST(VersionTest, ParseEmptyPrereleaseFails) {
    auto result = Version::parse("1.2.3-");
    ASSERT_TRUE(result.is_err());
    EXPECT_EQ(result.error(), PackageError::InvalidPrerelease);
}

// ============================================================================
// Version Comparison Tests
// ============================================================================

TEST(VersionTest, CompareEqualVersions) {
    auto v1 = Version::parse("1.2.3").value();
    auto v2 = Version::parse("1.2.3").value();

    EXPECT_EQ(v1, v2);
    EXPECT_LE(v1, v2);
    EXPECT_GE(v1, v2);
    EXPECT_FALSE(v1 < v2);
    EXPECT_FALSE(v1 > v2);
}

TEST(VersionTest, CompareMajorDifference) {
    auto v1 = Version::parse("1.0.0").value();
    auto v2 = Version::parse("2.0.0").value();

    EXPECT_LT(v1, v2);
    EXPECT_GT(v2, v1);
    EXPECT_NE(v1, v2);
}

TEST(VersionTest, CompareMinorDifference) {
    auto v1 = Version::parse("1.1.0").value();
    auto v2 = Version::parse("1.2.0").value();

    EXPECT_LT(v1, v2);
    EXPECT_GT(v2, v1);
}

TEST(VersionTest, ComparePatchDifference) {
    auto v1 = Version::parse("1.2.3").value();
    auto v2 = Version::parse("1.2.4").value();

    EXPECT_LT(v1, v2);
    EXPECT_GT(v2, v1);
}

TEST(VersionTest, ComparePrereleaseVsRelease) {
    // Prerelease versions have lower precedence than release versions
    auto v1 = Version::parse("1.0.0-alpha").value();
    auto v2 = Version::parse("1.0.0").value();

    EXPECT_LT(v1, v2);
    EXPECT_GT(v2, v1);
}

TEST(VersionTest, ComparePrereleaseAlphabetically) {
    auto v1 = Version::parse("1.0.0-alpha").value();
    auto v2 = Version::parse("1.0.0-beta").value();

    EXPECT_LT(v1, v2);
    EXPECT_GT(v2, v1);
}

TEST(VersionTest, ComparePrereleaseAlphaVsAlpha1) {
    auto v1 = Version::parse("1.0.0-alpha").value();
    auto v2 = Version::parse("1.0.0-alpha.1").value();

    // alpha < alpha.1 (longer identifier wins when prefix matches)
    EXPECT_LT(v1, v2);
}

TEST(VersionTest, CompareSpaceshipOperator) {
    auto v1 = Version::parse("1.0.0").value();
    auto v2 = Version::parse("2.0.0").value();

    auto result = v1 <=> v2;
    EXPECT_TRUE(result < 0);
}

// ============================================================================
// Version String Conversion Tests
// ============================================================================

TEST(VersionTest, ToStringSimpleVersion) {
    auto version = Version::parse("1.2.3").value();
    EXPECT_EQ(version.to_string(), "1.2.3");
}

TEST(VersionTest, ToStringWithPrerelease) {
    auto version = Version::parse("1.0.0-alpha").value();
    EXPECT_EQ(version.to_string(), "1.0.0-alpha");
}

TEST(VersionTest, ToStringZeroVersion) {
    auto version = Version::parse("0.0.0").value();
    EXPECT_EQ(version.to_string(), "0.0.0");
}

// ============================================================================
// Version Construction Tests
// ============================================================================

TEST(VersionTest, DirectConstruction) {
    Version v{1, 2, 3, "alpha"};
    EXPECT_EQ(v.major, 1);
    EXPECT_EQ(v.minor, 2);
    EXPECT_EQ(v.patch, 3);
    EXPECT_EQ(v.prerelease, "alpha");
}

TEST(VersionTest, DirectConstructionNoPrerelease) {
    Version v{1, 2, 3, {}};
    EXPECT_EQ(v.major, 1);
    EXPECT_EQ(v.minor, 2);
    EXPECT_EQ(v.patch, 3);
    EXPECT_TRUE(v.prerelease.empty());
}

// ============================================================================
// Version Validation Tests
// ============================================================================

TEST(VersionTest, IsPrerelease) {
    auto stable = Version::parse("1.0.0").value();
    auto prerel = Version::parse("1.0.0-alpha").value();

    EXPECT_FALSE(stable.is_prerelease());
    EXPECT_TRUE(prerel.is_prerelease());
}

TEST(VersionTest, IsStable) {
    auto stable = Version::parse("1.0.0").value();
    auto unstable = Version::parse("0.9.0").value();

    EXPECT_TRUE(stable.is_stable());
    EXPECT_FALSE(unstable.is_stable());  // major = 0 means unstable
}

// ============================================================================
// Version Arithmetic Tests
// ============================================================================

TEST(VersionTest, IncrementMajor) {
    Version v{1, 2, 3, "alpha"};
    auto next = v.increment_major();

    EXPECT_EQ(next.major, 2);
    EXPECT_EQ(next.minor, 0);
    EXPECT_EQ(next.patch, 0);
    EXPECT_TRUE(next.prerelease.empty());
}

TEST(VersionTest, IncrementMinor) {
    Version v{1, 2, 3, "alpha"};
    auto next = v.increment_minor();

    EXPECT_EQ(next.major, 1);
    EXPECT_EQ(next.minor, 3);
    EXPECT_EQ(next.patch, 0);
    EXPECT_TRUE(next.prerelease.empty());
}

TEST(VersionTest, IncrementPatch) {
    Version v{1, 2, 3, "alpha"};
    auto next = v.increment_patch();

    EXPECT_EQ(next.major, 1);
    EXPECT_EQ(next.minor, 2);
    EXPECT_EQ(next.patch, 4);
    EXPECT_TRUE(next.prerelease.empty());
}

}  // namespace
}  // namespace dotvm::pkg
