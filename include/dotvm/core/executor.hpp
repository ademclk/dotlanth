#pragma once

/// @file executor.hpp
/// @brief Instruction executor for DotVM
///
/// This header provides the Executor class which handles instruction
/// fetching, decoding, and dispatch. It includes specialized executors
/// for different opcode categories (arithmetic, bitwise, control flow, etc.)

#include <concepts>
#include <cstdint>
#include <span>

#include "alu.hpp"
#include "execution_error.hpp"
#include "instruction.hpp"
#include "opcode.hpp"
#include "value.hpp"
#include "vm_context.hpp"

namespace dotvm::core {

// ============================================================================
// Branch Prediction Hints
// ============================================================================

/// Hint that a condition is likely to be true (hot path optimization)
#define DOTVM_LIKELY(x)   (__builtin_expect(!!(x), 1))

/// Hint that a condition is unlikely to be true (cold path optimization)
#define DOTVM_UNLIKELY(x) (__builtin_expect(!!(x), 0))

// ============================================================================
// Step Result
// ============================================================================

/// Result of executing a single instruction
struct StepResult {
    /// Error code (Success if no error)
    ExecutionError err{ExecutionError::Success};

    /// Whether execution should halt after this instruction
    bool should_halt{false};

    /// Next PC value (0 = advance by instruction size, non-zero = absolute jump)
    std::uint64_t next_pc{0};

    /// Create a success result that advances PC normally
    [[nodiscard]] static constexpr StepResult success() noexcept {
        return StepResult{};
    }

    /// Create a success result that jumps to absolute address
    [[nodiscard]] static constexpr StepResult jump(std::uint64_t target) noexcept {
        return StepResult{ExecutionError::Success, false, target};
    }

    /// Create a halt result
    [[nodiscard]] static constexpr StepResult halt() noexcept {
        return StepResult{ExecutionError::Halted, true, 0};
    }

    /// Create an error result
    [[nodiscard]] static constexpr StepResult make_error(ExecutionError error) noexcept {
        return StepResult{error, is_fatal_error(error), 0};
    }
};

// ============================================================================
// Arithmetic Executor
// ============================================================================

/// Executes arithmetic opcodes (ADD, SUB, MUL, DIV, MOD, NEG, ADDI, SUBI, MULI)
///
/// This class handles the actual execution of arithmetic instructions,
/// including overflow detection when strict_overflow is enabled.
class ArithmeticExecutor {
public:
    /// Construct with reference to VM context
    explicit ArithmeticExecutor(VmContext& ctx) noexcept : ctx_{ctx} {}

    /// Execute a Type A arithmetic instruction (register-register)
    ///
    /// @param decoded Decoded Type A instruction
    /// @return StepResult indicating success or error
    [[nodiscard]] StepResult execute_type_a(const DecodedTypeA& decoded) noexcept;

    /// Execute a Type B arithmetic instruction (register-immediate)
    ///
    /// @param decoded Decoded Type B instruction
    /// @return StepResult indicating success or error
    [[nodiscard]] StepResult execute_type_b(const DecodedTypeB& decoded) noexcept;

private:
    VmContext& ctx_;

    // -------------------------------------------------------------------------
    // Overflow-checked arithmetic operations
    // -------------------------------------------------------------------------

    /// ADD with overflow check: Rd = a + b
    [[nodiscard]] StepResult add_op(std::uint8_t rd, Value a, Value b) noexcept;

    /// SUB with overflow check: Rd = a - b
    [[nodiscard]] StepResult sub_op(std::uint8_t rd, Value a, Value b) noexcept;

    /// MUL with overflow check: Rd = a * b
    [[nodiscard]] StepResult mul_op(std::uint8_t rd, Value a, Value b) noexcept;

    /// DIV with zero check: Rd = a / b
    [[nodiscard]] StepResult div_op(std::uint8_t rd, Value a, Value b) noexcept;

    /// MOD with zero check: Rd = a % b
    [[nodiscard]] StepResult mod_op(std::uint8_t rd, Value a, Value b) noexcept;

    /// NEG: Rd = -a
    [[nodiscard]] StepResult neg_op(std::uint8_t rd, Value a) noexcept;

    // -------------------------------------------------------------------------
    // Helper to write result and return success
    // -------------------------------------------------------------------------

