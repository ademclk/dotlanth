#pragma once

/// @file mock_memory_manager.hpp
/// @brief Mock memory manager for testing
///
/// Provides a simplified mock implementation for testing components
/// that depend on memory operations, without actual OS allocations.

#include <cstddef>
#include <cstring>
#include <expected>
#include <map>
#include <vector>

#include <dotvm/core/memory.hpp>
#include <dotvm/core/memory_config.hpp>
#include <dotvm/core/value.hpp>

namespace dotvm::test {

/// Mock memory manager for testing purposes.
///
/// Features:
/// - In-memory allocations (no OS calls)
/// - Configurable failure injection
/// - Access logging for verification
/// - Simplified handle management
class MockMemoryManager {
public:
    using Handle = core::Handle;
    using MemoryError = core::MemoryError;

    template <typename T>
    using Result = std::expected<T, MemoryError>;

    /// Access log entry for verification
    struct AccessLog {
        enum class Type { Allocate, Deallocate, Read, Write };
        Type type;
        Handle handle;
        std::size_t offset;
        std::size_t size;
        bool success;
    };

    MockMemoryManager() = default;

    // ========== Core Operations ==========

    /// Allocate memory and return a handle
    [[nodiscard]] Result<Handle> allocate(std::size_t size) noexcept {
        // Check failure injection
        if (fail_next_allocate_) {
            fail_next_allocate_ = false;
            log({AccessLog::Type::Allocate, {0, 0}, 0, size, false});
            return std::unexpected{inject_error_};
        }

        if (size == 0 || size > core::mem_config::MAX_ALLOCATION_SIZE) {
            log({AccessLog::Type::Allocate, {0, 0}, 0, size, false});
            return std::unexpected{MemoryError::InvalidSize};
        }

        // Align to page size
        std::size_t aligned_size = ((size + 4095) / 4096) * 4096;

        // Create allocation
        std::uint32_t index = next_index_++;
        std::uint32_t generation = 1;

        allocations_[index] = Allocation{.data = std::vector<std::uint8_t>(aligned_size, 0),
                                         .generation = generation,
                                         .active = true};

        Handle h{index, generation};
        total_allocated_ += aligned_size;
        log({AccessLog::Type::Allocate, h, 0, size, true});
        return h;
    }

    /// Deallocate memory associated with a handle
    [[nodiscard]] MemoryError deallocate(Handle h) noexcept {
        auto it = allocations_.find(h.index);
        if (it == allocations_.end() || !it->second.active ||
            it->second.generation != h.generation) {
            log({AccessLog::Type::Deallocate, h, 0, 0, false});
            return MemoryError::InvalidHandle;
        }

        total_allocated_ -= it->second.data.size();
        it->second.active = false;
        it->second.generation++;  // Increment for reuse detection

        log({AccessLog::Type::Deallocate, h, 0, 0, true});
        return MemoryError::Success;
    }

    /// Check if a handle is valid
    [[nodiscard]] bool is_valid(Handle h) const noexcept {
        auto it = allocations_.find(h.index);
        return it != allocations_.end() && it->second.active &&
               it->second.generation == h.generation;
    }

    // ========== Typed Read/Write ==========

    /// Read a value from memory
    template <typename T>
    [[nodiscard]] Result<T> read(Handle h, std::size_t offset) const noexcept {
        static_assert(std::is_trivially_copyable_v<T>);

        auto it = allocations_.find(h.index);
        if (it == allocations_.end() || !it->second.active ||
            it->second.generation != h.generation) {
            return std::unexpected{MemoryError::InvalidHandle};
        }

        const auto& alloc = it->second;
        if (offset + sizeof(T) > alloc.data.size()) {
            return std::unexpected{MemoryError::BoundsViolation};
        }

        T result;
        std::memcpy(&result, alloc.data.data() + offset, sizeof(T));
        return result;
    }

