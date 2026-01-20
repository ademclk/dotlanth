#include <gtest/gtest.h>

#include "dotvm/core/dsl/compiler/ir_builder.hpp"
#include "dotvm/core/dsl/compiler/optimizer.hpp"
#include "dotvm/core/dsl/ir/printer.hpp"
#include "dotvm/core/dsl/parser.hpp"

using namespace dotvm::core::dsl::compiler;
using namespace dotvm::core::dsl::ir;
using dotvm::core::dsl::DslParser;

class OptimizerTest : public ::testing::Test {
protected:
    IRBuilder builder;
    Optimizer optimizer{Optimizer::Level::Basic};

    DotIR build_dot(std::string_view source) {
        auto parse_result = DslParser::parse(source);
        EXPECT_TRUE(parse_result.is_ok());
        auto ir_result = builder.build(parse_result.value());
        EXPECT_TRUE(ir_result.has_value());
        EXPECT_FALSE(ir_result->dots.empty());
        return std::move(ir_result->dots[0]);
    }
};

TEST_F(OptimizerTest, ConstantFolding) {
    // Create IR with constant expressions manually
    DotIR dot;
    dot.name = "const_fold_test";

    auto* block = new BasicBlock();
    block->id = dot.alloc_block_id();
    block->label = "entry";

    // %0 = const 2
    auto v0 = Value::make(dot.alloc_value_id(), ValueType::Int64, "a");
    block->instructions.push_back(
        Instruction::make(LoadConst{.result = v0, .constant = dotvm::core::Value::from_int(2)}));

    // %1 = const 3
    auto v1 = Value::make(dot.alloc_value_id(), ValueType::Int64, "b");
    block->instructions.push_back(
        Instruction::make(LoadConst{.result = v1, .constant = dotvm::core::Value::from_int(3)}));

    // %2 = add %0, %1 (should fold to const 5)
    auto v2 = Value::make(dot.alloc_value_id(), ValueType::Int64, "sum");
    block->instructions.push_back(Instruction::make(
        BinaryOp{.result = v2, .op = BinaryOpKind::Add, .left_id = v0.id, .right_id = v1.id}));

    block->terminator = Instruction::make(Halt{});

    dot.blocks.push_back(std::unique_ptr<BasicBlock>(block));
    dot.entry_block_id = block->id;

    // Run constant folding
    ConstantFolder folder;
    auto folded = folder.run(dot);

    EXPECT_GE(folded, 1);

    // Verify the add was folded to a constant
    bool found_folded = false;
    for (const auto& blk : dot.blocks) {
        for (const auto& instr : blk->instructions) {
            if (auto* lc = std::get_if<LoadConst>(&instr->kind)) {
                if (lc->result.id == v2.id) {
                    found_folded = true;
                    EXPECT_TRUE(lc->constant.is_integer());
                    EXPECT_EQ(lc->constant.as_integer(), 5);
                }
            }
        }
    }
    EXPECT_TRUE(found_folded);
}

TEST_F(OptimizerTest, ConstantFoldingSubtraction) {
    DotIR dot;
    dot.name = "sub_test";

    auto* block = new BasicBlock();
    block->id = dot.alloc_block_id();

    auto v0 = Value::make(dot.alloc_value_id(), ValueType::Int64);
    block->instructions.push_back(
        Instruction::make(LoadConst{.result = v0, .constant = dotvm::core::Value::from_int(10)}));

    auto v1 = Value::make(dot.alloc_value_id(), ValueType::Int64);
    block->instructions.push_back(
        Instruction::make(LoadConst{.result = v1, .constant = dotvm::core::Value::from_int(3)}));

    auto v2 = Value::make(dot.alloc_value_id(), ValueType::Int64);
    block->instructions.push_back(Instruction::make(
        BinaryOp{.result = v2, .op = BinaryOpKind::Sub, .left_id = v0.id, .right_id = v1.id}));

    block->terminator = Instruction::make(Halt{});
    dot.blocks.push_back(std::unique_ptr<BasicBlock>(block));

    ConstantFolder folder;
    folder.run(dot);

    // Check v2 is now const 7
    for (const auto& blk : dot.blocks) {
        for (const auto& instr : blk->instructions) {
            if (auto* lc = std::get_if<LoadConst>(&instr->kind)) {
                if (lc->result.id == v2.id) {
                    EXPECT_EQ(lc->constant.as_integer(), 7);
                }
            }
        }
    }
}