    /// Write value to register and return success
    StepResult write_success(std::uint8_t rd, Value result) noexcept {
        ctx_.registers().write(rd, result);
        return StepResult::success();
    }

    /// Write value to register and return overflow error
    StepResult write_overflow(std::uint8_t rd, Value result) noexcept {
        ctx_.registers().write(rd, result);
        return StepResult::make_error(ExecutionError::IntegerOverflow);
    }

    /// Write value to register and return division by zero error
    StepResult write_div_zero(std::uint8_t rd, Value result) noexcept {
        ctx_.registers().write(rd, result);
        return StepResult::make_error(ExecutionError::DivisionByZero);
    }
};

// ============================================================================
// Floating Point Executor
// ============================================================================

/// Executes floating-point opcodes (FADD through I2F)
///
/// This class handles IEEE 754 compliant floating-point operations:
/// - Binary operations: FADD, FSUB, FMUL, FDIV, FCMP
/// - Unary operations: FNEG, FSQRT
/// - Type conversions: F2I, I2F
///
/// Features:
/// - NaN propagation: if either operand is NaN, result is NaN
/// - Infinity handling per IEEE 754 semantics
/// - FDIV by zero produces +/-Inf based on dividend sign
/// - FCMP sets comparison flags (LT, EQ, GT, UNORD)
/// - F2I uses saturation semantics (NaN->0, overflow->clamp)
class FloatingPointExecutor {
public:
    /// Construct with references to VM context and execution state
    ///
    /// @param ctx VM context with registers, memory, configuration
    /// @param state Execution state for setting FP flags and error counts
    explicit FloatingPointExecutor(VmContext& ctx, ExecutionState& state) noexcept
        : ctx_{ctx}, state_{state} {}

    /// Execute a Type A floating-point instruction
    ///
    /// @param decoded Decoded Type A instruction (FADD through I2F)
    /// @return StepResult indicating success or error
    [[nodiscard]] StepResult execute_type_a(const DecodedTypeA& decoded) noexcept;

private:
    VmContext& ctx_;
    ExecutionState& state_;

    // -------------------------------------------------------------------------
    // Binary Floating-Point Operations
    // -------------------------------------------------------------------------

    /// FADD: Rd = Rs1 + Rs2 (IEEE 754 double addition)
    [[nodiscard]] StepResult fadd_op(std::uint8_t rd, Value a, Value b) noexcept;

    /// FSUB: Rd = Rs1 - Rs2 (IEEE 754 double subtraction)
    [[nodiscard]] StepResult fsub_op(std::uint8_t rd, Value a, Value b) noexcept;

    /// FMUL: Rd = Rs1 * Rs2 (IEEE 754 double multiplication)
    [[nodiscard]] StepResult fmul_op(std::uint8_t rd, Value a, Value b) noexcept;

    /// FDIV: Rd = Rs1 / Rs2 (IEEE 754 double division)
    /// Division by zero returns +/-Inf based on dividend sign
    [[nodiscard]] StepResult fdiv_op(std::uint8_t rd, Value a, Value b) noexcept;

    /// FCMP: Compare Rs1 and Rs2, set FP flags in ExecutionState
    /// Sets LT, EQ, GT, or UNORD (unordered if NaN involved)
    [[nodiscard]] StepResult fcmp_op(Value a, Value b) noexcept;

    // -------------------------------------------------------------------------
    // Unary Floating-Point Operations
    // -------------------------------------------------------------------------

    /// FNEG: Rd = -Rs1 (IEEE 754 negation)
    /// Works correctly for NaN, Inf, and -0.0
    [[nodiscard]] StepResult fneg_op(std::uint8_t rd, Value a) noexcept;

    /// FSQRT: Rd = sqrt(Rs1) (IEEE 754 square root)
    /// sqrt(-0.0) = -0.0, sqrt(+Inf) = +Inf, sqrt(NaN) = NaN
    /// sqrt(negative) = NaN (signals FloatingPointInvalid in strict mode)
    [[nodiscard]] StepResult fsqrt_op(std::uint8_t rd, Value a) noexcept;

    // -------------------------------------------------------------------------
    // Type Conversion Operations
    // -------------------------------------------------------------------------

