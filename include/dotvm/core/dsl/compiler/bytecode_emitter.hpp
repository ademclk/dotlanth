#pragma once

/// @file bytecode_emitter.hpp
/// @brief TOOL-003 BytecodeEmitter - Unified API for bytecode production
///
/// Provides a high-level interface for producing bytecode from DotIR:
/// - Primary API: emit(DotIR) → bytecode vector (single-shot compilation)
/// - Low-level API: incremental building for advanced use cases
///
/// Architecture:
///   DotIR → Lowerer → CodeGenerator → assemble → validate → .dot bytecode

#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include "dotvm/core/bytecode.hpp"
#include "dotvm/core/dsl/compiler/codegen.hpp"
#include "dotvm/core/dsl/compiler/lowerer.hpp"
#include "dotvm/core/dsl/ir/types.hpp"
#include "dotvm/core/dsl/source_location.hpp"
#include "dotvm/core/value.hpp"

namespace dotvm::core::dsl::compiler {

// ============================================================================
// EmitterError - Unified error type for bytecode emission
// ============================================================================

/// @brief Error during bytecode emission
///
/// Consolidates errors from all pipeline stages (lowering, codegen, validation)
/// into a single error type with rich context.
struct EmitterError {
    /// @brief Error category
    enum class Kind {
        // Lowering errors
        RegisterSpill,  ///< Too many live values, register allocation failed
        UnsupportedIR,  ///< IR construct not supported for lowering

        // Codegen errors
        UnsupportedInstruction,  ///< Instruction type not implemented
        ConstantPoolOverflow,    ///< Too many constants
        JumpOutOfRange,          ///< Jump offset exceeds 24-bit limit

        // Validation errors
        HeaderValidationFailed,  ///< Bytecode header validation failed
        BytecodeSizeMismatch,    ///< Actual size doesn't match header
        EntryPointInvalid,       ///< Entry point outside code section

        // General errors
        InternalError,  ///< Unexpected internal error
    };

    Kind kind;
    std::string message;
    std::optional<SourceSpan> span;

    // Factory methods for each error kind
    static EmitterError register_spill(const std::string& msg,
                                       std::optional<SourceSpan> span = std::nullopt) {
        return EmitterError{Kind::RegisterSpill, msg, span};
    }

    static EmitterError unsupported_ir(const std::string& msg,
                                       std::optional<SourceSpan> span = std::nullopt) {
        return EmitterError{Kind::UnsupportedIR, msg, span};
    }

    static EmitterError unsupported_instruction(const std::string& msg,
                                                std::optional<SourceSpan> span = std::nullopt) {
        return EmitterError{Kind::UnsupportedInstruction, msg, span};
    }

    static EmitterError constant_pool_overflow(const std::string& msg,
                                               std::optional<SourceSpan> span = std::nullopt) {
        return EmitterError{Kind::ConstantPoolOverflow, msg, span};
    }

    static EmitterError jump_out_of_range(const std::string& msg,
                                          std::optional<SourceSpan> span = std::nullopt) {
        return EmitterError{Kind::JumpOutOfRange, msg, span};
    }

    static EmitterError header_validation_failed(const std::string& msg,
                                                 std::optional<SourceSpan> span = std::nullopt) {
        return EmitterError{Kind::HeaderValidationFailed, msg, span};
    }

    static EmitterError bytecode_size_mismatch(const std::string& msg,
                                               std::optional<SourceSpan> span = std::nullopt) {
        return EmitterError{Kind::BytecodeSizeMismatch, msg, span};
    }

    static EmitterError entry_point_invalid(const std::string& msg,
                                            std::optional<SourceSpan> span = std::nullopt) {
        return EmitterError{Kind::EntryPointInvalid, msg, span};
    }

    static EmitterError internal(const std::string& msg,
                                 std::optional<SourceSpan> span = std::nullopt) {
        return EmitterError{Kind::InternalError, msg, span};
    }
};

/// @brief Result type for emitter operations
template <typename T>
using EmitResult = std::expected<T, EmitterError>;

// ============================================================================
// EmitterConfig - Configuration for bytecode emission
// ============================================================================

/// @brief Configuration options for bytecode emission
struct EmitterConfig {
    /// Target architecture (Arch32 or Arch64)
    Architecture arch = Architecture::Arch64;

    /// Bytecode flags (FLAG_DEBUG, FLAG_OPTIMIZED, etc.)
    std::uint16_t flags = bytecode::FLAG_NONE;

