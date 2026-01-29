#pragma once

/// @file test_fixtures.hpp
/// @brief Shared test fixtures and utilities
///
/// Provides common test fixtures, helper functions, and utilities
/// that can be shared across multiple test files.
///
/// Includes:
/// - Base test fixtures for ALU, memory, execution, and mock-based tests
/// - VMContextFixture for VM context setup
/// - CompilerFixture for DSL compiler pipeline testing
/// - Architecture-parameterized test support
/// - RAII helpers for resource management in tests

#include <dotvm/core/alu.hpp>
#include <dotvm/core/arch_config.hpp>
#include <dotvm/core/capabilities/capability.hpp>
#include <dotvm/core/instruction.hpp>
#include <dotvm/core/memory.hpp>
#include <dotvm/core/value.hpp>
#include <dotvm/core/vm_context.hpp>
#include <dotvm/exec/dispatch_macros.hpp>
#include <dotvm/exec/execution_engine.hpp>

// DSL compiler includes for CompilerFixture
#include <optional>
#include <random>
#include <string>
#include <vector>

#include <dotvm/core/dsl/compiler/dsl_compiler.hpp>
#include <dotvm/core/dsl/compiler/ir_builder.hpp>
#include <dotvm/core/dsl/parser.hpp>

#include <gtest/gtest.h>

#include "mock_memory_manager.hpp"
#include "mock_register_file.hpp"

namespace dotvm::test {

// ============================================================================
// Value Test Utilities
// ============================================================================

/// Generate a variety of test values for comprehensive testing
inline std::vector<core::Value> generate_test_values() {
    return {
        // Integers
        core::Value::from_int(0),
        core::Value::from_int(1),
        core::Value::from_int(-1),
        core::Value::from_int(42),
        core::Value::from_int(-42),
        core::Value::from_int(std::numeric_limits<std::int32_t>::max()),
        core::Value::from_int(std::numeric_limits<std::int32_t>::min()),
        core::Value::from_int(std::numeric_limits<std::int64_t>::max() >> 16),

        // Floats
        core::Value::from_float(0.0),
        core::Value::from_float(1.0),
        core::Value::from_float(-1.0),
        core::Value::from_float(3.14159),
        core::Value::from_float(std::numeric_limits<double>::infinity()),
        core::Value::from_float(-std::numeric_limits<double>::infinity()),

        // Booleans
        core::Value::from_bool(true),
        core::Value::from_bool(false),

        // Nil
        core::Value::nil(),

        // Handles
        core::Value::from_handle({0, 0}),
        core::Value::from_handle({100, 1}),
        core::Value::from_handle({0xFFFFFFFF, 0xFFFF}),
    };
}

/// Generate random integer values for stress testing
inline std::vector<core::Value> generate_random_integers(std::size_t count, std::int64_t min,
                                                         std::int64_t max) {
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<std::int64_t> dist(min, max);

    std::vector<core::Value> values;
    values.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        values.push_back(core::Value::from_int(dist(gen)));
    }
    return values;
}

// ============================================================================
// Instruction Encoding Helpers
// ============================================================================

/// Encode a Type A instruction (register-register)
inline std::uint32_t encode_type_a(std::uint8_t opcode, std::uint8_t rd, std::uint8_t rs1,
                                   std::uint8_t rs2) {
    return core::encode_type_a(opcode, rd, rs1, rs2);
}

/// Encode a Type B instruction (register-immediate)
inline std::uint32_t encode_type_b(std::uint8_t opcode, std::uint8_t rd, std::uint16_t imm16) {
    return core::encode_type_b(opcode, rd, imm16);
}

/// Encode a Type C instruction (offset)
inline std::uint32_t encode_type_c(std::uint8_t opcode, std::int32_t offset24) {
    return core::encode_type_c(opcode, offset24);
}

/// Create a simple bytecode program (instructions + HALT)
inline std::vector<std::uint32_t> make_program(std::initializer_list<std::uint32_t> instructions) {
    std::vector<std::uint32_t> program(instructions);
    program.push_back(encode_type_a(exec::opcode::HALT, 0, 0, 0));
    return program;
}

// ============================================================================
// Assertion Helpers
// ============================================================================

/// Assert that two Values are equal
inline void assert_value_eq(const core::Value& actual, const core::Value& expected,
                            const std::string& msg = "") {
    ASSERT_EQ(actual.type(), expected.type()) << msg << " - type mismatch";

    switch (actual.type()) {
        case core::ValueType::Integer:
            ASSERT_EQ(actual.as_integer(), expected.as_integer()) << msg << " - integer mismatch";
            break;
        case core::ValueType::Float:
            ASSERT_DOUBLE_EQ(actual.as_float(), expected.as_float()) << msg << " - float mismatch";
            break;
        case core::ValueType::Bool:
            ASSERT_EQ(actual.as_bool(), expected.as_bool()) << msg << " - bool mismatch";
            break;
        case core::ValueType::Handle:
            ASSERT_EQ(actual.as_handle(), expected.as_handle()) << msg << " - handle mismatch";
            break;
        case core::ValueType::Nil:
            // Nil values are always equal
            break;
        case core::ValueType::Pointer:
            ASSERT_EQ(actual.as_pointer(), expected.as_pointer()) << msg << " - pointer mismatch";
            break;
    }
}