    /// F2I: Rd = int64(Rs1) (float to integer conversion)
    /// Uses saturation semantics:
    /// - NaN -> 0
    /// - +Inf or value > INT64_MAX -> INT64_MAX
    /// - -Inf or value < INT64_MIN -> INT64_MIN
    /// In strict mode, signals ConversionOverflow on saturation
    [[nodiscard]] StepResult f2i_op(std::uint8_t rd, Value a) noexcept;

    /// I2F: Rd = double(Rs1) (integer to float conversion)
    /// May lose precision for large integers (> 2^53)
    [[nodiscard]] StepResult i2f_op(std::uint8_t rd, Value a) noexcept;

    // -------------------------------------------------------------------------
    // Helper Functions
    // -------------------------------------------------------------------------

    /// Extract double value from Value, converting integer if needed
    /// Returns NaN for non-numeric types
    [[nodiscard]] double get_float(Value v) const noexcept;

    /// Write double result to register and return success
    StepResult write_float(std::uint8_t rd, double result) noexcept;

    /// Write double result and return FP invalid error
    StepResult write_fp_invalid(std::uint8_t rd, double result) noexcept;

    /// Write integer result and return conversion overflow error
    StepResult write_conversion_overflow(std::uint8_t rd, std::int64_t result) noexcept;
};

// ============================================================================
// Bitwise Executor
// ============================================================================

/// Executes bitwise opcodes (AND, OR, XOR, NOT, SHL, SHR, SAR, ROL, ROR)
/// and their immediate variants (SHLI, SHRI, SARI, ANDI, ORI, XORI)
///
/// This class handles bitwise operations with architecture-aware masking:
/// - Type A (register-register): AND, OR, XOR, NOT, SHL, SHR, SAR, ROL, ROR
/// - Type S (shift-immediate): SHLI, SHRI, SARI with 6-bit shift amount
/// - Type B (accumulator-immediate): ANDI, ORI, XORI with zero-extended imm16
///
/// All operations are delegated to the ALU which handles proper masking
/// for Arch32 (32-bit) and Arch64 (48-bit NaN-boxed) modes.
class BitwiseExecutor {
public:
    /// Construct with reference to VM context
    explicit BitwiseExecutor(VmContext& ctx) noexcept : ctx_{ctx} {}

    /// Execute a Type A bitwise instruction (register-register)
    ///
    /// Handles: AND, OR, XOR, NOT, SHL, SHR, SAR, ROL, ROR
    ///
    /// @param decoded Decoded Type A instruction
    /// @return StepResult indicating success or error
    [[nodiscard]] StepResult execute_type_a(const DecodedTypeA& decoded) noexcept;

    /// Execute a Type S bitwise instruction (shift-immediate)
    ///
    /// Handles: SHLI, SHRI, SARI with 6-bit immediate shift amount
    ///
    /// @param decoded Decoded Type S instruction
    /// @return StepResult indicating success or error
    [[nodiscard]] StepResult execute_type_s(const DecodedTypeS& decoded) noexcept;

    /// Execute a Type B bitwise instruction (accumulator-immediate)
    ///
    /// Handles: ANDI, ORI, XORI with zero-extended 16-bit immediate
    /// Note: These are accumulator-style (Rd = Rd OP imm16)
    ///
    /// @param decoded Decoded Type B instruction
    /// @return StepResult indicating success or error
    [[nodiscard]] StepResult execute_type_b(const DecodedTypeB& decoded) noexcept;

private:
    VmContext& ctx_;

