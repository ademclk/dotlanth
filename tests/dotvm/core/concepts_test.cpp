#include <gtest/gtest.h>

#include <dotvm/core/concepts/concepts.hpp>
#include <dotvm/core/value.hpp>
#include <dotvm/core/memory_config.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>

using namespace dotvm::core;
using namespace dotvm::core::concepts;

// ============================================================================
// Mock Implementations for Testing
// ============================================================================

/// Mock register file for testing
/// Satisfies RegisterFileInterface but with simplified implementation
struct MockRegisterFile {
    std::array<Value, 256> regs_{};

    Value read(std::uint8_t idx) const noexcept {
        return regs_[idx];
    }

    void write(std::uint8_t idx, Value val) noexcept {
        regs_[idx] = val;
    }

    static constexpr std::size_t size() noexcept {
        return 256;
    }
};

/// Mock ALU for testing
/// Satisfies MinimalAlu concept with simplified operations
struct MockMinimalAlu {
    Value add(Value a, Value b) const noexcept {
        return Value::from_int(a.as_integer() + b.as_integer());
    }

    Value sub(Value a, Value b) const noexcept {
        return Value::from_int(a.as_integer() - b.as_integer());
    }

    Value mul(Value a, Value b) const noexcept {
        return Value::from_int(a.as_integer() * b.as_integer());
    }

    Value div(Value a, Value b) const noexcept {
        auto divisor = b.as_integer();
        return divisor == 0 ? Value::from_int(0) :
               Value::from_int(a.as_integer() / divisor);
    }

    Value mod(Value a, Value b) const noexcept {
        auto divisor = b.as_integer();
        return divisor == 0 ? Value::from_int(0) :
               Value::from_int(a.as_integer() % divisor);
    }

    Value neg(Value a) const noexcept {
        return Value::from_int(-a.as_integer());
    }

    Value abs(Value a) const noexcept {
        auto val = a.as_integer();
        return Value::from_int(val < 0 ? -val : val);
    }
};

// ============================================================================
// Concept Verification Tests
// ============================================================================

TEST(ConceptsTest, MockRegisterFileSatisfiesInterface) {
    // Compile-time verification
    static_assert(RegisterFileInterface<MockRegisterFile>,
                  "MockRegisterFile must satisfy RegisterFileInterface");

    // Runtime verification
    MockRegisterFile rf;
    rf.write(1, Value::from_int(42));
    EXPECT_EQ(rf.read(1).as_integer(), 42);
    EXPECT_EQ(rf.size(), 256);
}

TEST(ConceptsTest, MockMinimalAluSatisfiesMinimalAlu) {
    // Compile-time verification
    static_assert(MinimalAlu<MockMinimalAlu>,
                  "MockMinimalAlu must satisfy MinimalAlu concept");

    // Runtime verification
    MockMinimalAlu alu;
    auto a = Value::from_int(10);
    auto b = Value::from_int(3);

    EXPECT_EQ(alu.add(a, b).as_integer(), 13);
    EXPECT_EQ(alu.sub(a, b).as_integer(), 7);
    EXPECT_EQ(alu.mul(a, b).as_integer(), 30);
    EXPECT_EQ(alu.div(a, b).as_integer(), 3);
    EXPECT_EQ(alu.mod(a, b).as_integer(), 1);
    EXPECT_EQ(alu.neg(a).as_integer(), -10);
    EXPECT_EQ(alu.abs(Value::from_int(-10)).as_integer(), 10);
}

TEST(ConceptsTest, StandardTypesSatisfyConcepts) {
    // These are compile-time checks that were already verified in concepts.hpp
    // but we include them here for documentation purposes
    static_assert(RegisterFileInterface<RegisterFile>);
    static_assert(RegisterFileInterface<ArchRegisterFile>);
    static_assert(ArchAwareRegisterFile<ArchRegisterFile>);
    static_assert(AluInterface<ALU>);
    static_assert(MemoryManagerInterface<MemoryManager>);

    SUCCEED() << "All standard types satisfy their respective concepts";
}

// ============================================================================
// Template Function Tests Using Concepts
// ============================================================================

/// Example template function constrained by RegisterFileInterface
template<RegisterFileInterface RF>
Value read_and_write(RF& rf, std::uint8_t src, std::uint8_t dst) {
    auto val = rf.read(src);
    rf.write(dst, val);
    return val;
}

/// Example template function constrained by MinimalAlu
template<MinimalAlu A>
Value compute_expression(const A& alu, Value a, Value b, Value c) {
    // Compute (a + b) * c
    return alu.mul(alu.add(a, b), c);
}

TEST(ConceptsTest, TemplatedFunctionWithMockRegisterFile) {
    MockRegisterFile rf;
    rf.write(5, Value::from_int(100));

    auto result = read_and_write(rf, 5, 10);
    EXPECT_EQ(result.as_integer(), 100);
    EXPECT_EQ(rf.read(10).as_integer(), 100);
}

TEST(ConceptsTest, TemplatedFunctionWithStandardRegisterFile) {
    ArchRegisterFile rf{Architecture::Arch64};
    rf.write(5, Value::from_int(200));

    auto result = read_and_write(rf, 5, 10);
    EXPECT_EQ(result.as_integer(), 200);
    EXPECT_EQ(rf.read(10).as_integer(), 200);
}

TEST(ConceptsTest, TemplatedFunctionWithMockAlu) {
    MockMinimalAlu alu;
    auto a = Value::from_int(2);
    auto b = Value::from_int(3);
    auto c = Value::from_int(4);

    // (2 + 3) * 4 = 20
    auto result = compute_expression(alu, a, b, c);
    EXPECT_EQ(result.as_integer(), 20);
}

TEST(ConceptsTest, TemplatedFunctionWithStandardAlu) {
    ALU alu{Architecture::Arch64};
    auto a = Value::from_int(2);
    auto b = Value::from_int(3);
    auto c = Value::from_int(4);

    // (2 + 3) * 4 = 20
    auto result = compute_expression(alu, a, b, c);
    EXPECT_EQ(result.as_integer(), 20);
}

// ============================================================================
// Type Trait Tests
// ============================================================================

TEST(ConceptsTest, TypeTraits) {
    // is_register_file trait
    static_assert(is_register_file<RegisterFile>);
    static_assert(is_register_file<ArchRegisterFile>);
    static_assert(is_register_file<MockRegisterFile>);

    // is_alu trait
    static_assert(is_alu<ALU>);

    // is_minimal_alu trait
    static_assert(is_minimal_alu<ALU>);
    static_assert(is_minimal_alu<MockMinimalAlu>);

    // is_memory_manager trait
    static_assert(is_memory_manager<MemoryManager>);

    SUCCEED() << "All type traits correctly identify conforming types";
}
