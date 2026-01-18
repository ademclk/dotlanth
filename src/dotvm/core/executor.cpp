/// @file executor.cpp
/// @brief Instruction executor implementation

#include <bit>
#include <cmath>
#include <limits>

#include <dotvm/core/arch_config.hpp>
#include <dotvm/core/executor.hpp>

namespace dotvm::core {

// ============================================================================
// ArithmeticExecutor Implementation
// ============================================================================

StepResult ArithmeticExecutor::execute_type_a(const DecodedTypeA& decoded) noexcept {
    const auto rs1_val = ctx_.registers().read(decoded.rs1);
    const auto rs2_val = ctx_.registers().read(decoded.rs2);

    switch (decoded.opcode) {
        case opcode::ADD:
            return add_op(decoded.rd, rs1_val, rs2_val);

        case opcode::SUB:
            return sub_op(decoded.rd, rs1_val, rs2_val);

        case opcode::MUL:
            return mul_op(decoded.rd, rs1_val, rs2_val);

        case opcode::DIV:
            return div_op(decoded.rd, rs1_val, rs2_val);

        case opcode::MOD:
            return mod_op(decoded.rd, rs1_val, rs2_val);

        case opcode::NEG:
            return neg_op(decoded.rd, rs1_val);

        default:
            return StepResult::make_error(ExecutionError::InvalidOpcode);
    }
}

StepResult ArithmeticExecutor::execute_type_b(const DecodedTypeB& decoded) noexcept {
    // Sign-extend the 16-bit immediate to 64-bit
    const auto imm = sign_extend_imm16(decoded.imm16);
    const auto imm_val = ctx_.make_int(imm);

    // Accumulator style: Rd = Rd OP imm
    const auto rd_val = ctx_.registers().read(decoded.rd);

    switch (decoded.opcode) {
        case opcode::ADDI:
            return add_op(decoded.rd, rd_val, imm_val);

        case opcode::SUBI:
            return sub_op(decoded.rd, rd_val, imm_val);

        case opcode::MULI:
            return mul_op(decoded.rd, rd_val, imm_val);

        default:
            return StepResult::make_error(ExecutionError::InvalidOpcode);
    }
}

// -----------------------------------------------------------------------------
// Arithmetic Operations with Overflow Detection
// -----------------------------------------------------------------------------

StepResult ArithmeticExecutor::add_op(std::uint8_t rd, Value a, Value b) noexcept {
    const auto va = a.as_integer();
    const auto vb = b.as_integer();

    if (ctx_.config().strict_overflow) [[unlikely]] {
        std::int64_t result{};
        if (__builtin_add_overflow(va, vb, &result)) [[unlikely]] {
            // 64-bit overflow occurred
            return write_overflow(rd, ctx_.alu().add(a, b));
        }

        // For Arch32, also check if result fits in 32-bit signed range
        if (ctx_.is_arch32()) [[unlikely]] {
            if (result < std::numeric_limits<std::int32_t>::min() ||
                result > std::numeric_limits<std::int32_t>::max()) [[unlikely]] {
                return write_overflow(rd, ctx_.alu().add(a, b));
            }
        }

        return write_success(rd, ctx_.make_int(result));
    }

    // Non-strict mode: use ALU's wrap-around behavior
    return write_success(rd, ctx_.alu().add(a, b));
}

StepResult ArithmeticExecutor::sub_op(std::uint8_t rd, Value a, Value b) noexcept {
    const auto va = a.as_integer();
    const auto vb = b.as_integer();

    if (ctx_.config().strict_overflow) [[unlikely]] {
        std::int64_t result{};
        if (__builtin_sub_overflow(va, vb, &result)) [[unlikely]] {
            // 64-bit overflow occurred
            return write_overflow(rd, ctx_.alu().sub(a, b));
        }

        // For Arch32, also check if result fits in 32-bit signed range
        if (ctx_.is_arch32()) [[unlikely]] {
            if (result < std::numeric_limits<std::int32_t>::min() ||
                result > std::numeric_limits<std::int32_t>::max()) [[unlikely]] {
                return write_overflow(rd, ctx_.alu().sub(a, b));
            }
        }

        return write_success(rd, ctx_.make_int(result));
    }

    return write_success(rd, ctx_.alu().sub(a, b));
}

StepResult ArithmeticExecutor::mul_op(std::uint8_t rd, Value a, Value b) noexcept {
    const auto va = a.as_integer();
    const auto vb = b.as_integer();

    if (ctx_.config().strict_overflow) [[unlikely]] {
        std::int64_t result{};
        if (__builtin_mul_overflow(va, vb, &result)) [[unlikely]] {
            // 64-bit overflow occurred
            return write_overflow(rd, ctx_.alu().mul(a, b));
        }

        // For Arch32, also check if result fits in 32-bit signed range
        if (ctx_.is_arch32()) [[unlikely]] {
            if (result < std::numeric_limits<std::int32_t>::min() ||
                result > std::numeric_limits<std::int32_t>::max()) [[unlikely]] {
                return write_overflow(rd, ctx_.alu().mul(a, b));
            }
        }

        return write_success(rd, ctx_.make_int(result));
    }

    return write_success(rd, ctx_.alu().mul(a, b));
}

StepResult ArithmeticExecutor::div_op(std::uint8_t rd, Value a, Value b) noexcept {
    const auto divisor = b.as_integer();

    if (divisor == 0) [[unlikely]] {
        // Division by zero: write 0 to rd
        ctx_.registers().write(rd, Value::from_int(0));
        if (ctx_.config().strict_overflow) {
            return StepResult::make_error(ExecutionError::DivisionByZero);
        }
        return StepResult::success();
    }

    // Check for overflow: INT64_MIN / -1 overflows
    const auto dividend = a.as_integer();
    if (ctx_.config().strict_overflow) [[unlikely]] {
        if (dividend == std::numeric_limits<std::int64_t>::min() && divisor == -1) [[unlikely]] {
            return write_overflow(rd, ctx_.alu().div(a, b));
        }
    }

    return write_success(rd, ctx_.alu().div(a, b));
}

StepResult ArithmeticExecutor::mod_op(std::uint8_t rd, Value a, Value b) noexcept {
    const auto divisor = b.as_integer();

    if (divisor == 0) [[unlikely]] {
        ctx_.registers().write(rd, Value::from_int(0));
        if (ctx_.config().strict_overflow) {
            return StepResult::make_error(ExecutionError::DivisionByZero);
        }
        return StepResult::success();
    }

    return write_success(rd, ctx_.alu().mod(a, b));
}

StepResult ArithmeticExecutor::neg_op(std::uint8_t rd, Value a) noexcept {
    const auto va = a.as_integer();

    // NEG overflow: negating INT64_MIN
    if (ctx_.config().strict_overflow) [[unlikely]] {
        if (va == std::numeric_limits<std::int64_t>::min()) [[unlikely]] {
            return write_overflow(rd, ctx_.alu().neg(a));
        }
    }

    return write_success(rd, ctx_.alu().neg(a));
}

// ============================================================================
// FloatingPointExecutor Implementation
// ============================================================================

StepResult FloatingPointExecutor::execute_type_a(const DecodedTypeA& decoded) noexcept {
    const auto rs1_val = ctx_.registers().read(decoded.rs1);
    const auto rs2_val = ctx_.registers().read(decoded.rs2);

    switch (decoded.opcode) {
        case opcode::FADD:
            return fadd_op(decoded.rd, rs1_val, rs2_val);

        case opcode::FSUB:
            return fsub_op(decoded.rd, rs1_val, rs2_val);

        case opcode::FMUL:
            return fmul_op(decoded.rd, rs1_val, rs2_val);

        case opcode::FDIV:
            return fdiv_op(decoded.rd, rs1_val, rs2_val);

        case opcode::FNEG:
            return fneg_op(decoded.rd, rs1_val);

        case opcode::FSQRT:
            return fsqrt_op(decoded.rd, rs1_val);

        case opcode::FCMP:
            return fcmp_op(rs1_val, rs2_val);

        case opcode::F2I:
            return f2i_op(decoded.rd, rs1_val);

        case opcode::I2F:
            return i2f_op(decoded.rd, rs1_val);

        default:
            return StepResult::make_error(ExecutionError::InvalidOpcode);
    }
}

// -----------------------------------------------------------------------------
// Helper Functions
// -----------------------------------------------------------------------------

double FloatingPointExecutor::get_float(Value v) const noexcept {
    if (v.is_float()) [[likely]] {
        return v.as_float();
    }
    if (v.is_integer()) {
        return static_cast<double>(v.as_integer());
    }
    // Non-numeric types return NaN
    return std::numeric_limits<double>::quiet_NaN();
}

StepResult FloatingPointExecutor::write_float(std::uint8_t rd, double result) noexcept {
    ctx_.registers().write(rd, Value::from_float(result));
    return StepResult::success();
}

StepResult FloatingPointExecutor::write_fp_invalid(std::uint8_t rd, double result) noexcept {
    ctx_.registers().write(rd, Value::from_float(result));
    return StepResult::make_error(ExecutionError::FloatingPointInvalid);
}

StepResult FloatingPointExecutor::write_conversion_overflow(std::uint8_t rd,
                                                            std::int64_t result) noexcept {
    ctx_.registers().write(rd, ctx_.make_int(result));
    return StepResult::make_error(ExecutionError::ConversionOverflow);
}

// -----------------------------------------------------------------------------
// Binary Floating-Point Operations
// -----------------------------------------------------------------------------

StepResult FloatingPointExecutor::fadd_op(std::uint8_t rd, Value a, Value b) noexcept {
    const double fa = get_float(a);
    const double fb = get_float(b);
    // IEEE 754 semantics: NaN propagation and Inf handling are automatic
    return write_float(rd, fa + fb);
}

StepResult FloatingPointExecutor::fsub_op(std::uint8_t rd, Value a, Value b) noexcept {
    const double fa = get_float(a);
    const double fb = get_float(b);
    return write_float(rd, fa - fb);
}

StepResult FloatingPointExecutor::fmul_op(std::uint8_t rd, Value a, Value b) noexcept {
    const double fa = get_float(a);
    const double fb = get_float(b);
    return write_float(rd, fa * fb);
}

StepResult FloatingPointExecutor::fdiv_op(std::uint8_t rd, Value a, Value b) noexcept {
    const double fa = get_float(a);
    const double fb = get_float(b);
    // IEEE 754: division by zero produces +/-Inf based on dividend sign
    // 0/0 produces NaN - this is all handled automatically
    return write_float(rd, fa / fb);
}

StepResult FloatingPointExecutor::fcmp_op(Value a, Value b) noexcept {
    const double fa = get_float(a);
    const double fb = get_float(b);

    // Use C++20 three-way comparison which correctly handles NaN
    // Returns std::partial_ordering::unordered if either operand is NaN
    const auto cmp = fa <=> fb;
    state_.fp_flags.set_from_ordering(cmp);

    return StepResult::success();
}

// -----------------------------------------------------------------------------
// Unary Floating-Point Operations
// -----------------------------------------------------------------------------

StepResult FloatingPointExecutor::fneg_op(std::uint8_t rd, Value a) noexcept {
    const double fa = get_float(a);
    // Negation works correctly for NaN (preserves NaN), Inf (negates), and -0.0
    return write_float(rd, -fa);
}

StepResult FloatingPointExecutor::fsqrt_op(std::uint8_t rd, Value a) noexcept {
    const double fa = get_float(a);
    const double result = std::sqrt(fa);

    // Check for invalid operation: sqrt of negative number (excluding -0.0)
    // std::sqrt returns NaN for negative inputs
    if (std::isnan(result) && !std::isnan(fa) && fa < 0.0) [[unlikely]] {
        ++state_.fp_invalid_count;
        if (ctx_.config().strict_overflow) {
            return write_fp_invalid(rd, result);
        }
    }

    return write_float(rd, result);
}

// -----------------------------------------------------------------------------
// Type Conversion Operations
// -----------------------------------------------------------------------------

StepResult FloatingPointExecutor::f2i_op(std::uint8_t rd, Value a) noexcept {
    const double fa = get_float(a);
    std::int64_t result;

    // Get architecture-specific integer limits
    // Arch32: 32-bit signed, Arch64: 48-bit signed (NaN-boxing)
    const std::int64_t int_max =
        ctx_.is_arch32() ? static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::max())
                         : arch_config::INT48_MAX;
    const std::int64_t int_min =
        ctx_.is_arch32() ? static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::min())
                         : arch_config::INT48_MIN;

    // Handle special cases with saturation semantics
    if (std::isnan(fa)) [[unlikely]] {
        // NaN converts to 0
        result = 0;
    } else if (fa >= static_cast<double>(int_max)) [[unlikely]] {
        // Positive overflow: saturate to architecture max
        result = int_max;
        if (ctx_.config().strict_overflow) {
            return write_conversion_overflow(rd, result);
        }
    } else if (fa <= static_cast<double>(int_min)) [[unlikely]] {
        // Negative overflow: saturate to architecture min
        result = int_min;
        if (ctx_.config().strict_overflow) {
            return write_conversion_overflow(rd, result);
        }
    } else {
        // Normal truncation toward zero
        result = static_cast<std::int64_t>(fa);
    }

    ctx_.registers().write(rd, ctx_.make_int(result));
    return StepResult::success();
}