    /// Write value to register and return success
    StepResult write_success(std::uint8_t rd, Value result) noexcept {
        ctx_.registers().write(rd, result);
        return StepResult::success();
    }
};

// ============================================================================
// Executor Interface Concepts (C++20)
// ============================================================================

/// @brief Concept for types that can execute bitwise instructions
///
/// This concept validates at compile-time that a type provides the required
/// interface for executing bitwise operations across all instruction formats.
///
/// Required operations:
/// - execute_type_a: Handle register-register bitwise ops (AND, OR, XOR, etc.)
/// - execute_type_s: Handle shift-immediate ops (SHLI, SHRI, SARI)
/// - execute_type_b: Handle accumulator-immediate ops (ANDI, ORI, XORI)
template<typename T>
concept BitwiseExecutorInterface = requires(T exec, const DecodedTypeA& da,
                                            const DecodedTypeS& ds, const DecodedTypeB& db) {
    { exec.execute_type_a(da) } -> std::same_as<StepResult>;
    { exec.execute_type_s(ds) } -> std::same_as<StepResult>;
    { exec.execute_type_b(db) } -> std::same_as<StepResult>;
};

/// @brief Verify BitwiseExecutor satisfies BitwiseExecutorInterface at compile time
static_assert(BitwiseExecutorInterface<BitwiseExecutor>,
              "BitwiseExecutor must satisfy BitwiseExecutorInterface concept");

// ============================================================================
// Main Executor
// ============================================================================

/// Main instruction executor for DotVM
///
/// The Executor handles the fetch-decode-execute cycle. It manages the
/// program counter and delegates to specialized sub-executors for different
/// opcode categories.
///
/// Usage:
/// @code
/// VmContext ctx{VmConfig::arch64()};
/// Executor exec{ctx, code_span};
/// exec.run();  // Run until halt
/// @endcode
class Executor {
public:
    /// Instruction size in bytes (all instructions are 32-bit)
    static constexpr std::uint64_t INSTRUCTION_SIZE = 4;

    /// Construct with VM context and code span
    ///
    /// @param ctx VM context with registers, memory, ALU
    /// @param code Code section span (bytecode instructions)
    explicit Executor(VmContext& ctx, std::span<const std::uint8_t> code) noexcept
        : ctx_{ctx}, code_{code}, state_{}, arith_exec_{ctx}, fp_exec_{ctx, state_},
          bitwise_exec_{ctx} {}

    // =========================================================================
    // Execution
    // =========================================================================

    /// Execute a single instruction at the current PC
    ///
    /// @return StepResult indicating success, error, or halt
    [[nodiscard]] StepResult step() noexcept;

    /// Execute until halt or error
    ///
    /// @param max_instructions Maximum instructions to execute (0 = unlimited)
    /// @return Final execution error (Success if halted normally via HALT)
    [[nodiscard]] ExecutionError run(std::uint64_t max_instructions = 0) noexcept;

    // =========================================================================
    // State Access
    // =========================================================================

    /// Get current execution state
    [[nodiscard]] const ExecutionState& state() const noexcept { return state_; }

    /// Get mutable execution state (for setting PC, resetting)
    [[nodiscard]] ExecutionState& state() noexcept { return state_; }

    /// Get VM context
    [[nodiscard]] VmContext& context() noexcept { return ctx_; }

    /// Get const VM context
    [[nodiscard]] const VmContext& context() const noexcept { return ctx_; }

private:
    VmContext& ctx_;
    std::span<const std::uint8_t> code_;
    ExecutionState state_;
    ArithmeticExecutor arith_exec_;
    FloatingPointExecutor fp_exec_;
    BitwiseExecutor bitwise_exec_;

    // -------------------------------------------------------------------------
    // Instruction Fetch and Decode
    // -------------------------------------------------------------------------

    /// Fetch 32-bit instruction at current PC (little-endian)
    [[nodiscard]] std::uint32_t fetch() const noexcept;

    /// Check if PC is valid and aligned
    [[nodiscard]] StepResult validate_pc() const noexcept;

    // -------------------------------------------------------------------------
    // Opcode Dispatch
    // -------------------------------------------------------------------------

    /// Dispatch instruction to appropriate executor
    [[nodiscard]] StepResult dispatch(std::uint32_t instr) noexcept;

    /// Dispatch arithmetic opcode (0x00-0x08)
    [[nodiscard]] StepResult dispatch_arithmetic(std::uint32_t instr,
                                                   std::uint8_t opcode) noexcept;

    /// Dispatch floating-point opcode (0x10-0x18)
    [[nodiscard]] StepResult dispatch_floating_point(std::uint32_t instr,
                                                      std::uint8_t opcode) noexcept;

    /// Dispatch bitwise opcode (0x20-0x2F)
    [[nodiscard]] StepResult dispatch_bitwise(std::uint32_t instr,
                                               std::uint8_t opcode) noexcept;

    /// Handle system opcodes (NOP, HALT, etc.)
    [[nodiscard]] StepResult dispatch_system(std::uint32_t instr,
                                              std::uint8_t opcode) noexcept;
};

}  // namespace dotvm::core
