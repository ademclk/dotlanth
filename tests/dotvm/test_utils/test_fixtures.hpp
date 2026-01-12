#pragma once

/// @file test_fixtures.hpp
/// @brief Shared test fixtures and utilities
///
/// Provides common test fixtures, helper functions, and utilities
/// that can be shared across multiple test files.

#include <dotvm/core/value.hpp>
#include <dotvm/core/memory.hpp>
#include <dotvm/core/alu.hpp>
#include <dotvm/core/arch_config.hpp>
#include <dotvm/core/instruction.hpp>
#include <dotvm/core/vm_context.hpp>
#include <dotvm/exec/dispatch_macros.hpp>
#include <dotvm/exec/execution_engine.hpp>

#include "mock_register_file.hpp"
#include "mock_memory_manager.hpp"

#include <gtest/gtest.h>
#include <vector>
#include <random>

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
inline std::vector<core::Value> generate_random_integers(
    std::size_t count, std::int64_t min, std::int64_t max) {

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
inline std::uint32_t encode_type_a(std::uint8_t opcode, std::uint8_t rd,
                                    std::uint8_t rs1, std::uint8_t rs2) {
    return core::encode_type_a(opcode, rd, rs1, rs2);
}

/// Encode a Type B instruction (register-immediate)
inline std::uint32_t encode_type_b(std::uint8_t opcode, std::uint8_t rd,
                                    std::uint16_t imm16) {
    return core::encode_type_b(opcode, rd, imm16);
}

/// Encode a Type C instruction (offset)
inline std::uint32_t encode_type_c(std::uint8_t opcode, std::int32_t offset24) {
    return core::encode_type_c(opcode, offset24);
}

/// Create a simple bytecode program (instructions + HALT)
inline std::vector<std::uint32_t> make_program(
    std::initializer_list<std::uint32_t> instructions) {

    std::vector<std::uint32_t> program(instructions);
    program.push_back(encode_type_a(exec::opcode::HALT, 0, 0, 0));
    return program;
}

// ============================================================================
// Assertion Helpers
// ============================================================================

/// Assert that two Values are equal
inline void assert_value_eq(const core::Value& actual,
                            const core::Value& expected,
                            const std::string& msg = "") {
    ASSERT_EQ(actual.type(), expected.type())
        << msg << " - type mismatch";

    switch (actual.type()) {
        case core::ValueType::Integer:
            ASSERT_EQ(actual.as_integer(), expected.as_integer())
                << msg << " - integer mismatch";
            break;
        case core::ValueType::Float:
            ASSERT_DOUBLE_EQ(actual.as_float(), expected.as_float())
                << msg << " - float mismatch";
            break;
        case core::ValueType::Bool:
            ASSERT_EQ(actual.as_bool(), expected.as_bool())
                << msg << " - bool mismatch";
            break;
        case core::ValueType::Handle:
            ASSERT_EQ(actual.as_handle(), expected.as_handle())
                << msg << " - handle mismatch";
            break;
        case core::ValueType::Nil:
            // Nil values are always equal
            break;
        case core::ValueType::Pointer:
            ASSERT_EQ(actual.as_pointer(), expected.as_pointer())
                << msg << " - pointer mismatch";
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

    void SetUp() override {
        ctx.reset();
    }

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
    ScopedAllocation(core::MemoryManager& mm, std::size_t size)
        : mm_(mm), handle_{} {
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

} // namespace dotvm::test
