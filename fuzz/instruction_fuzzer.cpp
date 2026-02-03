// SPDX-License-Identifier: MIT
// Instruction Format Fuzzer
//
// Tests instruction decoding edge cases for all 6 instruction types:
// - R-type: Register-register operations
// - I-type: Immediate operations
// - S-type: Store operations
// - B-type: Branch operations
// - U-type: Upper immediate operations
// - J-type: Jump operations
//
// Each instruction is 32 bits. This fuzzer tests:
// - Opcode validation
// - Field extraction correctness
// - Invalid encoding handling

#include <cstdint>
#include <cstddef>
#include <span>

#include "dotvm/core/instruction.hpp"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    // Process input in 4-byte chunks (32-bit instructions)
    for (size_t i = 0; i + 4 <= size; i += 4) {
        // Reconstruct a 32-bit instruction from bytes (little-endian)
        uint32_t raw_instruction =
            static_cast<uint32_t>(data[i]) |
            (static_cast<uint32_t>(data[i + 1]) << 8) |
            (static_cast<uint32_t>(data[i + 2]) << 16) |
            (static_cast<uint32_t>(data[i + 3]) << 24);

        try {
            // Attempt to decode the instruction
            auto instruction = dotvm::core::Instruction::decode(raw_instruction);

            // If decoding succeeded, validate all fields can be accessed
            [[maybe_unused]] auto opcode = instruction.opcode();
            [[maybe_unused]] auto format = instruction.format();

            // Access format-specific fields based on instruction type
            switch (format) {
                case dotvm::core::InstructionFormat::R:
                    [[maybe_unused]] auto rd_r = instruction.rd();
                    [[maybe_unused]] auto rs1_r = instruction.rs1();
                    [[maybe_unused]] auto rs2_r = instruction.rs2();
                    [[maybe_unused]] auto funct3_r = instruction.funct3();
                    [[maybe_unused]] auto funct7_r = instruction.funct7();
                    break;

                case dotvm::core::InstructionFormat::I:
                    [[maybe_unused]] auto rd_i = instruction.rd();
                    [[maybe_unused]] auto rs1_i = instruction.rs1();
                    [[maybe_unused]] auto imm_i = instruction.imm_i();
                    [[maybe_unused]] auto funct3_i = instruction.funct3();
                    break;

                case dotvm::core::InstructionFormat::S:
                    [[maybe_unused]] auto rs1_s = instruction.rs1();
                    [[maybe_unused]] auto rs2_s = instruction.rs2();
                    [[maybe_unused]] auto imm_s = instruction.imm_s();
                    [[maybe_unused]] auto funct3_s = instruction.funct3();
                    break;

                case dotvm::core::InstructionFormat::B:
                    [[maybe_unused]] auto rs1_b = instruction.rs1();
                    [[maybe_unused]] auto rs2_b = instruction.rs2();
                    [[maybe_unused]] auto imm_b = instruction.imm_b();
                    [[maybe_unused]] auto funct3_b = instruction.funct3();
                    break;

                case dotvm::core::InstructionFormat::U:
                    [[maybe_unused]] auto rd_u = instruction.rd();
                    [[maybe_unused]] auto imm_u = instruction.imm_u();
                    break;

                case dotvm::core::InstructionFormat::J:
                    [[maybe_unused]] auto rd_j = instruction.rd();
                    [[maybe_unused]] auto imm_j = instruction.imm_j();
                    break;

                default:
                    // Unknown format - this shouldn't happen but handle gracefully
                    break;
            }
        } catch (...) {
            // Invalid instructions are expected - that's fine
        }
    }

    return 0;
}
