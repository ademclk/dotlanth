/// @file jit_cache.cpp
/// @brief Implementation of compiled code cache

#include "dotvm/jit/jit_cache.hpp"

namespace dotvm::jit {

JitCache::JitCache(const JitConfig& config) : max_size_(config.max_code_cache) {}

JitCache::JitCache() : max_size_(thresholds::DEFAULT_MAX_CODE_CACHE) {}

const CompiledEntry* JitCache::lookup(FunctionId func_id) noexcept {
    auto it = entries_.find(func_id);
    if (it == entries_.end()) [[unlikely]] {
        ++misses_;
        return nullptr;
    }

    auto& entry = it->second;
    if (!entry.is_valid()) [[unlikely]] {
        ++misses_;
        return nullptr;
    }

    ++hits_;
    ++entry.execution_count;
    return &entry;
}

const CompiledEntry* JitCache::lookup_by_pc(std::size_t entry_pc) noexcept {
    auto it = pc_to_func_.find(entry_pc);
    if (it == pc_to_func_.end()) [[unlikely]] {
        ++misses_;
        return nullptr;
    }
    return lookup(it->second);
}

bool JitCache::store(FunctionId func_id, const std::uint8_t* code, std::size_t code_size,
                     std::size_t entry_pc, std::size_t end_pc) noexcept {
    // Check if we already have this entry
    auto existing = entries_.find(func_id);
    if (existing != entries_.end()) {
        // Update existing entry
        used_size_ -= existing->second.code_size;
    }

    // Check capacity
    if (used_size_ + code_size > max_size_) [[unlikely]] {
        return false;
    }

    // Create the entry
    CompiledEntry entry;
    entry.code = code;
    entry.code_size = code_size;
    entry.function_id = func_id;
    entry.entry_pc = entry_pc;
    entry.end_pc = end_pc;
    entry.valid = true;
    entry.execution_count = 0;

    // Store it
    entries_[func_id] = entry;
    pc_to_func_[entry_pc] = func_id;
    used_size_ += code_size;

    return true;
}

void JitCache::register_osr_entry(LoopId loop_id, const std::uint8_t* entry_point,
                                  std::size_t bytecode_pc, FunctionId func_id) noexcept {
    OsrEntry entry;
    entry.loop_id = loop_id;
    entry.entry_point = entry_point;
    entry.bytecode_pc = bytecode_pc;
    entry.function_id = func_id;

    osr_entries_[loop_id] = entry;
}

const OsrEntry* JitCache::lookup_osr(LoopId loop_id) noexcept {
    auto it = osr_entries_.find(loop_id);
    if (it == osr_entries_.end()) {
        return nullptr;
    }
    return &it->second;
}

void JitCache::invalidate(FunctionId func_id) noexcept {
    auto it = entries_.find(func_id);
    if (it == entries_.end()) {
        return;
    }

    auto& entry = it->second;
    if (entry.valid) {
        entry.valid = false;
        used_size_ -= entry.code_size;
        pc_to_func_.erase(entry.entry_pc);

        // Also invalidate any OSR entries for this function
        std::vector<LoopId> to_remove;
        for (const auto& [loop_id, osr_entry] : osr_entries_) {
            if (osr_entry.function_id == func_id) {
                to_remove.push_back(loop_id);
            }
        }
        for (auto loop_id : to_remove) {
            osr_entries_.erase(loop_id);
        }

        ++invalidations_;
    }
}

void JitCache::invalidate_range(std::size_t start_pc, std::size_t end_pc) noexcept {
    std::vector<FunctionId> to_invalidate;

    for (const auto& [func_id, entry] : entries_) {
        if (!entry.valid)
            continue;

        // Check if function overlaps with the range
        bool overlaps = (entry.entry_pc < end_pc && entry.end_pc > start_pc);
        if (overlaps) {
            to_invalidate.push_back(func_id);
        }
    }

    for (auto func_id : to_invalidate) {
        invalidate(func_id);
    }
}

void JitCache::clear() noexcept {
    entries_.clear();
    pc_to_func_.clear();
    osr_entries_.clear();
    used_size_ = 0;
    // Keep stats for historical tracking
}

bool JitCache::contains(FunctionId func_id) const noexcept {
    auto it = entries_.find(func_id);
    return it != entries_.end() && it->second.is_valid();
}

CacheStats JitCache::stats() const noexcept {
    CacheStats s;
    s.entry_count = entries_.size();
    s.total_code_bytes = used_size_;
    s.hits = hits_;
    s.misses = misses_;
    s.evictions = evictions_;
    s.invalidations = invalidations_;

    for (const auto& [_, entry] : entries_) {
        if (entry.is_valid()) {
            ++s.valid_entries;
        }
    }

    return s;
}

}  // namespace dotvm::jit
