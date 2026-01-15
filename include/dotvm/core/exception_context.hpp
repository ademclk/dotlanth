#pragma once

/// @file exception_context.hpp
/// @brief Exception frame stack and context for DotVM exception handling (EXEC-011)
///
/// This header provides the ExceptionFrame structure and ExceptionContext class
/// for managing exception handlers during VM execution. The design follows the
/// CallStack pattern from EXEC-007.

#include "exception_types.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace dotvm::core {

// ============================================================================
// Exception Frame
// ============================================================================

/// Exception handler frame pushed by TRY instruction
///
/// Stores all state needed to transfer control to an exception handler:
/// - Program counter of the CATCH handler
/// - TRY instruction location (for diagnostics)
/// - Call stack depth for unwinding
/// - Bitmask of catchable exception types
///
/// Memory layout is optimized for 32-byte alignment.
struct ExceptionFrame {
    /// Program counter of the CATCH handler
    ///
    /// This is the instruction index (not byte offset) where execution
    /// should transfer when a matching exception is thrown.
    std::size_t handler_pc{0};

    /// Program counter of the TRY instruction
    ///
    /// Stored for debugging and diagnostics. Allows stack traces to
    /// show where the try block began.
    std::size_t try_pc{0};

    /// Call stack depth when TRY was executed
    ///
    /// During stack unwinding, CallStack frames are popped until
    /// the depth returns to this value, restoring the call context
    /// that existed when TRY executed.
    std::size_t stack_depth{0};

    /// Bitmask of exception types this handler catches
    ///
    /// Uses catch_mask constants (ALL, DIVZERO, BOUNDS, etc.)
    /// to selectively catch specific exception types.
    std::uint8_t catch_types{catch_mask::ALL};

    /// Reserved for future use (alignment padding)
    std::uint8_t _reserved[7]{};

    // ========================================================================
    // Matching
    // ========================================================================

    /// Check if this handler catches the given exception type
    ///
    /// @param code The exception type being thrown
    /// @return true if this handler should catch the exception
    [[nodiscard]] bool catches(ErrorCode code) const noexcept {
        return catch_mask_matches(catch_types, code);
    }

    // ========================================================================
    // Factory Methods
    // ========================================================================

    /// Create an exception frame
    ///
    /// @param handler Handler PC (CATCH instruction location)
    /// @param try_loc TRY instruction PC
    /// @param depth Current call stack depth
    /// @param types Catch type mask (default ALL)
    /// @return ExceptionFrame instance
    [[nodiscard]] static ExceptionFrame make(
        std::size_t handler,
        std::size_t try_loc,
        std::size_t depth,
        std::uint8_t types = catch_mask::ALL) noexcept {

        ExceptionFrame frame;
        frame.handler_pc = handler;
        frame.try_pc = try_loc;
        frame.stack_depth = depth;
        frame.catch_types = types;
        return frame;
    }

    /// Default equality comparison
    [[nodiscard]] bool operator==(const ExceptionFrame&) const noexcept = default;
};

// Verify ExceptionFrame size for memory estimation
// Size: 8 (handler_pc) + 8 (try_pc) + 8 (stack_depth) + 1 (catch_types) + 7 (reserved) = 32 bytes
static_assert(sizeof(ExceptionFrame) == 32,
              "ExceptionFrame should be 32 bytes for cache alignment");

// ============================================================================
// Exception Context Constants
// ============================================================================

/// Default maximum exception frame stack depth
///
/// Smaller than call stack depth since deeply nested exception handlers
/// are unusual. A depth of 256 should be sufficient for most programs.
inline constexpr std::size_t DEFAULT_MAX_EXCEPTION_DEPTH = 256;

// ============================================================================
// Exception Context
// ============================================================================

/// Exception handling context for managing TRY/CATCH frames
///
/// Maintains a stack of exception frames pushed by TRY instructions and
/// the current exception state. When THROW executes, the context is
/// searched for a matching handler and used for stack unwinding.
///
/// Thread Safety: NOT thread-safe. Use one ExceptionContext per execution.
///
/// @note This works alongside CallStack (EXEC-007). During unwinding,
///       both stacks are popped to restore proper execution state.
class ExceptionContext {
public:
    /// Constructs an exception context with the specified maximum depth
    ///
    /// @param max_depth Maximum number of frames allowed (default 256)
    explicit ExceptionContext(std::size_t max_depth = DEFAULT_MAX_EXCEPTION_DEPTH) noexcept
        : max_depth_{max_depth} {
        // Pre-allocate some capacity to reduce early reallocations
        frames_.reserve(max_depth > 64 ? 64 : max_depth);
    }

    // =========================================================================
    // Frame Operations
    // =========================================================================

    /// Push an exception frame onto the stack
    ///
    /// Called when TRY instruction executes. Saves the handler location
    /// and current call stack depth for later unwinding.
    ///
    /// @param frame The exception frame to push
    /// @return true if push succeeded, false if stack overflow
    [[nodiscard]] bool push_frame(const ExceptionFrame& frame) noexcept {
        if (frames_.size() >= max_depth_) {
            return false;  // Stack overflow
        }
        frames_.push_back(frame);
        return true;
    }

