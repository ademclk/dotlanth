/// @file execution_engine.cpp
/// @brief Implementation of computed-goto dispatch loop
///
/// This file contains the core dispatch loop implementation using GCC/Clang's
/// computed-goto extension for high-performance instruction dispatch.
///
/// Uses X-macros from opcode_handlers.hpp to eliminate duplication between
/// switch-based and computed-goto dispatch implementations.

#include <dotvm/exec/execution_engine.hpp>
#include <dotvm/exec/opcode_handlers.hpp>
#include <dotvm/core/instruction.hpp>
#include <dotvm/core/value.hpp>
#include <dotvm/core/exception_types.hpp>
#include <dotvm/core/exception_context.hpp>
#include <dotvm/jit/jit_context.hpp>

namespace dotvm::exec {

// ============================================================================
// ExecutionEngine Implementation
// ============================================================================

ExecutionEngine::ExecutionEngine(core::VmContext& ctx) noexcept
    : vm_ctx_{ctx}
    , exec_ctx_{}
    , debug_ctx_{}
    , const_pool_{} {}

void ExecutionEngine::reset() noexcept {
    exec_ctx_ = ExecutionContext{};
    debug_ctx_.clear();  // EXEC-010: Clear debug state on reset
    const_pool_ = {};
}

ExecResult ExecutionEngine::execute(
    const std::uint32_t* code,
    std::size_t code_size,
    std::size_t entry_point,
    std::span<const core::Value> const_pool) noexcept {

    // Extract max_instructions from VmConfig (EXEC-008)
    std::uint64_t max_instr = vm_ctx_.config().resource_limits.max_instructions;

    // Initialize execution context with limit
    exec_ctx_.reset(code, code_size, entry_point, max_instr);
    const_pool_ = const_pool;

    // Run the dispatch loop (EXEC-010: use debug-aware loop if debug enabled)
    if (debug_ctx_.enabled) [[unlikely]] {
        return dispatch_loop_debug();
    }
    return dispatch_loop();
}

ExecResult ExecutionEngine::step() noexcept {
    if (exec_ctx_.halted) {
        return exec_ctx_.error;
    }

    if (exec_ctx_.pc >= exec_ctx_.code_size) {
        exec_ctx_.halt_with_error(ExecResult::OutOfBounds);
        return ExecResult::OutOfBounds;
    }

    // Check instruction limit (EXEC-008)
    if (exec_ctx_.max_instructions > 0 &&
        exec_ctx_.instructions_executed >= exec_ctx_.max_instructions) {
        exec_ctx_.halt_with_error(ExecResult::ExecutionLimit);
        return ExecResult::ExecutionLimit;
    }

    std::uint32_t instr = exec_ctx_.code[exec_ctx_.pc++];
    ++exec_ctx_.instructions_executed;

    if (!execute_instruction(instr)) {
        return exec_ctx_.error;
    }

    return ExecResult::Success;
}

bool ExecutionEngine::execute_instruction(std::uint32_t instr) noexcept {
    // Get references to frequently accessed components
    auto& regs = vm_ctx_.registers();
    auto& alu = vm_ctx_.alu();
    auto& mem = vm_ctx_.memory();

    std::uint8_t opcode_val = static_cast<std::uint8_t>(instr >> 24);

    switch (opcode_val) {
        // =====================================================================
        // ARITHMETIC & BITWISE - Generated via X-macros
        // =====================================================================

        // Type A Binary Operations (rd = op(rs1, rs2))
        DOTVM_TYPE_A_BINARY_OPS(DOTVM_SWITCH_TYPE_A_BINARY)

        // Type A Unary Operations (rd = op(rs1))
        DOTVM_TYPE_A_UNARY_OPS(DOTVM_SWITCH_TYPE_A_UNARY)

        // Type A Comparison Operations (rd = cmp_op(rs1, rs2))
        DOTVM_TYPE_A_COMPARISON_OPS(DOTVM_SWITCH_TYPE_A_BINARY)

        // Type B Immediate Arithmetic (rd = op(rd, sign_extend(imm16)))
        DOTVM_TYPE_B_ARITH_IMM_OPS(DOTVM_SWITCH_TYPE_B_ARITH_IMM)

        // Type B Immediate Bitwise (rd = op(rd, zero_extend(imm16)))
        DOTVM_TYPE_B_BITWISE_IMM_OPS(DOTVM_SWITCH_TYPE_B_BITWISE_IMM)

        // Type B Immediate Comparison (rd = cmp_op(rd, sign_extend(imm16))) - EXEC-009
        DOTVM_TYPE_B_CMP_IMM_OPS(DOTVM_SWITCH_TYPE_B_CMP_IMM)

        // =====================================================================
        // CONTROL FLOW (0x40-0x5F) - EXEC-005
        // =====================================================================
        case opcode::JMP: {
            auto d = core::decode_type_c(instr);
            exec_ctx_.jump_relative(d.offset24 - 1);  // -1 because pc already advanced
            return true;
        }
        case opcode::JZ: {
            auto d = core::decode_type_d(instr);
            if (regs.read(d.rs).as_integer() == 0) {
                exec_ctx_.jump_relative(static_cast<std::int32_t>(d.offset16) - 1);
            }
            return true;
        }
        case opcode::JNZ: {
            auto d = core::decode_type_d(instr);
            if (regs.read(d.rs).as_integer() != 0) {
                exec_ctx_.jump_relative(static_cast<std::int32_t>(d.offset16) - 1);
            }
            return true;
        }
        case opcode::BEQ: {
            auto d = core::decode_type_a(instr);
            if (regs.read(d.rd).as_integer() == regs.read(d.rs1).as_integer()) {
                auto offset = static_cast<std::int8_t>(d.rs2);
                exec_ctx_.jump_relative(offset - 1);
            }
            return true;
        }
        case opcode::BNE: {
            auto d = core::decode_type_a(instr);
            if (regs.read(d.rd).as_integer() != regs.read(d.rs1).as_integer()) {
                auto offset = static_cast<std::int8_t>(d.rs2);
                exec_ctx_.jump_relative(offset - 1);
            }
            return true;
        }
        case opcode::BLT: {
            auto d = core::decode_type_a(instr);
            if (regs.read(d.rd).as_integer() < regs.read(d.rs1).as_integer()) {
                auto offset = static_cast<std::int8_t>(d.rs2);
                exec_ctx_.jump_relative(offset - 1);
            }
            return true;
        }
        case opcode::BLE: {
            auto d = core::decode_type_a(instr);
            if (regs.read(d.rd).as_integer() <= regs.read(d.rs1).as_integer()) {
                auto offset = static_cast<std::int8_t>(d.rs2);
                exec_ctx_.jump_relative(offset - 1);
            }
            return true;
        }
        case opcode::BGT: {
            auto d = core::decode_type_a(instr);
            if (regs.read(d.rd).as_integer() > regs.read(d.rs1).as_integer()) {
                auto offset = static_cast<std::int8_t>(d.rs2);
                exec_ctx_.jump_relative(offset - 1);
            }
            return true;
        }
        case opcode::BGE: {
            auto d = core::decode_type_a(instr);
            if (regs.read(d.rd).as_integer() >= regs.read(d.rs1).as_integer()) {
                auto offset = static_cast<std::int8_t>(d.rs2);
                exec_ctx_.jump_relative(offset - 1);
            }
            return true;
        }
        case opcode::CALL: {
            auto d = core::decode_type_c(instr);

            // EXEC-007: Save callee-saved registers R16-R31
            std::array<core::Value, core::reg_range::CALLEE_SAVED_COUNT> saved_regs;
            regs.raw().save_callee_saved(saved_regs);

            // Push call frame with return address and saved registers
            if (!vm_ctx_.call_stack().push(exec_ctx_.pc, saved_regs)) {
                exec_ctx_.halt_with_error(ExecResult::StackOverflow);
                return false;
            }

            // Backward compatibility: save return address in link register (R1)
            regs.write(1, core::Value::from_int(static_cast<std::int64_t>(exec_ctx_.pc)));

            // CFI integration: push to CFI call stack if enabled
            if (vm_ctx_.cfi_enabled()) {
                if (!vm_ctx_.cfi().push_call(static_cast<std::uint32_t>(exec_ctx_.pc))) {
                    // Roll back call stack push on CFI failure
                    (void)vm_ctx_.call_stack().pop();
                    exec_ctx_.halt_with_error(ExecResult::CfiViolation);
                    return false;
                }
            }

            exec_ctx_.jump_relative(d.offset24 - 1);
            return true;
        }
        case opcode::RET: {
            // EXEC-007: Pop call frame and restore callee-saved registers
            // IMPORTANT: Validate CFI BEFORE any state changes for atomicity

            // First, peek at call stack to check if empty
            const auto* top_frame = vm_ctx_.call_stack().top();

            if (top_frame == nullptr) {
                // Stack underflow - fall back to R1 for backward compatibility
                auto ret_addr = static_cast<std::size_t>(regs.read(1).as_integer());
                exec_ctx_.jump_to(ret_addr);
                return true;
            }

            // CFI validation BEFORE any state changes (atomicity guarantee)
            if (vm_ctx_.cfi_enabled()) {
                auto addr_opt = vm_ctx_.cfi().pop_call();
                if (!addr_opt.has_value()) {
                    exec_ctx_.halt_with_error(ExecResult::CfiViolation);
                    return false;
                }
                // Verify CFI return address matches call stack's return address
                if (static_cast<std::size_t>(*addr_opt) != top_frame->return_pc) {
                    exec_ctx_.halt_with_error(ExecResult::CfiViolation);
                    return false;
                }
            }

            // CFI validated (or disabled) - now safe to pop and modify state
            auto frame_opt = vm_ctx_.call_stack().pop();
            // We already verified non-empty via top(), so this must succeed
            const auto& frame = *frame_opt;

            // Restore callee-saved registers R16-R31
            std::span<const core::Value, core::reg_range::CALLEE_SAVED_COUNT> saved_span{
                frame.saved_regs.data(), frame.saved_regs.size()
            };
            regs.raw().restore_callee_saved(saved_span);

            // Clear local registers if specified (with overflow protection)
            // Use unsigned arithmetic to prevent uint8_t overflow
            const auto end_reg = static_cast<unsigned>(frame.base_reg) +
                                 static_cast<unsigned>(frame.local_count);
            if (end_reg <= 256) {  // Valid register range (0-255)
                for (unsigned i = 0; i < frame.local_count; ++i) {
                    regs.write(static_cast<std::uint8_t>(frame.base_reg + i),
                               core::Value::zero());
                }
            }

            exec_ctx_.jump_to(frame.return_pc);
            return true;
        }
        case opcode::HALT: {
            exec_ctx_.halt();
            return false;
        }

        // =====================================================================
        // EXCEPTION HANDLING (0x52-0x55) - EXEC-011
        // =====================================================================
        case opcode::TRY: {
            // TRY: Push exception handler frame
            // Format: [TRY][handler_offset16][catch_types8]
            auto d = core::decode_type_b(instr);
            auto handler_offset = static_cast<std::int16_t>(d.imm16);
            auto catch_types = d.rd;  // Using rd field as catch_types

            // Compute handler PC (relative to current PC, which is already advanced)
            auto handler_pc = static_cast<std::size_t>(
                static_cast<std::int64_t>(exec_ctx_.pc - 1) + handler_offset);

            // Create and push exception frame
            auto frame = core::ExceptionFrame::make(
                handler_pc,
                exec_ctx_.pc - 1,  // TRY instruction location
                vm_ctx_.call_stack().depth(),
                catch_types);

            auto& exc_ctx = vm_ctx_.exception_context();
            if (!exc_ctx.push_frame(frame)) {
                exec_ctx_.halt_with_error(ExecResult::StackOverflow);
                return false;
            }
            return true;
        }
        case opcode::CATCH: {
            // CATCH: Handler entry marker (NOP during normal execution)
            // When jumped to by THROW, the exception is available via exception_context
            return true;
        }
        case opcode::THROW: {
            // THROW: Raise an exception
            // Format: [THROW][Rtype][Rpayload][unused]
            auto d = core::decode_type_a(instr);
            auto type_val = static_cast<std::uint32_t>(regs.read(d.rd).as_integer());
            auto payload_val = static_cast<std::uint64_t>(regs.read(d.rs1).as_integer());
            auto error_code = static_cast<core::ErrorCode>(type_val);

            // Create exception
            auto exc = core::Exception::make(error_code, payload_val, exec_ctx_.pc - 1);

            auto& exc_ctx = vm_ctx_.exception_context();
            exc_ctx.set_exception(std::move(exc));

            // Invoke debug callback if enabled
            if (debug_ctx_.enabled) [[unlikely]] {
                debug_ctx_.invoke_callback(DebugEvent::Exception, exec_ctx_);
            }

            // Search for matching handler
            auto handler_idx = exc_ctx.find_handler_index(error_code);

            if (!handler_idx.has_value()) {
                // No handler found - unhandled exception
                exec_ctx_.halt_with_error(ExecResult::UnhandledException);
                return false;
            }

            // Get the handler frame
            const auto* handler_frame = exc_ctx.frame_at(*handler_idx);
            if (!handler_frame) {
                exec_ctx_.halt_with_error(ExecResult::Error);
                return false;
            }

            // Unwind call stack to the handler's depth
            auto& call_stack = vm_ctx_.call_stack();
            while (call_stack.depth() > handler_frame->stack_depth) {
                auto frame_opt = call_stack.pop();
                if (!frame_opt) break;
                // Restore callee-saved registers from the popped frame
                std::span<const core::Value, core::reg_range::CALLEE_SAVED_COUNT> saved_span{
                    frame_opt->saved_regs.data(), frame_opt->saved_regs.size()
                };
                regs.raw().restore_callee_saved(saved_span);
            }

            // Pop exception frames above the handler frame, then pop the handler itself
            exc_ctx.unwind_to(*handler_idx + 1);  // Keep frames 0..handler_idx
            (void)exc_ctx.pop_frame();  // Pop the handler frame itself

            // Jump to handler
            exec_ctx_.jump_to(handler_frame->handler_pc);
            return true;
        }
        case opcode::ENDTRY: {
            // ENDTRY: Normal exit from try block - pop exception frame
            auto& exc_ctx = vm_ctx_.exception_context();
            if (exc_ctx.empty()) {
                // Malformed bytecode - ENDTRY without TRY
                exec_ctx_.halt_with_error(ExecResult::Error);
                return false;
            }
            (void)exc_ctx.pop_frame();
            return true;
        }

        // =====================================================================
        // MEMORY LOAD/STORE (0x60-0x68) - EXEC-006
        // =====================================================================
        case opcode::LOAD8: {
            auto d = core::decode_type_m(instr);
            auto handle = regs.read(d.rs1).as_handle();
            auto offset = static_cast<std::size_t>(static_cast<std::int32_t>(d.offset8));
            auto result = mem.read<std::uint8_t>(handle, offset);
            if (result) {
                regs.write(d.rd_rs2, core::Value::from_int(static_cast<std::int64_t>(*result)));
            } else {
                exec_ctx_.halt_with_error(ExecResult::MemoryError);
                return false;
            }
            return true;
        }
        case opcode::LOAD16: {
            auto d = core::decode_type_m(instr);
            auto handle = regs.read(d.rs1).as_handle();
            auto offset = static_cast<std::size_t>(static_cast<std::int32_t>(d.offset8));
            if ((offset & 1) != 0) {
                exec_ctx_.halt_with_error(ExecResult::UnalignedAccess);
                return false;
            }
            auto result = mem.read<std::uint16_t>(handle, offset);
            if (result) {
                regs.write(d.rd_rs2, core::Value::from_int(static_cast<std::int64_t>(*result)));
            } else {
                exec_ctx_.halt_with_error(ExecResult::MemoryError);
                return false;
            }
            return true;
        }
        case opcode::LOAD32: {
            auto d = core::decode_type_m(instr);
            auto handle = regs.read(d.rs1).as_handle();
            auto offset = static_cast<std::size_t>(static_cast<std::int32_t>(d.offset8));
            if ((offset & 3) != 0) {
                exec_ctx_.halt_with_error(ExecResult::UnalignedAccess);
                return false;
            }
            auto result = mem.read<std::uint32_t>(handle, offset);
            if (result) {
                regs.write(d.rd_rs2, core::Value::from_int(static_cast<std::int64_t>(*result)));
            } else {
                exec_ctx_.halt_with_error(ExecResult::MemoryError);
                return false;
            }
            return true;
        }
        case opcode::LOAD64: {
            auto d = core::decode_type_m(instr);
            auto handle = regs.read(d.rs1).as_handle();
            auto offset = static_cast<std::size_t>(static_cast<std::int32_t>(d.offset8));
            if ((offset & 7) != 0) {
                exec_ctx_.halt_with_error(ExecResult::UnalignedAccess);
                return false;
            }
            auto result = mem.read<std::uint64_t>(handle, offset);
            if (result) {
                regs.write(d.rd_rs2, core::Value::from_raw(*result));
            } else {
                exec_ctx_.halt_with_error(ExecResult::MemoryError);
                return false;
            }
            return true;
        }
        case opcode::STORE8: {
            auto d = core::decode_type_m(instr);
            auto handle = regs.read(d.rs1).as_handle();
            auto offset = static_cast<std::size_t>(static_cast<std::int32_t>(d.offset8));
            auto value = static_cast<std::uint8_t>(regs.read(d.rd_rs2).as_integer());
            auto err = mem.write<std::uint8_t>(handle, offset, value);
            if (err != core::MemoryError::Success) {
                exec_ctx_.halt_with_error(ExecResult::MemoryError);
                return false;
            }
            return true;
        }
        case opcode::STORE16: {
            auto d = core::decode_type_m(instr);
            auto handle = regs.read(d.rs1).as_handle();
            auto offset = static_cast<std::size_t>(static_cast<std::int32_t>(d.offset8));
            if ((offset & 1) != 0) {
                exec_ctx_.halt_with_error(ExecResult::UnalignedAccess);
                return false;
            }
            auto value = static_cast<std::uint16_t>(regs.read(d.rd_rs2).as_integer());
            auto err = mem.write<std::uint16_t>(handle, offset, value);
            if (err != core::MemoryError::Success) {
                exec_ctx_.halt_with_error(ExecResult::MemoryError);
                return false;
            }
            return true;
        }
        case opcode::STORE32: {
            auto d = core::decode_type_m(instr);
            auto handle = regs.read(d.rs1).as_handle();
            auto offset = static_cast<std::size_t>(static_cast<std::int32_t>(d.offset8));
            if ((offset & 3) != 0) {
                exec_ctx_.halt_with_error(ExecResult::UnalignedAccess);
                return false;
            }
            auto value = static_cast<std::uint32_t>(regs.read(d.rd_rs2).as_integer());
            auto err = mem.write<std::uint32_t>(handle, offset, value);
            if (err != core::MemoryError::Success) {
                exec_ctx_.halt_with_error(ExecResult::MemoryError);
                return false;
            }
            return true;
        }
        case opcode::STORE64: {
            auto d = core::decode_type_m(instr);
            auto handle = regs.read(d.rs1).as_handle();
            auto offset = static_cast<std::size_t>(static_cast<std::int32_t>(d.offset8));
            if ((offset & 7) != 0) {
                exec_ctx_.halt_with_error(ExecResult::UnalignedAccess);
                return false;
            }
            auto value = regs.read(d.rd_rs2).raw_bits();
            auto err = mem.write<std::uint64_t>(handle, offset, value);
            if (err != core::MemoryError::Success) {
                exec_ctx_.halt_with_error(ExecResult::MemoryError);
                return false;
            }
            return true;
        }
        case opcode::LEA: {
            auto d = core::decode_type_m(instr);
            auto handle = regs.read(d.rs1).as_handle();
            auto ptr_result = mem.get_ptr(handle);
            if (!ptr_result) {
                exec_ctx_.halt_with_error(ExecResult::MemoryError);
                return false;
            }
            auto base_addr = static_cast<std::int64_t>(reinterpret_cast<std::uintptr_t>(*ptr_result));
            auto effective_addr = base_addr + static_cast<std::int64_t>(d.offset8);
            regs.write(d.rd_rs2, core::Value::from_int(effective_addr));
            return true;
        }

        // =====================================================================
        // DATA MOVE (0x80-0x8F)
        // =====================================================================
        case opcode::MOV: {
            auto d = core::decode_type_a(instr);
            regs.write(d.rd, regs.read(d.rs1));
            return true;
        }
        case opcode::MOVI: {
            auto d = core::decode_type_b(instr);
            regs.write(d.rd, core::Value::from_int(static_cast<std::int16_t>(d.imm16)));
            return true;
        }
        case opcode::LOADK: {
            auto d = core::decode_type_b(instr);
            auto idx = static_cast<std::size_t>(d.imm16);
            if (idx < const_pool_.size()) [[likely]] {
                regs.write(d.rd, const_pool_[idx]);
            } else {
                regs.write(d.rd, core::Value::nil());
            }
            return true;
        }
        case opcode::MOVHI: {
            auto d = core::decode_type_b(instr);
            auto current = regs.read(d.rd).as_integer();
            auto upper = static_cast<std::int64_t>(d.imm16) << 16;
            regs.write(d.rd, core::Value::from_int((current & 0xFFFF) | upper));
            return true;
        }

        // =====================================================================
        // SYSTEM (0xF0-0xFF)
        // =====================================================================
        case opcode::NOP: {
            return true;
        }
        case opcode::DEBUG: {
            // EXEC-010: Debug mode breakpoint
            if (debug_ctx_.enabled) [[unlikely]] {
                debug_ctx_.invoke_callback(DebugEvent::Break, exec_ctx_);
                exec_ctx_.halt_with_error(ExecResult::Interrupted);
                return false;
            }
            return true;  // Behaves like NOP when debug disabled
        }
        // Note: HALT moved to Control Flow section (0x5F) per EXEC-005

        default: {
            // Check for reserved opcodes
            if (core::is_reserved_opcode(opcode_val)) {
                exec_ctx_.halt_with_error(ExecResult::CfiViolation);
            } else {
                exec_ctx_.halt_with_error(ExecResult::InvalidOpcode);
            }
            return false;
        }
    }
}

// ============================================================================
// Computed-Goto Dispatch Loop
// ============================================================================

#if DOTVM_HAS_COMPUTED_GOTO

ExecResult ExecutionEngine::dispatch_loop() noexcept {
    // Declare all handler labels using GCC __label__ extension
    // NOTE: __label__ MUST be at the very beginning of the block (GCC requirement)
    __label__
        // Arithmetic (0x00-0x1F)
        op_ADD, op_SUB, op_MUL, op_DIV, op_MOD, op_NEG, op_ABS,
        op_ADDI, op_SUBI, op_MULI,
        // Bitwise (0x20-0x2F)
        op_AND, op_OR, op_XOR, op_NOT, op_SHL, op_SHR, op_SAR,
        op_ANDI, op_ORI, op_XORI,
        // Comparison (0x30-0x3F) - EXEC-009
        op_EQ, op_NE, op_LT, op_LE, op_GT, op_GE,
        op_LTU, op_LEU, op_GTU, op_GEU,
        op_TEST, op_CMPI_EQ, op_CMPI_NE, op_CMPI_LT, op_CMPI_GE,
        // Control Flow (0x40-0x5F) - EXEC-005
        op_JMP, op_JZ, op_JNZ, op_BEQ, op_BNE, op_BLT, op_BLE, op_BGT, op_BGE,
        op_CALL, op_RET, op_HALT,
        // Exception Handling (0x52-0x55) - EXEC-011
        op_TRY, op_CATCH, op_THROW, op_ENDTRY,
        // Memory Load/Store (0x60-0x68) - EXEC-006
        op_LOAD8, op_LOAD16, op_LOAD32, op_LOAD64,
        op_STORE8, op_STORE16, op_STORE32, op_STORE64,
        op_LEA,
        // Data Move (0x80-0x8F)
        op_MOV, op_MOVI, op_LOADK, op_MOVHI, op_MOVLO, op_XCHG,
        // System (0xF0-0xFF)
        op_NOP, op_BREAK, op_DEBUG, op_SYSCALL,
        // Error handlers
        op_INVALID, op_RESERVED, op_OUT_OF_BOUNDS, op_EXECUTION_LIMIT;

    // Local references for hot path (avoid repeated member access)
    auto& regs = vm_ctx_.registers();
    auto& alu = vm_ctx_.alu();
    auto& mem = vm_ctx_.memory();

    // Current instruction being executed
    std::uint32_t instr = 0;

    // Build dispatch table with label addresses
    // Note: This is computed at runtime when the function is first called
    // and cached in static storage for subsequent calls
    static void* dispatch_table[OPCODE_COUNT];
    static bool table_initialized = false;

    if (!table_initialized) [[unlikely]] {
        // Initialize all entries to invalid handler
        for (auto& entry : dispatch_table) {
            entry = &&op_INVALID;
        }

        // Arithmetic handlers (0x00-0x1F)
        dispatch_table[opcode::ADD]  = &&op_ADD;
        dispatch_table[opcode::SUB]  = &&op_SUB;
        dispatch_table[opcode::MUL]  = &&op_MUL;
        dispatch_table[opcode::DIV]  = &&op_DIV;
        dispatch_table[opcode::MOD]  = &&op_MOD;
        dispatch_table[opcode::NEG]  = &&op_NEG;
        dispatch_table[opcode::ABS]  = &&op_ABS;
        dispatch_table[opcode::ADDI] = &&op_ADDI;
        dispatch_table[opcode::SUBI] = &&op_SUBI;
        dispatch_table[opcode::MULI] = &&op_MULI;

        // Bitwise handlers (0x20-0x2F)
        dispatch_table[opcode::AND]  = &&op_AND;
        dispatch_table[opcode::OR]   = &&op_OR;
        dispatch_table[opcode::XOR]  = &&op_XOR;
        dispatch_table[opcode::NOT]  = &&op_NOT;
        dispatch_table[opcode::SHL]  = &&op_SHL;
        dispatch_table[opcode::SHR]  = &&op_SHR;
        dispatch_table[opcode::SAR]  = &&op_SAR;
        dispatch_table[opcode::ANDI] = &&op_ANDI;
        dispatch_table[opcode::ORI]  = &&op_ORI;
        dispatch_table[opcode::XORI] = &&op_XORI;

        // Comparison handlers (0x30-0x3F) - EXEC-009
        dispatch_table[opcode::EQ]  = &&op_EQ;
        dispatch_table[opcode::NE]  = &&op_NE;
        dispatch_table[opcode::LT]  = &&op_LT;
        dispatch_table[opcode::LE]  = &&op_LE;
        dispatch_table[opcode::GT]  = &&op_GT;
        dispatch_table[opcode::GE]  = &&op_GE;
        dispatch_table[opcode::LTU] = &&op_LTU;
        dispatch_table[opcode::LEU] = &&op_LEU;
        dispatch_table[opcode::GTU] = &&op_GTU;
        dispatch_table[opcode::GEU] = &&op_GEU;
        dispatch_table[opcode::TEST]    = &&op_TEST;
        dispatch_table[opcode::CMPI_EQ] = &&op_CMPI_EQ;
        dispatch_table[opcode::CMPI_NE] = &&op_CMPI_NE;
        dispatch_table[opcode::CMPI_LT] = &&op_CMPI_LT;
        dispatch_table[opcode::CMPI_GE] = &&op_CMPI_GE;

        // Control flow handlers (0x40-0x5F) - EXEC-005
        dispatch_table[opcode::JMP]  = &&op_JMP;
        dispatch_table[opcode::JZ]   = &&op_JZ;
        dispatch_table[opcode::JNZ]  = &&op_JNZ;
        dispatch_table[opcode::BEQ]  = &&op_BEQ;
        dispatch_table[opcode::BNE]  = &&op_BNE;
        dispatch_table[opcode::BLT]  = &&op_BLT;
        dispatch_table[opcode::BLE]  = &&op_BLE;
        dispatch_table[opcode::BGT]  = &&op_BGT;
        dispatch_table[opcode::BGE]  = &&op_BGE;
        dispatch_table[opcode::CALL] = &&op_CALL;
        dispatch_table[opcode::RET]  = &&op_RET;
        dispatch_table[opcode::HALT] = &&op_HALT;

        // Exception handling handlers (0x52-0x55) - EXEC-011
        dispatch_table[opcode::TRY]    = &&op_TRY;
        dispatch_table[opcode::CATCH]  = &&op_CATCH;
        dispatch_table[opcode::THROW]  = &&op_THROW;
        dispatch_table[opcode::ENDTRY] = &&op_ENDTRY;

        // Memory Load/Store handlers (0x60-0x68) - EXEC-006
        dispatch_table[opcode::LOAD8]   = &&op_LOAD8;
        dispatch_table[opcode::LOAD16]  = &&op_LOAD16;
        dispatch_table[opcode::LOAD32]  = &&op_LOAD32;
        dispatch_table[opcode::LOAD64]  = &&op_LOAD64;
        dispatch_table[opcode::STORE8]  = &&op_STORE8;
        dispatch_table[opcode::STORE16] = &&op_STORE16;
        dispatch_table[opcode::STORE32] = &&op_STORE32;
        dispatch_table[opcode::STORE64] = &&op_STORE64;
        dispatch_table[opcode::LEA]     = &&op_LEA;

        // Data move handlers (0x80-0x8F)
        dispatch_table[opcode::MOV]   = &&op_MOV;
        dispatch_table[opcode::MOVI]  = &&op_MOVI;
        dispatch_table[opcode::LOADK] = &&op_LOADK;
        dispatch_table[opcode::MOVHI] = &&op_MOVHI;
        dispatch_table[opcode::MOVLO] = &&op_MOVLO;
        dispatch_table[opcode::XCHG]  = &&op_XCHG;

        // System handlers (0xF0-0xFF)
        dispatch_table[opcode::NOP]     = &&op_NOP;
        dispatch_table[opcode::BREAK]   = &&op_BREAK;
        dispatch_table[opcode::DEBUG]   = &&op_DEBUG;   // EXEC-010
        dispatch_table[opcode::SYSCALL] = &&op_SYSCALL;
        // Note: HALT now in control flow section (0x5F) per EXEC-005

        // Mark reserved ranges
        for (std::size_t i = 0x90; i <= 0x9F; ++i) {
            dispatch_table[i] = &&op_RESERVED;
        }
        for (std::size_t i = 0xD0; i <= 0xEF; ++i) {
            dispatch_table[i] = &&op_RESERVED;
        }

        table_initialized = true;
    }

    // Start execution - fetch first instruction
    DOTVM_NEXT();

    // =========================================================================
    // ARITHMETIC HANDLERS (0x00-0x1F)
    // =========================================================================

    op_ADD: {
        auto d = core::decode_type_a(instr);
        regs.write(d.rd, alu.add(regs.read(d.rs1), regs.read(d.rs2)));
        DOTVM_NEXT();
    }

    op_SUB: {
        auto d = core::decode_type_a(instr);
        regs.write(d.rd, alu.sub(regs.read(d.rs1), regs.read(d.rs2)));
        DOTVM_NEXT();
    }

    op_MUL: {
        auto d = core::decode_type_a(instr);
        regs.write(d.rd, alu.mul(regs.read(d.rs1), regs.read(d.rs2)));
        DOTVM_NEXT();
    }

    op_DIV: {
        auto d = core::decode_type_a(instr);
        regs.write(d.rd, alu.div(regs.read(d.rs1), regs.read(d.rs2)));
        DOTVM_NEXT();
    }

    op_MOD: {
        auto d = core::decode_type_a(instr);
        regs.write(d.rd, alu.mod(regs.read(d.rs1), regs.read(d.rs2)));
        DOTVM_NEXT();
    }

    op_NEG: {
        auto d = core::decode_type_a(instr);
        regs.write(d.rd, alu.neg(regs.read(d.rs1)));
        DOTVM_NEXT();
    }

    op_ABS: {
        auto d = core::decode_type_a(instr);
        regs.write(d.rd, alu.abs(regs.read(d.rs1)));
        DOTVM_NEXT();
    }

    op_ADDI: {
        auto d = core::decode_type_b(instr);
        auto imm = core::Value::from_int(static_cast<std::int16_t>(d.imm16));
        regs.write(d.rd, alu.add(regs.read(d.rd), imm));
        DOTVM_NEXT();
    }

    op_SUBI: {
        auto d = core::decode_type_b(instr);
        auto imm = core::Value::from_int(static_cast<std::int16_t>(d.imm16));
        regs.write(d.rd, alu.sub(regs.read(d.rd), imm));
        DOTVM_NEXT();
    }

    op_MULI: {
        auto d = core::decode_type_b(instr);
        auto imm = core::Value::from_int(static_cast<std::int16_t>(d.imm16));
        regs.write(d.rd, alu.mul(regs.read(d.rd), imm));
        DOTVM_NEXT();
    }

    // =========================================================================
    // BITWISE HANDLERS (0x20-0x2F)
    // =========================================================================

    op_AND: {
        auto d = core::decode_type_a(instr);
        regs.write(d.rd, alu.bit_and(regs.read(d.rs1), regs.read(d.rs2)));
        DOTVM_NEXT();
    }

    op_OR: {
        auto d = core::decode_type_a(instr);
        regs.write(d.rd, alu.bit_or(regs.read(d.rs1), regs.read(d.rs2)));
        DOTVM_NEXT();
    }

    op_XOR: {
        auto d = core::decode_type_a(instr);
        regs.write(d.rd, alu.bit_xor(regs.read(d.rs1), regs.read(d.rs2)));
        DOTVM_NEXT();
    }

    op_NOT: {
        auto d = core::decode_type_a(instr);
        regs.write(d.rd, alu.bit_not(regs.read(d.rs1)));
        DOTVM_NEXT();
    }

    op_SHL: {
        auto d = core::decode_type_a(instr);
        regs.write(d.rd, alu.shl(regs.read(d.rs1), regs.read(d.rs2)));
        DOTVM_NEXT();
    }

    op_SHR: {
        auto d = core::decode_type_a(instr);
        regs.write(d.rd, alu.shr(regs.read(d.rs1), regs.read(d.rs2)));
        DOTVM_NEXT();
    }

    op_SAR: {
        auto d = core::decode_type_a(instr);
        regs.write(d.rd, alu.sar(regs.read(d.rs1), regs.read(d.rs2)));
        DOTVM_NEXT();
    }

    op_ANDI: {
        auto d = core::decode_type_b(instr);
        auto imm = core::Value::from_int(d.imm16);
        regs.write(d.rd, alu.bit_and(regs.read(d.rd), imm));
        DOTVM_NEXT();
    }

    op_ORI: {
        auto d = core::decode_type_b(instr);
        auto imm = core::Value::from_int(d.imm16);
        regs.write(d.rd, alu.bit_or(regs.read(d.rd), imm));
        DOTVM_NEXT();
    }

    op_XORI: {
        auto d = core::decode_type_b(instr);
        auto imm = core::Value::from_int(d.imm16);
        regs.write(d.rd, alu.bit_xor(regs.read(d.rd), imm));
        DOTVM_NEXT();
    }

    // =========================================================================
    // COMPARISON HANDLERS (0x30-0x3F)
    // =========================================================================

    op_EQ: {
        auto d = core::decode_type_a(instr);
        regs.write(d.rd, alu.cmp_eq(regs.read(d.rs1), regs.read(d.rs2)));
        DOTVM_NEXT();
    }

    op_NE: {
        auto d = core::decode_type_a(instr);
        regs.write(d.rd, alu.cmp_ne(regs.read(d.rs1), regs.read(d.rs2)));
        DOTVM_NEXT();
    }

    op_LT: {
        auto d = core::decode_type_a(instr);
        regs.write(d.rd, alu.cmp_lt(regs.read(d.rs1), regs.read(d.rs2)));
        DOTVM_NEXT();
    }

    op_LE: {
        auto d = core::decode_type_a(instr);
        regs.write(d.rd, alu.cmp_le(regs.read(d.rs1), regs.read(d.rs2)));
        DOTVM_NEXT();
    }

    op_GT: {
        auto d = core::decode_type_a(instr);
        regs.write(d.rd, alu.cmp_gt(regs.read(d.rs1), regs.read(d.rs2)));
        DOTVM_NEXT();
    }

    op_GE: {
        auto d = core::decode_type_a(instr);
        regs.write(d.rd, alu.cmp_ge(regs.read(d.rs1), regs.read(d.rs2)));
        DOTVM_NEXT();
    }

    op_LTU: {
        auto d = core::decode_type_a(instr);
        regs.write(d.rd, alu.cmp_ltu(regs.read(d.rs1), regs.read(d.rs2)));
        DOTVM_NEXT();
    }

    op_LEU: {
        auto d = core::decode_type_a(instr);
        regs.write(d.rd, alu.cmp_leu(regs.read(d.rs1), regs.read(d.rs2)));
        DOTVM_NEXT();
    }

    op_GTU: {
        auto d = core::decode_type_a(instr);
        regs.write(d.rd, alu.cmp_gtu(regs.read(d.rs1), regs.read(d.rs2)));
        DOTVM_NEXT();
    }

    op_GEU: {
        auto d = core::decode_type_a(instr);
        regs.write(d.rd, alu.cmp_geu(regs.read(d.rs1), regs.read(d.rs2)));
        DOTVM_NEXT();
    }

    op_TEST: {
        auto d = core::decode_type_a(instr);
        regs.write(d.rd, alu.test(regs.read(d.rs1), regs.read(d.rs2)));
        DOTVM_NEXT();
    }

    op_CMPI_EQ: {
        auto d = core::decode_type_b(instr);
        auto imm = core::Value::from_int(static_cast<std::int16_t>(d.imm16));
        regs.write(d.rd, alu.cmp_eq(regs.read(d.rd), imm));
        DOTVM_NEXT();
    }

    op_CMPI_NE: {
        auto d = core::decode_type_b(instr);
        auto imm = core::Value::from_int(static_cast<std::int16_t>(d.imm16));
        regs.write(d.rd, alu.cmp_ne(regs.read(d.rd), imm));
        DOTVM_NEXT();
    }

    op_CMPI_LT: {
        auto d = core::decode_type_b(instr);
        auto imm = core::Value::from_int(static_cast<std::int16_t>(d.imm16));
        regs.write(d.rd, alu.cmp_lt(regs.read(d.rd), imm));
        DOTVM_NEXT();
    }

    op_CMPI_GE: {
        auto d = core::decode_type_b(instr);
        auto imm = core::Value::from_int(static_cast<std::int16_t>(d.imm16));
        regs.write(d.rd, alu.cmp_ge(regs.read(d.rd), imm));
        DOTVM_NEXT();
    }

    // =========================================================================
    // CONTROL FLOW HANDLERS (0x40-0x5F) - EXEC-005
    // =========================================================================

    op_JMP: {
        auto d = core::decode_type_c(instr);
        exec_ctx_.jump_relative(d.offset24 - 1);
        DOTVM_NEXT();
    }

    op_JZ: {
        auto d = core::decode_type_d(instr);
        if (regs.read(d.rs).as_integer() == 0) {
            exec_ctx_.jump_relative(static_cast<std::int32_t>(d.offset16) - 1);
        }
        DOTVM_NEXT();
    }

    op_JNZ: {
        auto d = core::decode_type_d(instr);
        if (regs.read(d.rs).as_integer() != 0) {
            exec_ctx_.jump_relative(static_cast<std::int32_t>(d.offset16) - 1);
        }
        DOTVM_NEXT();
    }

    op_BEQ: {
        auto d = core::decode_type_a(instr);
        if (regs.read(d.rd).as_integer() == regs.read(d.rs1).as_integer()) {
            auto offset = static_cast<std::int8_t>(d.rs2);
            exec_ctx_.jump_relative(offset - 1);
        }
        DOTVM_NEXT();
    }

    op_BNE: {
        auto d = core::decode_type_a(instr);
        if (regs.read(d.rd).as_integer() != regs.read(d.rs1).as_integer()) {
            auto offset = static_cast<std::int8_t>(d.rs2);
            exec_ctx_.jump_relative(offset - 1);
        }
        DOTVM_NEXT();
    }

    op_BLT: {
        auto d = core::decode_type_a(instr);
        if (regs.read(d.rd).as_integer() < regs.read(d.rs1).as_integer()) {
            auto offset = static_cast<std::int8_t>(d.rs2);
            exec_ctx_.jump_relative(offset - 1);
        }
        DOTVM_NEXT();
    }

    op_BLE: {
        auto d = core::decode_type_a(instr);
        if (regs.read(d.rd).as_integer() <= regs.read(d.rs1).as_integer()) {
            auto offset = static_cast<std::int8_t>(d.rs2);
            exec_ctx_.jump_relative(offset - 1);
        }
        DOTVM_NEXT();
    }

    op_BGT: {
        auto d = core::decode_type_a(instr);
        if (regs.read(d.rd).as_integer() > regs.read(d.rs1).as_integer()) {
            auto offset = static_cast<std::int8_t>(d.rs2);
            exec_ctx_.jump_relative(offset - 1);
        }
        DOTVM_NEXT();
    }

    op_BGE: {
        auto d = core::decode_type_a(instr);
        if (regs.read(d.rd).as_integer() >= regs.read(d.rs1).as_integer()) {
            auto offset = static_cast<std::int8_t>(d.rs2);
            exec_ctx_.jump_relative(offset - 1);
        }
        DOTVM_NEXT();
    }

    op_CALL: {
        auto d = core::decode_type_c(instr);

        // EXEC-007: Save callee-saved registers R16-R31
        std::array<core::Value, core::reg_range::CALLEE_SAVED_COUNT> saved_regs;
        regs.raw().save_callee_saved(saved_regs);

        // Push call frame with return address and saved registers
        if (!vm_ctx_.call_stack().push(exec_ctx_.pc, saved_regs)) {
            DOTVM_RETURN_ERROR(ExecResult::StackOverflow);
        }

        // Backward compatibility: save return address in link register (R1)
        regs.write(1, core::Value::from_int(static_cast<std::int64_t>(exec_ctx_.pc)));

        // CFI integration: push to CFI call stack if enabled
        if (vm_ctx_.cfi_enabled()) {
            if (!vm_ctx_.cfi().push_call(static_cast<std::uint32_t>(exec_ctx_.pc))) {
                // Roll back call stack push on CFI failure
                (void)vm_ctx_.call_stack().pop();
                DOTVM_RETURN_ERROR(ExecResult::CfiViolation);
            }
        }

        exec_ctx_.jump_relative(d.offset24 - 1);
        DOTVM_NEXT();
    }

    op_RET: {
        // EXEC-007: Pop call frame and restore callee-saved registers
        // IMPORTANT: Validate CFI BEFORE any state changes for atomicity

        // First, peek at call stack to check if empty
        const auto* top_frame = vm_ctx_.call_stack().top();

        if (top_frame == nullptr) {
            // Stack underflow - fall back to R1 for backward compatibility
            auto ret_addr = static_cast<std::size_t>(regs.read(1).as_integer());
            exec_ctx_.jump_to(ret_addr);
            DOTVM_NEXT();
        }

        // CFI validation BEFORE any state changes (atomicity guarantee)
        if (vm_ctx_.cfi_enabled()) {
            auto addr_opt = vm_ctx_.cfi().pop_call();
            if (!addr_opt.has_value()) {
                DOTVM_RETURN_ERROR(ExecResult::CfiViolation);
            }
            // Verify CFI return address matches call stack's return address
            if (static_cast<std::size_t>(*addr_opt) != top_frame->return_pc) {
                DOTVM_RETURN_ERROR(ExecResult::CfiViolation);
            }
        }

        // CFI validated (or disabled) - now safe to pop and modify state
        auto frame_opt = vm_ctx_.call_stack().pop();
        // We already verified non-empty via top(), so this must succeed
        const auto& frame = *frame_opt;

        // Restore callee-saved registers R16-R31
        std::span<const core::Value, core::reg_range::CALLEE_SAVED_COUNT> saved_span{
            frame.saved_regs.data(), frame.saved_regs.size()
        };
        regs.raw().restore_callee_saved(saved_span);

        // Clear local registers if specified (with overflow protection)
        // Use unsigned arithmetic to prevent uint8_t overflow
        const auto end_reg = static_cast<unsigned>(frame.base_reg) +
                             static_cast<unsigned>(frame.local_count);
        if (end_reg <= 256) {  // Valid register range (0-255)
            for (unsigned i = 0; i < frame.local_count; ++i) {
                regs.write(static_cast<std::uint8_t>(frame.base_reg + i),
                           core::Value::zero());
            }
        }

        exec_ctx_.jump_to(frame.return_pc);
        DOTVM_NEXT();
    }

    op_HALT: {
        exec_ctx_.halt();
        return ExecResult::Success;
    }

    // =========================================================================
    // EXCEPTION HANDLING HANDLERS (0x52-0x55) - EXEC-011
    // =========================================================================

    op_TRY: {
        // TRY: Push exception handler frame
        // Format: [TRY][handler_offset16][catch_types8]
        auto d = core::decode_type_b(instr);
        auto handler_offset = static_cast<std::int16_t>(d.imm16);
        auto catch_types = d.rd;  // Using rd field as catch_types

        // Compute handler PC (relative to current PC, which is already advanced)
        auto handler_pc = static_cast<std::size_t>(
            static_cast<std::int64_t>(exec_ctx_.pc - 1) + handler_offset);

        // Create and push exception frame
        auto frame = core::ExceptionFrame::make(
            handler_pc,
            exec_ctx_.pc - 1,  // TRY instruction location
            vm_ctx_.call_stack().depth(),
            catch_types);

        auto& exc_ctx = vm_ctx_.exception_context();
        if (!exc_ctx.push_frame(frame)) [[unlikely]] {
            DOTVM_RETURN_ERROR(ExecResult::StackOverflow);
        }
        DOTVM_NEXT();
    }

    op_CATCH: {
        // CATCH: Handler entry marker (NOP during normal execution)
        // When jumped to by THROW, the exception is available via exception_context
        DOTVM_NEXT();
    }

    op_THROW: {
        // THROW: Raise an exception
        // Format: [THROW][Rtype][Rpayload][unused]
        auto d = core::decode_type_a(instr);
        auto type_val = static_cast<std::uint32_t>(regs.read(d.rd).as_integer());
        auto payload_val = static_cast<std::uint64_t>(regs.read(d.rs1).as_integer());
        auto error_code = static_cast<core::ErrorCode>(type_val);

        // Set exception directly (avoid local string object that can't cross goto)
        auto& exc_ctx = vm_ctx_.exception_context();
        exc_ctx.current_exception().type_id = error_code;
        exc_ctx.current_exception().payload = payload_val;
        exc_ctx.current_exception().throw_pc = exec_ctx_.pc - 1;
        exc_ctx.current_exception().message.clear();

        // Invoke debug callback if enabled
        if (debug_ctx_.enabled) [[unlikely]] {
            debug_ctx_.invoke_callback(DebugEvent::Exception, exec_ctx_);
        }

        // Search for matching handler
        auto handler_idx = exc_ctx.find_handler_index(error_code);

        if (!handler_idx.has_value()) [[unlikely]] {
            // No handler found - unhandled exception
            DOTVM_RETURN_ERROR(ExecResult::UnhandledException);
        }

        // Get the handler frame
        const auto* handler_frame = exc_ctx.frame_at(*handler_idx);
        if (!handler_frame) [[unlikely]] {
            DOTVM_RETURN_ERROR(ExecResult::Error);
        }

        // Store handler PC before unwinding (handler_frame may become invalid)
        std::size_t target_handler_pc = handler_frame->handler_pc;
        std::size_t target_stack_depth = handler_frame->stack_depth;
        std::size_t target_frame_idx = *handler_idx;

        // Unwind call stack to the handler's depth
        auto& call_stack = vm_ctx_.call_stack();
        while (call_stack.depth() > target_stack_depth) {
            auto frame_opt = call_stack.pop();
            if (!frame_opt) break;
            // Restore callee-saved registers from the popped frame
            std::span<const core::Value, core::reg_range::CALLEE_SAVED_COUNT> saved_span{
                frame_opt->saved_regs.data(), frame_opt->saved_regs.size()
            };
            regs.raw().restore_callee_saved(saved_span);
        }

        // Pop exception frames above the handler frame, then pop the handler itself
        exc_ctx.unwind_to(target_frame_idx + 1);  // Keep frames 0..target_frame_idx
        (void)exc_ctx.pop_frame();  // Pop the handler frame itself

        // Jump to handler
        exec_ctx_.jump_to(target_handler_pc);
        DOTVM_NEXT();
    }

    op_ENDTRY: {
        // ENDTRY: Normal exit from try block - pop exception frame
        auto& exc_ctx = vm_ctx_.exception_context();
        if (exc_ctx.empty()) [[unlikely]] {
            // Malformed bytecode - ENDTRY without TRY
            DOTVM_RETURN_ERROR(ExecResult::Error);
        }
        (void)exc_ctx.pop_frame();
        DOTVM_NEXT();
    }

    // =========================================================================
    // MEMORY LOAD/STORE HANDLERS (0x60-0x68) - EXEC-006
    // Type M format: [opcode(8)][Rd/Rs2(8)][Rs1(8)][offset8(8)]
    // =========================================================================

    op_LOAD8: {
        auto d = core::decode_type_m(instr);
        auto handle = regs.read(d.rs1).as_handle();
        auto offset = static_cast<std::size_t>(static_cast<std::int32_t>(d.offset8));
        auto result = mem.read<std::uint8_t>(handle, offset);
        if (result) [[likely]] {
            regs.write(d.rd_rs2, core::Value::from_int(static_cast<std::int64_t>(*result)));
        } else {
            DOTVM_RETURN_ERROR(ExecResult::MemoryError);
        }
        DOTVM_NEXT();
    }

    op_LOAD16: {
        auto d = core::decode_type_m(instr);
        auto handle = regs.read(d.rs1).as_handle();
        auto offset = static_cast<std::size_t>(static_cast<std::int32_t>(d.offset8));
        // Alignment check: 2-byte boundary
        if ((offset & 1) != 0) [[unlikely]] {
            DOTVM_RETURN_ERROR(ExecResult::UnalignedAccess);
        }
        auto result = mem.read<std::uint16_t>(handle, offset);
        if (result) [[likely]] {
            regs.write(d.rd_rs2, core::Value::from_int(static_cast<std::int64_t>(*result)));
        } else {
            DOTVM_RETURN_ERROR(ExecResult::MemoryError);
        }
        DOTVM_NEXT();
    }

    op_LOAD32: {
        auto d = core::decode_type_m(instr);
        auto handle = regs.read(d.rs1).as_handle();
        auto offset = static_cast<std::size_t>(static_cast<std::int32_t>(d.offset8));
        // Alignment check: 4-byte boundary
        if ((offset & 3) != 0) [[unlikely]] {
            DOTVM_RETURN_ERROR(ExecResult::UnalignedAccess);
        }
        auto result = mem.read<std::uint32_t>(handle, offset);
        if (result) [[likely]] {
            regs.write(d.rd_rs2, core::Value::from_int(static_cast<std::int64_t>(*result)));
        } else {
            DOTVM_RETURN_ERROR(ExecResult::MemoryError);
        }
        DOTVM_NEXT();
    }

    op_LOAD64: {
        auto d = core::decode_type_m(instr);
        auto handle = regs.read(d.rs1).as_handle();
        auto offset = static_cast<std::size_t>(static_cast<std::int32_t>(d.offset8));
        // Alignment check: 8-byte boundary
        if ((offset & 7) != 0) [[unlikely]] {
            DOTVM_RETURN_ERROR(ExecResult::UnalignedAccess);
        }
        auto result = mem.read<std::uint64_t>(handle, offset);
        if (result) [[likely]] {
            // Store as integer (lower 48 bits due to Value's NaN boxing)
            regs.write(d.rd_rs2, core::Value::from_int(static_cast<std::int64_t>(*result)));
        } else {
            DOTVM_RETURN_ERROR(ExecResult::MemoryError);
        }
        DOTVM_NEXT();
    }

    op_STORE8: {
        auto d = core::decode_type_m(instr);
        auto handle = regs.read(d.rs1).as_handle();
        auto offset = static_cast<std::size_t>(static_cast<std::int32_t>(d.offset8));
        auto value = static_cast<std::uint8_t>(regs.read(d.rd_rs2).as_integer());
        auto err = mem.write<std::uint8_t>(handle, offset, value);
        if (err != core::MemoryError::Success) [[unlikely]] {
            DOTVM_RETURN_ERROR(ExecResult::MemoryError);
        }
        DOTVM_NEXT();
    }

    op_STORE16: {
        auto d = core::decode_type_m(instr);
        auto handle = regs.read(d.rs1).as_handle();
        auto offset = static_cast<std::size_t>(static_cast<std::int32_t>(d.offset8));
        // Alignment check: 2-byte boundary
        if ((offset & 1) != 0) [[unlikely]] {
            DOTVM_RETURN_ERROR(ExecResult::UnalignedAccess);
        }
        auto value = static_cast<std::uint16_t>(regs.read(d.rd_rs2).as_integer());
        auto err = mem.write<std::uint16_t>(handle, offset, value);
        if (err != core::MemoryError::Success) [[unlikely]] {
            DOTVM_RETURN_ERROR(ExecResult::MemoryError);
        }
        DOTVM_NEXT();
    }

    op_STORE32: {
        auto d = core::decode_type_m(instr);
        auto handle = regs.read(d.rs1).as_handle();
        auto offset = static_cast<std::size_t>(static_cast<std::int32_t>(d.offset8));
        // Alignment check: 4-byte boundary
        if ((offset & 3) != 0) [[unlikely]] {
            DOTVM_RETURN_ERROR(ExecResult::UnalignedAccess);
        }
        auto value = static_cast<std::uint32_t>(regs.read(d.rd_rs2).as_integer());
        auto err = mem.write<std::uint32_t>(handle, offset, value);
        if (err != core::MemoryError::Success) [[unlikely]] {
            DOTVM_RETURN_ERROR(ExecResult::MemoryError);
        }
        DOTVM_NEXT();
    }

    op_STORE64: {
        auto d = core::decode_type_m(instr);
        auto handle = regs.read(d.rs1).as_handle();
        auto offset = static_cast<std::size_t>(static_cast<std::int32_t>(d.offset8));
        // Alignment check: 8-byte boundary
        if ((offset & 7) != 0) [[unlikely]] {
            DOTVM_RETURN_ERROR(ExecResult::UnalignedAccess);
        }
        // Use as_integer() to get the value, then cast to uint64_t
        // This sign-extends 48-bit integers properly
        auto value = static_cast<std::uint64_t>(regs.read(d.rd_rs2).as_integer());
        auto err = mem.write<std::uint64_t>(handle, offset, value);
        if (err != core::MemoryError::Success) [[unlikely]] {
            DOTVM_RETURN_ERROR(ExecResult::MemoryError);
        }
        DOTVM_NEXT();
    }

    op_LEA: {
        // LEA: Rd = base_address + sign_extend(offset8)
        // Computes effective address without memory access
        auto d = core::decode_type_m(instr);
        auto handle = regs.read(d.rs1).as_handle();

        // Get the base address for the handle
        auto ptr_result = mem.get_ptr(handle);
        if (!ptr_result) [[unlikely]] {
            DOTVM_RETURN_ERROR(ExecResult::MemoryError);
        }

        // Compute effective address (use signed arithmetic for proper offset handling)
        auto base_addr = static_cast<std::int64_t>(reinterpret_cast<std::uintptr_t>(*ptr_result));
        auto effective_addr = base_addr + static_cast<std::int64_t>(d.offset8);

        // Store as integer (pointer address)
        regs.write(d.rd_rs2, core::Value::from_int(effective_addr));
        DOTVM_NEXT();
    }

    // =========================================================================
    // DATA MOVE HANDLERS (0x80-0x8F)
    // =========================================================================

    op_MOV: {
        auto d = core::decode_type_a(instr);
        regs.write(d.rd, regs.read(d.rs1));
        DOTVM_NEXT();
    }

    op_MOVI: {
        auto d = core::decode_type_b(instr);
        regs.write(d.rd, core::Value::from_int(static_cast<std::int16_t>(d.imm16)));
        DOTVM_NEXT();
    }

    op_LOADK: {
        auto d = core::decode_type_b(instr);
        auto idx = static_cast<std::size_t>(d.imm16);
        if (idx < const_pool_.size()) [[likely]] {
            regs.write(d.rd, const_pool_[idx]);
        } else {
            regs.write(d.rd, core::Value::nil());
        }
        DOTVM_NEXT();
    }

    op_MOVHI: {
        auto d = core::decode_type_b(instr);
        auto current = regs.read(d.rd).as_integer();
        auto upper = static_cast<std::int64_t>(d.imm16) << 16;
        regs.write(d.rd, core::Value::from_int((current & 0xFFFF) | upper));
        DOTVM_NEXT();
    }

    op_MOVLO: {
        auto d = core::decode_type_b(instr);
        auto current = regs.read(d.rd).as_integer();
        regs.write(d.rd, core::Value::from_int((current & ~0xFFFFLL) | d.imm16));
        DOTVM_NEXT();
    }

    op_XCHG: {
        auto d = core::decode_type_a(instr);
        auto tmp = regs.read(d.rd);
        regs.write(d.rd, regs.read(d.rs1));
        regs.write(d.rs1, tmp);
        DOTVM_NEXT();
    }

    // =========================================================================
    // SYSTEM HANDLERS (0xF0-0xFF)
    // =========================================================================

    op_NOP: {
        DOTVM_NEXT();
    }

    op_BREAK: {
        // Breakpoint - return to allow debugging
        exec_ctx_.halt_with_error(ExecResult::Interrupted);
        return ExecResult::Interrupted;
    }

    op_SYSCALL: {
        // System call - not implemented yet
        DOTVM_NEXT();
    }

    op_DEBUG: {
        // EXEC-010: Debug mode breakpoint
        // When debug mode is enabled, triggers Break callback
        // When disabled, behaves like NOP
        if (debug_ctx_.enabled) [[unlikely]] {
            debug_ctx_.invoke_callback(DebugEvent::Break, exec_ctx_);
            return ExecResult::Interrupted;
        }
        DOTVM_NEXT();
    }

    // Note: op_HALT moved to control flow section (0x5F) per EXEC-005

    // =========================================================================
    // ERROR HANDLERS
    // =========================================================================

    op_INVALID: {
        exec_ctx_.halt_with_error(ExecResult::InvalidOpcode);
        return ExecResult::InvalidOpcode;
    }

    op_RESERVED: {
        exec_ctx_.halt_with_error(ExecResult::CfiViolation);
        return ExecResult::CfiViolation;
    }

    op_OUT_OF_BOUNDS: {
        exec_ctx_.halt_with_error(ExecResult::OutOfBounds);
        return ExecResult::OutOfBounds;
    }

    op_EXECUTION_LIMIT: {
        exec_ctx_.halt_with_error(ExecResult::ExecutionLimit);
        return ExecResult::ExecutionLimit;
    }
}

#else // !DOTVM_HAS_COMPUTED_GOTO

// Fallback switch-based dispatch for non-GCC/Clang compilers
ExecResult ExecutionEngine::dispatch_loop() noexcept {
    while (exec_ctx_.should_continue()) {
        // Check instruction limit before fetching (EXEC-008)
        if (exec_ctx_.max_instructions > 0 &&
            exec_ctx_.instructions_executed >= exec_ctx_.max_instructions) {
            exec_ctx_.halt_with_error(ExecResult::ExecutionLimit);
            return ExecResult::ExecutionLimit;
        }

        std::uint32_t instr = exec_ctx_.code[exec_ctx_.pc++];
        ++exec_ctx_.instructions_executed;

        if (!execute_instruction(instr)) {
            return exec_ctx_.error;
        }
    }

    if (exec_ctx_.pc >= exec_ctx_.code_size && !exec_ctx_.halted) {
        exec_ctx_.halt_with_error(ExecResult::OutOfBounds);
        return ExecResult::OutOfBounds;
    }

    return exec_ctx_.error;
}

#endif // DOTVM_HAS_COMPUTED_GOTO

// ============================================================================
// Debug API Implementation (EXEC-010)
// ============================================================================

ExecResult ExecutionEngine::step_into() noexcept {
    if (exec_ctx_.halted) {
        return exec_ctx_.error;
    }

    if (exec_ctx_.pc >= exec_ctx_.code_size) {
        exec_ctx_.halt_with_error(ExecResult::OutOfBounds);
        return ExecResult::OutOfBounds;
    }

    // Check instruction limit (EXEC-008)
    if (exec_ctx_.max_instructions > 0 &&
        exec_ctx_.instructions_executed >= exec_ctx_.max_instructions) {
        exec_ctx_.halt_with_error(ExecResult::ExecutionLimit);
        return ExecResult::ExecutionLimit;
    }

    // Execute one instruction
    std::uint32_t instr = exec_ctx_.code[exec_ctx_.pc++];
    ++exec_ctx_.instructions_executed;

    if (!execute_instruction(instr)) {
        // Invoke callback on exception
        if (debug_ctx_.enabled) {
            debug_ctx_.invoke_callback(DebugEvent::Exception, exec_ctx_);
        }
        return exec_ctx_.error;
    }

    // Invoke step callback
    if (debug_ctx_.enabled) {
        debug_ctx_.invoke_callback(DebugEvent::Step, exec_ctx_);
    }

    return ExecResult::Interrupted;
}

ExecResult ExecutionEngine::continue_execution() noexcept {
    if (exec_ctx_.halted) {
        return exec_ctx_.error;
    }

    // Use debug-aware dispatch if debug mode is enabled
    if (debug_ctx_.enabled) {
        return dispatch_loop_debug();
    }

    // Normal execution
    return dispatch_loop();
}

core::Value ExecutionEngine::inspect_register(std::uint8_t idx) const {
    return vm_ctx_.registers().read(idx);
}

std::vector<std::uint8_t> ExecutionEngine::inspect_memory(
    core::Handle handle,
    std::size_t offset,
    std::size_t size) const {

    std::vector<std::uint8_t> result;
    result.reserve(size);

    for (std::size_t i = 0; i < size; ++i) {
        auto byte_result = vm_ctx_.memory().read<std::uint8_t>(handle, offset + i);
        if (byte_result) {
            result.push_back(*byte_result);
        } else {
            break;  // Stop on memory error
        }
    }

    return result;
}

// ============================================================================
// Debug-Aware Dispatch Loop (EXEC-010)
// ============================================================================

#if DOTVM_HAS_COMPUTED_GOTO

ExecResult ExecutionEngine::dispatch_loop_debug() noexcept {
    // This is a simplified debug-aware loop that checks breakpoints
    // at each instruction. It trades some performance for flexibility.

    while (!exec_ctx_.halted && exec_ctx_.pc < exec_ctx_.code_size) {
        // Check instruction limit (EXEC-008)
        if (exec_ctx_.max_instructions > 0 &&
            exec_ctx_.instructions_executed >= exec_ctx_.max_instructions) {
            exec_ctx_.halt_with_error(ExecResult::ExecutionLimit);
            return ExecResult::ExecutionLimit;
        }

        // Check for breakpoint at current PC (before fetch)
        if (debug_ctx_.has_breakpoint(exec_ctx_.pc)) {
            debug_ctx_.invoke_callback(DebugEvent::Break, exec_ctx_);
            return ExecResult::Interrupted;
        }

        // Check for stepping mode
        if (debug_ctx_.stepping) {
            debug_ctx_.invoke_callback(DebugEvent::Step, exec_ctx_);
            debug_ctx_.stepping = false;
            return ExecResult::Interrupted;
        }

        // Fetch and execute
        std::uint32_t instr = exec_ctx_.code[exec_ctx_.pc++];
        ++exec_ctx_.instructions_executed;

        if (!execute_instruction(instr)) {
            // Invoke callback on exception
            debug_ctx_.invoke_callback(DebugEvent::Exception, exec_ctx_);
            return exec_ctx_.error;
        }
    }

    if (exec_ctx_.pc >= exec_ctx_.code_size && !exec_ctx_.halted) {
        exec_ctx_.halt_with_error(ExecResult::OutOfBounds);
        return ExecResult::OutOfBounds;
    }

    return exec_ctx_.error;
}

#else // !DOTVM_HAS_COMPUTED_GOTO

ExecResult ExecutionEngine::dispatch_loop_debug() noexcept {
    while (!exec_ctx_.halted && exec_ctx_.pc < exec_ctx_.code_size) {
        // Check instruction limit (EXEC-008)
        if (exec_ctx_.max_instructions > 0 &&
            exec_ctx_.instructions_executed >= exec_ctx_.max_instructions) {
            exec_ctx_.halt_with_error(ExecResult::ExecutionLimit);
            return ExecResult::ExecutionLimit;
        }

        // Check for breakpoint at current PC (before fetch)
        if (debug_ctx_.has_breakpoint(exec_ctx_.pc)) {
            debug_ctx_.invoke_callback(DebugEvent::Break, exec_ctx_);
            return ExecResult::Interrupted;
        }

        // Check for stepping mode
        if (debug_ctx_.stepping) {
            debug_ctx_.invoke_callback(DebugEvent::Step, exec_ctx_);
            debug_ctx_.stepping = false;
            return ExecResult::Interrupted;
        }

        // Fetch and execute
        std::uint32_t instr = exec_ctx_.code[exec_ctx_.pc++];
        ++exec_ctx_.instructions_executed;

        if (!execute_instruction(instr)) {
            // Invoke callback on exception
            debug_ctx_.invoke_callback(DebugEvent::Exception, exec_ctx_);
            return exec_ctx_.error;
        }
    }

    if (exec_ctx_.pc >= exec_ctx_.code_size && !exec_ctx_.halted) {
        exec_ctx_.halt_with_error(ExecResult::OutOfBounds);
        return ExecResult::OutOfBounds;
    }

    return exec_ctx_.error;
}

#endif // DOTVM_HAS_COMPUTED_GOTO

// ============================================================================
// JIT Integration (EXEC-012)
// ============================================================================

bool ExecutionEngine::jit_available() const noexcept {
    return vm_ctx_.jit_enabled();
}

std::uint32_t ExecutionEngine::jit_register_function(
    std::size_t entry_pc,
    std::size_t end_pc
) noexcept {
    if (auto* jit = vm_ctx_.jit_context()) {
        return jit->register_function(entry_pc, end_pc);
    }
    return 0;
}

std::uint64_t ExecutionEngine::jit_register_loop(
    std::uint32_t func_id,
    std::size_t header_pc,
    std::size_t backedge_pc
) noexcept {
    if (auto* jit = vm_ctx_.jit_context()) {
        return jit->register_loop(func_id, header_pc, backedge_pc);
    }
    return 0;
}

bool ExecutionEngine::jit_has_compiled(std::size_t entry_pc) const noexcept {
    if (auto* jit = vm_ctx_.jit_context()) {
        return jit->lookup_by_pc(entry_pc) != nullptr;
    }
    return false;
}

ExecResult ExecutionEngine::jit_execute(std::size_t entry_pc) noexcept {
    auto* jit = vm_ctx_.jit_context();
    if (!jit) [[unlikely]] {
        return ExecResult::JitFallback;
    }

    // Look up compiled code
    const auto* entry = jit->lookup_by_pc(entry_pc);
    if (!entry || !entry->is_valid()) [[unlikely]] {
        return ExecResult::JitFallback;
    }

    // Execute the compiled code
    // Pass pointer to register file and VM context
    jit->execute(entry, &vm_ctx_.registers(), &vm_ctx_);

    return ExecResult::Success;
}

void ExecutionEngine::jit_record_call(std::size_t entry_pc) noexcept {
    auto* jit = vm_ctx_.jit_context();
    if (!jit) [[unlikely]] {
        return;
    }

    // Find the function by entry PC
    auto func_id_opt = jit->find_function(entry_pc);
    if (!func_id_opt) [[unlikely]] {
        return;  // Function not registered
    }

    // Record the call and check if we should compile
    if (jit->record_call(*func_id_opt)) {
        jit_try_compile(entry_pc);
    }
}

void ExecutionEngine::jit_record_iteration(std::size_t backedge_pc) noexcept {
    auto* jit = vm_ctx_.jit_context();
    if (!jit || !jit->osr_enabled()) [[unlikely]] {
        return;
    }

    // Find the loop by backedge PC
    auto loop_id_opt = jit->find_loop(backedge_pc);
    if (!loop_id_opt) [[unlikely]] {
        return;  // Loop not registered
    }

    // Record the iteration and check if we should trigger OSR
    if (jit->record_iteration(*loop_id_opt)) {
        // Trigger OSR compilation
        // Note: For now, OSR just compiles the containing function
        (void)jit->compile_osr(*loop_id_opt, bytecode_bytes_);
    }
}

void ExecutionEngine::jit_try_compile(std::size_t entry_pc) noexcept {
    auto* jit = vm_ctx_.jit_context();
    if (!jit) [[unlikely]] {
        return;
    }

    // Find the function ID
    auto func_id_opt = jit->find_function(entry_pc);
    if (!func_id_opt) [[unlikely]] {
        return;
    }

    // Already compiled?
    if (jit->is_compiled(*func_id_opt)) {
        return;
    }

    // Try to compile
    (void)jit->compile_function(*func_id_opt, bytecode_bytes_);
}

}  // namespace dotvm::exec
