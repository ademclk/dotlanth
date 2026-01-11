#pragma once

#include <dotvm/core/memory_config.hpp>
#include <dotvm/core/value.hpp>

#include <cstring>
#include <type_traits>
#include <utility>
#include <vector>

namespace dotvm::core {

/// Entry in the handle table tracking a single allocation.
struct HandleEntry {
    void* ptr;                  ///< Pointer to allocated memory (nullptr if inactive).
    std::size_t size;           ///< Allocated size in bytes (page-aligned).
    std::uint32_t generation;   ///< Generation counter for use-after-free detection.
    bool is_active;             ///< Whether this entry is currently in use.

    constexpr bool operator==(const HandleEntry&) const noexcept = default;

    /// Creates an active entry with the given allocation.
    [[nodiscard]] static HandleEntry create(void* p, std::size_t s,
                                            std::uint32_t gen) noexcept {
        return HandleEntry{
            .ptr = p,
            .size = s,
            .generation = gen,
            .is_active = true
        };
    }

    /// Creates an inactive entry (for free list slots).
    [[nodiscard]] static constexpr HandleEntry inactive(std::uint32_t gen) noexcept {
        return HandleEntry{
            .ptr = nullptr,
            .size = 0,
            .generation = gen,
            .is_active = false
        };
    }
};

static_assert(sizeof(HandleEntry) <= 32, "HandleEntry should fit in half a cache line");

/// Table managing handle entries with a free list for efficient slot reuse.
class HandleTable {
public:
    /// Constructs an empty handle table.
    HandleTable() noexcept = default;

    /// Constructs a handle table with reserved capacity.
    explicit HandleTable(std::size_t initial_capacity) {
        entries_.reserve(initial_capacity);
        free_list_.reserve(initial_capacity);
    }

    // Non-copyable, movable
    HandleTable(const HandleTable&) = delete;
    HandleTable& operator=(const HandleTable&) = delete;
    HandleTable(HandleTable&&) noexcept = default;
    HandleTable& operator=(HandleTable&&) noexcept = default;

    /// Allocates a slot and returns its index.
    /// @return Index of the allocated slot, or INVALID_INDEX if table is full.
    [[nodiscard]] std::uint32_t allocate_slot() noexcept {
        if (!free_list_.empty()) {
            // Reuse a free slot
            std::uint32_t index = free_list_.back();
            free_list_.pop_back();
            // Generation was already incremented when slot was released
            entries_[index].is_active = true;
            return index;
        }

        // Need to allocate a new slot
        if (entries_.size() >= mem_config::MAX_TABLE_SIZE) {
            return mem_config::INVALID_INDEX;
        }

        std::uint32_t index = static_cast<std::uint32_t>(entries_.size());
        entries_.push_back(HandleEntry::inactive(mem_config::INITIAL_GENERATION));
        entries_[index].is_active = true;
        return index;
    }

    /// Releases a slot back to the free list.
    /// @param index The slot index to release.
    void release_slot(std::uint32_t index) noexcept {
        if (index >= entries_.size()) return;

        auto& entry = entries_[index];
        if (!entry.is_active) return;

        entry.ptr = nullptr;
        entry.size = 0;
        entry.is_active = false;

        // Increment generation (with wrap-around)
        entry.generation = (entry.generation < mem_config::MAX_GENERATION)
                           ? entry.generation + 1
                           : mem_config::INITIAL_GENERATION;

        free_list_.push_back(index);
    }

    /// Access entry by index (no bounds checking).
    [[nodiscard]] HandleEntry& operator[](std::uint32_t index) noexcept {
        return entries_[index];
    }

    /// Access entry by index (const, no bounds checking).
    [[nodiscard]] const HandleEntry& operator[](std::uint32_t index) const noexcept {
        return entries_[index];
    }

    /// Checks if an index is within bounds.
    [[nodiscard]] bool is_valid_index(std::uint32_t index) const noexcept {
        return index < entries_.size();
    }

