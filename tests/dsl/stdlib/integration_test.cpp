/// @file integration_test.cpp
/// @brief DSL-004 Standard Library Integration Tests
///
/// Tests that stdlib imports work correctly with the compiler pipeline.
/// Note: The DSL parser currently supports action statements (e.g., `print "hello"`)
/// but not function call expressions on the RHS of assignments. These tests
/// focus on what the current parser supports.

#include <gtest/gtest.h>

#include "dotvm/core/bytecode.hpp"
#include "dotvm/core/capabilities/capability.hpp"
#include "dotvm/core/dsl/compiler/dsl_compiler.hpp"
#include "dotvm/core/opcode.hpp"

using namespace dotvm::core;
using namespace dotvm::core::dsl::compiler;
using namespace dotvm::core::capabilities;

class StdlibIntegrationTest : public ::testing::Test {
protected:
    [[nodiscard]] std::expected<CompileResult, CompileError> compile_with_caps(std::string_view source,
                                                                               Permission caps) {
        CompileOptions opts;
        opts.granted_caps = caps;
        DslCompiler compiler(opts);
        return compiler.compile_source(source);
    }

    // Helper to find SYSCALL instructions in bytecode
    [[nodiscard]] static std::vector<std::uint16_t> find_syscalls(
        const std::vector<std::uint8_t>& bytecode) {
        std::vector<std::uint16_t> syscalls;

        // Skip header and find code section
        auto header = read_header(bytecode);
        if (!header.has_value()) {
            return syscalls;
        }

        // Scan for SYSCALL opcode (0xFE) in the code section
        for (std::size_t i = header->code_offset; i + 3 < bytecode.size(); ++i) {
            if (bytecode[i] == static_cast<std::uint8_t>(opcode::SYSCALL)) {
                // SYSCALL is Type B: [opcode][Rd][imm16_lo][imm16_hi]
                std::uint16_t syscall_id = static_cast<std::uint16_t>(bytecode[i + 2]) |
                                           (static_cast<std::uint16_t>(bytecode[i + 3]) << 8);
                syscalls.push_back(syscall_id);
                i += 3;  // Skip the instruction bytes
            }
        }

        return syscalls;
    }
};

// ============================================================================
// Basic Import Tests
// ============================================================================

TEST_F(StdlibIntegrationTest, ImportPreludeSucceeds) {
    auto result = compile_with_caps(R"(
        import "std/prelude"

        dot test:
            state:
                x: 0
            when true:
                do:
                    state.x = 1
    )",
                                    Permission::None);

    ASSERT_TRUE(result.has_value()) << result.error().message;
    EXPECT_FALSE(result->bytecode.empty());
}

TEST_F(StdlibIntegrationTest, ImportMathSucceeds) {
    auto result = compile_with_caps(R"(
        import "std/math"

        dot test:
            state:
                x: 0
            when true:
                do:
                    state.x = 1
    )",
                                    Permission::None);

    ASSERT_TRUE(result.has_value()) << result.error().message;
    EXPECT_FALSE(result->bytecode.empty());
}

TEST_F(StdlibIntegrationTest, ImportStringSucceeds) {
    auto result = compile_with_caps(R"(
        import "std/string"

        dot test:
            state:
                x: 0
            when true:
                do:
                    state.x = 1
    )",
                                    Permission::None);

    ASSERT_TRUE(result.has_value()) << result.error().message;
    EXPECT_FALSE(result->bytecode.empty());
}

TEST_F(StdlibIntegrationTest, ImportIoWithCapSucceeds) {
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

    ASSERT_TRUE(result.has_value()) << result.error().message;
    EXPECT_FALSE(result->bytecode.empty());
}

TEST_F(StdlibIntegrationTest, ImportCryptoWithCapSucceeds) {
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

    ASSERT_TRUE(result.has_value()) << result.error().message;
    EXPECT_FALSE(result->bytecode.empty());
}

TEST_F(StdlibIntegrationTest, ImportNetWithCapSucceeds) {
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

    ASSERT_TRUE(result.has_value()) << result.error().message;
    EXPECT_FALSE(result->bytecode.empty());
}

// ============================================================================
// Multiple Module Import Tests
// ============================================================================

TEST_F(StdlibIntegrationTest, ImportMultipleNonCapModulesSucceeds) {
    auto result = compile_with_caps(R"(
        import "std/prelude"
        import "std/math"
        import "std/string"
        import "std/collections"
        import "std/time"

        dot test:
            state:
                x: 0
            when true:
                do:
                    state.x = 1
    )",
                                    Permission::None);

    ASSERT_TRUE(result.has_value()) << result.error().message;
    EXPECT_FALSE(result->bytecode.empty());
}

