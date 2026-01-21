#pragma once

/// @file types.hpp
/// @brief DSL-002 Core IR types for the DotLanth compiler
///
/// Defines the typed SSA (Static Single Assignment) intermediate representation
/// used by the DSL compiler pipeline. The IR is designed to enable optimization
/// passes while maintaining type safety.

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include "dotvm/core/dsl/source_location.hpp"
#include "dotvm/core/value.hpp"

namespace dotvm::core::dsl::ir {

// Forward declarations
struct Instruction;
struct BasicBlock;
struct PhiNode;

// ============================================================================
// IR Value Types
// ============================================================================

/// @brief Type of an SSA value
enum class ValueType : std::uint8_t {
    Void,     ///< No value (for terminators that don't produce values)
    Int64,    ///< 64-bit signed integer (48-bit in NaN-boxed form)
    Float64,  ///< IEEE 754 double-precision float
    Bool,     ///< Boolean value
    Handle,   ///< Memory handle (for strings, arrays, etc.)
    Any,      ///< Dynamic type (requires runtime checks)
};

/// @brief Convert ValueType to string representation
[[nodiscard]] constexpr const char* to_string(ValueType type) noexcept {
    switch (type) {
        case ValueType::Void:
            return "void";
        case ValueType::Int64:
            return "i64";
        case ValueType::Float64:
            return "f64";
        case ValueType::Bool:
            return "bool";
        case ValueType::Handle:
            return "handle";
        case ValueType::Any:
            return "any";
    }
    return "?";
}

/// @brief Check if type is numeric (Int64 or Float64)
[[nodiscard]] constexpr bool is_numeric(ValueType type) noexcept {
    return type == ValueType::Int64 || type == ValueType::Float64;
}

// ============================================================================
// SSA Value
// ============================================================================

/// @brief Represents an SSA value (result of an instruction)
///
/// In SSA form, each value is assigned exactly once. Values are identified
/// by a unique ID and have an associated type.
struct Value {
    /// Unique identifier for this value within the function
    std::uint32_t id;

    /// Type of this value
    ValueType type;

    /// Optional constant value (for constant propagation)
    std::optional<dotvm::core::Value> constant;

    /// Debug name (for IR printing)
    std::string name;

    /// Create a new SSA value
    static Value make(std::uint32_t id, ValueType type, std::string name = "") {
        return Value{.id = id, .type = type, .constant = std::nullopt, .name = std::move(name)};
    }

    /// Create a constant SSA value
    static Value make_const(std::uint32_t id, ValueType type, dotvm::core::Value val,
                            std::string name = "") {
        return Value{.id = id, .type = type, .constant = val, .name = std::move(name)};
    }

    /// Check if this value is a constant
    [[nodiscard]] bool is_constant() const noexcept { return constant.has_value(); }

    /// Equality comparison (by ID)
    bool operator==(const Value& other) const noexcept { return id == other.id; }
};

// ============================================================================
// State Slot
// ============================================================================

/// @brief Represents a mutable state variable slot
///
/// State slots are the only mutable storage in the IR. Each state variable
/// in the DSL maps to a slot, accessed via StateGet/StatePut instructions.
struct StateSlot {
    /// Unique slot index
    std::uint32_t index;

    /// Type of the state variable
    ValueType type;

    /// Name of the state variable
    std::string name;

    /// Initial value (evaluated at runtime)
    std::optional<dotvm::core::Value> initial_value;
};

// ============================================================================
// Symbol Table Entry
// ============================================================================

/// @brief Kind of symbol
enum class SymbolKind : std::uint8_t {
    StateVar,   ///< State variable (maps to StateSlot)
    Parameter,  ///< Function parameter
    Local,      ///< Local SSA value
    Function,   ///< External function reference
    Dot,        ///< Dot (agent) reference
};

/// @brief Entry in the symbol table
struct SymbolEntry {
    SymbolKind kind;
    std::string name;

    /// For StateVar: slot index; for Local: value id; for Function: function id
    std::uint32_t ref_id;

    /// Type of the symbol
    ValueType type;

    /// Source location for error reporting
    SourceSpan span;
};

/// @brief Link metadata for ontology relationships
struct LinkEntry {
    std::string source_dot;
    std::string target_dot;
    SourceSpan span;
};

/// @brief Symbol table for name resolution
struct SymbolTable {
    /// Named symbols indexed by name
    std::unordered_map<std::string, SymbolEntry> symbols;

