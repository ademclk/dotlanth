#pragma once

/// @file common.hpp
/// @brief Common test utilities and factory functions for DotVM tests
///
/// Provides:
/// - Factory functions for creating test objects (Values, Instructions)
/// - Random seed control for reproducibility
/// - Type tag utilities and assertion helpers

#include <cstdint>
#include <optional>
#include <random>
#include <string>
#include <vector>

#include <dotvm/core/arch_config.hpp>
#include <dotvm/core/instruction.hpp>
#include <dotvm/core/value.hpp>

#include <gtest/gtest.h>

namespace dotvm::test {

// ============================================================================
// Random Seed Control
// ============================================================================

/// @brief Global test random seed for reproducibility
/// Can be set via environment variable DOTVM_TEST_SEED or set_test_seed()
inline std::uint64_t g_test_seed = []() -> std::uint64_t {
    // Check for environment variable
    if (const char* env_seed = std::getenv("DOTVM_TEST_SEED")) {
        try {
            return static_cast<std::uint64_t>(std::stoull(env_seed));
        } catch (...) {
            // Fall through to random seed
        }
    }
    return static_cast<std::uint64_t>(std::random_device{}());
}();

/// @brief Set the global test seed
inline void set_test_seed(std::uint64_t seed) noexcept {
    g_test_seed = seed;
}

/// @brief Get the current test seed
[[nodiscard]] inline std::uint64_t get_test_seed() noexcept {
    return g_test_seed;
}

/// @brief Create a seeded random engine for tests
[[nodiscard]] inline std::mt19937_64 make_test_rng() {
    return std::mt19937_64{g_test_seed};
}

/// @brief RAII guard for temporarily setting a test seed
class SeedGuard {
public:
    explicit SeedGuard(std::uint64_t temp_seed) : old_seed_(g_test_seed) {
        g_test_seed = temp_seed;
    }

    ~SeedGuard() { g_test_seed = old_seed_; }