    /// Write a value to memory
    template <typename T>
    [[nodiscard]] MemoryError write(Handle h, std::size_t offset, T value) noexcept {
        static_assert(std::is_trivially_copyable_v<T>);

        auto it = allocations_.find(h.index);
        if (it == allocations_.end() || !it->second.active ||
            it->second.generation != h.generation) {
            log({AccessLog::Type::Write, h, offset, sizeof(T), false});
            return MemoryError::InvalidHandle;
        }

        auto& alloc = it->second;
        if (offset + sizeof(T) > alloc.data.size()) {
            log({AccessLog::Type::Write, h, offset, sizeof(T), false});
            return MemoryError::BoundsViolation;
        }

        std::memcpy(alloc.data.data() + offset, &value, sizeof(T));
        log({AccessLog::Type::Write, h, offset, sizeof(T), true});
        return MemoryError::Success;
    }

    // ========== Bulk Operations ==========

    [[nodiscard]] MemoryError write_bytes(Handle h, std::size_t offset, const void* src,
                                          std::size_t count) noexcept {
        if (!src || count == 0)
            return MemoryError::Success;

        auto it = allocations_.find(h.index);
        if (it == allocations_.end() || !it->second.active ||
            it->second.generation != h.generation) {
            return MemoryError::InvalidHandle;
        }

        auto& alloc = it->second;
        if (offset + count > alloc.data.size()) {
            return MemoryError::BoundsViolation;
        }

        std::memcpy(alloc.data.data() + offset, src, count);
        return MemoryError::Success;
    }

    [[nodiscard]] MemoryError read_bytes(Handle h, std::size_t offset, void* dst,
                                         std::size_t count) const noexcept {
        if (!dst || count == 0)
            return MemoryError::Success;

        auto it = allocations_.find(h.index);
        if (it == allocations_.end() || !it->second.active ||
            it->second.generation != h.generation) {
            return MemoryError::InvalidHandle;
        }

        const auto& alloc = it->second;
        if (offset + count > alloc.data.size()) {
            return MemoryError::BoundsViolation;
        }

        std::memcpy(dst, alloc.data.data() + offset, count);
        return MemoryError::Success;
    }

    // ========== Query Operations ==========

    [[nodiscard]] Result<std::size_t> get_size(Handle h) const noexcept {
        auto it = allocations_.find(h.index);
        if (it == allocations_.end() || !it->second.active ||
            it->second.generation != h.generation) {
            return std::unexpected{MemoryError::InvalidHandle};
        }
        return it->second.data.size();
    }

    [[nodiscard]] std::size_t active_allocations() const noexcept {
        std::size_t count = 0;
        for (const auto& [idx, alloc] : allocations_) {
            if (alloc.active)
                ++count;
        }
        return count;
    }

    [[nodiscard]] std::size_t total_allocated_bytes() const noexcept { return total_allocated_; }

    [[nodiscard]] std::size_t max_allocation_size() const noexcept {
        return core::mem_config::MAX_ALLOCATION_SIZE;
    }

    // ========== Failure Injection ==========

    /// Make the next allocate() call fail with the given error
    void fail_next_allocate(MemoryError error = MemoryError::AllocationFailed) {
        fail_next_allocate_ = true;
        inject_error_ = error;
    }

    // ========== Test Helpers ==========

    /// Get the access log
    [[nodiscard]] const std::vector<AccessLog>& access_log() const noexcept { return access_log_; }

    /// Clear the access log
    void clear_log() noexcept { access_log_.clear(); }

    /// Reset all state
    void reset() noexcept {
        allocations_.clear();
        access_log_.clear();
        total_allocated_ = 0;
        next_index_ = 0;
        fail_next_allocate_ = false;
    }

private:
    struct Allocation {
        std::vector<std::uint8_t> data;
        std::uint32_t generation;
        bool active;
    };

    void log(AccessLog entry) const { access_log_.push_back(entry); }

    std::map<std::uint32_t, Allocation> allocations_;
    mutable std::vector<AccessLog> access_log_;
    std::size_t total_allocated_{0};
    std::uint32_t next_index_{0};
    bool fail_next_allocate_{false};
    MemoryError inject_error_{MemoryError::AllocationFailed};
};

}  // namespace dotvm::test