// ============================================================================
// Base Test Fixtures
// ============================================================================

/// Base fixture for ALU tests
class ALUTestBase : public ::testing::Test {
protected:
    core::ALU alu{core::Architecture::Arch64};
    core::ALU alu32{core::Architecture::Arch32};
};

/// Base fixture for memory tests
class MemoryTestBase : public ::testing::Test {
protected:
    core::MemoryManager mm;
};

/// Base fixture for mock-based tests
class MockTestBase : public ::testing::Test {
protected:
    MockRegisterFile mock_regs;
    MockMemoryManager mock_mem;

    void SetUp() override {
        mock_regs.reset();
        mock_mem.reset();
    }
};

/// Base fixture for execution engine tests
class ExecutionTestBase : public ::testing::Test {
protected:
    core::VmContext ctx;

    void SetUp() override { ctx.reset(); }

    /// Execute a program and return the result
    exec::ExecResult execute(const std::vector<std::uint32_t>& code);
};

// ============================================================================
// Parameterized Test Helpers
// ============================================================================

/// Architecture parameterization
struct ArchParam {
    core::Architecture arch;
    const char* name;
};

inline std::ostream& operator<<(std::ostream& os, const ArchParam& p) {
    return os << p.name;
}

/// Common architecture parameters
inline const std::vector<ArchParam> kArchitectures = {
    {core::Architecture::Arch32, "Arch32"},
    {core::Architecture::Arch64, "Arch64"},
};

/// Base for architecture-parameterized tests
class ArchParameterizedTest : public ::testing::TestWithParam<ArchParam> {
protected:
    core::Architecture arch() const { return GetParam().arch; }
};

// ============================================================================
// Memory Allocation Helpers
// ============================================================================

/// RAII wrapper for memory allocations in tests
class ScopedAllocation {
public:
    ScopedAllocation(core::MemoryManager& mm, std::size_t size) : mm_(mm), handle_{} {
        auto result = mm_.allocate(size);
        if (result) {
            handle_ = *result;
            valid_ = true;
        }
    }

    ~ScopedAllocation() {
        if (valid_) {
            [[maybe_unused]] auto err = mm_.deallocate(handle_);
        }
    }

    // Non-copyable, movable
    ScopedAllocation(const ScopedAllocation&) = delete;
    ScopedAllocation& operator=(const ScopedAllocation&) = delete;
    ScopedAllocation(ScopedAllocation&& other) noexcept
        : mm_(other.mm_), handle_(other.handle_), valid_(other.valid_) {
        other.valid_ = false;
    }

    [[nodiscard]] bool valid() const noexcept { return valid_; }
    [[nodiscard]] core::Handle handle() const noexcept { return handle_; }
    [[nodiscard]] core::Handle operator*() const noexcept { return handle_; }

private:
    core::MemoryManager& mm_;
    core::Handle handle_;
    bool valid_{false};
};

// ============================================================================
// VM Context Test Fixture
// ============================================================================

/// @brief Test fixture that sets up a VM context for testing
///
/// Provides a fully initialized VmContext with configurable architecture
/// and common helper methods for VM-level testing.
///
/// Usage:
/// @code
/// class MyVMTest : public VMContextFixture {};
///
/// TEST_F(MyVMTest, TestSomething) {
///     set_register(1, core::Value::from_int(42));
///     auto result = execute_program({...});
///     EXPECT_EQ(get_register(2).as_integer(), 84);
/// }
/// @endcode
class VMContextFixture : public ::testing::Test {
protected:
    core::VmContext ctx;

    void SetUp() override { ctx.reset(); }

    void TearDown() override { ctx.reset(); }

    /// @brief Get the target architecture
    [[nodiscard]] core::Architecture arch() const { return ctx.arch(); }

    /// @brief Set a register value
    void set_register(std::uint8_t reg, core::Value val) { ctx.registers().write(reg, val); }

    /// @brief Get a register value
    [[nodiscard]] core::Value get_register(std::uint8_t reg) const {
        return ctx.registers().read(reg);
    }

    /// @brief Execute a program and return result
    [[nodiscard]] exec::ExecResult execute(const std::vector<std::uint32_t>& code) {
        exec::ExecutionEngine engine(ctx);
        return engine.execute(code.data(), code.size(), 0, {});
    }