    /// Link definitions (ontology metadata, not executable)
    std::vector<LinkEntry> links;

    /// Look up a symbol by name
    [[nodiscard]] const SymbolEntry* find(const std::string& name) const {
        auto it = symbols.find(name);
        return it != symbols.end() ? &it->second : nullptr;
    }

    /// Insert a symbol
    bool insert(SymbolEntry entry) {
        auto name = entry.name;
        return symbols.emplace(std::move(name), std::move(entry)).second;
    }
};

// ============================================================================
// Basic Block
// ============================================================================

/// @brief A basic block in the control flow graph
///
/// Basic blocks are sequences of instructions with:
/// - Single entry point (from predecessors or function entry)
/// - Single exit point (via terminator instruction)
/// - Phi nodes at the beginning for SSA join points
struct BasicBlock {
    /// Unique block identifier
    std::uint32_t id;

    /// Debug label (e.g., "entry", "loop_header", "if_then")
    std::string label;

    /// Phi nodes at block entry (for SSA join points)
    std::vector<std::unique_ptr<PhiNode>> phis;

    /// Instructions in this block (excluding terminator)
    std::vector<std::unique_ptr<Instruction>> instructions;

    /// Terminator instruction (jump, branch, return, etc.)
    std::unique_ptr<Instruction> terminator;

    /// Predecessor blocks in CFG
    std::vector<BasicBlock*> predecessors;

    /// Successor blocks in CFG
    std::vector<BasicBlock*> successors;

    /// Check if block has a terminator
    [[nodiscard]] bool is_terminated() const noexcept { return terminator != nullptr; }

    /// Get the number of incoming edges (for phi node validation)
    [[nodiscard]] std::size_t predecessor_count() const noexcept { return predecessors.size(); }
};

// ============================================================================
// Phi Node
// ============================================================================

/// @brief Phi node for SSA join points
///
/// Phi nodes select between values from different predecessor blocks.
/// They are placed at the beginning of basic blocks with multiple predecessors.
struct PhiNode {
    /// Result value of the phi node
    Value result;

    /// Incoming values: (predecessor block ID, value ID)
    std::vector<std::pair<std::uint32_t, std::uint32_t>> incoming;

    /// Source location for error reporting
    SourceSpan span;
};

// ============================================================================
// Trigger Info
// ============================================================================

/// @brief Information about a trigger condition
struct TriggerInfo {
    /// Entry block for this trigger
    std::uint32_t entry_block_id;

    /// Condition value (evaluated each tick)
    std::uint32_t condition_value_id;

    /// Action block (executed when condition is true)
    std::uint32_t action_block_id;

    /// Continuation block (after action completes)
    std::uint32_t continuation_block_id;
};

// ============================================================================
// Dot IR (Complete Compiled Dot)
// ============================================================================

/// @brief Complete IR representation of a compiled dot (agent)
struct DotIR {
    /// Name of the dot
    std::string name;

    /// State slots for mutable variables
    std::vector<StateSlot> state_slots;

    /// All basic blocks in the function
    std::vector<std::unique_ptr<BasicBlock>> blocks;

    /// Entry block ID
    std::uint32_t entry_block_id{0};

    /// Trigger information for polling loop
    std::vector<TriggerInfo> triggers;

    /// Symbol table
    SymbolTable symbols;

    /// Value counter for generating unique IDs
    std::uint32_t next_value_id{0};

    /// Block counter for generating unique IDs
    std::uint32_t next_block_id{0};

    /// Allocate a new value ID
    [[nodiscard]] std::uint32_t alloc_value_id() noexcept { return next_value_id++; }

    /// Allocate a new block ID
    [[nodiscard]] std::uint32_t alloc_block_id() noexcept { return next_block_id++; }

    /// Find a block by ID
    [[nodiscard]] BasicBlock* find_block(std::uint32_t id) {
        for (auto& block : blocks) {
            if (block->id == id) {
                return block.get();
            }
        }
        return nullptr;
    }

    /// Get the entry block
    [[nodiscard]] BasicBlock* entry_block() { return find_block(entry_block_id); }
};

// ============================================================================
// Compiled Module (Collection of Dots)
// ============================================================================

/// @brief Complete compiled module containing all dots
struct CompiledModule {
    /// All compiled dots
    std::vector<DotIR> dots;

    /// Global symbol table (for cross-dot references)
    SymbolTable global_symbols;

    /// Link definitions (ontology relationships)
    std::vector<LinkEntry> links;
};

}  // namespace dotvm::core::dsl::ir
