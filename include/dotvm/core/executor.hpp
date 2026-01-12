#pragma once

/// @file executor.hpp
/// @brief Instruction executor for DotVM
///
/// This header provides the Executor class which handles instruction
/// fetching, decoding, and dispatch. It includes specialized executors
/// for different opcode categories (arithmetic, bitwise, control flow, etc.)

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
        : ctx_{ctx}, code_{code}, arith_exec_{ctx} {}

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

    /// Dispatch arithmetic opcode (0x00-0x1F)
    [[nodiscard]] StepResult dispatch_arithmetic(std::uint32_t instr,
                                                   std::uint8_t opcode) noexcept;

    /// Handle system opcodes (NOP, HALT, etc.)
    [[nodiscard]] StepResult dispatch_system(std::uint32_t instr,
                                              std::uint8_t opcode) noexcept;
};

}  // namespace dotvm::core
