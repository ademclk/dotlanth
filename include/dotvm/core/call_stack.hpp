#pragma once

/// @file call_stack.hpp
/// @brief Call stack and frame management for EXEC-007
///
/// This header provides the CallFrame structure and CallStack class for
/// managing function call frames during VM execution. CallFrame stores
/// the return address, callee-saved registers (R16-R31), and local
/// register information for proper stack unwinding.

#include "value.hpp"
#include "register_conventions.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace dotvm::core {

// ============================================================================
// Call Frame
// ============================================================================

/// Call frame representing a function activation record
///
/// Stores all state needed to restore execution after a function returns:
/// - Return program counter (where to resume execution)
/// - Base register index for local registers
/// - Saved callee-saved registers (R16-R31)
/// - Local register count for clearing on return
///
/// Memory layout is optimized for cache efficiency with the most
/// frequently accessed fields at the beginning.
struct CallFrame {
    /// Return address (program counter to return to after RET)
    ///
    /// This is the instruction index (not byte offset) where execution
    /// should resume after the called function returns.
    std::size_t return_pc{0};

    /// Base register index for local registers
    ///
    /// Reserved for future register windowing support. Currently unused
    /// but included for forward compatibility.
    std::uint8_t base_reg{0};

    /// Number of local registers to clear on return
    ///
    /// When the frame is popped, registers from base_reg to
    /// base_reg + local_count - 1 are cleared to zero.
    std::uint8_t local_count{0};

    /// Padding for alignment
    std::uint8_t _reserved[6]{};

    /// Saved callee-saved registers (R16-R31)
    ///
    /// These 16 registers are preserved across function calls per the
    /// DotVM calling convention. They are saved when CALL executes and
    /// restored when RET executes.
    std::array<Value, reg_range::CALLEE_SAVED_COUNT> saved_regs{};

    /// Default equality comparison
    constexpr bool operator==(const CallFrame&) const noexcept = default;
};

// Verify CallFrame size for memory estimation
// Size: 8 (return_pc) + 1 (base_reg) + 1 (local_count) + 6 (padding) + 128 (saved_regs) = 144 bytes
static_assert(sizeof(CallFrame) == 144,
              "CallFrame should be 144 bytes for predictable memory usage");

// ============================================================================
// Call Stack Constants
// ============================================================================

/// Default maximum call stack depth
///
/// This matches the CFI policy default for consistency. A depth of 1024
/// allows reasonably deep recursion while preventing stack overflow attacks.
inline constexpr std::size_t DEFAULT_MAX_CALL_DEPTH = 1024;

// ============================================================================
// Call Stack
// ============================================================================

/// Call stack for managing function call frames
///
/// Provides push/pop operations with overflow protection. Each CALL
/// instruction pushes a frame containing the return address and saved
/// callee-saved registers. Each RET instruction pops a frame and
/// restores the registers.
///
/// The stack has a configurable maximum depth (default 1024) to prevent
/// stack overflow attacks from unbounded recursion.
///
/// Thread Safety: NOT thread-safe. Use one CallStack per execution thread.
///
/// @note This complements (does not replace) the CFI call stack in
///       cfi::CfiContext, which tracks return addresses for integrity
///       checking. CallStack provides full frame management including
///       register preservation.
class CallStack {
public:
    /// Constructs a call stack with the specified maximum depth
    ///
    /// @param max_depth Maximum number of frames allowed (default 1024)
    explicit CallStack(std::size_t max_depth = DEFAULT_MAX_CALL_DEPTH) noexcept
        : max_depth_{max_depth} {
        // Pre-allocate some capacity to reduce early reallocations
        // Use smaller of 256 or max_depth to avoid over-allocation
        frames_.reserve(max_depth > 256 ? 256 : max_depth);
    }

    // =========================================================================
    // Stack Operations
    // =========================================================================

    /// Push a new call frame onto the stack
    ///
    /// Saves the return address and callee-saved registers for later
    /// restoration when the function returns.
    ///
    /// @param return_pc Return address (program counter after CALL)
    /// @param saved_regs Span of callee-saved registers (R16-R31)
    /// @param base_reg Base register for locals (default 0, reserved)
    /// @param local_count Number of local registers (default 0)
    /// @return true if push succeeded, false if stack overflow
    [[nodiscard]] bool push(
        std::size_t return_pc,
        std::span<const Value, reg_range::CALLEE_SAVED_COUNT> saved_regs,
        std::uint8_t base_reg = 0,
        std::uint8_t local_count = 0) noexcept {

        if (frames_.size() >= max_depth_) {
            return false;  // Stack overflow
        }

        CallFrame frame{};
        frame.return_pc = return_pc;
        frame.base_reg = base_reg;
        frame.local_count = local_count;
        std::copy(saved_regs.begin(), saved_regs.end(),
                  frame.saved_regs.begin());

        frames_.push_back(std::move(frame));
        return true;
    }

    /// Pop the top call frame from the stack
    ///
    /// Removes and returns the most recent frame for register restoration
    /// and control flow transfer.
    ///
    /// @return The popped frame, or std::nullopt if stack is empty
    [[nodiscard]] std::optional<CallFrame> pop() noexcept {
        if (frames_.empty()) {
            return std::nullopt;
        }

        CallFrame frame = std::move(frames_.back());
        frames_.pop_back();
        return frame;
    }

    /// Peek at the top frame without removing it
    ///
    /// Useful for debugging or inspecting the current call context.
    ///
    /// @return Pointer to the top frame, or nullptr if stack is empty
    [[nodiscard]] const CallFrame* top() const noexcept {
        return frames_.empty() ? nullptr : &frames_.back();
    }

    // =========================================================================
    // Stack Information
    // =========================================================================

    /// Get current stack depth
    ///
    /// @return Number of frames currently on the stack
    [[nodiscard]] std::size_t depth() const noexcept {
        return frames_.size();
    }

    /// Check if stack is empty
    ///
    /// @return true if no frames are on the stack
    [[nodiscard]] bool empty() const noexcept {
        return frames_.empty();
    }

    /// Get maximum allowed depth
    ///
    /// @return Maximum number of frames allowed before overflow
    [[nodiscard]] std::size_t max_depth() const noexcept {
        return max_depth_;
    }

    /// Check if stack would overflow with one more push
    ///
    /// @return true if stack is at maximum capacity
    [[nodiscard]] bool would_overflow() const noexcept {
        return frames_.size() >= max_depth_;
    }

    // =========================================================================
    // State Management
    // =========================================================================

    /// Clear all frames from the stack
    ///
    /// Resets the stack to empty state. Does not change max_depth.
    void clear() noexcept {
        frames_.clear();
    }

    /// Set a new maximum depth
    ///
    /// @param new_max New maximum depth
    /// @note Does not truncate if current depth exceeds new_max
    void set_max_depth(std::size_t new_max) noexcept {
        max_depth_ = new_max;
    }

private:
    std::vector<CallFrame> frames_;
    std::size_t max_depth_;
};

}  // namespace dotvm::core