TEST_F(OptimizerTest, ConstantFoldingComparison) {
    DotIR dot;
    dot.name = "cmp_test";

    auto* block = new BasicBlock();
    block->id = dot.alloc_block_id();

    auto v0 = Value::make(dot.alloc_value_id(), ValueType::Int64);
    block->instructions.push_back(
        Instruction::make(LoadConst{.result = v0, .constant = dotvm::core::Value::from_int(5)}));

    auto v1 = Value::make(dot.alloc_value_id(), ValueType::Int64);
    block->instructions.push_back(
        Instruction::make(LoadConst{.result = v1, .constant = dotvm::core::Value::from_int(3)}));

    // 5 < 3 should fold to false
    auto v2 = Value::make(dot.alloc_value_id(), ValueType::Bool);
    block->instructions.push_back(Instruction::make(
        Compare{.result = v2, .op = CompareKind::Lt, .left_id = v0.id, .right_id = v1.id}));

    block->terminator = Instruction::make(Halt{});
    dot.blocks.push_back(std::unique_ptr<BasicBlock>(block));

    ConstantFolder folder;
    folder.run(dot);

    // Check v2 is now const false
    for (const auto& blk : dot.blocks) {
        for (const auto& instr : blk->instructions) {
            if (auto* lc = std::get_if<LoadConst>(&instr->kind)) {
                if (lc->result.id == v2.id) {
                    EXPECT_TRUE(lc->constant.is_bool());
                    EXPECT_FALSE(lc->constant.as_bool());
                }
            }
        }
    }
}

TEST_F(OptimizerTest, DeadCodeElimination) {
    DotIR dot;
    dot.name = "dce_test";

    auto* entry = new BasicBlock();
    entry->id = dot.alloc_block_id();
    entry->label = "entry";

    // Unused value (should be removed)
    auto v0 = Value::make(dot.alloc_value_id(), ValueType::Int64, "unused");
    entry->instructions.push_back(
        Instruction::make(LoadConst{.result = v0, .constant = dotvm::core::Value::from_int(42)}));

    // Used value
    auto v1 = Value::make(dot.alloc_value_id(), ValueType::Int64, "used");
    entry->instructions.push_back(
        Instruction::make(LoadConst{.result = v1, .constant = dotvm::core::Value::from_int(1)}));

    // StatePut uses v1 (so v1 should not be removed)
    dot.state_slots.push_back(StateSlot{0, ValueType::Int64, "x", std::nullopt});
    entry->instructions.push_back(Instruction::make(StatePut{.slot_index = 0, .value_id = v1.id}));

    entry->terminator = Instruction::make(Halt{});
    dot.blocks.push_back(std::unique_ptr<BasicBlock>(entry));
    dot.entry_block_id = entry->id;

    std::size_t initial_count = dot.blocks[0]->instructions.size();

    DeadCodeEliminator dce;
    auto removed = dce.run(dot);

    EXPECT_GE(removed, 1);
    EXPECT_LT(dot.blocks[0]->instructions.size(), initial_count);
}

TEST_F(OptimizerTest, UnreachableBlockRemoval) {
    DotIR dot;
    dot.name = "unreachable_test";

    // Entry block
    auto* entry = new BasicBlock();
    entry->id = dot.alloc_block_id();
    entry->label = "entry";
    entry->terminator = Instruction::make(Halt{});
    dot.blocks.push_back(std::unique_ptr<BasicBlock>(entry));
    dot.entry_block_id = entry->id;

    // Unreachable block (no predecessors)
    auto* dead = new BasicBlock();
    dead->id = dot.alloc_block_id();
    dead->label = "dead";
    dead->terminator = Instruction::make(Halt{});
    dot.blocks.push_back(std::unique_ptr<BasicBlock>(dead));

    EXPECT_EQ(dot.blocks.size(), 2);

    DeadCodeEliminator dce;
    dce.run(dot);

    // Dead block should be removed
    EXPECT_EQ(dot.blocks.size(), 1);
    EXPECT_EQ(dot.blocks[0]->label, "entry");
}

TEST_F(OptimizerTest, OptimizerLevel) {
    DotIR dot;
    dot.name = "level_test";

    auto* block = new BasicBlock();
    block->id = dot.alloc_block_id();

    auto v0 = Value::make(dot.alloc_value_id(), ValueType::Int64);
    block->instructions.push_back(
        Instruction::make(LoadConst{.result = v0, .constant = dotvm::core::Value::from_int(1)}));

    auto v1 = Value::make(dot.alloc_value_id(), ValueType::Int64);
    block->instructions.push_back(
        Instruction::make(LoadConst{.result = v1, .constant = dotvm::core::Value::from_int(2)}));

    auto v2 = Value::make(dot.alloc_value_id(), ValueType::Int64);
    block->instructions.push_back(Instruction::make(
        BinaryOp{.result = v2, .op = BinaryOpKind::Add, .left_id = v0.id, .right_id = v1.id}));

    block->terminator = Instruction::make(Halt{});
    dot.blocks.push_back(std::unique_ptr<BasicBlock>(block));

    // With Level::None, nothing should be optimized
    Optimizer no_opt(Optimizer::Level::None);
    auto stats = no_opt.optimize(dot);
    EXPECT_EQ(stats.constants_folded, 0);
}
