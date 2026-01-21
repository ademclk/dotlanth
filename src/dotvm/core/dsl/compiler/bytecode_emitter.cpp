#include "dotvm/core/dsl/compiler/bytecode_emitter.hpp"

namespace dotvm::core::dsl::compiler {

// ============================================================================
// Error Translation
// ============================================================================

EmitterError BytecodeEmitter::translate_codegen_error(const CodegenError& err) {
    switch (err.kind) {
        case CodegenError::Kind::UnsupportedInstruction:
            return EmitterError::unsupported_instruction(err.message);
        case CodegenError::Kind::ConstantPoolOverflow:
            return EmitterError::constant_pool_overflow(err.message);
        case CodegenError::Kind::JumpOutOfRange:
            return EmitterError::jump_out_of_range(err.message);
        case CodegenError::Kind::InternalError:
            return EmitterError::internal(err.message);
    }
    return EmitterError::internal("Unknown codegen error");
}

// ============================================================================
// Primary API - Single-shot compilation
// ============================================================================

EmitResult<std::vector<std::uint8_t>> BytecodeEmitter::emit(const ir::DotIR& dot) {
    // Step 1: Lower DotIR to LinearIR
    Lowerer lowerer;
    LinearIR linear_ir = lowerer.lower(dot);

    // Step 2: Generate bytecode from LinearIR
    CodeGenerator codegen(config_.arch);
    auto gen_result = codegen.generate(linear_ir);
    if (!gen_result) {
        return std::unexpected(translate_codegen_error(gen_result.error()));
    }

    // Apply configured flags to generated code
    GeneratedCode& generated = *gen_result;
    // Note: The CodeGenerator doesn't currently take flags, so we'll handle
    // them in the assembly phase if needed in the future

    // Step 3: Assemble into final bytecode format
    std::vector<std::uint8_t> bytecode = codegen.assemble(generated);

    // Step 4: Validate if enabled
    if (config_.validate_output) {
        auto validate_result = validate_bytecode(bytecode);
        if (!validate_result) {
            return std::unexpected(validate_result.error());
        }
    }

    return bytecode;
}

// ============================================================================
// Validation
// ============================================================================

EmitResult<void> BytecodeEmitter::validate_bytecode(std::span<const std::uint8_t> bytecode) {
    // Read the header
    auto header_result = read_header(bytecode);
    if (!header_result) {
        return std::unexpected(EmitterError::header_validation_failed(
            "Failed to read bytecode header: " + std::string(to_string(header_result.error()))));
    }

    // Validate the header against the file size
    BytecodeError validation_error = validate_header(*header_result, bytecode.size());
    if (validation_error != BytecodeError::Success) {
        return std::unexpected(EmitterError::header_validation_failed(
            "Header validation failed: " + std::string(to_string(validation_error))));
    }

    // Additional validation: verify section sizes match
    const auto& header = *header_result;

    // Check constant pool section
    if (header.const_pool_size > 0) {
        std::size_t expected_end = header.const_pool_offset + header.const_pool_size;
        if (expected_end > bytecode.size()) {
            return std::unexpected(EmitterError::bytecode_size_mismatch(
                "Constant pool extends beyond bytecode: expected " + std::to_string(expected_end) +
                ", got " + std::to_string(bytecode.size())));
        }
    }

    // Check code section
    if (header.code_size > 0) {
        std::size_t expected_end = header.code_offset + header.code_size;
        if (expected_end > bytecode.size()) {
            return std::unexpected(EmitterError::bytecode_size_mismatch(
                "Code section extends beyond bytecode: expected " + std::to_string(expected_end) +
                ", got " + std::to_string(bytecode.size())));
        }
    }

    return {};
}

// ============================================================================
// Low-level API - Incremental building
// ============================================================================

void BytecodeEmitter::begin() {
    code_.clear();
    constants_.clear();
    label_offsets_.clear();
    pending_labels_.clear();
}

std::uint32_t BytecodeEmitter::add_constant(dotvm::core::Value val) {
    // Check if constant already exists (deduplication)
    for (std::uint32_t i = 0; i < constants_.size(); ++i) {
        if (constants_[i] == val) {
            return i;
        }
    }

    auto idx = static_cast<std::uint32_t>(constants_.size());
    constants_.push_back(val);
    return idx;
}

void BytecodeEmitter::emit_instruction(std::uint8_t opcode, std::span<const std::uint8_t> args) {
    // All instructions are 4 bytes in DotVM
    code_.push_back(opcode);

    // Add argument bytes (up to 3)
    for (std::size_t i = 0; i < 3; ++i) {
        if (i < args.size()) {
            code_.push_back(args[i]);
        } else {
            code_.push_back(0);  // Padding
        }
    }
}

void BytecodeEmitter::define_label(const std::string& name) {
    label_offsets_[name] = code_.size();
}

void BytecodeEmitter::add_label_reference(const std::string& name, bool is_relative) {
    pending_labels_.push_back(LabelRef{code_.size(), name, is_relative});
}

EmitResult<std::vector<std::uint8_t>> BytecodeEmitter::finalize() {
    // Resolve all label references
    for (const auto& ref : pending_labels_) {
        auto it = label_offsets_.find(ref.label);
        if (it == label_offsets_.end()) {
            return std::unexpected(
                EmitterError::internal("Unresolved label: " + ref.label));
        }

        auto target = static_cast<std::int32_t>(it->second);
        std::int32_t value;

        if (ref.is_relative) {
            value = target - static_cast<std::int32_t>(ref.code_offset);
        } else {
            value = target;
        }

        // Check range for 24-bit offset
        constexpr std::int32_t max_offset = (1 << 23) - 1;
        constexpr std::int32_t min_offset = -(1 << 23);
        if (value < min_offset || value > max_offset) {
            return std::unexpected(
                EmitterError::jump_out_of_range("Jump offset out of 24-bit range: " + ref.label));
        }

        // Patch bytes 1-3 of the instruction at ref.code_offset
        if (ref.code_offset + 3 < code_.size()) {
            code_[ref.code_offset + 1] = static_cast<std::uint8_t>(value & 0xFF);
            code_[ref.code_offset + 2] = static_cast<std::uint8_t>((value >> 8) & 0xFF);
            code_[ref.code_offset + 3] = static_cast<std::uint8_t>((value >> 16) & 0xFF);
        }
    }

    // Build constant pool data
    std::vector<std::uint8_t> const_pool_data;
    if (!constants_.empty()) {
        // Header: entry count (4 bytes)
        auto count = static_cast<std::uint32_t>(constants_.size());
        const_pool_data.resize(4);
        endian::write_u32_le(const_pool_data.data(), count);

        // Entries
        for (const auto& c : constants_) {
            if (c.is_integer()) {
                const_pool_data.push_back(bytecode::CONST_TYPE_I64);
                std::array<std::uint8_t, 8> buf{};
                endian::write_i64_le(buf.data(), c.as_integer());
                const_pool_data.insert(const_pool_data.end(), buf.begin(), buf.end());
            } else if (c.is_float()) {
                const_pool_data.push_back(bytecode::CONST_TYPE_F64);
                std::array<std::uint8_t, 8> buf{};
                endian::write_f64_le(buf.data(), c.as_float());
                const_pool_data.insert(const_pool_data.end(), buf.begin(), buf.end());
            }
        }
    }

    // Calculate offsets
    std::uint64_t const_pool_offset = bytecode::HEADER_SIZE;
    std::uint64_t const_pool_size = const_pool_data.size();
    std::uint64_t code_offset = const_pool_offset + const_pool_size;

    // Align code section to 4 bytes
    while (code_offset % bytecode::INSTRUCTION_ALIGNMENT != 0) {
        ++code_offset;
        const_pool_data.push_back(0);  // Padding
        ++const_pool_size;
    }

    std::uint64_t code_size = code_.size();

    // Entry point defaults to 0 (start of code section)
    std::uint64_t entry_point = 0;

    // Create header
    auto header = make_header(config_.arch, config_.flags, entry_point,
                              const_pool_offset, const_pool_size,
                              code_offset, code_size);

    // Assemble output
    std::vector<std::uint8_t> output;
    output.reserve(bytecode::HEADER_SIZE + const_pool_size + code_size);

    auto header_bytes = write_header(header);
    output.insert(output.end(), header_bytes.begin(), header_bytes.end());
    output.insert(output.end(), const_pool_data.begin(), const_pool_data.end());
    output.insert(output.end(), code_.begin(), code_.end());

    // Validate if enabled
    if (config_.validate_output) {
        auto validate_result = validate_bytecode(output);
        if (!validate_result) {
            return std::unexpected(validate_result.error());
        }
    }

    return output;
}

}  // namespace dotvm::core::dsl::compiler