StepResult FloatingPointExecutor::i2f_op(std::uint8_t rd, Value a) noexcept {
    std::int64_t ia;

    if (a.is_integer()) [[likely]] {
        ia = a.as_integer();
    } else if (a.is_float()) {
        // If given a float, convert to int first then back to float
        // This provides consistent behavior
        ia = static_cast<std::int64_t>(a.as_float());
    } else {
        // Non-numeric types become 0
        ia = 0;
    }

    // Note: Large integers (> 2^53) may lose precision
    return write_float(rd, static_cast<double>(ia));
}

// ============================================================================
// Executor Implementation
// ============================================================================

StepResult Executor::step() noexcept {
    // Validate PC before fetching
    if (auto result = validate_pc(); !is_success(result.err)) [[unlikely]] {
        return result;
    }

    // Fetch instruction
    const auto instr = fetch();

    // Dispatch and execute
    auto result = dispatch(instr);

    // Update state
    ++state_.instructions_executed;

    // Update PC
    if (result.should_halt) {
        state_.halted = true;
    } else if (result.next_pc != 0) {
        // Jump to absolute address
        state_.pc = result.next_pc;
    } else {
        // Advance to next instruction
        state_.pc += INSTRUCTION_SIZE;
    }

    // Track errors for diagnostics
    if (result.err == ExecutionError::IntegerOverflow) {
        ++state_.overflow_count;
    } else if (result.err == ExecutionError::DivisionByZero) {
        ++state_.div_zero_count;
    }

    state_.last_error = result.err;
    return result;
}