    /// Whether to validate output bytecode after assembly
    bool validate_output = true;
};

// ============================================================================
// BytecodeEmitter - Main API
// ============================================================================

/// @brief Unified API for producing bytecode from DotIR
///
/// Wraps the compilation pipeline (Lowerer → CodeGenerator → assemble)
/// and provides both high-level single-shot and low-level incremental APIs.
///
/// @par Primary Usage (single-shot):
/// @code
/// BytecodeEmitter emitter;
/// auto result = emitter.emit(dot_ir);
/// if (result) {
///     auto& bytecode = *result;
///     // Write to .dot file
/// }
/// @endcode
///
/// @par Low-level Usage (incremental):
/// @code
/// BytecodeEmitter emitter;
/// emitter.begin();
/// emitter.define_label("entry");
/// auto idx = emitter.add_constant(Value::from_int(42));
/// emitter.emit_instruction(opcode::ADDI, {0, idx & 0xFF, idx >> 8});
/// auto result = emitter.finalize();
/// @endcode
class BytecodeEmitter {
public:
    /// @brief Construct an emitter with default configuration
    BytecodeEmitter() = default;

    /// @brief Construct an emitter with custom configuration
    explicit BytecodeEmitter(EmitterConfig config) : config_(std::move(config)) {}

    // ========================================================================
    // Primary API - Single-shot compilation
    // ========================================================================

    /// @brief Emit bytecode from DotIR
    ///
    /// Runs the full compilation pipeline:
    /// 1. Lower DotIR to LinearIR (SSA elimination, register allocation)
    /// 2. Generate bytecode from LinearIR
    /// 3. Assemble into final bytecode format with header
    /// 4. Optionally validate the output
    ///
    /// @param dot The DotIR to compile
    /// @return Bytecode vector on success, EmitterError on failure
    [[nodiscard]] EmitResult<std::vector<std::uint8_t>> emit(const ir::DotIR& dot);

    // ========================================================================
    // Low-level API - Incremental building
    // ========================================================================

    /// @brief Begin a new incremental build session
    ///
    /// Clears any existing state and prepares for incremental bytecode building.
    void begin();

    /// @brief Add a constant to the constant pool
    ///
    /// Constants are deduplicated - if the same value already exists,
    /// the existing index is returned.
    ///
    /// @param val The constant value to add
    /// @return Index of the constant in the pool
    [[nodiscard]] std::uint32_t add_constant(dotvm::core::Value val);

    /// @brief Emit a raw instruction
    ///
    /// Emits an instruction with the given opcode and argument bytes.
    /// Instructions are padded to 4-byte alignment.
    ///
    /// @param opcode The instruction opcode
    /// @param args Argument bytes (up to 3 bytes for standard instructions)
    void emit_instruction(std::uint8_t opcode, std::span<const std::uint8_t> args);

    /// @brief Define a label at the current code position
    ///
    /// Labels can be referenced by add_label_reference() for jump targets.
    ///
    /// @param name Label name (must be unique)
    void define_label(const std::string& name);

    /// @brief Add a reference to a label (for jump instructions)
    ///
    /// The reference will be resolved during finalize().
    ///
    /// @param name Label name to reference
    /// @param is_relative If true, emit relative offset; if false, absolute
    void add_label_reference(const std::string& name, bool is_relative = true);

    /// @brief Finalize the incremental build
    ///
    /// Resolves all label references, builds the constant pool,
    /// creates the bytecode header, and assembles the final output.
    ///
    /// @return Bytecode vector on success, EmitterError on failure
    [[nodiscard]] EmitResult<std::vector<std::uint8_t>> finalize();

    // ========================================================================
    // State queries
    // ========================================================================

    /// @brief Get the current configuration
    [[nodiscard]] const EmitterConfig& config() const noexcept { return config_; }

    /// @brief Get the current code section size (bytes)
    [[nodiscard]] std::size_t current_code_size() const noexcept { return code_.size(); }

    /// @brief Get the current constant pool size (entry count)
    [[nodiscard]] std::size_t constant_pool_size() const noexcept { return constants_.size(); }

private:
    // Configuration
    EmitterConfig config_;

    // Internal state for incremental building
    std::vector<std::uint8_t> code_;
    std::vector<dotvm::core::Value> constants_;
    std::unordered_map<std::string, std::size_t> label_offsets_;

    /// Pending label reference for resolution during finalize()
    struct LabelRef {
        std::size_t code_offset;
        std::string label;
        bool is_relative;
    };
    std::vector<LabelRef> pending_labels_;

    // Helper for error translation
    [[nodiscard]] static EmitterError translate_codegen_error(const CodegenError& err);

    // Validation helper
    [[nodiscard]] EmitResult<void> validate_bytecode(std::span<const std::uint8_t> bytecode);
};

}  // namespace dotvm::core::dsl::compiler
