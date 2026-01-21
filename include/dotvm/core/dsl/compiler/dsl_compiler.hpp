#pragma once

/// @file dsl_compiler.hpp
/// @brief DSL-002 Public Compiler API
///
/// Main entry point for compiling DSL source code to bytecode.
/// Orchestrates the full compilation pipeline:
/// AST → IR → Optimized IR → Linear IR → Bytecode

#include <expected>
#include <string>
#include <vector>

#include "dotvm/core/arch_types.hpp"
#include "dotvm/core/capabilities/capability.hpp"
#include "dotvm/core/dsl/ast.hpp"
#include "dotvm/core/dsl/compiler/codegen.hpp"
#include "dotvm/core/dsl/compiler/ir_builder.hpp"
#include "dotvm/core/dsl/compiler/lowerer.hpp"
#include "dotvm/core/dsl/compiler/optimizer.hpp"
#include "dotvm/core/dsl/ir/types.hpp"

namespace dotvm::core::dsl::compiler {

/// @brief Compilation error type
struct CompileError {
    enum class Stage {
        Parse,
        IRBuild,
        Optimize,
        Lower,
        Codegen,
    };

    Stage stage;
    std::string message;
    SourceSpan span;

    static CompileError from_ir_error(const IRBuildError& err) {
        return CompileError{Stage::IRBuild, err.message, err.span};
    }

    static CompileError from_codegen_error(const CodegenError& err) {
        return CompileError{Stage::Codegen, err.message, {}};
    }
};

/// @brief Compilation options
struct CompileOptions {
    Architecture arch = Architecture::Arch64;
    Optimizer::Level opt_level = Optimizer::Level::Basic;
    bool emit_debug_info = false;
    bool dump_ir = false;  ///< Print IR to stderr for debugging

    /// DSL-004: Capabilities granted for stdlib access
    capabilities::Permission granted_caps = capabilities::Permission::None;
};

/// @brief Compilation result
struct CompileResult {
    std::vector<std::uint8_t> bytecode;
    ir::CompiledModule ir;  ///< Preserved for debugging/introspection
    OptimizationStats opt_stats;
};

/// @brief Main DSL compiler class
class DslCompiler {
public:
    explicit DslCompiler(CompileOptions opts = CompileOptions{}) : opts_(std::move(opts)) {}

    /// @brief Compile a parsed DSL module to bytecode
    [[nodiscard]] std::expected<CompileResult, CompileError> compile(const DslModule& module);

    /// @brief Compile DSL source string to bytecode
    /// @note Requires parser (from DSL-001)
    [[nodiscard]] std::expected<CompileResult, CompileError>
    compile_source(std::string_view source);

private:
    CompileOptions opts_;
};

}  // namespace dotvm::core::dsl::compiler
