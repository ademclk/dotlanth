// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2025 DotLanth Project

#ifndef DOTVM_JIT_CACHE_HPP
#define DOTVM_JIT_CACHE_HPP

#include "jit_types.hpp"

#include <cstdint>
#include <cstring>
#include <list>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

#ifdef __linux__
    #include <sys/mman.h>
    #include <unistd.h>
#endif

namespace dotvm::jit {

/// Result of a memory allocation for JIT code
struct CodeAllocation {
    /// Pointer to the allocated memory (nullptr on failure)
    void* ptr{nullptr};
    /// Size of the allocation in bytes
    std::size_t size{0};
    /// Whether the allocation succeeded
    [[nodiscard]] constexpr auto is_valid() const noexcept -> bool {
        return ptr != nullptr && size > 0;
    }
};

/// Entry in the JIT cache
struct CacheEntry {
    /// Function identifier (bytecode entry PC)
    FunctionId function_id{0};
    /// Pointer to executable code
    void* code_ptr{nullptr};
    /// Size of the code in bytes
    std::size_t code_size{0};
    /// Start of bytecode range this code covers
    std::size_t bytecode_start{0};
    /// End of bytecode range this code covers
    std::size_t bytecode_end{0};
    /// Number of times this code has been executed
    std::uint32_t execution_count{0};
    /// Age counter for LRU eviction
    std::uint32_t age{0};
};

/// Executable memory region manager
///
/// Handles allocation of executable memory with W^X (write XOR execute) policy.
/// Memory is allocated with PROT_WRITE, code is written, then switched to
/// PROT_READ | PROT_EXEC for execution.
class ExecutableMemory {
public:
    /// Allocate a region of memory for code
    /// @param size The size in bytes (will be rounded up to page size)
    /// @return The allocation result
    [[nodiscard]] static auto allocate(std::size_t size) noexcept -> CodeAllocation {
#ifdef __linux__
        // Round up to page size
        const auto page_size = static_cast<std::size_t>(sysconf(_SC_PAGESIZE));
        const auto aligned_size = (size + page_size - 1) & ~(page_size - 1);

        void* ptr = mmap(
            nullptr,
            aligned_size,
            PROT_READ | PROT_WRITE,  // Start writable for code generation
            MAP_PRIVATE | MAP_ANONYMOUS,
            -1,
            0
        );

        if (ptr == MAP_FAILED) {
            return CodeAllocation{nullptr, 0};
        }

        return CodeAllocation{ptr, aligned_size};
#else
        // Unsupported platform
        (void)size;
        return CodeAllocation{nullptr, 0};
#endif
    }

    /// Make a region of memory executable (and remove write permission)
    /// @param ptr Pointer to the memory region
    /// @param size Size of the region in bytes
    /// @return true if successful, false otherwise
    [[nodiscard]] static auto make_executable(void* ptr, std::size_t size) noexcept -> bool {
#ifdef __linux__
        return mprotect(ptr, size, PROT_READ | PROT_EXEC) == 0;
#else
        (void)ptr;
        (void)size;
        return false;
#endif
    }

    /// Make a region of memory writable (and remove execute permission)
    /// @param ptr Pointer to the memory region
    /// @param size Size of the region in bytes
    /// @return true if successful, false otherwise
    [[nodiscard]] static auto make_writable(void* ptr, std::size_t size) noexcept -> bool {
#ifdef __linux__
        return mprotect(ptr, size, PROT_READ | PROT_WRITE) == 0;
#else
        (void)ptr;
        (void)size;
        return false;
#endif
    }

    /// Free a region of memory
    /// @param ptr Pointer to the memory region
    /// @param size Size of the region in bytes
    static void deallocate(void* ptr, std::size_t size) noexcept {
#ifdef __linux__
        if (ptr != nullptr && size > 0) {
            munmap(ptr, size);
        }
#else
        (void)ptr;
        (void)size;
#endif
    }