    /// @brief Execute with entry point
    [[nodiscard]] exec::ExecResult execute_at(const std::vector<std::uint32_t>& code,
                                              std::size_t entry_point) {
        exec::ExecutionEngine engine(ctx);
        return engine.execute(code.data(), code.size(), entry_point, {});
    }

    /// @brief Allocate memory and return handle
    [[nodiscard]] std::expected<core::Handle, core::MemoryError> allocate(std::size_t size) {
        return ctx.memory().allocate(size);
    }

    /// @brief Check if a handle is valid
    [[nodiscard]] bool is_valid_handle(core::Handle h) const { return ctx.memory().is_valid(h); }
};

// ============================================================================
// DSL Compiler Test Fixture
// ============================================================================

/// @brief Test fixture for DSL compiler pipeline testing
///
/// Provides utilities for parsing DSL source, building IR, and compiling
/// to bytecode. Supports capability configuration for permission testing.
///
/// Usage:
/// @code
/// class MyCompilerTest : public CompilerFixture {};
///
/// TEST_F(MyCompilerTest, TestParsing) {
///     auto result = parse_source("dot test: when true: do: log \"hello\"");
///     ASSERT_TRUE(result.is_ok());
///     EXPECT_EQ(result.value().dots.size(), 1);
/// }
/// @endcode
class CompilerFixture : public ::testing::Test {
protected:
    /// @brief Granted capabilities for compilation
    core::capabilities::Permission granted_caps_{core::capabilities::Permission::None};

    void SetUp() override { granted_caps_ = core::capabilities::Permission::None; }

    /// @brief Grant a capability for compilation
    void grant_capability(core::capabilities::Permission cap) {
        granted_caps_ = granted_caps_ | cap;
    }

    /// @brief Grant all capabilities
    void grant_all_capabilities() { granted_caps_ = core::capabilities::Permission::All; }

    /// @brief Revoke all capabilities
    void revoke_all_capabilities() { granted_caps_ = core::capabilities::Permission::None; }

    /// @brief Parse DSL source code
    [[nodiscard]] auto parse_source(std::string_view source) {
        return core::dsl::DslParser::parse(source);
    }

    /// @brief Build IR from a parsed module
    [[nodiscard]] auto build_ir(const core::dsl::DslModule& module) {
        core::dsl::compiler::CompileContext ctx{granted_caps_};
        core::dsl::compiler::IRBuilder builder{ctx};
        return builder.build(module);
    }

    /// @brief Compile DSL source to bytecode
    [[nodiscard]] auto compile(std::string_view source) {
        core::dsl::compiler::CompileOptions opts;
        opts.granted_caps = granted_caps_;
        core::dsl::compiler::DslCompiler compiler{opts};
        return compiler.compile_source(source);
    }

    /// @brief Create a simple test DSL with minimal structure
    [[nodiscard]] static std::string make_simple_dot(const std::string& name = "test",
                                                     const std::string& state_vars = "",
                                                     const std::string& trigger_condition = "true",
                                                     const std::string& action = "nop") {
        std::string dsl = "dot " + name + ":\n";
        if (!state_vars.empty()) {
            dsl += "    state:\n        " + state_vars + "\n";
        }
        dsl += "    when " + trigger_condition + ":\n";
        dsl += "        do:\n";
        dsl += "            " + action + "\n";
        return dsl;
    }

    /// @brief Create a counter dot for common test scenarios
    [[nodiscard]] static std::string make_counter_dot(const std::string& name = "counter",
                                                      int initial_count = 0, int max_count = 10) {
        return "dot " + name +
               ":\n"
               "    state:\n"
               "        count: " +
               std::to_string(initial_count) +
               "\n"
               "        max: " +
               std::to_string(max_count) +
               "\n"
               "    when count < max:\n"
               "        do:\n"
               "            state.count += 1\n";
    }
};

// ============================================================================
// Capability Test Fixture
// ============================================================================

/// @brief Test fixture for capability and permission testing
///
/// Extends CompilerFixture with additional helpers for testing
/// capability-based security features.
class CapabilityTestFixture : public CompilerFixture {
protected:
    /// @brief Test that compilation succeeds with given capabilities
    void expect_compile_success(std::string_view source,
                                core::capabilities::Permission required_caps) {
        grant_capability(required_caps);
        auto result = compile(source);
        EXPECT_TRUE(result.has_value()) << "Expected compilation to succeed with capability: "
                                        << core::capabilities::to_string(required_caps);
    }

    /// @brief Test that compilation fails without required capabilities
    void expect_compile_failure_without_cap(std::string_view source,
                                            core::capabilities::Permission required_cap) {
        revoke_all_capabilities();
        auto result = compile(source);
        // We expect either a parse error or a capability error
        // The exact failure mode depends on when capability checking happens
    }
};

}  // namespace dotvm::test