    SeedGuard(const SeedGuard&) = delete;
    SeedGuard& operator=(const SeedGuard&) = delete;

private:
    std::uint64_t old_seed_;
};

// ============================================================================
// Value Factory Functions
// ============================================================================

/// @brief Create a test integer Value
/// @param val The integer value
/// @return A Value containing the integer
[[nodiscard]] inline core::Value make_test_value(std::int64_t val) {
    return core::Value::from_int(val);
}

/// @brief Create a test float Value
/// @param val The float value
/// @return A Value containing the float
[[nodiscard]] inline core::Value make_test_value(double val) {
    return core::Value::from_float(val);
}

/// @brief Create a test boolean Value
/// @param val The boolean value
/// @return A Value containing the boolean
[[nodiscard]] inline core::Value make_test_value(bool val) {
    return core::Value::from_bool(val);
}

/// @brief Create a test handle Value
/// @param index The handle index
/// @param generation The handle generation (default 1)
/// @return A Value containing the handle
[[nodiscard]] inline core::Value make_test_handle(std::uint32_t index,
                                                  std::uint16_t generation = 1) {
    return core::Value::from_handle({index, generation});
}

/// @brief Create a nil test Value
/// @return A nil Value
[[nodiscard]] inline core::Value make_test_nil() {
    return core::Value::nil();
}

/// @brief Create a random integer Value within a range
/// @param rng Random engine to use
/// @param min Minimum value (inclusive)
/// @param max Maximum value (inclusive)
/// @return A random integer Value
[[nodiscard]] inline core::Value make_random_int(std::mt19937_64& rng, std::int64_t min = -1000000,
                                                 std::int64_t max = 1000000) {
    std::uniform_int_distribution<std::int64_t> dist(min, max);
    return core::Value::from_int(dist(rng));
}

/// @brief Create a random float Value within a range
/// @param rng Random engine to use
/// @param min Minimum value
/// @param max Maximum value
/// @return A random float Value
[[nodiscard]] inline core::Value make_random_float(std::mt19937_64& rng, double min = -1e10,
                                                   double max = 1e10) {
    std::uniform_real_distribution<double> dist(min, max);
    return core::Value::from_float(dist(rng));
}

/// @brief Create a random Value of any type
/// @param rng Random engine to use
/// @return A random Value (integer, float, bool, nil, or handle)
[[nodiscard]] inline core::Value make_random_value(std::mt19937_64& rng) {
    std::uniform_int_distribution<int> type_dist(0, 4);
    switch (type_dist(rng)) {
        case 0:
            return make_random_int(rng);
        case 1:
            return make_random_float(rng);
        case 2:
            return core::Value::from_bool(std::uniform_int_distribution<int>(0, 1)(rng) != 0);
        case 3:
            return core::Value::nil();
        case 4:
        default: {
            std::uniform_int_distribution<std::uint32_t> idx_dist(0, 10000);
            std::uniform_int_distribution<std::uint16_t> gen_dist(1, 100);
            return core::Value::from_handle({idx_dist(rng), gen_dist(rng)});
        }
    }
}

/// @brief Generate a vector of test Values with variety
/// @param count Number of values to generate
/// @return Vector of mixed-type Values
[[nodiscard]] inline std::vector<core::Value> make_test_value_set(std::size_t count = 20) {
    auto rng = make_test_rng();
    std::vector<core::Value> values;
    values.reserve(count);

    // Ensure we have at least one of each type
    values.push_back(core::Value::from_int(0));
    values.push_back(core::Value::from_int(42));
    values.push_back(core::Value::from_int(-1));
    values.push_back(core::Value::from_float(0.0));
    values.push_back(core::Value::from_float(3.14159));
    values.push_back(core::Value::from_bool(true));
    values.push_back(core::Value::from_bool(false));
    values.push_back(core::Value::nil());
    values.push_back(core::Value::from_handle({1, 1}));

    // Fill rest with random values
    while (values.size() < count) {
        values.push_back(make_random_value(rng));
    }

    return values;
}

// ============================================================================
// Instruction Factory Functions
// ============================================================================

/// @brief Create a Type A instruction (register-register)
/// @param opcode The opcode
/// @param rd Destination register
/// @param rs1 Source register 1
/// @param rs2 Source register 2
/// @return Encoded instruction
[[nodiscard]] inline std::uint32_t make_test_instruction_a(std::uint8_t opcode, std::uint8_t rd,
                                                           std::uint8_t rs1, std::uint8_t rs2) {
    return core::encode_type_a(opcode, rd, rs1, rs2);
}

/// @brief Create a Type B instruction (register-immediate)
/// @param opcode The opcode
/// @param rd Destination register
/// @param imm16 16-bit immediate value
/// @return Encoded instruction
[[nodiscard]] inline std::uint32_t make_test_instruction_b(std::uint8_t opcode, std::uint8_t rd,
                                                           std::uint16_t imm16) {
    return core::encode_type_b(opcode, rd, imm16);
}

/// @brief Create a Type C instruction (offset)
/// @param opcode The opcode
/// @param offset24 24-bit signed offset
/// @return Encoded instruction
[[nodiscard]] inline std::uint32_t make_test_instruction_c(std::uint8_t opcode,
                                                           std::int32_t offset24) {
    return core::encode_type_c(opcode, offset24);
}

/// @brief Create an ADD instruction
[[nodiscard]] inline std::uint32_t make_add_instr(std::uint8_t rd, std::uint8_t rs1,
                                                  std::uint8_t rs2) {
    return make_test_instruction_a(0x00, rd, rs1, rs2);  // ADD opcode
}

/// @brief Create a SUB instruction
[[nodiscard]] inline std::uint32_t make_sub_instr(std::uint8_t rd, std::uint8_t rs1,
                                                  std::uint8_t rs2) {
    return make_test_instruction_a(0x01, rd, rs1, rs2);  // SUB opcode
}

/// @brief Create a MUL instruction
[[nodiscard]] inline std::uint32_t make_mul_instr(std::uint8_t rd, std::uint8_t rs1,
                                                  std::uint8_t rs2) {
    return make_test_instruction_a(0x02, rd, rs1, rs2);  // MUL opcode
}

/// @brief Create a DIV instruction
[[nodiscard]] inline std::uint32_t make_div_instr(std::uint8_t rd, std::uint8_t rs1,
                                                  std::uint8_t rs2) {
    return make_test_instruction_a(0x03, rd, rs1, rs2);  // DIV opcode
}

/// @brief Create a LOAD_IMM instruction
[[nodiscard]] inline std::uint32_t make_load_imm_instr(std::uint8_t rd, std::uint16_t imm) {
    return make_test_instruction_b(0x10, rd, imm);  // LOAD_IMM opcode
}

/// @brief Create a NOP instruction
[[nodiscard]] inline std::uint32_t make_nop_instr() {
    return make_test_instruction_a(0x00, 0, 0, 0);  // ADD r0, r0, r0 is NOP
}

/// @brief Create a HALT instruction
[[nodiscard]] inline std::uint32_t make_halt_instr() {
    return make_test_instruction_a(0xFF, 0, 0, 0);  // HALT opcode
}

/// @brief Create a simple test program with instructions followed by HALT
/// @param instructions The instructions to include
/// @return Vector with instructions + HALT
[[nodiscard]] inline std::vector<std::uint32_t>
make_test_program(std::initializer_list<std::uint32_t> instructions) {
    std::vector<std::uint32_t> program(instructions);
    program.push_back(make_halt_instr());
    return program;
}

// ============================================================================
// Value Assertion Helpers
// ============================================================================

/// @brief Assert that a Value is of expected type and value (for integers)
inline void assert_int_value(const core::Value& val, std::int64_t expected,
                             const std::string& context = "") {
    ASSERT_TRUE(val.is_integer()) << context << " Expected integer, got "
                                  << static_cast<int>(val.type());
    EXPECT_EQ(val.as_integer(), expected) << context;
}

/// @brief Assert that a Value is of expected type and value (for floats)
inline void assert_float_value(const core::Value& val, double expected,
                               const std::string& context = "", double epsilon = 1e-9) {
    ASSERT_TRUE(val.is_float()) << context << " Expected float, got "
                                << static_cast<int>(val.type());
    EXPECT_NEAR(val.as_float(), expected, epsilon) << context;
}

/// @brief Assert that a Value is of expected type and value (for bools)
inline void assert_bool_value(const core::Value& val, bool expected,
                              const std::string& context = "") {
    ASSERT_TRUE(val.is_bool()) << context << " Expected bool, got " << static_cast<int>(val.type());
    EXPECT_EQ(val.as_bool(), expected) << context;
}

/// @brief Assert that a Value is nil
inline void assert_nil_value(const core::Value& val, const std::string& context = "") {
    EXPECT_TRUE(val.is_nil()) << context << " Expected nil";
}

/// @brief Assert that two Values are equal
inline void assert_values_equal(const core::Value& actual, const core::Value& expected,
                                const std::string& context = "") {
    ASSERT_EQ(actual.type(), expected.type())
        << context << " Type mismatch: " << static_cast<int>(actual.type()) << " vs "
        << static_cast<int>(expected.type());

    switch (actual.type()) {
        case core::ValueType::Integer:
            EXPECT_EQ(actual.as_integer(), expected.as_integer()) << context;
            break;
        case core::ValueType::Float:
            EXPECT_DOUBLE_EQ(actual.as_float(), expected.as_float()) << context;
            break;
        case core::ValueType::Bool:
            EXPECT_EQ(actual.as_bool(), expected.as_bool()) << context;
            break;
        case core::ValueType::Handle:
            EXPECT_EQ(actual.as_handle(), expected.as_handle()) << context;
            break;
        case core::ValueType::Nil:
            // Both nil, nothing more to check
            break;
        case core::ValueType::Pointer:
            EXPECT_EQ(actual.as_pointer(), expected.as_pointer()) << context;
            break;
    }
}

// ============================================================================
// Architecture Testing Helpers
// ============================================================================

/// @brief Get all supported architectures for parameterized tests
[[nodiscard]] inline std::vector<core::Architecture> all_architectures() {
    return {core::Architecture::Arch32, core::Architecture::Arch64};
}

/// @brief Format architecture for test output
[[nodiscard]] inline std::string arch_name(core::Architecture arch) {
    switch (arch) {
        case core::Architecture::Arch32:
            return "Arch32";
        case core::Architecture::Arch64:
            return "Arch64";
        case core::Architecture::Arch128:
            return "Arch128";
        case core::Architecture::Arch256:
            return "Arch256";
        case core::Architecture::Arch512:
            return "Arch512";
    }
    return "Unknown";
}

}  // namespace dotvm::test