    /// Validates a handle (index in bounds, active, generation matches).
    [[nodiscard]] bool is_valid_handle(Handle h) const noexcept {
        if (h.index >= entries_.size()) return false;
        const auto& entry = entries_[h.index];
        return entry.is_active && entry.generation == h.generation;
    }

    /// Returns the number of allocated slots (active + inactive).
    [[nodiscard]] std::size_t capacity() const noexcept {
        return entries_.size();
    }

    /// Returns the number of free slots.
    [[nodiscard]] std::size_t free_count() const noexcept {
        return free_list_.size();
    }

    /// Returns the number of active entries.
    [[nodiscard]] std::size_t active_count() const noexcept {
        return entries_.size() - free_list_.size();
    }

private:
    std::vector<HandleEntry> entries_;
    std::vector<std::uint32_t> free_list_;
};

/// Error codes for memory operations.
enum class MemoryError : std::uint8_t {
    Success = 0,        ///< Operation completed successfully.
    InvalidSize,        ///< Size is 0 or exceeds MAX_ALLOCATION_SIZE.
    AllocationFailed,   ///< OS allocation failed (out of memory).
    InvalidHandle,      ///< Handle not found or generation mismatch.
    BoundsViolation,    ///< Read/write offset + size exceeds allocation.
    HandleTableFull     ///< Cannot allocate more handles.
};

/// Memory manager providing safe, generation-based memory allocation.
///
/// Features:
/// - 4KB page granularity (sizes rounded up, addresses aligned)
/// - Generation counters prevent use-after-free
/// - Bounds checking on all read/write operations
/// - Integrates with NaN-boxed Value type via Handle
///
/// Thread Safety: NOT thread-safe. Use one MemoryManager per thread.
class MemoryManager {
public:
    /// Result type for operations that can fail.
    template<typename T>
    using Result = std::pair<T, MemoryError>;

    /// Constructs a memory manager with default limits.
    MemoryManager() noexcept
        : table_(mem_config::INITIAL_TABLE_CAPACITY) {}

    /// Constructs a memory manager with custom max allocation size.
    explicit MemoryManager(std::size_t max_allocation_size) noexcept
        : table_(mem_config::INITIAL_TABLE_CAPACITY),
          max_allocation_size_(max_allocation_size) {}

    // Non-copyable, movable
    MemoryManager(const MemoryManager&) = delete;
    MemoryManager& operator=(const MemoryManager&) = delete;
    MemoryManager(MemoryManager&&) noexcept = default;
    MemoryManager& operator=(MemoryManager&&) noexcept = default;

    /// Destructor frees all active allocations.
    ~MemoryManager();

    // ========== Core API ==========

    /// Allocates memory and returns a handle.
    /// Size is rounded UP to 4KB boundary.
    /// @param size Requested size in bytes (must be > 0 and <= max_allocation_size).
    /// @return Handle and error code.
    [[nodiscard]] Result<Handle> allocate(std::size_t size) noexcept;

    /// Frees memory associated with a handle.
    /// Increments generation counter to invalidate existing handles.
    /// @param h The handle to deallocate.
    /// @return Error code.
    [[nodiscard]] MemoryError deallocate(Handle h) noexcept;

    /// Gets the raw pointer for a handle (validated).
    /// @param h The handle.
    /// @return Pointer and error code.
    [[nodiscard]] Result<void*> get_ptr(Handle h) noexcept;

    /// Gets the raw pointer for a handle (const version).
    [[nodiscard]] Result<const void*> get_ptr(Handle h) const noexcept;

    // ========== Typed Read/Write ==========

