#include "dotvm/core/dsl/compiler/ir_builder.hpp"

#include <variant>

namespace dotvm::core::dsl::compiler {

using namespace ir;

// ============================================================================
// Public API
// ============================================================================

IRBuildResult<CompiledModule> IRBuilder::build(const DslModule& module) {
    CompiledModule result;

    // Build each dot
    for (const auto& dot : module.dots) {
        auto dot_result = build_dot(dot);
        if (!dot_result) {
            return std::unexpected(dot_result.error());
        }
        result.dots.push_back(std::move(*dot_result));
    }

    // Copy links as metadata
    for (const auto& link : module.links) {
        result.links.push_back(LinkEntry{
            .source_dot = link.source,
            .target_dot = link.target,
            .span = link.span,
        });
    }

    return result;
}

IRBuildResult<DotIR> IRBuilder::build_dot(const DotDef& dot) {
    DotIR result;
    result.name = dot.name;
    current_dot_ = &result;
    state_name_to_slot_.clear();
    local_values_.clear();

    // Build state slots
    if (dot.state) {
        auto state_result = build_state(*dot.state);
        if (!state_result) {
            return std::unexpected(state_result.error());
        }
    }

    // Create entry block
    auto* entry = create_block("entry");
    result.entry_block_id = entry->id;
    set_current_block(entry);

    // Build triggers
    for (const auto& trigger : dot.triggers) {
        auto trigger_result = build_trigger(trigger);
        if (!trigger_result) {
            return std::unexpected(trigger_result.error());
        }
    }

    // Ensure last block is terminated
    if (current_block_ && !current_block_->is_terminated()) {
        emit_terminator(Instruction::make(Halt{}));
    }

    current_dot_ = nullptr;
    return result;
}

// ============================================================================
// Block Management
// ============================================================================

BasicBlock* IRBuilder::create_block(const std::string& label) {
    auto block = std::make_unique<BasicBlock>();
    block->id = current_dot_->alloc_block_id();
    block->label = label;
    auto* ptr = block.get();
    current_dot_->blocks.push_back(std::move(block));
    return ptr;
}

void IRBuilder::set_current_block(BasicBlock* block) {
    current_block_ = block;
}

void IRBuilder::seal_block(BasicBlock* /*block*/) {
    // In a full implementation, this would finalize phi nodes
    // For now, we use a simplified SSA construction
}

// ============================================================================
// Value Management
// ============================================================================

Value IRBuilder::create_value(ValueType type, const std::string& name) {
    return Value::make(current_dot_->alloc_value_id(), type, name);
}

Value IRBuilder::create_const_value(ValueType type, dotvm::core::Value val,
                                    const std::string& name) {
    return Value::make_const(current_dot_->alloc_value_id(), type, val, name);
}

// ============================================================================
// State Building
// ============================================================================

IRBuildResult<void> IRBuilder::build_state(const StateDef& state) {
    for (const auto& var : state.variables) {
        // Infer type from initial value
        ValueType type = ValueType::Any;
        std::optional<dotvm::core::Value> init_val;

        if (var.value) {
            type = infer_type(*var.value);

            // Evaluate constant if possible
            const auto& expr_val = var.value->value;
            if (std::holds_alternative<IntegerExpr>(expr_val)) {
                init_val = dotvm::core::Value::from_int(std::get<IntegerExpr>(expr_val).value);
            } else if (std::holds_alternative<FloatExpr>(expr_val)) {
                init_val = dotvm::core::Value::from_float(std::get<FloatExpr>(expr_val).value);
            } else if (std::holds_alternative<BoolExpr>(expr_val)) {
                init_val = dotvm::core::Value::from_bool(std::get<BoolExpr>(expr_val).value);
            }
        }

        auto slot_idx = static_cast<std::uint32_t>(current_dot_->state_slots.size());
        current_dot_->state_slots.push_back(StateSlot{
            .index = slot_idx,
            .type = type,
            .name = var.name,
            .initial_value = init_val,
        });

        state_name_to_slot_[var.name] = slot_idx;

        // Also add to symbol table
        current_dot_->symbols.insert(SymbolEntry{
            .kind = SymbolKind::StateVar,
            .name = var.name,
            .ref_id = slot_idx,
            .type = type,
            .span = var.span,
        });
    }

    return {};
}

std::uint32_t IRBuilder::get_or_create_state_slot(const std::string& name, ValueType type) {
    auto it = state_name_to_slot_.find(name);
    if (it != state_name_to_slot_.end()) {
        return it->second;
    }

    auto slot_idx = static_cast<std::uint32_t>(current_dot_->state_slots.size());
    current_dot_->state_slots.push_back(StateSlot{
        .index = slot_idx,
        .type = type,
        .name = name,
        .initial_value = std::nullopt,
    });
    state_name_to_slot_[name] = slot_idx;
    return slot_idx;
}

// ============================================================================
// Expression Building
// ============================================================================

IRBuildResult<std::uint32_t> IRBuilder::build_expression(const Expression& expr) {
    return std::visit(
        [this, &expr](const auto& e) -> IRBuildResult<std::uint32_t> {
            using T = std::decay_t<decltype(e)>;

            if constexpr (std::is_same_v<T, IntegerExpr>) {
                auto val =
                    create_const_value(ValueType::Int64, dotvm::core::Value::from_int(e.value));
                emit(Instruction::make(
                    LoadConst{.result = val, .constant = dotvm::core::Value::from_int(e.value)},
                    expr.span));
                return val.id;
            } else if constexpr (std::is_same_v<T, FloatExpr>) {
                auto val =
                    create_const_value(ValueType::Float64, dotvm::core::Value::from_float(e.value));
                emit(Instruction::make(
                    LoadConst{.result = val, .constant = dotvm::core::Value::from_float(e.value)},
                    expr.span));
                return val.id;
            } else if constexpr (std::is_same_v<T, BoolExpr>) {
                auto val =
                    create_const_value(ValueType::Bool, dotvm::core::Value::from_bool(e.value));
                emit(Instruction::make(
                    LoadConst{.result = val, .constant = dotvm::core::Value::from_bool(e.value)},
                    expr.span));
                return val.id;
            } else if constexpr (std::is_same_v<T, StringExpr>) {
                // Strings become handles (placeholder for now)
                auto val = create_value(ValueType::Handle);
                emit(Instruction::make(
                    LoadConst{.result = val, .constant = dotvm::core::Value::nil()}, expr.span));
                return val.id;
            } else if constexpr (std::is_same_v<T, IdentifierExpr>) {
                return build_identifier(e);
            } else if constexpr (std::is_same_v<T, MemberExpr>) {
                return build_member_expr(e);
            } else if constexpr (std::is_same_v<T, BinaryExpr>) {
                return build_binary_expr(e);
            } else if constexpr (std::is_same_v<T, UnaryExpr>) {
                return build_unary_expr(e);
            } else if constexpr (std::is_same_v<T, CallExpr>) {
                return build_call_expr(e);
            } else if constexpr (std::is_same_v<T, GroupExpr>) {
                return build_expression(*e.inner);
            } else if constexpr (std::is_same_v<T, InterpolatedString>) {
                // TODO: Implement string interpolation
                return std::unexpected(
                    IRBuildError::unsupported("String interpolation not yet supported", expr.span));
            } else {
                return std::unexpected(
                    IRBuildError::unsupported("Unknown expression type", expr.span));
            }
        },
        expr.value);
}

IRBuildResult<std::uint32_t> IRBuilder::build_binary_expr(const BinaryExpr& expr) {
    // Build left and right operands
    auto left_result = build_expression(*expr.left);
    if (!left_result)
        return left_result;
    auto left_id = *left_result;

    auto right_result = build_expression(*expr.right);
    if (!right_result)
        return right_result;
    auto right_id = *right_result;

    // Check if it's a comparison operator
    if (is_comparison_op(expr.op)) {
        auto cmp_op = map_compare_op(expr.op);
        if (!cmp_op) {
            return std::unexpected(IRBuildError::internal("Failed to map comparison op"));
        }

        auto result = create_value(ValueType::Bool);
        emit(Instruction::make(
            Compare{.result = result, .op = *cmp_op, .left_id = left_id, .right_id = right_id},
            expr.span));
        return result.id;
    }

    // Arithmetic/logical binary op
    auto bin_op = map_binary_op(expr.op);
    if (!bin_op) {
        return std::unexpected(IRBuildError::unsupported("Unsupported binary operator", expr.span));
    }

    // Infer result type
    auto left_type = ValueType::Any;  // Would need value tracking for precision
    auto right_type = ValueType::Any;
    auto result_type = infer_binary_result_type(left_type, right_type, *bin_op);

    auto result = create_value(result_type);
    emit(Instruction::make(
        ir::BinaryOp{.result = result, .op = *bin_op, .left_id = left_id, .right_id = right_id},
        expr.span));
    return result.id;
}

IRBuildResult<std::uint32_t> IRBuilder::build_unary_expr(const UnaryExpr& expr) {
    auto operand_result = build_expression(*expr.operand);
    if (!operand_result)
        return operand_result;
    auto operand_id = *operand_result;

    auto unary_op = map_unary_op(expr.op);
    if (!unary_op) {
        return std::unexpected(IRBuildError::unsupported("Unsupported unary operator", expr.span));
    }

    auto result_type = (expr.op == dsl::AstUnaryOp::Not) ? ValueType::Bool : ValueType::Any;
    auto result = create_value(result_type);
    emit(Instruction::make(ir::UnaryOp{.result = result, .op = *unary_op, .operand_id = operand_id},
                           expr.span));
    return result.id;
}

IRBuildResult<std::uint32_t> IRBuilder::build_identifier(const IdentifierExpr& expr) {
    // Check if it's a state variable
    auto it = state_name_to_slot_.find(expr.name);
    if (it != state_name_to_slot_.end()) {
        auto slot_idx = it->second;
        auto& slot = current_dot_->state_slots[slot_idx];
        auto result = create_value(slot.type, expr.name);
        emit(Instruction::make(StateGet{.result = result, .slot_index = slot_idx}, expr.span));
        return result.id;
    }

    // Check local values
    auto local_it = local_values_.find(expr.name);
    if (local_it != local_values_.end()) {
        return local_it->second;
    }

    return std::unexpected(IRBuildError::unknown_identifier(expr.name, expr.span));
}

IRBuildResult<std::uint32_t> IRBuilder::build_member_expr(const MemberExpr& expr) {
    // Handle state.x access pattern
    if (auto* ident = std::get_if<IdentifierExpr>(&expr.object->value)) {
        if (ident->name == "state") {
            auto it = state_name_to_slot_.find(expr.member);
            if (it != state_name_to_slot_.end()) {
                auto slot_idx = it->second;
                auto& slot = current_dot_->state_slots[slot_idx];
                auto result = create_value(slot.type, expr.member);
                emit(Instruction::make(StateGet{.result = result, .slot_index = slot_idx},
                                       expr.span));
                return result.id;
            }
            return std::unexpected(
                IRBuildError::unknown_identifier("state." + expr.member, expr.span));
        }
    }

    return std::unexpected(
        IRBuildError::unsupported("Complex member access not yet supported", expr.span));
}

IRBuildResult<std::uint32_t> IRBuilder::build_call_expr(const CallExpr& expr) {
    // Build arguments
    std::vector<std::uint32_t> arg_ids;
    for (const auto& arg : expr.arguments) {
        auto arg_result = build_expression(*arg);
        if (!arg_result)
            return arg_result;
        arg_ids.push_back(*arg_result);
    }

    auto result = create_value(ValueType::Any);
    emit(Instruction::make(
        Call{.result = result, .callee = expr.callee, .arg_ids = std::move(arg_ids)}, expr.span));
    return result.id;
}

// ============================================================================
// Trigger Building
// ============================================================================

IRBuildResult<void> IRBuilder::build_trigger(const TriggerDef& trigger) {
    // Create blocks for trigger structure
    auto* cond_block = create_block("trigger_cond");
    auto* action_block = create_block("trigger_action");
    auto* cont_block = create_block("trigger_cont");

    // Jump from current block to condition block
    if (!current_block_->is_terminated()) {
        emit_terminator(Instruction::make(Jump{.target_block_id = cond_block->id}));
    }

    // Build condition in condition block
    set_current_block(cond_block);
    cond_block->predecessors.push_back(current_dot_->blocks[current_dot_->blocks.size() - 4].get());

    auto cond_result = build_expression(*trigger.condition);
    if (!cond_result) {
        return std::unexpected(cond_result.error());
    }

    // Branch based on condition
    emit_terminator(Instruction::make(Branch{
        .condition_id = *cond_result,
        .true_block_id = action_block->id,
        .false_block_id = cont_block->id,
    }));

    cond_block->successors.push_back(action_block);
    cond_block->successors.push_back(cont_block);
    action_block->predecessors.push_back(cond_block);
    cont_block->predecessors.push_back(cond_block);

    // Build actions in action block
    set_current_block(action_block);
    for (const auto& action : trigger.do_block.actions) {
        auto action_result = build_action(action);
        if (!action_result) {
            return action_result;
        }
    }

    // Jump to continuation
    if (!action_block->is_terminated()) {
        emit_terminator(Instruction::make(Jump{.target_block_id = cont_block->id}));
        action_block->successors.push_back(cont_block);
        cont_block->predecessors.push_back(action_block);
    }

    // Record trigger info
    current_dot_->triggers.push_back(TriggerInfo{
        .entry_block_id = cond_block->id,
        .condition_value_id = *cond_result,
        .action_block_id = action_block->id,
        .continuation_block_id = cont_block->id,
    });

    // Continue in continuation block
    set_current_block(cont_block);

    return {};
}

IRBuildResult<void> IRBuilder::build_action(const ActionStmt& action) {
    if (action.type == ActionStmt::Type::Assignment) {
        return build_assignment(action);
    } else {
        return build_call_action(action);
    }
}

IRBuildResult<void> IRBuilder::build_assignment(const ActionStmt& action) {
    // Build the value to assign
    auto value_result = build_expression(*action.value);
    if (!value_result) {
        return std::unexpected(value_result.error());
    }
    auto value_id = *value_result;

    // Handle compound assignments (+=, -=, etc.)
    if (action.assign_op != AssignOp::Assign) {
        // Get current value
        std::uint32_t current_id;

        // Determine target slot
        std::uint32_t slot_idx;
        if (auto* ident = std::get_if<IdentifierExpr>(&action.target->value)) {
            auto it = state_name_to_slot_.find(ident->name);
            if (it == state_name_to_slot_.end()) {
                return std::unexpected(
                    IRBuildError::unknown_identifier(ident->name, action.target->span));
            }
            slot_idx = it->second;
        } else if (auto* member = std::get_if<MemberExpr>(&action.target->value)) {
            if (auto* obj = std::get_if<IdentifierExpr>(&member->object->value)) {
                if (obj->name == "state") {
                    auto it = state_name_to_slot_.find(member->member);
                    if (it == state_name_to_slot_.end()) {
                        return std::unexpected(IRBuildError::unknown_identifier(
                            "state." + member->member, action.target->span));
                    }
                    slot_idx = it->second;
                } else {
                    return std::unexpected(IRBuildError::invalid_target(action.target->span));
                }
            } else {
                return std::unexpected(IRBuildError::invalid_target(action.target->span));
            }
        } else {
            return std::unexpected(IRBuildError::invalid_target(action.target->span));
        }

        // Load current value
        auto& slot = current_dot_->state_slots[slot_idx];
        auto current_val = create_value(slot.type);
        emit(Instruction::make(StateGet{.result = current_val, .slot_index = slot_idx},
                               action.span));
        current_id = current_val.id;

        // Compute new value
        BinaryOpKind op;
        switch (action.assign_op) {
            case AssignOp::AddAssign:
                op = BinaryOpKind::Add;
                break;
            case AssignOp::SubAssign:
                op = BinaryOpKind::Sub;
                break;
            case AssignOp::MulAssign:
                op = BinaryOpKind::Mul;
                break;
            case AssignOp::DivAssign:
                op = BinaryOpKind::Div;
                break;
            default:
                return std::unexpected(IRBuildError::internal("Invalid assign op"));
        }

        auto result = create_value(slot.type);
        emit(Instruction::make(
            ir::BinaryOp{.result = result, .op = op, .left_id = current_id, .right_id = value_id},
            action.span));
        value_id = result.id;

        // Store result
        emit(
            Instruction::make(StatePut{.slot_index = slot_idx, .value_id = value_id}, action.span));
        return {};
    }

    // Simple assignment
    if (auto* ident = std::get_if<IdentifierExpr>(&action.target->value)) {
        auto it = state_name_to_slot_.find(ident->name);
        if (it != state_name_to_slot_.end()) {
            emit(Instruction::make(StatePut{.slot_index = it->second, .value_id = value_id},
                                   action.span));
            return {};
        }
        return std::unexpected(IRBuildError::unknown_identifier(ident->name, action.target->span));
    }

    if (auto* member = std::get_if<MemberExpr>(&action.target->value)) {
        if (auto* obj = std::get_if<IdentifierExpr>(&member->object->value)) {
            if (obj->name == "state") {
                auto it = state_name_to_slot_.find(member->member);
                if (it != state_name_to_slot_.end()) {
                    emit(Instruction::make(StatePut{.slot_index = it->second, .value_id = value_id},
                                           action.span));
                    return {};
                }
                return std::unexpected(IRBuildError::unknown_identifier("state." + member->member,
                                                                        action.target->span));
            }
        }
    }

    return std::unexpected(IRBuildError::invalid_target(action.target->span));
}

IRBuildResult<void> IRBuilder::build_call_action(const ActionStmt& action) {
    // Build arguments
    std::vector<std::uint32_t> arg_ids;
    for (const auto& arg : action.arguments) {
        auto arg_result = build_expression(*arg);
        if (!arg_result) {
            return std::unexpected(arg_result.error());
        }
        arg_ids.push_back(*arg_result);
    }

    auto result = create_value(ValueType::Void);
    emit(Instruction::make(
        Call{.result = result, .callee = action.callee, .arg_ids = std::move(arg_ids)},
        action.span));

    return {};
}

// ============================================================================
// Type Inference
// ============================================================================

ValueType IRBuilder::infer_type(const Expression& expr) {
    return std::visit(
        [this](const auto& e) -> ValueType {
            using T = std::decay_t<decltype(e)>;

            if constexpr (std::is_same_v<T, IntegerExpr>) {
                return ValueType::Int64;
            } else if constexpr (std::is_same_v<T, FloatExpr>) {
                return ValueType::Float64;
            } else if constexpr (std::is_same_v<T, BoolExpr>) {
                return ValueType::Bool;
            } else if constexpr (std::is_same_v<T, StringExpr> ||
                                 std::is_same_v<T, InterpolatedString>) {
                return ValueType::Handle;
            } else if constexpr (std::is_same_v<T, IdentifierExpr>) {
                auto it = state_name_to_slot_.find(e.name);
                if (it != state_name_to_slot_.end()) {
                    return current_dot_->state_slots[it->second].type;
                }
                return ValueType::Any;
            } else if constexpr (std::is_same_v<T, BinaryExpr>) {
                if (is_comparison_op(e.op)) {
                    return ValueType::Bool;
                }
                auto left_type = infer_type(*e.left);
                auto right_type = infer_type(*e.right);
                if (left_type == ValueType::Float64 || right_type == ValueType::Float64) {
                    return ValueType::Float64;
                }
                if (left_type == ValueType::Int64 && right_type == ValueType::Int64) {
                    return ValueType::Int64;
                }
                return ValueType::Any;
            } else if constexpr (std::is_same_v<T, UnaryExpr>) {
                if (e.op == dsl::AstUnaryOp::Not) {
                    return ValueType::Bool;
                }
                return infer_type(*e.operand);
            } else if constexpr (std::is_same_v<T, GroupExpr>) {
                return infer_type(*e.inner);
            } else {
                return ValueType::Any;
            }
        },
        expr.value);
}

ValueType IRBuilder::infer_binary_result_type(ValueType left, ValueType right, BinaryOpKind op) {
    // Logical operators produce bool
    if (op == BinaryOpKind::And || op == BinaryOpKind::Or) {
        return ValueType::Bool;
    }

    // Float promotion
    if (left == ValueType::Float64 || right == ValueType::Float64) {
        return ValueType::Float64;
    }

    // Both integers
    if (left == ValueType::Int64 && right == ValueType::Int64) {
        return ValueType::Int64;
    }

    return ValueType::Any;
}

// ============================================================================
// Instruction Emission
// ============================================================================

void IRBuilder::emit(std::unique_ptr<Instruction> instr) {
    if (current_block_) {
        current_block_->instructions.push_back(std::move(instr));
    }
}

void IRBuilder::emit_terminator(std::unique_ptr<Instruction> term) {
    if (current_block_ && !current_block_->is_terminated()) {
        current_block_->terminator = std::move(term);
    }
}

}  // namespace dotvm::core::dsl::compiler
