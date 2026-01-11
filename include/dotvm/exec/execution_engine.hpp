#pragma once

/// @file execution_engine.hpp
/// @brief High-performance execution engine with computed-goto dispatch
///
/// This header provides the ExecutionEngine class which implements a
/// computed-goto based instruction dispatch loop for the DotLanth VM.
/// The dispatch mechanism achieves <10 cycles per instruction dispatch
/// on modern x86-64 processors.

#include <dotvm/core/vm_context.hpp>
#include <dotvm/core/instruction.hpp>
#include <dotvm/core/value.hpp>
#include <dotvm/exec/execution_context.hpp>
#include <dotvm/exec/dispatch_macros.hpp>

#include <cstdint>
#include <cstddef>
#include <span>

namespace dotvm::exec {

/// High-performance execution engine using computed-goto dispatch
///
/// The ExecutionEngine wraps a VmContext and provides efficient bytecode
/// execution through computed-goto based dispatch. Each instruction is
/// dispatched directly to its handler via a jump table, eliminating the
/// overhead of switch statements or function pointer calls.
///
/// @example
/// ```cpp
/// core::VmContext ctx;
/// exec::ExecutionEngine engine(ctx);
///
/// // Load bytecode (from file, network, etc.)
/// std::vector<std::uint32_t> code = load_bytecode();
/// std::vector<core::Value> const_pool = load_constants();
///
/// // Execute
/// auto result = engine.execute(
///     code.data(), code.size(), 0, const_pool);
///
/// if (result == exec::ExecResult::Success) {
///     // Execution completed normally
///     auto r1 = ctx.registers().read(1);
/// }
/// ```
class ExecutionEngine {
public:
    /// Construct an engine using the given VM context
    ///
    /// The engine takes a reference to a VmContext and uses it for
    /// all execution state (registers, memory, ALU). The context
    /// must outlive the engine.
    ///
    /// @param ctx VM context (registers, memory, ALU, CFI)
    explicit ExecutionEngine(core::VmContext& ctx) noexcept;

    /// Execute until HALT or error
    ///
    /// Runs the dispatch loop until a HALT instruction is encountered
    /// or an error occurs (invalid opcode, CFI violation, etc.).
    ///
    /// @param code Pointer to instruction stream (must be 4-byte aligned)
    /// @param code_size Number of instructions (NOT bytes)
    /// @param entry_point Starting instruction index
    /// @param const_pool Constant pool for LOADK instructions
    /// @return Execution result code
    [[nodiscard]] ExecResult execute(
        const std::uint32_t* code,
        std::size_t code_size,
        std::size_t entry_point,
        std::span<const core::Value> const_pool) noexcept;

    /// Execute a single instruction (single-step mode)
    ///
    /// Executes one instruction and returns. Useful for debugging
    /// and interactive execution.
    ///
    /// @return ExecResult::Success if instruction executed successfully,
    ///         error code otherwise
    [[nodiscard]] ExecResult step() noexcept;

    /// Get current execution context (readonly)
    ///
    /// Provides access to the lightweight execution state for
    /// debugging and introspection.
    [[nodiscard]] const ExecutionContext& context() const noexcept {
        return exec_ctx_;
    }

    /// Get current program counter
    [[nodiscard]] std::size_t pc() const noexcept {
        return exec_ctx_.pc;
    }

    /// Check if execution has halted
    [[nodiscard]] bool halted() const noexcept {
        return exec_ctx_.halted;
    }

    /// Get the halt reason (if halted)
    [[nodiscard]] ExecResult error() const noexcept {
        return exec_ctx_.error;
    }

    /// Get number of instructions executed
    [[nodiscard]] std::uint64_t instructions_executed() const noexcept {
        return exec_ctx_.instructions_executed;
    }

    /// Reset execution state
    ///
    /// Clears the execution context without affecting the VM context.
    /// Call before re-executing the same or different bytecode.
    void reset() noexcept;

    /// Get the underlying VM context
    [[nodiscard]] core::VmContext& vm_context() noexcept {
        return vm_ctx_;
    }

    /// Get the underlying VM context (const)
    [[nodiscard]] const core::VmContext& vm_context() const noexcept {
        return vm_ctx_;
    }

private:
    /// Reference to VM context (registers, memory, ALU, CFI)
    core::VmContext& vm_ctx_;

    /// Lightweight execution state (code ptr, pc, halted)
    ExecutionContext exec_ctx_;

    /// Constant pool for LOADK instructions
    std::span<const core::Value> const_pool_;

    /// The main dispatch loop implementation
    ///
    /// This is where the computed-goto magic happens. The function
    /// builds a static dispatch table with label addresses and uses
    /// indirect jumps for instruction dispatch.
    ///
    /// @return Execution result code
    [[nodiscard]] ExecResult dispatch_loop() noexcept;

    /// Execute a single instruction (internal)
    ///
    /// @param instr The 32-bit instruction to execute
    /// @return true if execution should continue, false to halt
    [[nodiscard]] bool execute_instruction(std::uint32_t instr) noexcept;
};

}  // namespace dotvm::exec
