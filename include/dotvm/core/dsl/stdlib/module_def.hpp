#pragma once

/// @file module_def.hpp
/// @brief DSL-004 Module and function definition structures
///
/// Defines the structures used to describe stdlib modules and their functions:
/// - FunctionDef: Describes a single stdlib function
/// - ModuleDef: Describes a stdlib module with its functions and requirements

#include <cstdint>
#include <string>
#include <vector>

#include "dotvm/core/capabilities/capability.hpp"
#include "dotvm/core/dsl/stdlib/stdlib_types.hpp"

namespace dotvm::core::dsl::stdlib {

// ============================================================================
// Function Definition
// ============================================================================

/// @brief Defines a stdlib function's signature and metadata
///
/// Each stdlib function has:
/// - A name (as called from DSL code)
/// - Parameter types for type checking
/// - Return type
/// - Syscall ID for runtime dispatch (0 = inline implementation)
/// - Purity flag for optimization
struct FunctionDef {
    /// Function name as used in DSL code (e.g., "print", "sqrt")
    std::string name;

    /// Parameter types in order
    std::vector<StdlibType> params;

    /// Return type (Void for procedures)
    StdlibType return_type{StdlibType::Void};

    /// Syscall ID for runtime dispatch
    /// 0 means the function is implemented inline at compile time
    std::uint16_t syscall_id{0};

    /// Whether this function has no side effects
    /// Pure functions can be optimized (constant folding, dead code elimination)
    bool is_pure{false};

    /// Whether this function accepts variadic arguments
    /// If true, params defines minimum required args
    bool is_variadic{false};

    // ========== Factory Methods ==========

    /// Create a pure function definition
    [[nodiscard]] static FunctionDef pure(std::string name, std::vector<StdlibType> params,
                                          StdlibType return_type, std::uint16_t syscall_id) {
        return FunctionDef{
            .name = std::move(name),
            .params = std::move(params),
            .return_type = return_type,
            .syscall_id = syscall_id,
            .is_pure = true,
            .is_variadic = false,
        };
    }

    /// Create an impure function definition
    [[nodiscard]] static FunctionDef impure(std::string name, std::vector<StdlibType> params,
                                            StdlibType return_type, std::uint16_t syscall_id) {
        return FunctionDef{
            .name = std::move(name),
            .params = std::move(params),
            .return_type = return_type,
            .syscall_id = syscall_id,
            .is_pure = false,
            .is_variadic = false,
        };
    }

    /// Create an inline (compile-time) function definition
    [[nodiscard]] static FunctionDef inline_fn(std::string name, std::vector<StdlibType> params,
                                               StdlibType return_type) {
        return FunctionDef{
            .name = std::move(name),
            .params = std::move(params),
            .return_type = return_type,
            .syscall_id = 0,  // No runtime dispatch
            .is_pure = true,
            .is_variadic = false,
        };
    }

    /// Create a variadic function definition
    [[nodiscard]] static FunctionDef variadic(std::string name, std::vector<StdlibType> min_params,
                                              StdlibType return_type, std::uint16_t syscall_id,
                                              bool is_pure_fn = false) {
        return FunctionDef{
            .name = std::move(name),
            .params = std::move(min_params),
            .return_type = return_type,
            .syscall_id = syscall_id,
            .is_pure = is_pure_fn,
            .is_variadic = true,
        };
    }

    // ========== Query Methods ==========

    /// Check if this function is implemented inline (no syscall)
    [[nodiscard]] bool is_inline() const noexcept { return syscall_id == 0; }

    /// Get the minimum number of required parameters
    [[nodiscard]] std::size_t min_params() const noexcept { return params.size(); }

    /// Check if a given argument count is valid
    [[nodiscard]] bool valid_arg_count(std::size_t count) const noexcept {
        if (is_variadic) {
            return count >= params.size();
        }
        return count == params.size();
    }
};

// ============================================================================
// Module Definition
// ============================================================================

/// @brief Defines a stdlib module and its contents
///
/// A module contains:
/// - A path (e.g., "std/prelude", "std/math")
/// - Required capabilities for using the module
/// - List of functions provided by the module
struct ModuleDef {
    /// Module import path (e.g., "std/prelude", "std/io")
    std::string path;

    /// Capabilities required to use this module
    /// If the compiler context doesn't have these, import fails
    capabilities::Permission required_caps{capabilities::Permission::None};

    /// Functions provided by this module
    std::vector<FunctionDef> functions;

    /// Whether this module is auto-imported (like prelude)
    bool auto_import{false};

    // ========== Query Methods ==========

    /// Find a function by name
    /// @return Pointer to FunctionDef or nullptr if not found
    [[nodiscard]] const FunctionDef* find_function(std::string_view name) const noexcept {
        for (const auto& fn : functions) {
            if (fn.name == name) {
                return &fn;
            }
        }
        return nullptr;
    }

    /// Check if module has a function with given name
    [[nodiscard]] bool has_function(std::string_view name) const noexcept {
        return find_function(name) != nullptr;
    }

    /// Check if module requires any capabilities
    [[nodiscard]] bool requires_caps() const noexcept {
        return required_caps != capabilities::Permission::None;
    }
};

}  // namespace dotvm::core::dsl::stdlib
