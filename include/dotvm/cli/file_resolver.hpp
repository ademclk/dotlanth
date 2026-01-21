#pragma once

/// @file file_resolver.hpp
/// @brief DSL-003 File resolution utilities
///
/// Handles file reading, path resolution, and include handling for the compiler.
/// Supports circular include detection and search path resolution.

#include <expected>
#include <filesystem>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "dotvm/cli/terminal.hpp"

namespace dotvm::cli {

/// @brief Error from file operations
struct FileError {
    ExitCode code;
    std::string message;
    std::filesystem::path path;

    /// @brief Format include chain for error messages
    /// @param chain List of files in the include chain
    /// @return Formatted string showing include chain
    static std::string
    format_include_chain(const std::vector<std::pair<std::filesystem::path, std::uint32_t>>& chain);
};

/// @brief File resolver for DSL compilation
///
/// Handles reading source files, resolving include paths, and detecting
/// circular dependencies. Supports search paths similar to -I in C compilers.
class FileResolver {
public:
    /// @brief Construct a file resolver
    /// @param base_dir Base directory for relative includes
    explicit FileResolver(std::filesystem::path base_dir = std::filesystem::current_path());

    /// @brief Add a search path for include resolution
    /// @param path Directory to search for includes
    ///
    /// Search paths are searched in order after relative path resolution fails.
    /// Paths that start with ./ or ../ are always resolved relative to the
    /// current file, not via search paths.
    void add_search_path(const std::filesystem::path& path);

    /// @brief Get the configured search paths
    [[nodiscard]] const std::vector<std::filesystem::path>& search_paths() const noexcept {
        return search_paths_;
    }

    /// @brief Read a file's contents
    /// @param path Path to the file
    /// @return File contents or error
    [[nodiscard]] std::expected<std::string, FileError>
    read_file(const std::filesystem::path& path) const;

    /// @brief Resolve an import path relative to the current file
    /// @param import_path The import path from the DSL source
    /// @param current_file The file containing the import
    /// @return Resolved absolute path or error
    [[nodiscard]] std::expected<std::filesystem::path, FileError>
    resolve_import(std::string_view import_path, const std::filesystem::path& current_file) const;

    /// @brief Resolve an include path
    /// @param include_path The include path from the DSL source
    /// @param from_file The file containing the include directive
    /// @return Resolved absolute path or error
    ///
    /// Resolution rules:
    /// 1. Paths starting with ./ or ../ are relative to the current file
    /// 2. Other paths are searched in configured search paths
    /// 3. If not found in search paths, try relative to current file
    [[nodiscard]] std::expected<std::filesystem::path, FileError>
    resolve_include(std::string_view include_path, const std::filesystem::path& from_file) const;

    /// @brief Check if a file has already been included (prevents re-inclusion)
    /// @param path Canonical path to check
    /// @return true if already included
    [[nodiscard]] bool is_included(const std::filesystem::path& path) const;

    /// @brief Mark a file as included
    /// @param path Canonical path to mark
    void mark_included(const std::filesystem::path& path);

    /// @brief Check if including a file would create a circular include
    /// @param path Path to check
    /// @return true if including this file would create a cycle
    ///
    /// This checks the current include stack, not just whether the file
    /// has been included before (which prevents re-inclusion but not cycles).
    [[nodiscard]] bool would_create_cycle(const std::filesystem::path& path) const;

    /// @brief Push a file onto the include stack (when starting to process it)
    /// @param path File path
    /// @param line Line number of the include directive (0 for root file)
    void push_include_stack(const std::filesystem::path& path, std::uint32_t line = 0);

    /// @brief Pop a file from the include stack (when done processing it)
    void pop_include_stack();

    /// @brief Get the current include stack for error reporting
    [[nodiscard]] const std::vector<std::pair<std::filesystem::path, std::uint32_t>>&
    include_stack() const noexcept {
        return include_stack_;
    }

    /// @brief Check if a file has already been imported (circular detection)
    /// @param path Canonical path to check
    /// @return true if already imported
    [[nodiscard]] bool is_imported(const std::filesystem::path& path) const;

    /// @brief Mark a file as imported
    /// @param path Canonical path to mark
    void mark_imported(const std::filesystem::path& path);

    /// @brief Clear the include tracking (for new compilation)
    void clear_includes();

    /// @brief Clear the import tracking (for new compilation)
    void clear_imports();

    /// @brief Get the base directory
    [[nodiscard]] const std::filesystem::path& base_dir() const noexcept { return base_dir_; }

    /// @brief Get default output path for a source file
    /// @param source_path Input source file path
    /// @return Output path with .dot extension
    [[nodiscard]] static std::filesystem::path
    default_output_path(const std::filesystem::path& source_path);

    /// @brief Get cached file contents if available
    /// @param path Canonical path to check
    /// @return Pointer to cached contents, or nullptr if not cached
    [[nodiscard]] const std::string* get_cached(const std::filesystem::path& path) const;

    /// @brief Cache file contents
    /// @param path Canonical path
    /// @param contents File contents to cache
    void cache_file(const std::filesystem::path& path, std::string contents);

    /// @brief Clear the file cache
    void clear_cache();

private:
    std::filesystem::path base_dir_;
    std::vector<std::filesystem::path> search_paths_;
    std::set<std::filesystem::path> included_files_;
    std::set<std::filesystem::path> imported_files_;
    std::vector<std::pair<std::filesystem::path, std::uint32_t>> include_stack_;
    mutable std::unordered_map<std::string, std::string> file_cache_;
};

}  // namespace dotvm::cli
