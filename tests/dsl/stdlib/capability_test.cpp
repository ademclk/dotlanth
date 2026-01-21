/// @file capability_test.cpp
/// @brief DSL-004 Capability Check Tests
///
/// Tests that compile-time capability checking works correctly:
/// - Modules without capability requirements compile without caps
/// - Modules with capability requirements fail without caps
/// - Modules with capability requirements succeed with correct caps
///
/// Note: Capability checking happens at import resolution time,
/// not at function call time. These tests verify import-level checks.

#include <gtest/gtest.h>

#include "dotvm/core/capabilities/capability.hpp"
#include "dotvm/core/dsl/compiler/dsl_compiler.hpp"
#include "dotvm/core/dsl/compiler/ir_builder.hpp"

using namespace dotvm::core;
using namespace dotvm::core::dsl::compiler;
using namespace dotvm::core::capabilities;

class CapabilityCheckTest : public ::testing::Test {
protected:
    // Compile with no capabilities (safe mode)
    [[nodiscard]] std::expected<CompileResult, CompileError> compile_safe(std::string_view source) {
        CompileOptions opts;
        opts.granted_caps = Permission::None;
        DslCompiler compiler(opts);
        return compiler.compile_source(source);
    }

    // Compile with specific capabilities
    [[nodiscard]] std::expected<CompileResult, CompileError> compile_with_caps(std::string_view source,
                                                                               Permission caps) {
        CompileOptions opts;
        opts.granted_caps = caps;
        DslCompiler compiler(opts);
        return compiler.compile_source(source);
    }

    // Check if error contains expected message fragment
    static bool error_contains(const CompileError& err, std::string_view fragment) {
        return err.message.find(fragment) != std::string::npos;
    }
};

// ============================================================================
// No-Capability Modules (should always compile)
// ============================================================================

TEST_F(CapabilityCheckTest, PreludeCompilesWithoutCaps) {
    auto result = compile_safe(R"(
        import "std/prelude"

        dot test:
            state:
                x: 0
            when true:
                do:
                    state.x = 1
    )");

    EXPECT_TRUE(result.has_value()) << "Expected success but got: " << result.error().message;
}

TEST_F(CapabilityCheckTest, MathCompilesWithoutCaps) {
    auto result = compile_safe(R"(
        import "std/math"

        dot test:
            state:
                x: 0
            when true:
                do:
                    state.x = 1
    )");

    EXPECT_TRUE(result.has_value()) << "Expected success but got: " << result.error().message;
}

TEST_F(CapabilityCheckTest, StringCompilesWithoutCaps) {
    auto result = compile_safe(R"(
        import "std/string"

        dot test:
            state:
                x: 0
            when true:
                do:
                    state.x = 1
    )");

    EXPECT_TRUE(result.has_value()) << "Expected success but got: " << result.error().message;
}

TEST_F(CapabilityCheckTest, CollectionsCompilesWithoutCaps) {
    auto result = compile_safe(R"(
        import "std/collections"

        dot test:
            state:
                x: 0
            when true:
                do:
                    state.x = 1
    )");

    EXPECT_TRUE(result.has_value()) << "Expected success but got: " << result.error().message;
}

TEST_F(CapabilityCheckTest, TimeCompilesWithoutCaps) {
    auto result = compile_safe(R"(
        import "std/time"

        dot test:
            state:
                x: 0
            when true:
                do:
                    state.x = 1
    )");

    EXPECT_TRUE(result.has_value()) << "Expected success but got: " << result.error().message;
}

// ============================================================================
// Filesystem Capability Tests (std/io)
// ============================================================================

TEST_F(CapabilityCheckTest, IoFailsWithoutFilesystemCap) {
    auto result = compile_safe(R"(
        import "std/io"

        dot test:
            state:
                x: 0
            when true:
                do:
                    state.x = 1
    )");

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().stage, CompileError::Stage::IRBuild);
    EXPECT_TRUE(error_contains(result.error(), "capability"))
        << "Error: " << result.error().message;
    EXPECT_TRUE(error_contains(result.error(), "Filesystem"))
        << "Error: " << result.error().message;
}

TEST_F(CapabilityCheckTest, IoSucceedsWithFilesystemCap) {
    auto result = compile_with_caps(R"(
        import "std/io"

        dot test:
            state:
                x: 0
            when true:
                do:
                    state.x = 1
    )",
                                    Permission::Filesystem);

    EXPECT_TRUE(result.has_value()) << "Expected success but got: " << result.error().message;
}

// ============================================================================
// Crypto Capability Tests (std/crypto)
// ============================================================================