    /// Get the system page size
    [[nodiscard]] static auto page_size() noexcept -> std::size_t {
#ifdef __linux__
        return static_cast<std::size_t>(sysconf(_SC_PAGESIZE));
#else
        return 4096;  // Default assumption
#endif
    }
};

/// JIT code cache with LRU eviction
///
/// Manages compiled native code with automatic eviction when the cache
/// reaches its size limit. Uses a two-tier approach:
/// - Hot entries: Frequently executed, never evicted
/// - Warm entries: Less frequent, subject to clock-based aging and eviction
class JITCache {
public:
    /// Default maximum cache size (64 MB)
    static constexpr std::size_t DEFAULT_MAX_SIZE = 64 * 1024 * 1024;

    /// Number of entries to keep in the "hot" set (never evicted)
    static constexpr std::size_t HOT_SET_SIZE = 256;

    /// Age threshold for eviction (clock algorithm)
    static constexpr std::uint32_t EVICTION_AGE_THRESHOLD = 1000;

    /// Construct a JIT cache with the specified maximum size
    /// @param max_size Maximum cache size in bytes
    explicit JITCache(std::size_t max_size = DEFAULT_MAX_SIZE) noexcept
        : max_size_{max_size} {}

    /// Destructor - frees all allocated memory
    ~JITCache() noexcept {
        clear();
    }

    // Non-copyable, non-movable (owns memory resources)
    JITCache(const JITCache&) = delete;
    JITCache& operator=(const JITCache&) = delete;
    JITCache(JITCache&&) = delete;
    JITCache& operator=(JITCache&&) = delete;

    /// Look up compiled code for a function
    /// @param function_id The function identifier (bytecode entry PC)
    /// @return Pointer to executable code, or nullptr if not found
    [[nodiscard]] auto lookup(FunctionId function_id) noexcept -> NativeCodePtr {
        auto it = entries_.find(function_id);
        if (it == entries_.end()) {
            return nullptr;
        }

        // Update LRU: move to front of list
        auto& entry = it->second;
        entry.execution_count++;
        entry.age = 0;  // Reset age on access

        // Move to front of LRU list
        auto lru_it = lru_map_.find(function_id);
        if (lru_it != lru_map_.end()) {
            lru_list_.splice(lru_list_.begin(), lru_list_, lru_it->second);
        }

        return entry.code_ptr;
    }

    /// Check if a function is in the cache
    /// @param function_id The function identifier
    /// @return true if the function is cached
    [[nodiscard]] auto contains(FunctionId function_id) const noexcept -> bool {
        return entries_.contains(function_id);
    }

    /// Get the cache entry for a function (if present)
    /// @param function_id The function identifier
    /// @return The cache entry, or nullopt if not found
    [[nodiscard]] auto get_entry(FunctionId function_id) const noexcept
        -> std::optional<CacheEntry> {
        auto it = entries_.find(function_id);
        if (it == entries_.end()) {
            return std::nullopt;
        }
        return it->second;
    }

    /// Insert compiled code into the cache
    /// @param function_id The function identifier
    /// @param code The native code bytes
    /// @param bytecode_start Start of bytecode range
    /// @param bytecode_end End of bytecode range
    /// @return Pointer to the cached code, or nullptr on failure
    [[nodiscard]] auto insert(
        FunctionId function_id,
        std::span<const std::uint8_t> code,
        std::size_t bytecode_start,
        std::size_t bytecode_end
    ) noexcept -> NativeCodePtr {
        // Check if already cached
        if (contains(function_id)) {
            return lookup(function_id);
        }

        const auto code_size = code.size();

        // Evict entries if necessary to make room
        while (total_allocated_ + code_size > max_size_ && !lru_list_.empty()) {
            if (!evict_one()) {
                break;  // Cannot evict any more
            }
        }

        // Allocate executable memory
        auto allocation = ExecutableMemory::allocate(code_size);
        if (!allocation.is_valid()) {
            return nullptr;
        }

        // Copy code to the allocated memory
        std::memcpy(allocation.ptr, code.data(), code_size);

        // Make the memory executable (W^X transition)
        if (!ExecutableMemory::make_executable(allocation.ptr, allocation.size)) {
            ExecutableMemory::deallocate(allocation.ptr, allocation.size);
            return nullptr;
        }

        // Create cache entry
        CacheEntry entry{
            .function_id = function_id,
            .code_ptr = allocation.ptr,
            .code_size = allocation.size,
            .bytecode_start = bytecode_start,
            .bytecode_end = bytecode_end,
            .execution_count = 0,
            .age = 0
        };

        // Insert into maps
        entries_[function_id] = entry;
        lru_list_.push_front(function_id);
        lru_map_[function_id] = lru_list_.begin();

        total_allocated_ += allocation.size;
        ++entry_count_;

        return allocation.ptr;
    }

