/// @file jit_profiler.cpp
/// @brief Implementation of JIT profiler

#include "dotvm/jit/jit_profiler.hpp"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>

#include "dotvm/jit/jit_config.hpp"

namespace dotvm::jit {

JitProfiler::JitProfiler(const JitConfig& config) noexcept
    : call_threshold_(config.call_threshold), loop_threshold_(config.loop_threshold) {}

FunctionId JitProfiler::register_function(std::size_t entry_pc, std::size_t end_pc) {
    const auto id = static_cast<FunctionId>(functions_.size());

    auto profile = std::make_unique<FunctionProfile>();
    profile->entry_pc = entry_pc;
    profile->end_pc = end_pc;

    functions_.push_back(std::move(profile));
    pc_to_function_[entry_pc] = id;

    return id;
}

LoopId JitProfiler::register_loop(FunctionId func_id, std::size_t header_pc,
                                  std::size_t backedge_pc) {
    // Find the next loop index for this function
    std::uint32_t loop_index = 0;
    for (const auto& [id, _] : loops_) {
        if (loop_id_function(id) == func_id) {
            loop_index = std::max(loop_index, loop_id_index(id) + 1);
        }
    }

    const LoopId id = make_loop_id(func_id, loop_index);

    auto profile = std::make_unique<LoopProfile>();
    profile->header_pc = header_pc;
    profile->backedge_pc = backedge_pc;

    loops_[id] = std::move(profile);
    backedge_to_loop_[backedge_pc] = id;

    return id;
}

std::uint32_t JitProfiler::record_call(FunctionId func_id) noexcept {
    if (func_id >= functions_.size()) [[unlikely]] {
        return 0;
    }
    return functions_[func_id]->increment();
}

std::uint32_t JitProfiler::record_iteration(LoopId loop_id) noexcept {
    auto it = loops_.find(loop_id);
    if (it == loops_.end()) [[unlikely]] {
        return 0;
    }
    return it->second->increment();
}

bool JitProfiler::should_compile(FunctionId func_id) const noexcept {
    if (func_id >= functions_.size()) [[unlikely]] {
        return false;
    }

    const auto& profile = *functions_[func_id];
    return profile.reached_threshold(call_threshold_) &&
           !profile.compiled.load(std::memory_order_relaxed);
}

bool JitProfiler::should_osr(LoopId loop_id) const noexcept {
    auto it = loops_.find(loop_id);
    if (it == loops_.end()) [[unlikely]] {
        return false;
    }

    const auto& profile = *it->second;
    return profile.reached_threshold(loop_threshold_) &&
           !profile.osr_triggered.load(std::memory_order_relaxed);
}

void JitProfiler::mark_compiled(FunctionId func_id) noexcept {
    if (func_id < functions_.size()) {
        functions_[func_id]->compiled.store(true, std::memory_order_release);
    }
}

void JitProfiler::mark_osr_triggered(LoopId loop_id) noexcept {
    auto it = loops_.find(loop_id);
    if (it != loops_.end()) {
        it->second->osr_triggered.store(true, std::memory_order_release);
    }
}

bool JitProfiler::is_compiled(FunctionId func_id) const noexcept {
    if (func_id >= functions_.size()) [[unlikely]] {
        return false;
    }
    return functions_[func_id]->compiled.load(std::memory_order_acquire);
}

bool JitProfiler::is_osr_triggered(LoopId loop_id) const noexcept {
    auto it = loops_.find(loop_id);
    if (it == loops_.end()) [[unlikely]] {
        return false;
    }
    return it->second->osr_triggered.load(std::memory_order_acquire);
}

const FunctionProfile* JitProfiler::get_function(FunctionId func_id) const noexcept {
    if (func_id >= functions_.size()) [[unlikely]] {
        return nullptr;
    }
    return functions_[func_id].get();
}

const LoopProfile* JitProfiler::get_loop(LoopId loop_id) const noexcept {
    auto it = loops_.find(loop_id);
    if (it == loops_.end()) [[unlikely]] {
        return nullptr;
    }
    return it->second.get();
}

std::optional<FunctionId> JitProfiler::find_function_by_pc(std::size_t pc) const noexcept {
    auto it = pc_to_function_.find(pc);
    if (it == pc_to_function_.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::optional<LoopId> JitProfiler::find_loop_by_backedge(std::size_t backedge_pc) const noexcept {
    auto it = backedge_to_loop_.find(backedge_pc);
    if (it == backedge_to_loop_.end()) {
        return std::nullopt;
    }
    return it->second;
}

void JitProfiler::reset() {
    for (auto& func : functions_) {
        func->reset();
    }
    for (auto& [_, loop] : loops_) {
        loop->reset();
    }
}

JitProfiler::Stats JitProfiler::get_stats() const noexcept {
    Stats stats;
    stats.total_functions = functions_.size();
    stats.total_loops = loops_.size();

    for (const auto& func : functions_) {
        stats.total_calls += func->call_count.load(std::memory_order_relaxed);
        if (func->compiled.load(std::memory_order_relaxed)) {
            ++stats.compiled_functions;
        }
    }

    for (const auto& [_, loop] : loops_) {
        stats.total_iterations += loop->iteration_count.load(std::memory_order_relaxed);
        if (loop->osr_triggered.load(std::memory_order_relaxed)) {
            ++stats.osr_triggered_loops;
        }
    }

    return stats;
}

}  // namespace dotvm::jit
