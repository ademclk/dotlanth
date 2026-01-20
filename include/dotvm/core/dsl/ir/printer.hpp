#pragma once

/// @file printer.hpp
/// @brief DSL-002 IR printer for debug output
///
/// Provides human-readable textual representation of the IR for debugging
/// and verification purposes.

#include <format>
#include <ostream>
#include <sstream>
#include <string>

#include "dotvm/core/dsl/ir/instruction.hpp"
#include "dotvm/core/dsl/ir/types.hpp"

namespace dotvm::core::dsl::ir {

/// @brief Print options for IRPrinter
struct PrintOptions {
    bool show_types = true;      ///< Show type annotations
    bool show_ids = true;        ///< Show value IDs
    bool show_spans = false;     ///< Show source locations
    std::string indent = "  ";   ///< Indentation string
};

/// @brief Printer for IR structures
class IRPrinter {
public:
    using Options = PrintOptions;

    explicit IRPrinter(std::ostream& os, Options opts = Options{})
        : os_(os), opts_(std::move(opts)) {}

    /// @brief Print a complete compiled module
    void print(const CompiledModule& module) {
        os_ << "; CompiledModule\n";
        os_ << "; dots: " << module.dots.size() << "\n";
        os_ << "; links: " << module.links.size() << "\n\n";

        for (const auto& link : module.links) {
            os_ << "link " << link.source_dot << " -> " << link.target_dot << "\n";
        }

        if (!module.links.empty()) {
            os_ << "\n";
        }

        for (const auto& dot : module.dots) {
            print(dot);
            os_ << "\n";
        }
    }

    /// @brief Print a single dot IR
    void print(const DotIR& dot) {
        os_ << "dot @" << dot.name << " {\n";

        // Print state slots
        if (!dot.state_slots.empty()) {
            os_ << opts_.indent << "; state slots\n";
            for (const auto& slot : dot.state_slots) {
                os_ << opts_.indent << "state." << slot.index << " : "
                    << to_string(slot.type) << " = \"" << slot.name << "\"";
                if (slot.initial_value) {
                    os_ << " init " << format_value(*slot.initial_value);
                }
                os_ << "\n";
            }
            os_ << "\n";
        }

        // Print triggers info
        if (!dot.triggers.empty()) {
            os_ << opts_.indent << "; triggers: " << dot.triggers.size() << "\n";
            for (std::size_t i = 0; i < dot.triggers.size(); ++i) {
                const auto& trigger = dot.triggers[i];
                os_ << opts_.indent << "; trigger[" << i << "]: entry=bb"
                    << trigger.entry_block_id << ", cond=%" << trigger.condition_value_id
                    << ", action=bb" << trigger.action_block_id
                    << ", cont=bb" << trigger.continuation_block_id << "\n";
            }
            os_ << "\n";
        }

        // Print basic blocks
        for (const auto& block : dot.blocks) {
            print(*block);
        }

        os_ << "}\n";
    }

    /// @brief Print a basic block
    void print(const BasicBlock& block) {
        os_ << "bb" << block.id;
        if (!block.label.empty()) {
            os_ << " (" << block.label << ")";
        }

        // Print predecessors
        if (!block.predecessors.empty()) {
            os_ << " ; preds: ";
            for (std::size_t i = 0; i < block.predecessors.size(); ++i) {
                if (i > 0) os_ << ", ";
                os_ << "bb" << block.predecessors[i]->id;
            }
        }
        os_ << ":\n";

        // Print phi nodes
        for (const auto& phi : block.phis) {
            print_phi(*phi);
        }

        // Print instructions
        for (const auto& instr : block.instructions) {
            print(*instr);
        }

        // Print terminator
        if (block.terminator) {
            print(*block.terminator);
        }

        os_ << "\n";
    }

    /// @brief Print a phi node
    void print_phi(const PhiNode& phi) {
        os_ << opts_.indent;
        print_value(phi.result);
        os_ << " = phi";
        if (opts_.show_types) {
            os_ << " " << to_string(phi.result.type);
        }
        os_ << " [";
        for (std::size_t i = 0; i < phi.incoming.size(); ++i) {
            if (i > 0) os_ << ", ";
            os_ << "bb" << phi.incoming[i].first << ": %"
                << phi.incoming[i].second;
        }
        os_ << "]\n";
    }

    /// @brief Print an instruction
    void print(const Instruction& instr) {
        os_ << opts_.indent;
        std::visit([this](const auto& inst) { print_instruction(inst); }, instr.kind);

        if (opts_.show_spans && instr.span.start.line > 0) {
            os_ << " ; line " << instr.span.start.line;
        }
        os_ << "\n";
    }

private:
    std::ostream& os_;
    Options opts_;