    /// Remove a specific entry from the cache
    /// @param function_id The function identifier to remove
    /// @return true if the entry was removed
    auto remove(FunctionId function_id) noexcept -> bool {
        auto it = entries_.find(function_id);
        if (it == entries_.end()) {
            return false;
        }

        auto& entry = it->second;

        // Free the memory
        ExecutableMemory::deallocate(entry.code_ptr, entry.code_size);
        total_allocated_ -= entry.code_size;

        // Remove from LRU list
        auto lru_it = lru_map_.find(function_id);
        if (lru_it != lru_map_.end()) {
            lru_list_.erase(lru_it->second);
            lru_map_.erase(lru_it);
        }

        // Remove from entries
        entries_.erase(it);
        --entry_count_;

        return true;
    }

    /// Clear all entries from the cache
    void clear() noexcept {
        for (auto& [id, entry] : entries_) {
            ExecutableMemory::deallocate(entry.code_ptr, entry.code_size);
        }
        entries_.clear();
        lru_list_.clear();
        lru_map_.clear();
        total_allocated_ = 0;
        entry_count_ = 0;
    }

    /// Advance the clock for aging (call periodically)
    /// @param ticks Number of clock ticks to advance
    void tick_clock(std::uint32_t ticks = 1) noexcept {
        for (auto& [id, entry] : entries_) {
            entry.age += ticks;
        }
    }

    /// Get the number of entries in the cache
    [[nodiscard]] auto entry_count() const noexcept -> std::size_t {
        return entry_count_;
    }

    /// Get the total allocated memory in bytes
    [[nodiscard]] auto total_allocated() const noexcept -> std::size_t {
        return total_allocated_;
    }

    /// Get the maximum cache size in bytes
    [[nodiscard]] auto max_size() const noexcept -> std::size_t {
        return max_size_;
    }

    /// Get the current cache utilization as a percentage
    [[nodiscard]] auto utilization() const noexcept -> double {
        if (max_size_ == 0) return 0.0;
        return static_cast<double>(total_allocated_) / static_cast<double>(max_size_) * 100.0;
    }

private:
    /// Evict one entry from the cache (LRU policy)
    /// @return true if an entry was evicted
    auto evict_one() noexcept -> bool {
        if (lru_list_.empty()) {
            return false;
        }

        // Find the oldest entry that's not in the hot set
        // Hot set = entries with execution_count in top HOT_SET_SIZE
        // For simplicity, we use the back of the LRU list

        // Find a candidate for eviction from the back of the list
        for (auto it = lru_list_.rbegin(); it != lru_list_.rend(); ++it) {
            FunctionId victim_id = *it;
            auto entry_it = entries_.find(victim_id);
            if (entry_it == entries_.end()) {
                continue;
            }

            auto& entry = entry_it->second;

            // Skip if it's too "hot" (frequently executed)
            // Simple heuristic: evict if age > threshold or execution_count is low
            if (entry.age < EVICTION_AGE_THRESHOLD &&
                entry.execution_count > HOT_SET_SIZE) {
                continue;  // Too hot to evict
            }

            // Evict this entry
            return remove(victim_id);
        }

        // If all entries are hot, force evict the oldest
        if (!lru_list_.empty()) {
            return remove(lru_list_.back());
        }

        return false;
    }

    /// Maximum cache size in bytes
    std::size_t max_size_;

    /// Total currently allocated memory
    std::size_t total_allocated_{0};

    /// Number of entries in the cache
    std::size_t entry_count_{0};

    /// Map from function ID to cache entry
    std::unordered_map<FunctionId, CacheEntry> entries_;

    /// LRU list (front = most recently used)
    std::list<FunctionId> lru_list_;

    /// Map from function ID to LRU list iterator (for O(1) updates)
    std::unordered_map<FunctionId, std::list<FunctionId>::iterator> lru_map_;
};

}  // namespace dotvm::jit

#endif  // DOTVM_JIT_CACHE_HPP
