#pragma once

#include <dotvm/core/instruction.hpp>
#include <dotvm/core/security_stats.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace dotvm::core::cfi {

/// Types of control flow integrity violations.
enum class CfiViolation : std::uint8_t {
    /// Attempted to execute an invalid or reserved opcode.
    InvalidOpcode = 0,

    /// Attempted to execute an opcode in the reserved range.
    ReservedOpcode = 1,

    /// Jump target is not properly aligned to instruction boundary.
    InvalidJumpTarget = 2,

    /// Too many backward jumps detected (potential infinite loop).
    BackwardJumpLimit = 3,

    /// Call stack exceeded maximum depth.
    CallStackOverflow = 4,

    /// Return instruction without matching call.
    ReturnMismatch = 5,

    /// Jump target is outside code section bounds.
    JumpOutOfBounds = 6,

    /// Indirect jump to non-validated target.
    IndirectJumpViolation = 7
};

/// Returns a string description of the CFI violation.
[[nodiscard]] constexpr const char* violation_name(CfiViolation v) noexcept {
    switch (v) {
        case CfiViolation::InvalidOpcode: return "InvalidOpcode";
        case CfiViolation::ReservedOpcode: return "ReservedOpcode";
        case CfiViolation::InvalidJumpTarget: return "InvalidJumpTarget";
        case CfiViolation::BackwardJumpLimit: return "BackwardJumpLimit";
        case CfiViolation::CallStackOverflow: return "CallStackOverflow";
        case CfiViolation::ReturnMismatch: return "ReturnMismatch";
        case CfiViolation::JumpOutOfBounds: return "JumpOutOfBounds";
        case CfiViolation::IndirectJumpViolation: return "IndirectJumpViolation";
    }
    return "Unknown";
}

/// Configuration policy for CFI enforcement.
struct CfiPolicy {
    /// Maximum number of backward jumps allowed before triggering violation.
    /// Helps detect infinite loops. Set to 0 to disable.
    std::uint32_t max_backward_jumps = 10000;

    /// Maximum call stack depth.
    std::uint32_t max_call_depth = 1024;

    /// Whether to require 4-byte alignment for jump targets.
    bool strict_jump_alignment = true;

    /// Whether to reject reserved opcodes (0x90-0x9F, 0xD0-0xEF).
    bool reject_reserved_opcodes = true;

    /// Whether to track and validate indirect jumps.
    bool validate_indirect_jumps = true;

    /// Creates a strict policy with all checks enabled.
    [[nodiscard]] static constexpr CfiPolicy strict() noexcept {
        return CfiPolicy{
            .max_backward_jumps = 5000,
            .max_call_depth = 512,
            .strict_jump_alignment = true,
            .reject_reserved_opcodes = true,
            .validate_indirect_jumps = true
        };
    }

    /// Creates a relaxed policy for debugging.
    [[nodiscard]] static constexpr CfiPolicy relaxed() noexcept {
        return CfiPolicy{
            .max_backward_jumps = 0,  // Disabled
            .max_call_depth = 4096,
            .strict_jump_alignment = false,
            .reject_reserved_opcodes = false,
            .validate_indirect_jumps = false
        };
    }

    /// Equality comparison for testing
    constexpr bool operator==(const CfiPolicy&) const noexcept = default;
};

/// Instruction alignment requirement (4 bytes).
inline constexpr std::size_t INSTRUCTION_ALIGNMENT = 4;

/// Control Flow Integrity context for tracking and validating execution.
///
/// Maintains state needed for CFI enforcement:
/// - Call stack for return validation
/// - Backward jump counter for loop detection
/// - Last violation information
///
/// Thread Safety: NOT thread-safe. Use one context per execution thread.
class CfiContext {
public:
    /// Constructs a CFI context with the specified policy.
    explicit CfiContext(CfiPolicy policy = {}) noexcept
        : policy_(policy) {
        call_stack_.reserve(policy.max_call_depth);
    }

    /// Constructs a CFI context with policy and optional security stats.
    CfiContext(CfiPolicy policy, SecurityStats* stats) noexcept
        : policy_(policy), stats_(stats) {
        call_stack_.reserve(policy.max_call_depth);
    }

    // ========== Instruction Validation ==========

    /// Validates an instruction before execution.
    ///
    /// @param pc Current program counter (byte offset in code section).
    /// @param instr The 32-bit instruction to validate.
    /// @param code_size Total size of code section in bytes.
    /// @return true if instruction is valid, false if CFI violation detected.
    [[nodiscard]] bool validate_instruction(std::uint32_t pc,
                                             std::uint32_t instr,
                                             std::size_t code_size) noexcept {
        // Check PC is within bounds
        if (pc >= code_size) {
            record_violation(CfiViolation::JumpOutOfBounds);
            return false;
        }

        // Check PC alignment
        if (policy_.strict_jump_alignment && (pc % INSTRUCTION_ALIGNMENT != 0)) {
            record_violation(CfiViolation::InvalidJumpTarget);
            return false;
        }

        // Extract opcode from instruction (bits 31:24)
        std::uint8_t opcode = static_cast<std::uint8_t>(
            (instr >> instr_bits::OPCODE_SHIFT) & instr_bits::OPCODE_MASK);

        // Check for reserved opcodes
        if (policy_.reject_reserved_opcodes && is_reserved_opcode(opcode)) {
            record_violation(CfiViolation::ReservedOpcode);
            return false;
        }

        return true;
    }

