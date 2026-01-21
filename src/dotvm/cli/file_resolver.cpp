/// @file file_resolver.cpp
/// @brief DSL-003 File resolution utilities implementation

#include "dotvm/cli/file_resolver.hpp"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <sstream>

namespace dotvm::cli {

namespace {

/// @brief Check if a string contains null bytes (indicates binary content)
/// @param data The string to check
/// @return true if null bytes are found
bool contains_null_bytes(const std::string& data) {
    return data.find('\0') != std::string::npos;
}

/// @brief Check if a byte sequence is valid UTF-8
/// @param data The data to validate
/// @return true if valid UTF-8, false if invalid sequences found
bool is_valid_utf8(const std::string& data) {
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(data.data());
    std::size_t len = data.size();
    std::size_t i = 0;

    while (i < len) {
        if (bytes[i] <= 0x7F) {
            // ASCII character
            ++i;
        } else if ((bytes[i] & 0xE0) == 0xC0) {
            // 2-byte sequence
            if (i + 1 >= len || (bytes[i + 1] & 0xC0) != 0x80) {
                return false;
            }
            // Check for overlong encoding
            if ((bytes[i] & 0x1E) == 0) {
                return false;
            }
            i += 2;
        } else if ((bytes[i] & 0xF0) == 0xE0) {
            // 3-byte sequence
            if (i + 2 >= len || (bytes[i + 1] & 0xC0) != 0x80 || (bytes[i + 2] & 0xC0) != 0x80) {
                return false;
            }
            // Check for overlong encoding
            if (bytes[i] == 0xE0 && (bytes[i + 1] & 0x20) == 0) {
                return false;
            }
            i += 3;
        } else if ((bytes[i] & 0xF8) == 0xF0) {
            // 4-byte sequence
            if (i + 3 >= len || (bytes[i + 1] & 0xC0) != 0x80 || (bytes[i + 2] & 0xC0) != 0x80 ||
                (bytes[i + 3] & 0xC0) != 0x80) {
                return false;
            }
            // Check for overlong encoding
            if (bytes[i] == 0xF0 && (bytes[i + 1] & 0x30) == 0) {
                return false;
            }
            i += 4;
        } else {
            // Invalid UTF-8 start byte
            return false;
        }
    }
    return true;
}

}  // namespace

// ============================================================================
// FileError
// ============================================================================

std::string FileError::format_include_chain(
    const std::vector<std::pair<std::filesystem::path, std::uint32_t>>& chain) {
    if (chain.empty()) {
        return "";
    }

    std::ostringstream oss;
    for (size_t i = chain.size(); i > 0; --i) {
        const auto& [path, line] = chain[i - 1];
        if (i == chain.size()) {
            oss << "In file included from " << path.string();
        } else {
            oss << ",\n                 from " << path.string();
        }
        if (line > 0) {
            oss << ":" << line;
        }
    }
    oss << ":";
    return oss.str();
}

// ============================================================================
// FileResolver Construction
// ============================================================================

FileResolver::FileResolver(std::filesystem::path base_dir) : base_dir_(std::move(base_dir)) {}

void FileResolver::add_search_path(const std::filesystem::path& path) {
    std::error_code ec;
    auto canonical = std::filesystem::weakly_canonical(path, ec);
    if (!ec && std::filesystem::is_directory(canonical, ec)) {
        // Avoid duplicates
        if (std::find(search_paths_.begin(), search_paths_.end(), canonical) ==
            search_paths_.end()) {
            search_paths_.push_back(canonical);
        }
    }
}

// ============================================================================
// File Reading
// ============================================================================

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