TEST_F(CapabilityCheckTest, CryptoFailsWithoutCryptoCap) {
    auto result = compile_safe(R"(
        import "std/crypto"

        dot test:
            state:
                x: 0
            when true:
                do:
                    state.x = 1
    )");

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().stage, CompileError::Stage::IRBuild);
    EXPECT_TRUE(error_contains(result.error(), "capability"))
        << "Error: " << result.error().message;
    EXPECT_TRUE(error_contains(result.error(), "Crypto")) << "Error: " << result.error().message;
}

TEST_F(CapabilityCheckTest, CryptoSucceedsWithCryptoCap) {
    auto result = compile_with_caps(R"(
        import "std/crypto"

        dot test:
            state:
                x: 0
            when true:
                do:
                    state.x = 1
    )",
                                    Permission::Crypto);

    EXPECT_TRUE(result.has_value()) << "Expected success but got: " << result.error().message;
}

// ============================================================================
// Network Capability Tests (std/net)
// ============================================================================

TEST_F(CapabilityCheckTest, NetFailsWithoutNetworkCap) {
    auto result = compile_safe(R"(
        import "std/net"

        dot test:
            state:
                x: 0
            when true:
                do:
                    state.x = 1
    )");

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().stage, CompileError::Stage::IRBuild);
    EXPECT_TRUE(error_contains(result.error(), "capability"))
        << "Error: " << result.error().message;
    EXPECT_TRUE(error_contains(result.error(), "Network")) << "Error: " << result.error().message;
}

TEST_F(CapabilityCheckTest, NetSucceedsWithNetworkCap) {
    auto result = compile_with_caps(R"(
        import "std/net"

        dot test:
            state:
                x: 0
            when true:
                do:
                    state.x = 1
    )",
                                    Permission::Network);

    EXPECT_TRUE(result.has_value()) << "Expected success but got: " << result.error().message;
}

// ============================================================================
// Multiple Capability Tests
// ============================================================================

TEST_F(CapabilityCheckTest, MultipleModulesRequireDifferentCaps) {
    // This should fail because we only grant Filesystem but also import crypto
    auto result = compile_with_caps(R"(
        import "std/io"
        import "std/crypto"

        dot test:
            state:
                x: 0
            when true:
                do:
                    state.x = 1
    )",
                                    Permission::Filesystem);

    ASSERT_FALSE(result.has_value());
    EXPECT_TRUE(error_contains(result.error(), "Crypto")) << "Error: " << result.error().message;
}

TEST_F(CapabilityCheckTest, MultipleModulesSucceedWithAllCaps) {
    auto result = compile_with_caps(R"(
        import "std/io"
        import "std/crypto"

        dot test:
            state:
                x: 0
            when true:
                do:
                    state.x = 1
    )",
                                    Permission::Filesystem | Permission::Crypto);

    EXPECT_TRUE(result.has_value()) << "Expected success but got: " << result.error().message;
}

TEST_F(CapabilityCheckTest, AllCapsGrantsEverything) {
    auto result = compile_with_caps(R"(
        import "std/io"
        import "std/crypto"
        import "std/net"

        dot test:
            state:
                x: 0
            when true:
                do:
                    state.x = 1
    )",
                                    Permission::All);

    EXPECT_TRUE(result.has_value()) << "Expected success but got: " << result.error().message;
}

// ============================================================================
// Error Message Quality Tests
// ============================================================================

TEST_F(CapabilityCheckTest, ErrorMessageMentionsModulePath) {
    auto result = compile_safe(R"(
        import "std/io"

        dot test:
            state:
                x: 0
            when true:
                do:
                    state.x = 1
    )");

    ASSERT_FALSE(result.has_value());
    EXPECT_TRUE(error_contains(result.error(), "std/io")) << "Error: " << result.error().message;
}

TEST_F(CapabilityCheckTest, ErrorMessageMentionsRequiredCapability) {
    auto result = compile_safe(R"(
        import "std/net"

        dot test:
            state:
                x: 0
            when true:
                do:
                    state.x = 1
    )");

    ASSERT_FALSE(result.has_value());
    EXPECT_TRUE(error_contains(result.error(), "Network")) << "Error: " << result.error().message;
}

// ============================================================================
// Unknown Module Tests
// ============================================================================

TEST_F(CapabilityCheckTest, UnknownModuleFailsGracefully) {
    auto result = compile_safe(R"(
        import "std/nonexistent"

        dot test:
            state:
                x: 0
            when true:
                do:
                    state.x = 1
    )");

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().stage, CompileError::Stage::IRBuild);
    EXPECT_TRUE(error_contains(result.error(), "Unknown module") ||
                error_contains(result.error(), "nonexistent"))
        << "Error: " << result.error().message;
}
