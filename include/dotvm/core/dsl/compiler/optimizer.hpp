#pragma once

/// @file optimizer.hpp
/// @brief DSL-002 IR Optimizer - Optimization passes for SSA IR
///
/// Implements essential optimization passes:
/// - Constant folding: Evaluate constant expressions at compile time
/// - Dead code elimination: Remove unused values and unreachable blocks

#include <unordered_set>
#include <vector>

#include "dotvm/core/dsl/ir/instruction.hpp"
#include "dotvm/core/dsl/ir/types.hpp"

namespace dotvm::core::dsl::compiler {

/// @brief Statistics from optimization passes
struct OptimizationStats {
    std::size_t constants_folded = 0;
    std::size_t dead_instructions_removed = 0;
    std::size_t dead_blocks_removed = 0;
};

/// @brief Constant folding pass
///
/// Evaluates constant expressions at compile time. For example:
/// %1 = const 2
/// %2 = const 3
/// %3 = add %1, %2
/// Becomes:
/// %3 = const 5
class ConstantFolder {
public:
    /// @brief Run constant folding on a DotIR
    /// @return Number of constants folded
    std::size_t run(ir::DotIR& dot);

private:
    std::unordered_map<std::uint32_t, dotvm::core::Value> known_constants_;

    bool try_fold_binary(ir::BinaryOp& op);
    bool try_fold_unary(ir::UnaryOp& op);
    bool try_fold_compare(ir::Compare& cmp);

    std::optional<dotvm::core::Value> get_constant(std::uint32_t value_id);
    void record_constant(std::uint32_t value_id, dotvm::core::Value val);
};

/// @brief Dead code elimination pass
///
/// Removes instructions whose results are never used, and removes
/// unreachable basic blocks.
class DeadCodeEliminator {
public:
    /// @brief Run DCE on a DotIR
    /// @return Number of instructions/blocks removed
    std::size_t run(ir::DotIR& dot);

private:
    std::unordered_set<std::uint32_t> used_values_;
    std::unordered_set<std::uint32_t> reachable_blocks_;

    void mark_used_values(ir::DotIR& dot);
    void mark_reachable_blocks(ir::DotIR& dot);
    std::size_t remove_dead_instructions(ir::DotIR& dot);
    std::size_t remove_dead_blocks(ir::DotIR& dot);

    void mark_value_used(std::uint32_t id);
    bool is_value_used(std::uint32_t id) const;
};

/// @brief Main optimizer class that runs all passes
class Optimizer {
public:
    /// @brief Optimization level
    enum class Level {
        None,   ///< No optimization (for debugging)
        Basic,  ///< Essential optimizations only
    };

    explicit Optimizer(Level level = Level::Basic) : level_(level) {}

    /// @brief Run all optimization passes on a DotIR
    OptimizationStats optimize(ir::DotIR& dot);

    /// @brief Run all optimization passes on a CompiledModule
    OptimizationStats optimize(ir::CompiledModule& module);

private:
    Level level_;
};

}  // namespace dotvm::core::dsl::compiler
