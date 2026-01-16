/// @file jit_compiler.hpp
/// @brief Copy-and-patch JIT compiler
///
/// Implements the core JIT compilation logic using the copy-and-patch
/// approach: pre-compiled stencils are copied and patched with runtime
/// operand values.

#pragma once

#include "jit_cache.hpp"
#include "jit_code_buffer.hpp"
#include "jit_config.hpp"
#include "jit_profiler.hpp"
#include "jit_stencil.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <vector>

namespace dotvm::jit {

/// @brief Compilation result type
template<typename T>
using CompileResult = std::expected<T, JitStatus>;

/// @brief Represents a bytecode instruction to be compiled
struct BytecodeInstr {
    /// @brief Opcode of the instruction
    std::uint8_t opcode{0};

    /// @brief Destination register index
    std::uint8_t dst{0};

    /// @brief Source register 1 index
    std::uint8_t src1{0};

    /// @brief Source register 2 index (or immediate low byte)
    std::uint8_t src2{0};

    /// @brief Immediate value (for instructions that use it)
    std::int64_t immediate{0};

    /// @brief PC of this instruction in bytecode
    std::size_t pc{0};
};

/// @brief Information about a compiled function
struct CompiledFunction {
    /// @brief Pointer to the start of compiled code
    const std::uint8_t* code{nullptr};

    /// @brief Size of compiled code in bytes
    std::size_t code_size{0};

    /// @brief Function ID
    FunctionId function_id{0};

    /// @brief Entry PC
    std::size_t entry_pc{0};

    /// @brief End PC
    std::size_t end_pc{0};

    /// @brief Check if compilation succeeded
    [[nodiscard]] bool valid() const noexcept {
        return code != nullptr && code_size > 0;
    }
};

/// @brief Copy-and-patch JIT compiler
///
/// Compiles bytecode functions to native code using pre-compiled stencils.
/// The copy-and-patch approach:
/// 1. Copy stencil template code to the buffer
/// 2. Patch holes with actual operand values
/// 3. Repeat for each instruction
/// 4. Add prologue/epilogue
///
/// @example
/// ```cpp
/// JitCompiler compiler(config, code_buffer, stencils);
///
/// // Prepare instructions
/// std::vector<BytecodeInstr> instrs = parse_function(bytecode, entry_pc, end_pc);
///
/// // Compile
/// auto result = compiler.compile(func_id, instrs);
/// if (result) {
///     auto& fn = *result;
///     // Store in cache and execute
/// }
/// ```
class JitCompiler {
public:
    /// @brief Create a compiler with given components
    ///
    /// @param config JIT configuration
    /// @param buffer Code buffer for output
    /// @param stencils Stencil registry
    JitCompiler(
        const JitConfig& config,
        JitCodeBuffer& buffer,
        const StencilRegistry& stencils
    ) noexcept;

    /// @brief Compile a function to native code
    ///
    /// @param func_id Function ID for tracking
    /// @param instructions Bytecode instructions to compile
    /// @return Compiled function info on success, error status on failure
    [[nodiscard]] CompileResult<CompiledFunction> compile(
        FunctionId func_id,
        std::span<const BytecodeInstr> instructions
    );

    /// @brief Check if an opcode is supported for JIT compilation
    [[nodiscard]] bool supports_opcode(std::uint8_t opcode) const noexcept;

    /// @brief Check if all instructions in a function are supported
    [[nodiscard]] bool can_compile(std::span<const BytecodeInstr> instructions) const noexcept;

    /// @brief Estimate compiled code size for a function
    [[nodiscard]] std::size_t estimate_code_size(
        std::span<const BytecodeInstr> instructions
    ) const noexcept;

    /// @brief Get the stencil registry
    [[nodiscard]] const StencilRegistry& stencils() const noexcept { return stencils_; }

    /// @brief Get compilation statistics
    struct CompileStats {
        std::size_t functions_compiled{0};
        std::size_t instructions_compiled{0};
        std::size_t bytes_generated{0};
        std::size_t fallbacks_emitted{0};
    };

    [[nodiscard]] CompileStats stats() const noexcept { return stats_; }

private:
    /// @brief Emit the function prologue
    [[nodiscard]] CompileResult<std::size_t> emit_prologue(
        std::span<std::uint8_t> output,
        std::size_t frame_size
    );

    /// @brief Emit the function epilogue
    [[nodiscard]] CompileResult<std::size_t> emit_epilogue(
        std::span<std::uint8_t> output,
        std::size_t frame_size
    );

    /// @brief Emit code for a single instruction
    [[nodiscard]] CompileResult<std::size_t> emit_instruction(
        std::span<std::uint8_t> output,
        const BytecodeInstr& instr
    );

    /// @brief Emit a stencil with patched operands
    [[nodiscard]] CompileResult<std::size_t> emit_stencil(
        std::span<std::uint8_t> output,
        const Stencil& stencil,
        std::span<const std::int64_t> operands
    );

    /// @brief Patch a single hole in copied stencil code
    void patch_hole(
        std::span<std::uint8_t> code,
        const StencilHole& hole,
        std::int64_t value
    ) noexcept;

    /// @brief Calculate register file offset for a register index
    [[nodiscard]] static constexpr std::int32_t reg_offset(std::uint8_t reg) noexcept {
        return static_cast<std::int32_t>(reg) * 8;  // 8 bytes per Value
    }

    /// @brief Configuration
    const JitConfig& config_;

    /// @brief Code buffer for output
    JitCodeBuffer& buffer_;

    /// @brief Stencil registry
    const StencilRegistry& stencils_;

    /// @brief Compilation statistics
    CompileStats stats_;
};

/// @brief Parse bytecode into instructions for compilation
///
/// Converts raw bytecode bytes into a vector of BytecodeInstr
/// suitable for JIT compilation.
///
/// @param bytecode Raw bytecode bytes
/// @param start_pc Starting PC
/// @param end_pc Ending PC (exclusive)
/// @return Vector of parsed instructions
[[nodiscard]] std::vector<BytecodeInstr> parse_bytecode(
    std::span<const std::uint8_t> bytecode,
    std::size_t start_pc,
    std::size_t end_pc
);

} // namespace dotvm::jit