    // ========== Jump Validation ==========

    /// Validates a direct jump target.
    ///
    /// @param current_pc Current program counter.
    /// @param target_pc Target program counter after jump.
    /// @param code_size Total size of code section.
    /// @return true if jump is valid, false if CFI violation detected.
    [[nodiscard]] bool validate_jump(std::uint32_t current_pc,
                                      std::uint32_t target_pc,
                                      std::size_t code_size) noexcept {
        // Bounds check
        if (target_pc >= code_size) {
            record_violation(CfiViolation::JumpOutOfBounds);
            return false;
        }

        // Alignment check
        if (policy_.strict_jump_alignment &&
            (target_pc % INSTRUCTION_ALIGNMENT != 0)) {
            record_violation(CfiViolation::InvalidJumpTarget);
            return false;
        }

        // Backward jump detection
        if (policy_.max_backward_jumps > 0 && target_pc < current_pc) {
            ++backward_jump_count_;
            if (backward_jump_count_ > policy_.max_backward_jumps) {
                record_violation(CfiViolation::BackwardJumpLimit);
                return false;
            }
        }

        return true;
    }

    /// Validates an indirect jump target (computed at runtime).
    ///
    /// @param target_pc Target program counter.
    /// @param code_size Total size of code section.
    /// @return true if jump is valid, false if CFI violation detected.
    [[nodiscard]] bool validate_indirect_jump(std::uint32_t target_pc,
                                               std::size_t code_size) noexcept {
        if (!policy_.validate_indirect_jumps) {
            return true;  // Indirect jump validation disabled
        }

        // Bounds check
        if (target_pc >= code_size) {
            record_violation(CfiViolation::JumpOutOfBounds);
            return false;
        }

        // Alignment check
        if (policy_.strict_jump_alignment &&
            (target_pc % INSTRUCTION_ALIGNMENT != 0)) {
            record_violation(CfiViolation::IndirectJumpViolation);
            return false;
        }

        return true;
    }

    // ========== Call Stack Management ==========

    /// Records a call instruction for return validation.
    ///
    /// @param return_addr Address to return to after call completes.
    /// @return true if call recorded, false if stack overflow.
    [[nodiscard]] bool push_call(std::uint32_t return_addr) noexcept {
        if (call_stack_.size() >= policy_.max_call_depth) {
            record_violation(CfiViolation::CallStackOverflow);
            return false;
        }
        call_stack_.push_back(return_addr);
        return true;
    }

    /// Validates and pops a return instruction.
    ///
    /// @return The expected return address, or nullopt if stack is empty.
    [[nodiscard]] std::optional<std::uint32_t> pop_call() noexcept {
        if (call_stack_.empty()) {
            record_violation(CfiViolation::ReturnMismatch);
            return std::nullopt;
        }
        std::uint32_t addr = call_stack_.back();
        call_stack_.pop_back();
        return addr;
    }

    /// Returns current call stack depth.
    [[nodiscard]] std::size_t call_depth() const noexcept {
        return call_stack_.size();
    }

    // ========== State Query ==========

    /// Returns the last CFI violation, if any.
    [[nodiscard]] std::optional<CfiViolation> last_violation() const noexcept {
        return last_violation_;
    }

    /// Returns true if any CFI violation has been recorded.
    [[nodiscard]] bool has_violation() const noexcept {
        return last_violation_.has_value();
    }

    /// Returns the backward jump count.
    [[nodiscard]] std::uint32_t backward_jump_count() const noexcept {
        return backward_jump_count_;
    }

    /// Returns the current policy.
    [[nodiscard]] const CfiPolicy& policy() const noexcept {
        return policy_;
    }

    // ========== Reset ==========

    /// Resets the CFI context for a new execution.
    void reset() noexcept {
        call_stack_.clear();
        backward_jump_count_ = 0;
        last_violation_.reset();
    }

private:
    CfiPolicy policy_;
    SecurityStats* stats_{nullptr};
    std::vector<std::uint32_t> call_stack_;
    std::uint32_t backward_jump_count_{0};
    std::optional<CfiViolation> last_violation_;

    /// Records a CFI violation.
    void record_violation(CfiViolation v) noexcept {
        last_violation_ = v;
        if (stats_) {
            stats_->record_cfi_violation();
        }
    }

    /// Checks if an opcode is in the reserved range.
    [[nodiscard]] static constexpr bool is_reserved_opcode(std::uint8_t op) noexcept {
        // Reserved ranges: 0x90-0x9F, 0xD0-0xEF
        return (op >= 0x90 && op <= 0x9F) || (op >= 0xD0 && op <= 0xEF);
    }
};

} // namespace dotvm::core::cfi