TEST_F(StdlibIntegrationTest, ImportAllCapModulesWithAllCaps) {
    auto result = compile_with_caps(R"(
        import "std/prelude"
        import "std/math"
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

    ASSERT_TRUE(result.has_value()) << result.error().message;
    EXPECT_FALSE(result->bytecode.empty());
}

// ============================================================================
// Action Statement Tests (using emit keyword which the parser supports)
// ============================================================================

TEST_F(StdlibIntegrationTest, EmitActionCompiles) {
    auto result = compile_with_caps(R"(
        import "std/prelude"

        dot test:
            state:
                x: 0
            when true:
                do:
                    emit "hello"
    )",
                                    Permission::None);

    ASSERT_TRUE(result.has_value()) << result.error().message;
    EXPECT_FALSE(result->bytecode.empty());
}

TEST_F(StdlibIntegrationTest, LogActionCompiles) {
    auto result = compile_with_caps(R"(
        import "std/prelude"

        dot test:
            state:
                x: 0
            when true:
                do:
                    log "message"
    )",
                                    Permission::None);

    ASSERT_TRUE(result.has_value()) << result.error().message;
    EXPECT_FALSE(result->bytecode.empty());
}

// ============================================================================
// IR Preservation Tests
// ============================================================================

TEST_F(StdlibIntegrationTest, IRPreservesSyscallInfo) {
    auto result = compile_with_caps(R"(
        import "std/prelude"

        dot test:
            state:
                x: 0
            when true:
                do:
                    emit "hello"
    )",
                                    Permission::None);

    ASSERT_TRUE(result.has_value()) << result.error().message;

    // The IR should be preserved in the compile result for debugging
    EXPECT_FALSE(result->ir.dots.empty());
}

// ============================================================================
// Bytecode Validity Tests
// ============================================================================

TEST_F(StdlibIntegrationTest, GeneratedBytecodeIsValid) {
    auto result = compile_with_caps(R"(
        import "std/prelude"
        import "std/math"

        dot calculator:
            state:
                result: 0
            when true:
                do:
                    state.result = 42
    )",
                                    Permission::None);

    ASSERT_TRUE(result.has_value()) << result.error().message;

    // Verify bytecode has valid header
    auto header = read_header(result->bytecode);
    ASSERT_TRUE(header.has_value()) << "Invalid bytecode header";

    // Verify header validation passes
    EXPECT_EQ(validate_header(*header, result->bytecode.size()), BytecodeError::Success);
}

TEST_F(StdlibIntegrationTest, BytecodeSizeIsReasonable) {
    auto result = compile_with_caps(R"(
        import "std/prelude"

        dot minimal:
            state:
                x: 0
            when true:
                do:
                    state.x = 1
    )",
                                    Permission::None);

    ASSERT_TRUE(result.has_value()) << result.error().message;

    // Bytecode should have reasonable size (header + at least some code)
    EXPECT_GE(result->bytecode.size(), 32);  // At least header size
    EXPECT_LT(result->bytecode.size(), 10000);  // Not ridiculously large
}

// ============================================================================
// Unknown Module Tests
// ============================================================================

TEST_F(StdlibIntegrationTest, UnknownModuleFails) {
    auto result = compile_with_caps(R"(
        import "std/nonexistent"

        dot test:
            state:
                x: 0
            when true:
                do:
                    state.x = 1
    )",
                                    Permission::None);

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().stage, CompileError::Stage::IRBuild);
}

TEST_F(StdlibIntegrationTest, NonStdModuleFails) {
    auto result = compile_with_caps(R"(
        import "custom/module"

        dot test:
            state:
                x: 0
            when true:
                do:
                    state.x = 1
    )",
                                    Permission::None);

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().stage, CompileError::Stage::IRBuild);
}

// ============================================================================
// Missing Capability Tests
// ============================================================================

TEST_F(StdlibIntegrationTest, IoWithoutCapFails) {
    auto result = compile_with_caps(R"(
        import "std/io"

        dot test:
            state:
                x: 0
            when true:
                do:
                    state.x = 1
    )",
                                    Permission::None);

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().stage, CompileError::Stage::IRBuild);
}

TEST_F(StdlibIntegrationTest, CryptoWithoutCapFails) {
    auto result = compile_with_caps(R"(
        import "std/crypto"

        dot test:
            state:
                x: 0
            when true:
                do:
                    state.x = 1
    )",
                                    Permission::None);

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().stage, CompileError::Stage::IRBuild);
}

TEST_F(StdlibIntegrationTest, NetWithoutCapFails) {
    auto result = compile_with_caps(R"(
        import "std/net"

        dot test:
            state:
                x: 0
            when true:
                do:
                    state.x = 1
    )",
                                    Permission::None);

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().stage, CompileError::Stage::IRBuild);
}