    // Check cache first
    auto canonical = std::filesystem::weakly_canonical(path, ec);
    if (!ec) {
        auto it = file_cache_.find(canonical.string());
        if (it != file_cache_.end()) {
            return it->second;
        }
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

    std::string result = contents.str();

    // Check for binary files (null bytes indicate binary content)
    if (contains_null_bytes(result)) {
        return std::unexpected(FileError{ExitCode::IoError,
                                         "Binary file detected (contains null bytes): " +
                                             path.string() + " - DSL source files must be text",
                                         path});
    }

    // Check for valid UTF-8 encoding
    if (!is_valid_utf8(result)) {
        return std::unexpected(FileError{ExitCode::IoError,
                                         "Invalid UTF-8 encoding in file: " + path.string() +
                                             " - DSL source files must use UTF-8 encoding",
                                         path});
    }

    return result;
}

// ============================================================================
// Import Resolution (existing behavior for imports)
// ============================================================================

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

// ============================================================================
// Include Resolution (new behavior for includes)
// ============================================================================

std::expected<std::filesystem::path, FileError>
FileResolver::resolve_include(std::string_view include_path,
                              const std::filesystem::path& from_file) const {
    std::error_code ec;
    std::string path_str{include_path};

    // Check if this is a relative path (starts with ./ or ../)
    bool is_explicit_relative =
        (path_str.size() >= 2 && path_str[0] == '.' && path_str[1] == '/') ||
        (path_str.size() >= 3 && path_str[0] == '.' && path_str[1] == '.' && path_str[2] == '/');

    // Get the directory of the current file
    std::filesystem::path from_dir = from_file.parent_path();
    if (from_dir.empty()) {
        from_dir = base_dir_;
    }

    // If explicit relative path, resolve relative to current file only
    if (is_explicit_relative) {
        std::filesystem::path resolved = from_dir / include_path;
        resolved = std::filesystem::weakly_canonical(resolved, ec);

        if (ec) {
            return std::unexpected(FileError{ExitCode::IoError,
                                             "Failed to resolve path: " + std::string(include_path),
                                             std::filesystem::path(include_path)});
        }

        if (!std::filesystem::exists(resolved, ec)) {
            return std::unexpected(FileError{ExitCode::FileNotFound,
                                             "Include file not found: " + std::string(include_path),
                                             resolved});
        }

        return resolved;
    }

    // For non-relative paths, try search paths first
    for (const auto& search_path : search_paths_) {
        std::filesystem::path candidate = search_path / include_path;
        candidate = std::filesystem::weakly_canonical(candidate, ec);

        if (!ec && std::filesystem::exists(candidate, ec) && !ec) {
            return candidate;
        }
    }

    // Fall back to relative resolution from current file
    std::filesystem::path resolved = from_dir / include_path;
    resolved = std::filesystem::weakly_canonical(resolved, ec);

    if (ec) {
        return std::unexpected(FileError{ExitCode::IoError,
                                         "Failed to resolve path: " + std::string(include_path),
                                         std::filesystem::path(include_path)});
    }

    if (!std::filesystem::exists(resolved, ec)) {
        // Build a helpful error message mentioning search paths
        std::string error_msg = "Include file not found: " + std::string(include_path);
        if (!search_paths_.empty()) {
            error_msg += " (searched in " + std::to_string(search_paths_.size()) + " include path";
            if (search_paths_.size() > 1) {
                error_msg += "s";
            }
            error_msg += ")";
        }
        return std::unexpected(FileError{ExitCode::FileNotFound, error_msg, resolved});
    }

    return resolved;
}

// ============================================================================
// Include Tracking
// ============================================================================

bool FileResolver::is_included(const std::filesystem::path& path) const {
    std::error_code ec;
    auto canonical = std::filesystem::weakly_canonical(path, ec);
    if (ec) {
        return false;
    }
    return included_files_.contains(canonical);
}

void FileResolver::mark_included(const std::filesystem::path& path) {
    std::error_code ec;
    auto canonical = std::filesystem::weakly_canonical(path, ec);
    if (!ec) {
        included_files_.insert(canonical);
    }
}

bool FileResolver::would_create_cycle(const std::filesystem::path& path) const {
    std::error_code ec;
    auto canonical = std::filesystem::weakly_canonical(path, ec);
    if (ec) {
        return false;
    }

    // Check if this file is already on the include stack
    for (const auto& [stack_path, _] : include_stack_) {
        std::error_code ec2;
        auto stack_canonical = std::filesystem::weakly_canonical(stack_path, ec2);
        if (!ec2 && stack_canonical == canonical) {
            return true;
        }
    }
    return false;
}

void FileResolver::push_include_stack(const std::filesystem::path& path, std::uint32_t line) {
    std::error_code ec;
    auto canonical = std::filesystem::weakly_canonical(path, ec);
    if (!ec) {
        include_stack_.emplace_back(canonical, line);
    } else {
        include_stack_.emplace_back(path, line);
    }
}

void FileResolver::pop_include_stack() {
    if (!include_stack_.empty()) {
        include_stack_.pop_back();
    }
}

void FileResolver::clear_includes() {
    included_files_.clear();
    include_stack_.clear();
}

// ============================================================================
// Import Tracking (kept for backward compatibility)
// ============================================================================

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

// ============================================================================
// File Caching
// ============================================================================

const std::string* FileResolver::get_cached(const std::filesystem::path& path) const {
    std::error_code ec;
    auto canonical = std::filesystem::weakly_canonical(path, ec);
    if (ec) {
        return nullptr;
    }

    auto it = file_cache_.find(canonical.string());
    if (it != file_cache_.end()) {
        return &it->second;
    }
    return nullptr;
}

void FileResolver::cache_file(const std::filesystem::path& path, std::string contents) {
    std::error_code ec;
    auto canonical = std::filesystem::weakly_canonical(path, ec);
    if (!ec) {
        file_cache_[canonical.string()] = std::move(contents);
    }
}

void FileResolver::clear_cache() {
    file_cache_.clear();
}

// ============================================================================
// Utility Functions
// ============================================================================

std::filesystem::path FileResolver::default_output_path(const std::filesystem::path& source_path) {
    std::filesystem::path output = source_path;
    output.replace_extension(".dot");
    return output;
}

}  // namespace dotvm::cli
