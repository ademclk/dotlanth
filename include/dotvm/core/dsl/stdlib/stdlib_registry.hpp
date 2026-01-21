#pragma once

/// @file stdlib_registry.hpp
/// @brief DSL-004 Standard Library registry
///
/// The StdlibRegistry is a singleton that holds all stdlib module definitions.
/// It is used by the compiler to:
/// - Resolve import statements
/// - Validate capability requirements
/// - Look up function signatures for type checking

#include <string_view>
#include <unordered_map>
#include <vector>

#include "dotvm/core/dsl/stdlib/module_def.hpp"

namespace dotvm::core::dsl::stdlib {

/// @brief Central registry for all stdlib modules
///
/// The registry is initialized once at startup with all compiled-in stdlib
/// modules. Modules are looked up by their import path (e.g., "std/prelude").
///
/// Thread Safety: Read-only after initialization (safe for concurrent access).
class StdlibRegistry {
public:
    /// @brief Get the singleton instance
    [[nodiscard]] static StdlibRegistry& instance();

    /// @brief Find a module by import path
    /// @param path Import path (e.g., "std/prelude", "std/math")
    /// @return Pointer to ModuleDef or nullptr if not found
    [[nodiscard]] const ModuleDef* find_module(std::string_view path) const;

    /// @brief Check if a module exists
    [[nodiscard]] bool has_module(std::string_view path) const;

    /// @brief Get all registered module paths
    [[nodiscard]] std::vector<std::string_view> module_paths() const;

    /// @brief Get all auto-import modules
    [[nodiscard]] std::vector<const ModuleDef*> auto_import_modules() const;

    /// @brief Resolve a qualified function call (e.g., "math.sqrt")
    ///
    /// Given a qualifier (e.g., "math") and function name (e.g., "sqrt"),
    /// finds the function in the appropriate module.
    ///
    /// @param qualifier Module alias or path component
    /// @param function_name Function name
    /// @param imported_modules Map of alias -> module path for current context
    /// @return Pointer to FunctionDef or nullptr if not found
    [[nodiscard]] const FunctionDef* resolve_qualified_call(
        std::string_view qualifier, std::string_view function_name,
        const std::unordered_map<std::string, std::string>& imported_modules) const;

    /// @brief Look up a function by syscall ID
    /// @return Pointer to FunctionDef or nullptr if not found
    [[nodiscard]] const FunctionDef* find_by_syscall_id(std::uint16_t syscall_id) const;

    // Delete copy/move operations (singleton)
    StdlibRegistry(const StdlibRegistry&) = delete;
    StdlibRegistry& operator=(const StdlibRegistry&) = delete;
    StdlibRegistry(StdlibRegistry&&) = delete;
    StdlibRegistry& operator=(StdlibRegistry&&) = delete;

private:
    StdlibRegistry();
    ~StdlibRegistry() = default;

    /// Register a module
    void register_module(ModuleDef module);

    /// Initialize all built-in modules
    void init_prelude();
    void init_math();
    void init_string();
    void init_collections();
    void init_io();
    void init_crypto();
    void init_net();
    void init_time();
    void init_async();
    void init_control();

    /// Module storage: path -> module definition
    std::unordered_map<std::string, ModuleDef> modules_;

    /// Index for syscall ID -> function lookup
    std::unordered_map<std::uint16_t, const FunctionDef*> syscall_index_;
};

}  // namespace dotvm::core::dsl::stdlib