ExecutionError Executor::run(std::uint64_t max_instructions) noexcept {
    const bool has_limit = (max_instructions > 0);

    while (!state_.halted) {
        // Check instruction limit
        if (has_limit && state_.instructions_executed >= max_instructions) [[unlikely]] {
            state_.last_error = ExecutionError::InstructionLimitExceeded;
            return ExecutionError::InstructionLimitExceeded;
        }

        auto result = step();

        // Fatal error stops execution
        if (is_fatal_error(result.err) && result.err != ExecutionError::Halted) [[unlikely]] {
            return result.err;
        }
    }

    // Normal halt
    return state_.last_error == ExecutionError::Halted ? ExecutionError::Success
                                                       : state_.last_error;
}

// -----------------------------------------------------------------------------
// Instruction Fetch
// -----------------------------------------------------------------------------

std::uint32_t Executor::fetch() const noexcept {
    // Read 4 bytes in little-endian order
    const auto* ptr = code_.data() + state_.pc;

    // Use std::bit_cast for efficient byte-to-int conversion if aligned,
    // otherwise manual composition for portability
    std::uint32_t instr =
        static_cast<std::uint32_t>(ptr[0]) | (static_cast<std::uint32_t>(ptr[1]) << 8) |
        (static_cast<std::uint32_t>(ptr[2]) << 16) | (static_cast<std::uint32_t>(ptr[3]) << 24);
    return instr;
}