    /// Pop the top exception frame from the stack
    ///
    /// Called when ENDTRY executes (normal exit) or during unwinding.
    ///
    /// @return The popped frame, or std::nullopt if stack is empty
    [[nodiscard]] std::optional<ExceptionFrame> pop_frame() noexcept {
        if (frames_.empty()) {
            return std::nullopt;
        }
        ExceptionFrame frame = frames_.back();
        frames_.pop_back();
        return frame;
    }

    /// Peek at the top frame without removing it
    ///
    /// @return Pointer to the top frame, or nullptr if stack is empty
    [[nodiscard]] const ExceptionFrame* top() const noexcept {
        return frames_.empty() ? nullptr : &frames_.back();
    }

    /// Find a handler for the given exception type
    ///
    /// Searches the frame stack from top to bottom for a handler that
    /// catches the specified exception type.
    ///
    /// @param type The exception type being thrown
    /// @return Pointer to the matching frame, or nullptr if none found
    [[nodiscard]] const ExceptionFrame* find_handler(ErrorCode type) const noexcept {
        // Search from top (most recent) to bottom (oldest)
        for (auto it = frames_.rbegin(); it != frames_.rend(); ++it) {
            if (it->catches(type)) {
                return &(*it);
            }
        }
        return nullptr;
    }

    /// Find handler and return its index in the stack
    ///
    /// @param type The exception type being thrown
    /// @return Index of matching frame, or std::nullopt if none found
    [[nodiscard]] std::optional<std::size_t> find_handler_index(ErrorCode type) const noexcept {
        // Search from top (most recent) to bottom (oldest)
        for (std::size_t i = frames_.size(); i > 0; --i) {
            if (frames_[i - 1].catches(type)) {
                return i - 1;
            }
        }
        return std::nullopt;
    }

    /// Pop frames until reaching the specified depth
    ///
    /// Used during exception handling to unwind to the handler's frame.
    ///
    /// @param target_depth Stop when frames_.size() equals this value
    void unwind_to(std::size_t target_depth) noexcept {
        while (frames_.size() > target_depth) {
            frames_.pop_back();
        }
    }

    // =========================================================================
    // Exception State
    // =========================================================================

    /// Set the current exception
    ///
    /// Called when THROW executes. Stores the exception for handler dispatch.
    ///
    /// @param exc The exception being thrown
    void set_exception(Exception exc) noexcept {
        current_ = std::move(exc);
    }

    /// Get the current exception
    ///
    /// @return Reference to the current exception state
    [[nodiscard]] const Exception& current_exception() const noexcept {
        return current_;
    }

    /// Get mutable reference to current exception
    ///
    /// @return Mutable reference to the current exception state
    [[nodiscard]] Exception& current_exception() noexcept {
        return current_;
    }

    /// Check if an exception is pending
    ///
    /// @return true if current exception is active
    [[nodiscard]] bool has_pending_exception() const noexcept {
        return current_.is_active();
    }

    /// Clear the current exception
    ///
    /// Called when an exception is caught and handled.
    void clear_exception() noexcept {
        current_.clear();
    }

    // =========================================================================
    // Stack Information
    // =========================================================================

    /// Get current frame stack depth
    ///
    /// @return Number of exception frames on the stack
    [[nodiscard]] std::size_t depth() const noexcept {
        return frames_.size();
    }

    /// Check if frame stack is empty
    ///
    /// @return true if no exception frames are active
    [[nodiscard]] bool empty() const noexcept {
        return frames_.empty();
    }

    /// Get maximum allowed depth
    ///
    /// @return Maximum number of frames before overflow
    [[nodiscard]] std::size_t max_depth() const noexcept {
        return max_depth_;
    }

    /// Check if stack would overflow with one more push
    ///
    /// @return true if stack is at maximum capacity
    [[nodiscard]] bool would_overflow() const noexcept {
        return frames_.size() >= max_depth_;
    }

    /// Access frame at index (for debugging/testing)
    ///
    /// @param index Frame index (0 = bottom, depth()-1 = top)
    /// @return Pointer to frame, or nullptr if out of bounds
    [[nodiscard]] const ExceptionFrame* frame_at(std::size_t index) const noexcept {
        return index < frames_.size() ? &frames_[index] : nullptr;
    }

    // =========================================================================
    // State Management
    // =========================================================================

    /// Clear all frames and exception state
    ///
    /// Resets the context to initial state. Does not change max_depth.
    void clear() noexcept {
        frames_.clear();
        current_.clear();
    }

    /// Set a new maximum depth
    ///
    /// @param new_max New maximum depth
    /// @note Does not truncate if current depth exceeds new_max
    void set_max_depth(std::size_t new_max) noexcept {
        max_depth_ = new_max;
    }

private:
    std::vector<ExceptionFrame> frames_;
    std::size_t max_depth_;
    Exception current_;
};

}  // namespace dotvm::core
