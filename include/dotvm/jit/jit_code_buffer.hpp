/// @file jit_code_buffer.hpp
/// @brief Executable memory management for JIT compilation
///
/// Provides safe allocation and management of executable memory regions
/// using platform-specific APIs (mmap/mprotect on POSIX, VirtualAlloc on Windows).
/// Memory is allocated as read-write for compilation, then made executable.

#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <span>

#include "jit_config.hpp"

namespace dotvm::jit {

/// @brief Error type for code buffer operations
enum class CodeBufferError : std::uint8_t {
    /// @brief Memory allocation failed
    AllocationFailed,

    /// @brief Memory protection change failed
    ProtectionFailed,

    /// @brief Requested size exceeds available space
    InsufficientSpace,

    /// @brief Buffer is not in writable state
    NotWritable,

    /// @brief Buffer is not in executable state
    NotExecutable,

    /// @brief Invalid parameters provided
    InvalidParameters,
};

/// @brief Convert error to string for debugging
[[nodiscard]] constexpr const char* code_buffer_error_string(CodeBufferError err) noexcept {
    switch (err) {
        case CodeBufferError::AllocationFailed:
            return "AllocationFailed";
        case CodeBufferError::ProtectionFailed:
            return "ProtectionFailed";
        case CodeBufferError::InsufficientSpace:
            return "InsufficientSpace";
        case CodeBufferError::NotWritable:
            return "NotWritable";
        case CodeBufferError::NotExecutable:
            return "NotExecutable";
        case CodeBufferError::InvalidParameters:
            return "InvalidParameters";
    }
    return "Unknown";
}

/// @brief Result type for code buffer operations
template <typename T>
using CodeBufferResult = std::expected<T, CodeBufferError>;

/// @brief Memory protection states
enum class MemoryProtection : std::uint8_t {
    /// @brief Memory is readable and writable (for compilation)
    ReadWrite,

    /// @brief Memory is readable and executable (for execution)
    ReadExecute,

    /// @brief Memory has no permissions (guard page)
    None,
};

/// @brief Manages a contiguous region of executable memory
///
/// Allocates memory using mmap (or VirtualAlloc on Windows) and provides
/// safe transitions between writable and executable states. Memory starts
/// as read-write for code generation, then is made executable.
///
/// @note Thread-safety: The buffer itself is not thread-safe. Use external
///       synchronization if sharing between threads.
///
/// @example
/// ```cpp
/// // Create a 64KB code buffer
/// auto buffer_result = JitCodeBuffer::create(64 * 1024);
/// if (!buffer_result) {
///     // Handle allocation failure
/// }
/// auto buffer = std::move(*buffer_result);
///
/// // Write code to buffer (starts in read-write mode)
/// auto region_result = buffer.allocate(128);
/// if (region_result) {
///     std::memcpy(region_result->data(), code, code_size);
/// }
///
/// // Make executable before calling
/// buffer.make_executable();
/// auto fn = reinterpret_cast<void(*)()>(region_result->data());
/// fn();
/// ```
class JitCodeBuffer {
public:
    /// @brief Create a new code buffer with the specified size
    ///
    /// @param size Size in bytes (will be rounded up to page size)
    /// @return Code buffer on success, error on failure
    [[nodiscard]] static CodeBufferResult<JitCodeBuffer> create(std::size_t size);

    /// @brief Destructor - unmaps memory
    ~JitCodeBuffer();

    // Non-copyable
    JitCodeBuffer(const JitCodeBuffer&) = delete;
    JitCodeBuffer& operator=(const JitCodeBuffer&) = delete;

    // Movable
    JitCodeBuffer(JitCodeBuffer&& other) noexcept;
    JitCodeBuffer& operator=(JitCodeBuffer&& other) noexcept;