StepResult Executor::validate_pc() const noexcept {
    // Check alignment first (instructions must be 4-byte aligned)
    if ((state_.pc & 0x3) != 0) [[unlikely]] {
        return StepResult::make_error(ExecutionError::PCNotAligned);
    }

    // Then check bounds
    if (state_.pc + INSTRUCTION_SIZE > code_.size()) [[unlikely]] {
        return StepResult::make_error(ExecutionError::PCOutOfBounds);
    }

    return StepResult::success();
}

// -----------------------------------------------------------------------------
// Opcode Dispatch
// -----------------------------------------------------------------------------

StepResult Executor::dispatch(std::uint32_t instr) noexcept {
    const auto op = extract_opcode(instr);

    // Fast path: integer arithmetic opcodes (most common in benchmarks)
    if (is_type_a_arithmetic(op) || is_type_b_arithmetic(op)) [[likely]] {
        return dispatch_arithmetic(instr, op);
    }

    // Floating-point opcodes
    if (is_floating_point(op)) {
        return dispatch_floating_point(instr, op);
    }

    // Bitwise opcodes
    if (is_bitwise_opcode(op)) {
        return dispatch_bitwise(instr, op);
    }

    // Control flow opcodes (includes HALT at 0x5F)
    if (is_control_flow_opcode(op)) {
        return dispatch_control_flow(instr, op);
    }

    // System opcodes (NOP at 0xF0, etc.)
    if (is_system_opcode(op)) {
        return dispatch_system(instr, op);
    }

    // Cryptographic opcodes (0xB0-0xBF) - SEC-008
    if (is_crypto_op(op)) {
        return dispatch_crypto(instr, op);
    }

    // Reserved opcodes
    if (is_reserved_opcode(op)) [[unlikely]] {
        return StepResult::make_error(ExecutionError::ReservedOpcode);
    }

    // Not implemented yet
    return StepResult::make_error(ExecutionError::NotImplemented);
}

