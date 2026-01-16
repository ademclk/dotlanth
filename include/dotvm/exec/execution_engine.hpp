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
#include <dotvm/exec/debug_context.hpp>
#include <dotvm/exec/dispatch_macros.hpp>

#include <cstdint>
#include <cstddef>
#include <span>
#include <vector>

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

    // =========================================================================
    // Debug API (EXEC-010)
    // =========================================================================

    /// Enable or disable debug mode
    ///
    /// When debug mode is enabled, the execution engine will check for
    /// breakpoints and invoke callbacks on debug events. This adds some
    /// overhead to execution but targets <5x slower than non-debug mode.
    ///
    /// @param enable True to enable debug mode, false to disable
    void enable_debug(bool enable = true) noexcept {
        debug_ctx_.enabled = enable;
    }

    /// Check if debug mode is enabled
    [[nodiscard]] bool debug_enabled() const noexcept {
        return debug_ctx_.enabled;
    }

    /// Set a breakpoint at the given program counter
    ///
    /// @param pc Program counter value where execution should pause
    void set_breakpoint(std::size_t pc) {
        debug_ctx_.set_breakpoint(pc);
    }

    /// Remove a breakpoint at the given program counter
    ///
    /// @param pc Program counter value to remove breakpoint from
    void remove_breakpoint(std::size_t pc) {
        debug_ctx_.remove_breakpoint(pc);
    }

    /// Clear all breakpoints
    void clear_breakpoints() {
        debug_ctx_.clear_breakpoints();
    }

    /// Check if a breakpoint exists at the given program counter
    [[nodiscard]] bool has_breakpoint(std::size_t pc) const noexcept {
        return debug_ctx_.has_breakpoint(pc);
    }

    /// Get reference to the breakpoints set
    [[nodiscard]] const std::unordered_set<std::size_t>& breakpoints() const noexcept {
        return debug_ctx_.breakpoints;
    }

    /// Set the debug callback function
    ///
    /// The callback is invoked when a debug event occurs (breakpoint hit,
    /// step completed, exception, etc.).
    ///
    /// @param callback Function to call on debug events (nullptr to clear)
    void set_debug_callback(DebugCallback callback) {
        debug_ctx_.callback = std::move(callback);
    }

    /// Execute a single instruction in stepping mode
    ///
    /// Executes one instruction and invokes the debug callback with
    /// DebugEvent::Step. Returns Interrupted to indicate pause.
    ///
    /// @return ExecResult::Interrupted after successful step,
    ///         error code on error
    [[nodiscard]] ExecResult step_into() noexcept;

    /// Continue execution until next breakpoint or halt
    ///
    /// Resumes execution from current PC until a breakpoint is hit,
    /// HALT instruction is encountered, or an error occurs.
    ///
    /// @return Execution result code
    [[nodiscard]] ExecResult continue_execution() noexcept;

    /// Inspect a register value
    ///
    /// @param idx Register index (0-255)
    /// @return Current value in the register
    [[nodiscard]] core::Value inspect_register(std::uint8_t idx) const;

    /// Inspect memory contents
    ///
    /// @param handle Memory handle from allocation
    /// @param offset Byte offset within the allocation
    /// @param size Number of bytes to read
    /// @return Vector containing the memory bytes
    [[nodiscard]] std::vector<std::uint8_t> inspect_memory(
        core::Handle handle,
        std::size_t offset,
        std::size_t size) const;

    /// Get the debug context (readonly)
    [[nodiscard]] const DebugContext& debug_context() const noexcept {
        return debug_ctx_;
    }

    // =========================================================================
    // JIT Integration (EXEC-012)
    // =========================================================================

    /// Check if JIT compilation is available
    ///
    /// @return true if JIT is enabled in the VM context
    [[nodiscard]] bool jit_available() const noexcept;

    /// Register a function for JIT profiling
    ///
    /// Call this when a new function is discovered in bytecode.
    ///
    /// @param entry_pc PC of function entry
    /// @param end_pc PC past last instruction
    /// @return Function ID for tracking
    std::uint32_t jit_register_function(
        std::size_t entry_pc,
        std::size_t end_pc
    ) noexcept;

    /// Register a loop for OSR tracking
    ///
    /// @param func_id Function containing the loop
    /// @param header_pc PC of loop header
    /// @param backedge_pc PC of backward edge
    /// @return Loop ID for tracking
    std::uint64_t jit_register_loop(
        std::uint32_t func_id,
        std::size_t header_pc,
        std::size_t backedge_pc
    ) noexcept;

    /// Check if a function has compiled code available
    ///
    /// @param entry_pc PC of function entry
    /// @return true if compiled code exists
    [[nodiscard]] bool jit_has_compiled(std::size_t entry_pc) const noexcept;

    /// Try to execute a function via JIT
    ///
    /// If compiled code is available, executes it. Otherwise falls back
    /// to interpretation.
    ///
    /// @param entry_pc PC of function entry
    /// @return ExecResult::Success if JIT executed, JitFallback if not available
    [[nodiscard]] ExecResult jit_execute(std::size_t entry_pc) noexcept;

private:
    /// Reference to VM context (registers, memory, ALU, CFI)
    core::VmContext& vm_ctx_;

    /// Lightweight execution state (code ptr, pc, halted)
    ExecutionContext exec_ctx_;

    /// Debug mode state (EXEC-010)
    DebugContext debug_ctx_;

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

    /// Debug-aware dispatch loop implementation
    ///
    /// Similar to dispatch_loop() but includes breakpoint checking.
    /// Used when debug mode is enabled.
    ///
    /// @return Execution result code
    [[nodiscard]] ExecResult dispatch_loop_debug() noexcept;

    /// Execute a single instruction (internal)
    ///
    /// @param instr The 32-bit instruction to execute
    /// @return true if execution should continue, false to halt
    [[nodiscard]] bool execute_instruction(std::uint32_t instr) noexcept;

    /// JIT profiling: record a function call
    ///
    /// Called internally when CALL is executed. May trigger JIT compilation.
    ///
    /// @param entry_pc PC of the called function
    void jit_record_call(std::size_t entry_pc) noexcept;

    /// JIT profiling: record a loop iteration
    ///
    /// Called internally at backward jumps. May trigger OSR.
    ///
    /// @param backedge_pc PC of the backward edge
    void jit_record_iteration(std::size_t backedge_pc) noexcept;

    /// Try to trigger JIT compilation for a function
    ///
    /// @param entry_pc PC of function entry
    void jit_try_compile(std::size_t entry_pc) noexcept;

    /// Bytecode as raw bytes for JIT compilation
    std::span<const std::uint8_t> bytecode_bytes_;
};

}  // namespace dotvm::exec
