#include "dotvm/core/dsl/compiler/optimizer.hpp"

#include <algorithm>
#include <queue>

namespace dotvm::core::dsl::compiler {

using namespace ir;

// ============================================================================
// Constant Folder
// ============================================================================

std::size_t ConstantFolder::run(DotIR& dot) {
    known_constants_.clear();
    std::size_t folded = 0;

    for (auto& block : dot.blocks) {
        for (auto& instr : block->instructions) {
            // First, record any constants from LoadConst
            if (auto* lc = std::get_if<LoadConst>(&instr->kind)) {
                record_constant(lc->result.id, lc->constant);
                continue;
            }

            // Try to fold binary operations
            if (auto* binop = std::get_if<ir::BinaryOp>(&instr->kind)) {
                if (try_fold_binary(*binop)) {
                    // Convert to LoadConst
                    auto constant = *get_constant(binop->result.id);
                    instr->kind = LoadConst{.result = binop->result, .constant = constant};
                    ++folded;
                }
                continue;
            }

            // Try to fold unary operations
            if (auto* unop = std::get_if<ir::UnaryOp>(&instr->kind)) {
                if (try_fold_unary(*unop)) {
                    auto constant = *get_constant(unop->result.id);
                    instr->kind = LoadConst{.result = unop->result, .constant = constant};
                    ++folded;
                }
                continue;
            }

            // Try to fold comparisons
            if (auto* cmp = std::get_if<Compare>(&instr->kind)) {
                if (try_fold_compare(*cmp)) {
                    auto constant = *get_constant(cmp->result.id);
                    instr->kind = LoadConst{.result = cmp->result, .constant = constant};
                    ++folded;
                }
                continue;
            }
        }
    }

    return folded;
}

bool ConstantFolder::try_fold_binary(ir::BinaryOp& op) {
    auto left = get_constant(op.left_id);
    auto right = get_constant(op.right_id);

    if (!left || !right) {
        return false;
    }

    dotvm::core::Value result;

    switch (op.op) {
        case BinaryOpKind::Add:
            result = value_ops::add(*left, *right);
            break;
        case BinaryOpKind::Sub:
            result = value_ops::sub(*left, *right);
            break;
        case BinaryOpKind::Mul:
            result = value_ops::mul(*left, *right);
            break;
        case BinaryOpKind::Div:
            result = value_ops::div(*left, *right);
            if (result.is_nil()) return false;  // Division by zero
            break;
        case BinaryOpKind::Mod:
            result = value_ops::mod(*left, *right);
            if (result.is_nil()) return false;
            break;
        case BinaryOpKind::And:
            result = dotvm::core::Value::from_bool(left->is_truthy() && right->is_truthy());
            break;
        case BinaryOpKind::Or:
            result = dotvm::core::Value::from_bool(left->is_truthy() || right->is_truthy());
            break;
        default:
            return false;  // Bitwise ops not folded for now
    }

    record_constant(op.result.id, result);
    op.result.constant = result;
    return true;
}

bool ConstantFolder::try_fold_unary(ir::UnaryOp& op) {
    auto operand = get_constant(op.operand_id);
    if (!operand) {
        return false;
    }

    dotvm::core::Value result;

    switch (op.op) {
        case UnaryOpKind::Neg:
            result = value_ops::neg(*operand);
            break;
        case UnaryOpKind::Not:
            result = dotvm::core::Value::from_bool(!operand->is_truthy());
            break;
        default:
            return false;
    }

    record_constant(op.result.id, result);
    op.result.constant = result;
    return true;
}

bool ConstantFolder::try_fold_compare(Compare& cmp) {
    auto left = get_constant(cmp.left_id);
    auto right = get_constant(cmp.right_id);

    if (!left || !right) {
        return false;
    }

    bool result_bool;

    switch (cmp.op) {
        case CompareKind::Eq:
            result_bool = (*left == *right);
            break;
        case CompareKind::Ne:
            result_bool = !(*left == *right);
            break;
        case CompareKind::Lt:
            result_bool = value_ops::less_than(*left, *right);
            break;
        case CompareKind::Le:
            result_bool = value_ops::less_equal(*left, *right);
            break;
        case CompareKind::Gt:
            result_bool = value_ops::greater_than(*left, *right);
            break;
        case CompareKind::Ge:
            result_bool = value_ops::greater_equal(*left, *right);
            break;
        default:
            return false;
    }

    auto result = dotvm::core::Value::from_bool(result_bool);
    record_constant(cmp.result.id, result);
    cmp.result.constant = result;
    return true;
}

std::optional<dotvm::core::Value> ConstantFolder::get_constant(std::uint32_t value_id) {
    auto it = known_constants_.find(value_id);
    if (it != known_constants_.end()) {
        return it->second;
    }
    return std::nullopt;
}

void ConstantFolder::record_constant(std::uint32_t value_id, dotvm::core::Value val) {
    known_constants_[value_id] = val;
}

// ============================================================================
// Dead Code Eliminator
// ============================================================================

std::size_t DeadCodeEliminator::run(DotIR& dot) {
    used_values_.clear();
    reachable_blocks_.clear();

    // Mark reachable blocks first
    mark_reachable_blocks(dot);

    // Mark used values
    mark_used_values(dot);

    // Remove dead instructions and blocks
    std::size_t removed = 0;
    removed += remove_dead_instructions(dot);
    removed += remove_dead_blocks(dot);

    return removed;
}

void DeadCodeEliminator::mark_reachable_blocks(DotIR& dot) {
    if (dot.blocks.empty()) return;

    std::queue<std::uint32_t> worklist;
    worklist.push(dot.entry_block_id);
    reachable_blocks_.insert(dot.entry_block_id);

    while (!worklist.empty()) {
        auto block_id = worklist.front();
        worklist.pop();

        auto* block = dot.find_block(block_id);
        if (!block || !block->terminator) continue;

        // Add successors based on terminator
        std::visit([&](const auto& term) {
            using T = std::decay_t<decltype(term)>;

            if constexpr (std::is_same_v<T, Jump>) {
                if (reachable_blocks_.insert(term.target_block_id).second) {
                    worklist.push(term.target_block_id);
                }
            } else if constexpr (std::is_same_v<T, Branch>) {
                if (reachable_blocks_.insert(term.true_block_id).second) {
                    worklist.push(term.true_block_id);
                }
                if (reachable_blocks_.insert(term.false_block_id).second) {
                    worklist.push(term.false_block_id);
                }
            }
        }, block->terminator->kind);
    }
}

void DeadCodeEliminator::mark_used_values(DotIR& dot) {
    // Walk all reachable blocks and mark values that are used
    for (auto& block : dot.blocks) {
        if (reachable_blocks_.find(block->id) == reachable_blocks_.end()) {
            continue;
        }

        for (auto& instr : block->instructions) {
            std::visit([this](const auto& inst) {
                using T = std::decay_t<decltype(inst)>;

                if constexpr (std::is_same_v<T, ir::BinaryOp>) {
                    mark_value_used(inst.left_id);
                    mark_value_used(inst.right_id);
                } else if constexpr (std::is_same_v<T, ir::UnaryOp>) {
                    mark_value_used(inst.operand_id);
                } else if constexpr (std::is_same_v<T, Compare>) {
                    mark_value_used(inst.left_id);
                    mark_value_used(inst.right_id);
                } else if constexpr (std::is_same_v<T, StatePut>) {
                    mark_value_used(inst.value_id);
                } else if constexpr (std::is_same_v<T, Call>) {
                    for (auto arg_id : inst.arg_ids) {
                        mark_value_used(arg_id);
                    }
                } else if constexpr (std::is_same_v<T, Cast>) {
                    mark_value_used(inst.value_id);
                } else if constexpr (std::is_same_v<T, Copy>) {
                    mark_value_used(inst.value_id);
                }
            }, instr->kind);
        }

        // Mark values used in terminator
        if (block->terminator) {
            std::visit([this](const auto& term) {
                using T = std::decay_t<decltype(term)>;

                if constexpr (std::is_same_v<T, Branch>) {
                    mark_value_used(term.condition_id);
                } else if constexpr (std::is_same_v<T, Return>) {
                    if (term.value_id) {
                        mark_value_used(*term.value_id);
                    }
                } else if constexpr (std::is_same_v<T, Halt>) {
                    if (term.exit_code_id) {
                        mark_value_used(*term.exit_code_id);
                    }
                }
            }, block->terminator->kind);
        }

        // Mark values used in phi nodes
        for (auto& phi : block->phis) {
            for (auto& [_, val_id] : phi->incoming) {
                mark_value_used(val_id);
            }
        }
    }

    // State puts are always considered used (side effects)
    for (auto& block : dot.blocks) {
        if (reachable_blocks_.find(block->id) == reachable_blocks_.end()) {
            continue;
        }
        for (auto& instr : block->instructions) {
            if (std::holds_alternative<StatePut>(instr->kind) ||
                std::holds_alternative<Call>(instr->kind)) {
                // These have side effects, mark their result as used
                if (auto* result = instr->get_result()) {
                    mark_value_used(result->id);
                }
            }
        }
    }
}

std::size_t DeadCodeEliminator::remove_dead_instructions(DotIR& dot) {
    std::size_t removed = 0;

    for (auto& block : dot.blocks) {
        if (reachable_blocks_.find(block->id) == reachable_blocks_.end()) {
            continue;
        }

        auto& instrs = block->instructions;
        auto new_end = std::remove_if(instrs.begin(), instrs.end(),
            [this, &removed](const std::unique_ptr<Instruction>& instr) {
                // Don't remove side-effecting instructions
                if (std::holds_alternative<StatePut>(instr->kind) ||
                    std::holds_alternative<Call>(instr->kind)) {
                    return false;
                }

                // Check if result is used
                if (auto* result = instr->get_result()) {
                    if (!is_value_used(result->id)) {
                        ++removed;
                        return true;
                    }
                }
                return false;
            });

        instrs.erase(new_end, instrs.end());
    }

    return removed;
}

std::size_t DeadCodeEliminator::remove_dead_blocks(DotIR& dot) {
    std::size_t removed = 0;

    auto new_end = std::remove_if(dot.blocks.begin(), dot.blocks.end(),
        [this, &removed](const std::unique_ptr<BasicBlock>& block) {
            if (reachable_blocks_.find(block->id) == reachable_blocks_.end()) {
                ++removed;
                return true;
            }
            return false;
        });

    dot.blocks.erase(new_end, dot.blocks.end());
    return removed;
}

void DeadCodeEliminator::mark_value_used(std::uint32_t id) {
    used_values_.insert(id);
}

bool DeadCodeEliminator::is_value_used(std::uint32_t id) const {
    return used_values_.find(id) != used_values_.end();
}

// ============================================================================
// Optimizer
// ============================================================================

OptimizationStats Optimizer::optimize(DotIR& dot) {
    OptimizationStats stats;

    if (level_ == Level::None) {
        return stats;
    }

    // Run constant folding
    ConstantFolder folder;
    stats.constants_folded = folder.run(dot);

    // Run dead code elimination
    DeadCodeEliminator dce;
    auto dce_removed = dce.run(dot);
    stats.dead_instructions_removed = dce_removed;

    return stats;
}

OptimizationStats Optimizer::optimize(CompiledModule& module) {
    OptimizationStats total_stats;

    for (auto& dot : module.dots) {
        auto stats = optimize(dot);
        total_stats.constants_folded += stats.constants_folded;
        total_stats.dead_instructions_removed += stats.dead_instructions_removed;
        total_stats.dead_blocks_removed += stats.dead_blocks_removed;
    }

    return total_stats;
}

}  // namespace dotvm::core::dsl::compiler
