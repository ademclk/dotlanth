#pragma once

/// @file file_resolver.hpp
/// @brief DSL-003 File resolution utilities
///
/// Handles file reading, path resolution, and import handling for the compiler.
/// Supports circular import detection.

#include <expected>
#include <filesystem>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "dotvm/cli/terminal.hpp"

namespace dotvm::cli {

/// @brief Error from file operations
struct FileError {
    ExitCode code;
    std::string message;
    std::filesystem::path path;
};

/// @brief File resolver for DSL compilation
///
/// Handles reading source files, resolving import paths, and detecting
/// circular dependencies.
class FileResolver {
public:
    /// @brief Construct a file resolver
    /// @param base_dir Base directory for relative imports
    explicit FileResolver(std::filesystem::path base_dir = std::filesystem::current_path());

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

    /// @brief Check if a file has already been imported (circular detection)
    /// @param path Canonical path to check
    /// @return true if already imported
    [[nodiscard]] bool is_imported(const std::filesystem::path& path) const;

    /// @brief Mark a file as imported
    /// @param path Canonical path to mark
    void mark_imported(const std::filesystem::path& path);

    /// @brief Clear the import tracking (for new compilation)
    void clear_imports();

    /// @brief Get the base directory
    [[nodiscard]] const std::filesystem::path& base_dir() const noexcept { return base_dir_; }

    /// @brief Get default output path for a source file
    /// @param source_path Input source file path
    /// @return Output path with .dot extension
    [[nodiscard]] static std::filesystem::path
    default_output_path(const std::filesystem::path& source_path);

private:
    std::filesystem::path base_dir_;
    std::set<std::filesystem::path> imported_files_;
};

}  // namespace dotvm::cli
