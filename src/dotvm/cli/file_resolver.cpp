/// @file file_resolver.cpp
/// @brief DSL-003 File resolution utilities implementation

#include "dotvm/cli/file_resolver.hpp"

#include <fstream>
#include <sstream>

namespace dotvm::cli {

FileResolver::FileResolver(std::filesystem::path base_dir) : base_dir_(std::move(base_dir)) {}

std::expected<std::string, FileError>
FileResolver::read_file(const std::filesystem::path& path) const {
    std::error_code ec;

    // Check if file exists
    if (!std::filesystem::exists(path, ec)) {
        return std::unexpected(
            FileError{ExitCode::FileNotFound, "File not found: " + path.string(), path});
    }

    // Check if it's a regular file
    if (!std::filesystem::is_regular_file(path, ec)) {
        return std::unexpected(
            FileError{ExitCode::IoError, "Not a regular file: " + path.string(), path});
    }

    // Open and read
    std::ifstream file(path, std::ios::in | std::ios::binary);
    if (!file) {
        return std::unexpected(
            FileError{ExitCode::IoError, "Failed to open file: " + path.string(), path});
    }

    std::ostringstream contents;
    contents << file.rdbuf();

    if (file.bad()) {
        return std::unexpected(
            FileError{ExitCode::IoError, "Failed to read file: " + path.string(), path});
    }

    return contents.str();
}

std::expected<std::filesystem::path, FileError>
FileResolver::resolve_import(std::string_view import_path,
                             const std::filesystem::path& current_file) const {
    std::error_code ec;

    // Get the directory of the current file
    std::filesystem::path current_dir = current_file.parent_path();
    if (current_dir.empty()) {
        current_dir = base_dir_;
    }

    // Resolve the import path
    std::filesystem::path resolved = current_dir / import_path;
    resolved = std::filesystem::weakly_canonical(resolved, ec);

    if (ec) {
        return std::unexpected(FileError{ExitCode::IoError,
                                         "Failed to resolve path: " + std::string(import_path),
                                         std::filesystem::path(import_path)});
    }

    // Check if file exists
    if (!std::filesystem::exists(resolved, ec)) {
        return std::unexpected(FileError{
            ExitCode::FileNotFound, "Import not found: " + std::string(import_path), resolved});
    }

    return resolved;
}

bool FileResolver::is_imported(const std::filesystem::path& path) const {
    std::error_code ec;
    auto canonical = std::filesystem::weakly_canonical(path, ec);
    if (ec) {
        return false;
    }
    return imported_files_.contains(canonical);
}

void FileResolver::mark_imported(const std::filesystem::path& path) {
    std::error_code ec;
    auto canonical = std::filesystem::weakly_canonical(path, ec);
    if (!ec) {
        imported_files_.insert(canonical);
    }
}

void FileResolver::clear_imports() {
    imported_files_.clear();
}

std::filesystem::path FileResolver::default_output_path(const std::filesystem::path& source_path) {
    std::filesystem::path output = source_path;
    output.replace_extension(".dot");
    return output;
}

}  // namespace dotvm::cli