    /// Reads a value at the given offset (bounds-checked).
    /// @tparam T Type to read (must be trivially copyable).
    /// @param h The handle.
    /// @param offset Byte offset within the allocation.
    /// @return Value and error code.
    template<typename T>
    [[nodiscard]] Result<T> read(Handle h, std::size_t offset) const noexcept {
        static_assert(std::is_trivially_copyable_v<T>,
                      "read<T> requires trivially copyable type");

        auto err = validate_bounds(h, offset, sizeof(T));
        if (err != MemoryError::Success) {
            return {T{}, err};
        }

        auto [ptr, ptr_err] = get_ptr(h);
        if (ptr_err != MemoryError::Success) {
            return {T{}, ptr_err};
        }

        T result;
        std::memcpy(&result, static_cast<const char*>(ptr) + offset, sizeof(T));
        return {result, MemoryError::Success};
    }

    /// Writes a value at the given offset (bounds-checked).
    /// @tparam T Type to write (must be trivially copyable).
    /// @param h The handle.
    /// @param offset Byte offset within the allocation.
    /// @param value The value to write.
    /// @return Error code.
    template<typename T>
    [[nodiscard]] MemoryError write(Handle h, std::size_t offset, T value) noexcept {
        static_assert(std::is_trivially_copyable_v<T>,
                      "write<T> requires trivially copyable type");

        auto err = validate_bounds(h, offset, sizeof(T));
        if (err != MemoryError::Success) {
            return err;
        }

        auto [ptr, ptr_err] = get_ptr(h);
        if (ptr_err != MemoryError::Success) {
            return ptr_err;
        }

        std::memcpy(static_cast<char*>(ptr) + offset, &value, sizeof(T));
        return MemoryError::Success;
    }

    // ========== Bulk Operations ==========

    /// Copies data into an allocation.
    /// @param h The handle.
    /// @param offset Byte offset within the allocation.
    /// @param src Source buffer.
    /// @param count Number of bytes to copy.
    /// @return Error code.
    [[nodiscard]] MemoryError write_bytes(Handle h, std::size_t offset,
                                          const void* src, std::size_t count) noexcept;

    /// Copies data from an allocation.
    /// @param h The handle.
    /// @param offset Byte offset within the allocation.
    /// @param dst Destination buffer.
    /// @param count Number of bytes to copy.
    /// @return Error code.
    MemoryError read_bytes(Handle h, std::size_t offset,
                           void* dst, std::size_t count) const noexcept;

    // ========== Query Operations ==========

    /// Gets the allocated size for a handle (page-aligned size, not requested).
    [[nodiscard]] Result<std::size_t> get_size(Handle h) const noexcept;

    /// Checks if a handle is valid without retrieving pointer.
    [[nodiscard]] bool is_valid(Handle h) const noexcept;

    // ========== Statistics ==========

    /// Returns the number of active allocations.
    [[nodiscard]] std::size_t active_allocations() const noexcept {
        return table_.active_count();
    }

    /// Returns total bytes currently allocated.
    [[nodiscard]] std::size_t total_allocated_bytes() const noexcept {
        return total_allocated_;
    }

    /// Returns the maximum allocation size.
    [[nodiscard]] std::size_t max_allocation_size() const noexcept {
        return max_allocation_size_;
    }

    // ========== Constants ==========

    /// Returns an invalid handle constant.
    [[nodiscard]] static constexpr Handle invalid_handle() noexcept {
        return Handle{
            .index = mem_config::INVALID_INDEX,
            .generation = 0
        };
    }

private:
    HandleTable table_;
    std::size_t max_allocation_size_{mem_config::MAX_ALLOCATION_SIZE};
    std::size_t total_allocated_{0};

    // Platform-specific allocation (implemented in memory.cpp)
    [[nodiscard]] void* os_allocate(std::size_t size) noexcept;
    void os_deallocate(void* ptr, std::size_t size) noexcept;

    // Internal validation
    [[nodiscard]] MemoryError validate_handle(Handle h) const noexcept;
    [[nodiscard]] MemoryError validate_bounds(Handle h, std::size_t offset,
                                              std::size_t size) const noexcept;
};

// Static assertions for type guarantees
static_assert(sizeof(Handle) == 8, "Handle must be 8 bytes");

} // namespace dotvm::core