    /// @brief Allocate a region within the buffer
    ///
    /// @param size Number of bytes to allocate
    /// @param alignment Alignment requirement (default 16 bytes for SIMD)
    /// @return Span to allocated region, or error if insufficient space
    [[nodiscard]] CodeBufferResult<std::span<std::uint8_t>>
    allocate(std::size_t size, std::size_t alignment = 16) noexcept;

    /// @brief Make the entire buffer executable
    ///
    /// Changes memory protection from read-write to read-execute.
    /// No more allocations are allowed after this call.
    ///
    /// @return Success or protection error
    [[nodiscard]] CodeBufferResult<void> make_executable() noexcept;

    /// @brief Make the entire buffer writable again
    ///
    /// Changes memory protection back to read-write.
    /// Useful for patching or adding more code.
    ///
    /// @return Success or protection error
    [[nodiscard]] CodeBufferResult<void> make_writable() noexcept;

    /// @brief Get the current memory protection state
    [[nodiscard]] MemoryProtection protection() const noexcept { return protection_; }

    /// @brief Check if buffer is currently writable
    [[nodiscard]] bool is_writable() const noexcept {
        return protection_ == MemoryProtection::ReadWrite;
    }

    /// @brief Check if buffer is currently executable
    [[nodiscard]] bool is_executable() const noexcept {
        return protection_ == MemoryProtection::ReadExecute;
    }

    /// @brief Get pointer to start of buffer
    [[nodiscard]] std::uint8_t* data() noexcept { return base_; }

    /// @brief Get const pointer to start of buffer
    [[nodiscard]] const std::uint8_t* data() const noexcept { return base_; }

    /// @brief Get total size of buffer
    [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }

    /// @brief Get number of bytes currently allocated
    [[nodiscard]] std::size_t used() const noexcept { return used_; }

    /// @brief Get number of bytes remaining
    [[nodiscard]] std::size_t available() const noexcept { return capacity_ - used_; }

    /// @brief Check if buffer has space for the given size
    [[nodiscard]] bool has_space(std::size_t size) const noexcept { return available() >= size; }

    /// @brief Check if buffer is empty
    [[nodiscard]] bool empty() const noexcept { return used_ == 0; }

    /// @brief Get the system page size
    [[nodiscard]] static std::size_t page_size() noexcept;

    /// @brief Reset the buffer, discarding all allocations
    ///
    /// Does not deallocate the underlying memory, just resets the
    /// allocation pointer. Buffer must be in writable state.
    ///
    /// @return Success or error if not writable
    [[nodiscard]] CodeBufferResult<void> reset() noexcept;

private:
    /// @brief Private constructor - use create() factory
    JitCodeBuffer(std::uint8_t* base, std::size_t capacity) noexcept;

    /// @brief Base address of memory region
    std::uint8_t* base_ = nullptr;

    /// @brief Total capacity in bytes
    std::size_t capacity_ = 0;

    /// @brief Current allocation offset
    std::size_t used_ = 0;

    /// @brief Current memory protection
    MemoryProtection protection_ = MemoryProtection::ReadWrite;
};

/// @brief RAII guard for temporarily making buffer writable
///
/// Makes buffer writable on construction, restores to executable on destruction.
/// Useful for patching compiled code.
class WritableGuard {
public:
    /// @brief Create guard, making buffer writable
    explicit WritableGuard(JitCodeBuffer& buffer);

    /// @brief Restore buffer to executable state
    ~WritableGuard();

    // Non-copyable, non-movable
    WritableGuard(const WritableGuard&) = delete;
    WritableGuard& operator=(const WritableGuard&) = delete;
    WritableGuard(WritableGuard&&) = delete;
    WritableGuard& operator=(WritableGuard&&) = delete;

    /// @brief Check if guard successfully made buffer writable
    [[nodiscard]] bool success() const noexcept { return success_; }

private:
    JitCodeBuffer& buffer_;
    MemoryProtection original_;
    bool success_;
};

}  // namespace dotvm::jit
