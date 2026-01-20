#include "dotvm/core/dsl/compiler/dsl_compiler.hpp"

#include <iostream>

#include "dotvm/core/dsl/ir/printer.hpp"
#include "dotvm/core/dsl/parser.hpp"

namespace dotvm::core::dsl::compiler {

std::expected<CompileResult, CompileError> DslCompiler::compile(const DslModule& module) {
    CompileResult result;

    // Stage 1: Build IR from AST
    IRBuilder builder;
    auto ir_result = builder.build(module);
    if (!ir_result) {
        return std::unexpected(CompileError::from_ir_error(ir_result.error()));
    }
    result.ir = std::move(*ir_result);

    // Debug: dump IR if requested
    if (opts_.dump_ir) {
        std::cerr << "=== IR after building ===\n";
        std::cerr << ir::print_to_string(result.ir);
        std::cerr << "========================\n";
    }

    // Stage 2: Optimize IR
    Optimizer optimizer(opts_.opt_level);
    result.opt_stats = optimizer.optimize(result.ir);

    if (opts_.dump_ir && opts_.opt_level != Optimizer::Level::None) {
        std::cerr << "=== IR after optimization ===\n";
        std::cerr << ir::print_to_string(result.ir);
        std::cerr << "Constants folded: " << result.opt_stats.constants_folded << "\n";
        std::cerr << "Dead code removed: " << result.opt_stats.dead_instructions_removed << "\n";
        std::cerr << "=============================\n";
    }

    // Stage 3: Lower and generate bytecode for each dot
    std::vector<std::uint8_t> combined_bytecode;
    Lowerer lowerer;
    CodeGenerator codegen(opts_.arch);

    for (const auto& dot : result.ir.dots) {
        // Lower to linear IR
        auto linear = lowerer.lower(dot);

        // Generate bytecode
        auto code_result = codegen.generate(linear);
        if (!code_result) {
            return std::unexpected(CompileError::from_codegen_error(code_result.error()));
        }

        // Assemble to final bytecode
        auto bytecode = codegen.assemble(*code_result);

        // For now, just use the first dot's bytecode
        // Multi-dot linking would be handled here
        if (combined_bytecode.empty()) {
            combined_bytecode = std::move(bytecode);
        }
    }

    result.bytecode = std::move(combined_bytecode);

    return result;
}

std::expected<CompileResult, CompileError>
DslCompiler::compile_source(std::string_view source) {
    // Parse source
    auto parse_result = DslParser::parse(source);
    if (!parse_result.is_ok()) {
        // Convert parse errors to compile error
        const auto& error_list = parse_result.error();
        std::string msg;
        for (const auto& err : error_list) {
            if (!msg.empty()) msg += "; ";
            msg += err.message;
        }
        return std::unexpected(CompileError{
            CompileError::Stage::Parse,
            msg,
            error_list.empty() ? SourceSpan{} : error_list[0].span,
        });
    }

    return compile(parse_result.value());
}

}  // namespace dotvm::core::dsl::compiler