StepResult Executor::dispatch_arithmetic(std::uint32_t instr, std::uint8_t op) noexcept {
    // Type B (immediate) instructions
    if (is_type_b_arithmetic(op)) {
        const auto decoded = decode_type_b(instr);
        return arith_exec_.execute_type_b(decoded);
    }

    // Type A (register-register) instructions
    const auto decoded = decode_type_a(instr);
    return arith_exec_.execute_type_a(decoded);
}

StepResult Executor::dispatch_floating_point(std::uint32_t instr, std::uint8_t /*op*/) noexcept {
    // All floating-point instructions use Type A format
    const auto decoded = decode_type_a(instr);
    return fp_exec_.execute_type_a(decoded);
}

// ============================================================================
// BitwiseExecutor Implementation
// ============================================================================

StepResult BitwiseExecutor::execute_type_a(const DecodedTypeA& decoded) noexcept {
    const auto rs1_val = ctx_.registers().read(decoded.rs1);
    const auto rs2_val = ctx_.registers().read(decoded.rs2);
    const auto& alu = ctx_.alu();

    switch (decoded.opcode) {
        case opcode::AND:
            return write_success(decoded.rd, alu.bit_and(rs1_val, rs2_val));

        case opcode::OR:
            return write_success(decoded.rd, alu.bit_or(rs1_val, rs2_val));

        case opcode::XOR:
            return write_success(decoded.rd, alu.bit_xor(rs1_val, rs2_val));

        case opcode::NOT:
            return write_success(decoded.rd, alu.bit_not(rs1_val));

        case opcode::SHL:
            return write_success(decoded.rd, alu.shl(rs1_val, rs2_val));

        case opcode::SHR:
            return write_success(decoded.rd, alu.shr(rs1_val, rs2_val));

        case opcode::SAR:
            return write_success(decoded.rd, alu.sar(rs1_val, rs2_val));

        case opcode::ROL:
            return write_success(decoded.rd, alu.rol(rs1_val, rs2_val));

        case opcode::ROR:
            return write_success(decoded.rd, alu.ror(rs1_val, rs2_val));

        default:
            return StepResult::make_error(ExecutionError::InvalidOpcode);
    }
}