    void print_value(const Value& val) {
        os_ << "%" << val.id;
        if (opts_.show_ids && !val.name.empty()) {
            os_ << "." << val.name;
        }
    }

    std::string format_value(const dotvm::core::Value& val) {
        if (val.is_integer()) {
            return std::format("{}", val.as_integer());
        } else if (val.is_float()) {
            return std::format("{}", val.as_float());
        } else if (val.is_bool()) {
            return val.as_bool() ? "true" : "false";
        } else if (val.is_nil()) {
            return "nil";
        } else if (val.is_handle()) {
            auto h = val.as_handle();
            return std::format("handle({}, {})", h.index, h.generation);
        }
        return "?";
    }

    // Print specific instruction types
    void print_instruction(const BinaryOp& op) {
        print_value(op.result);
        os_ << " = " << to_string(op.op);
        if (opts_.show_types) {
            os_ << " " << to_string(op.result.type);
        }
        os_ << " %" << op.left_id << ", %" << op.right_id;
    }

    void print_instruction(const UnaryOp& op) {
        print_value(op.result);
        os_ << " = " << to_string(op.op);
        if (opts_.show_types) {
            os_ << " " << to_string(op.result.type);
        }
        os_ << " %" << op.operand_id;
    }

    void print_instruction(const Compare& cmp) {
        print_value(cmp.result);
        os_ << " = " << to_string(cmp.op);
        if (opts_.show_types) {
            os_ << " " << to_string(cmp.result.type);
        }
        os_ << " %" << cmp.left_id << ", %" << cmp.right_id;
    }

    void print_instruction(const LoadConst& lc) {
        print_value(lc.result);
        os_ << " = const";
        if (opts_.show_types) {
            os_ << " " << to_string(lc.result.type);
        }
        os_ << " " << format_value(lc.constant);
    }

    void print_instruction(const StateGet& sg) {
        print_value(sg.result);
        os_ << " = state.get";
        if (opts_.show_types) {
            os_ << " " << to_string(sg.result.type);
        }
        os_ << " [" << sg.slot_index << "]";
    }

    void print_instruction(const StatePut& sp) {
        os_ << "state.put [" << sp.slot_index << "], %" << sp.value_id;
    }

    void print_instruction(const Call& call) {
        if (call.result.type != ValueType::Void) {
            print_value(call.result);
            os_ << " = ";
        }
        os_ << "call @" << call.callee << "(";
        for (std::size_t i = 0; i < call.arg_ids.size(); ++i) {
            if (i > 0) os_ << ", ";
            os_ << "%" << call.arg_ids[i];
        }
        os_ << ")";
    }

    void print_instruction(const Cast& cast) {
        print_value(cast.result);
        os_ << " = cast " << to_string(cast.target_type) << " %" << cast.value_id;
    }

    void print_instruction(const Copy& copy) {
        print_value(copy.result);
        os_ << " = copy %" << copy.value_id;
    }

    void print_instruction(const Jump& jmp) {
        os_ << "jmp bb" << jmp.target_block_id;
    }

    void print_instruction(const Branch& br) {
        os_ << "br %" << br.condition_id << ", bb" << br.true_block_id
            << ", bb" << br.false_block_id;
    }

    void print_instruction(const Return& ret) {
        os_ << "ret";
        if (ret.value_id) {
            os_ << " %" << *ret.value_id;
        }
    }

    void print_instruction(const Halt& halt) {
        os_ << "halt";
        if (halt.exit_code_id) {
            os_ << " %" << *halt.exit_code_id;
        }
    }

    void print_instruction(const Unreachable&) {
        os_ << "unreachable";
    }
};

/// @brief Print IR to a string
[[nodiscard]] inline std::string print_to_string(
    const DotIR& dot,
    IRPrinter::Options opts = IRPrinter::Options{}) {
    std::ostringstream oss;
    IRPrinter printer(oss, std::move(opts));
    printer.print(dot);
    return oss.str();
}

/// @brief Print IR to a string
[[nodiscard]] inline std::string print_to_string(
    const CompiledModule& module,
    IRPrinter::Options opts = IRPrinter::Options{}) {
    std::ostringstream oss;
    IRPrinter printer(oss, std::move(opts));
    printer.print(module);
    return oss.str();
}

/// @brief Print a single instruction to a string
[[nodiscard]] inline std::string print_to_string(
    const Instruction& instr,
    IRPrinter::Options opts = IRPrinter::Options{}) {
    std::ostringstream oss;
    IRPrinter printer(oss, std::move(opts));
    printer.print(instr);
    return oss.str();
}

}  // namespace dotvm::core::dsl::ir
