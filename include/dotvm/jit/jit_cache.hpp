/// @file jit_cache.hpp
/// @brief Compiled code cache for JIT compilation
///
/// Manages the mapping from function IDs to compiled native code entries.
/// Provides O(1) lookup and tracks cache statistics.

#pragma once

#include "jit_code_buffer.hpp"
#include "jit_config.hpp"
#include "jit_profiler.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

namespace dotvm::jit {

/// @brief Entry in the compiled code cache
///
/// Represents a single compiled function with metadata.
struct CompiledEntry {
    /// @brief Pointer to the start of compiled code
    const std::uint8_t* code{nullptr};

    /// @brief Size of the compiled code in bytes
    std::size_t code_size{0};

    /// @brief Function ID this entry corresponds to
    FunctionId function_id{0};

    /// @brief Entry PC of the function in bytecode
    std::size_t entry_pc{0};

    /// @brief End PC of the function in bytecode
    std::size_t end_pc{0};

    /// @brief Whether this entry is currently valid
    bool valid{false};

    /// @brief Number of times this compiled code has been executed
    std::uint64_t execution_count{0};

    /// @brief Get the compiled code as a function pointer
    template<typename Signature>
    [[nodiscard]] Signature* as_function() const noexcept {
        return reinterpret_cast<Signature*>(const_cast<std::uint8_t*>(code));
    }

    /// @brief Check if entry is usable
    [[nodiscard]] bool is_valid() const noexcept {
        return valid && code != nullptr && code_size > 0;
    }
};

/// @brief OSR entry point in compiled code
struct OsrEntry {
    /// @brief Loop ID this entry corresponds to
    LoopId loop_id{0};

    /// @brief Pointer to the OSR entry point in compiled code
    const std::uint8_t* entry_point{nullptr};

    /// @brief PC in bytecode where OSR can transfer
    std::size_t bytecode_pc{0};

    /// @brief Which compiled function this belongs to
    FunctionId function_id{0};
};

/// @brief Cache statistics
struct CacheStats {
    /// @brief Total number of cached entries
    std::size_t entry_count{0};

    /// @brief Number of valid entries
    std::size_t valid_entries{0};

    /// @brief Total bytes of compiled code
    std::size_t total_code_bytes{0};

    /// @brief Cache hit count
    std::uint64_t hits{0};

    /// @brief Cache miss count
    std::uint64_t misses{0};

    /// @brief Number of evictions due to capacity
    std::uint64_t evictions{0};

    /// @brief Number of invalidations
    std::uint64_t invalidations{0};

    /// @brief Hit rate (0.0 to 1.0)
    [[nodiscard]] double hit_rate() const noexcept {
        const auto total = hits + misses;
        return total > 0 ? static_cast<double>(hits) / static_cast<double>(total) : 0.0;
    }
};

/// @brief Compiled code cache for JIT
///
/// Stores compiled native code for functions and provides fast lookup.
/// Manages code buffer allocation and entry metadata.
///
/// @example
/// ```cpp
/// JitCache cache(config);
///
/// // Store compiled code
/// cache.store(func_id, code_ptr, code_size, entry_pc, end_pc);
///
/// // Look up compiled code
/// if (auto* entry = cache.lookup(func_id)) {
///     auto fn = entry->as_function<void(void*, void*)>();
///     fn(regs, ctx);
/// }
/// ```
class JitCache {
public:
    /// @brief Compiled function signature
    /// @param regs Pointer to register file
    /// @param ctx Pointer to VM context
    using CompiledFn = void (*)(void* regs, void* ctx);

    /// @brief Create a cache with specified configuration
    explicit JitCache(const JitConfig& config);

    /// @brief Create a cache with default configuration
    JitCache();

    // Non-copyable
    JitCache(const JitCache&) = delete;
    JitCache& operator=(const JitCache&) = delete;

    // Movable
    JitCache(JitCache&&) noexcept = default;
    JitCache& operator=(JitCache&&) noexcept = default;

    /// @brief Look up compiled code for a function
    ///
    /// @param func_id Function ID to look up
    /// @return Pointer to compiled entry, or nullptr if not found
    [[nodiscard]] const CompiledEntry* lookup(FunctionId func_id) noexcept;

    /// @brief Look up compiled code by entry PC
    ///
    /// @param entry_pc PC of function entry
    /// @return Pointer to compiled entry, or nullptr if not found
    [[nodiscard]] const CompiledEntry* lookup_by_pc(std::size_t entry_pc) noexcept;

    /// @brief Store compiled code in the cache
    ///
    /// @param func_id Function ID
    /// @param code Pointer to compiled code (in executable buffer)
    /// @param code_size Size of compiled code
    /// @param entry_pc Bytecode entry PC
    /// @param end_pc Bytecode end PC
    /// @return true if stored successfully, false if cache is full
    [[nodiscard]] bool store(
        FunctionId func_id,
        const std::uint8_t* code,
        std::size_t code_size,
        std::size_t entry_pc,
        std::size_t end_pc
    ) noexcept;

    /// @brief Register an OSR entry point
    ///
    /// @param loop_id Loop ID
    /// @param entry_point Pointer to OSR entry in compiled code
    /// @param bytecode_pc Bytecode PC for state transfer
    /// @param func_id Function containing the loop
    void register_osr_entry(
        LoopId loop_id,
        const std::uint8_t* entry_point,
        std::size_t bytecode_pc,
        FunctionId func_id
    ) noexcept;

    /// @brief Look up an OSR entry point
    [[nodiscard]] const OsrEntry* lookup_osr(LoopId loop_id) noexcept;

    /// @brief Invalidate a cached entry
    ///
    /// Called when the underlying bytecode changes.
    void invalidate(FunctionId func_id) noexcept;

    /// @brief Invalidate all entries in a PC range
    void invalidate_range(std::size_t start_pc, std::size_t end_pc) noexcept;

    /// @brief Clear the entire cache
    void clear() noexcept;

    /// @brief Check if function is cached
    [[nodiscard]] bool contains(FunctionId func_id) const noexcept;

    /// @brief Get cache statistics
    [[nodiscard]] CacheStats stats() const noexcept;

    /// @brief Get maximum cache size in bytes
    [[nodiscard]] std::size_t max_size() const noexcept { return max_size_; }

    /// @brief Get current cache usage in bytes
    [[nodiscard]] std::size_t used_size() const noexcept { return used_size_; }

    /// @brief Get available space in bytes
    [[nodiscard]] std::size_t available() const noexcept { return max_size_ - used_size_; }

    /// @brief Check if cache has space for more code
    [[nodiscard]] bool has_space(std::size_t code_size) const noexcept {
        return used_size_ + code_size <= max_size_;
    }

    /// @brief Get number of cached entries
    [[nodiscard]] std::size_t entry_count() const noexcept { return entries_.size(); }

private:
    /// @brief Entries indexed by function ID
    std::unordered_map<FunctionId, CompiledEntry> entries_;

    /// @brief PC to function ID mapping for fast lookup
    std::unordered_map<std::size_t, FunctionId> pc_to_func_;

    /// @brief OSR entries indexed by loop ID
    std::unordered_map<LoopId, OsrEntry> osr_entries_;

    /// @brief Maximum cache size in bytes
    std::size_t max_size_{0};

    /// @brief Current usage in bytes
    std::size_t used_size_{0};

    /// @brief Hit counter
    std::uint64_t hits_{0};

    /// @brief Miss counter
    std::uint64_t misses_{0};

    /// @brief Eviction counter
    std::uint64_t evictions_{0};

    /// @brief Invalidation counter
    std::uint64_t invalidations_{0};
};

} // namespace dotvm::jit