StepResult BitwiseExecutor::execute_type_s(const DecodedTypeS& decoded) noexcept {
    const auto rs1_val = ctx_.registers().read(decoded.rs1);
    const auto shamt_val = Value::from_int(decoded.shamt6);
    const auto& alu = ctx_.alu();

    switch (decoded.opcode) {
        case opcode::SHLI:
            return write_success(decoded.rd, alu.shl(rs1_val, shamt_val));

        case opcode::SHRI:
            return write_success(decoded.rd, alu.shr(rs1_val, shamt_val));

        case opcode::SARI:
            return write_success(decoded.rd, alu.sar(rs1_val, shamt_val));

        default:
            return StepResult::make_error(ExecutionError::InvalidOpcode);
    }
}

StepResult BitwiseExecutor::execute_type_b(const DecodedTypeB& decoded) noexcept {
    const auto rd_val = ctx_.registers().read(decoded.rd);
    // Zero-extend for bitwise operations
    const auto imm_val = Value::from_int(static_cast<std::int64_t>(decoded.imm16));
    const auto& alu = ctx_.alu();

    switch (decoded.opcode) {
        case opcode::ANDI:
            return write_success(decoded.rd, alu.bit_and(rd_val, imm_val));

        case opcode::ORI:
            return write_success(decoded.rd, alu.bit_or(rd_val, imm_val));

        case opcode::XORI:
            return write_success(decoded.rd, alu.bit_xor(rd_val, imm_val));

        default:
            return StepResult::make_error(ExecutionError::InvalidOpcode);
    }
}

// ============================================================================
// Executor Bitwise Dispatch (delegates to BitwiseExecutor)
// ============================================================================

StepResult Executor::dispatch_bitwise(std::uint32_t instr, std::uint8_t op) noexcept {
    // Type S (shift-immediate) instructions
    if (is_type_s_bitwise(op)) {
        const auto decoded = decode_type_s(instr);
        return bitwise_exec_.execute_type_s(decoded);
    }

    // Type B (immediate) instructions
    if (is_type_b_bitwise(op)) {
        const auto decoded = decode_type_b(instr);
        return bitwise_exec_.execute_type_b(decoded);
    }

    // Type A (register-register) instructions
    const auto decoded = decode_type_a(instr);
    return bitwise_exec_.execute_type_a(decoded);
}

StepResult Executor::dispatch_control_flow(std::uint32_t /*instr*/, std::uint8_t op) noexcept {
    switch (op) {
        case opcode::HALT:
            return StepResult::halt();

        default:
            // Other control flow instructions (JMP, branches, CALL, RET)
            // are not implemented in this basic executor
            return StepResult::make_error(ExecutionError::NotImplemented);
    }
}

StepResult Executor::dispatch_system(std::uint32_t /*instr*/, std::uint8_t op) noexcept {
    switch (op) {
        case opcode::NOP:
            return StepResult::success();

        default:
            return StepResult::make_error(ExecutionError::NotImplemented);
    }
}

StepResult Executor::dispatch_crypto(std::uint32_t instr, std::uint8_t /*op*/) noexcept {
    // All crypto instructions use Type A format: [opcode][Rd][Rs1][Rs2]
    const auto decoded = decode_type_a(instr);
    return crypto_exec_.execute_type_a(decoded);
}

}  // namespace dotvm::core
