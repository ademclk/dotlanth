/// @file filesystem_sandbox.cpp
/// @brief Implementation of filesystem sandboxing

#include "dotvm/core/io/filesystem_sandbox.hpp"

#include <fstream>
#include <sstream>

namespace dotvm::core::io {

namespace fs = std::filesystem;

// ============================================================================
// Sandbox Configuration
// ============================================================================

IoError FilesystemSandbox::allow_directory(std::string_view path) noexcept {
    try {
        fs::path p(path);
        if (!p.is_absolute()) {
            return IoError::InvalidPath;
        }

        // Canonicalize the path
        if (fs::exists(p)) {
            p = fs::canonical(p);
        } else {
            // For non-existent paths, at least normalize
            p = fs::weakly_canonical(p);
        }

        allowed_dirs_.push_back(std::move(p));
        return IoError::Success;
    } catch (...) {
        return IoError::InvalidPath;
    }
}

IoError FilesystemSandbox::allow_read_only(std::string_view path) noexcept {
    try {
        fs::path p(path);
        if (!p.is_absolute()) {
            return IoError::InvalidPath;
        }

        if (fs::exists(p)) {
            p = fs::canonical(p);
        } else {
            p = fs::weakly_canonical(p);
        }

        read_only_dirs_.push_back(std::move(p));
        return IoError::Success;
    } catch (...) {
        return IoError::InvalidPath;
    }
}

void FilesystemSandbox::clear_allowlist() noexcept {
    allowed_dirs_.clear();
    read_only_dirs_.clear();
}

// ============================================================================
// Path Validation
// ============================================================================

FilesystemSandbox::Result<fs::path>
FilesystemSandbox::validate_path(std::string_view path, bool require_write) const noexcept {
    // Check if sandbox has any allowed directories
    if (allowed_dirs_.empty() && read_only_dirs_.empty()) {
        return std::unexpected{IoError::SandboxDisabled};
    }

    try {
        fs::path p(path);

        // Require absolute paths or convert relative to absolute
        if (!p.is_absolute()) {
            // For security, we require absolute paths
            return std::unexpected{IoError::InvalidPath};
        }

        // Canonicalize (resolves .., ., and symlinks if they exist)
        fs::path canonical;
        if (fs::exists(p)) {
            canonical = fs::canonical(p);
        } else {
            // For non-existent files, check the parent directory
            canonical = fs::weakly_canonical(p);
        }

        // Check for path traversal (compare against canonical)
        // If the canonical path differs significantly, it might be traversal
        if (canonical.string().find("..") != std::string::npos) {
            return std::unexpected{IoError::PathTraversal};
        }

        // Check hidden files if not allowed
        if (!config_.allow_hidden) {
            auto filename = canonical.filename().string();
            if (!filename.empty() && filename[0] == '.') {
                return std::unexpected{IoError::PathDenied};
            }
        }

        // Check if path is in allowed directories
        bool in_read_write = is_under_directory(canonical, allowed_dirs_);
        bool in_read_only = is_under_directory(canonical, read_only_dirs_);

        if (!in_read_write && !in_read_only) {
            return std::unexpected{IoError::PathDenied};
        }

        if (require_write && !in_read_write) {
            return std::unexpected{IoError::PathDenied};
        }

        return canonical;
    } catch (const fs::filesystem_error&) {
        return std::unexpected{IoError::InvalidPath};
    } catch (...) {
        return std::unexpected{IoError::IoFailed};
    }
}

bool FilesystemSandbox::is_allowed(std::string_view path, bool require_write) const noexcept {
    auto result = validate_path(path, require_write);
    return result.has_value();
}

bool FilesystemSandbox::is_under_directory(const fs::path& canonical,
                                           const std::vector<fs::path>& dirs) const noexcept {
    for (const auto& dir : dirs) {
        // Check if canonical starts with dir
        auto canonical_str = canonical.string();
        auto dir_str = dir.string();

        // Ensure directory path ends with separator for proper prefix matching
        if (!dir_str.empty() && dir_str.back() != '/') {
            dir_str += '/';
        }

        // The path is under the directory if it starts with the directory path
        // or equals the directory exactly
        if (canonical_str.starts_with(dir_str) || canonical == dir) {
            return true;
        }
    }
    return false;
}

// ============================================================================
// File Operations
// ============================================================================

FilesystemSandbox::Result<std::string> FilesystemSandbox::read_file(std::string_view path) const {
    auto validated = validate_path(path, false);
    if (!validated) {
        return std::unexpected{validated.error()};
    }

    try {
        // Check file size
        auto size = fs::file_size(*validated);
        if (size > config_.max_file_size) {
            return std::unexpected{IoError::TooLarge};
        }

        std::ifstream file(*validated, std::ios::binary);
        if (!file) {
            return std::unexpected{IoError::NotFound};
        }

        std::ostringstream ss;
        ss << file.rdbuf();
        return ss.str();
    } catch (const fs::filesystem_error&) {
        return std::unexpected{IoError::NotFound};
    } catch (...) {
        return std::unexpected{IoError::IoFailed};
    }
}

IoError FilesystemSandbox::write_file(std::string_view path, std::string_view content) {
    auto validated = validate_path(path, true);
    if (!validated) {
        return validated.error();
    }

    if (content.size() > config_.max_file_size) {
        return IoError::TooLarge;
    }

    try {
        std::ofstream file(*validated, std::ios::binary | std::ios::trunc);
        if (!file) {
            return IoError::PermissionDenied;
        }

        file.write(content.data(), static_cast<std::streamsize>(content.size()));
        if (!file) {
            return IoError::IoFailed;
        }

        return IoError::Success;
    } catch (...) {
        return IoError::IoFailed;
    }
}

IoError FilesystemSandbox::append_file(std::string_view path, std::string_view content) {
    auto validated = validate_path(path, true);
    if (!validated) {
        return validated.error();
    }

    try {
        // Check current size + new content
        std::size_t current_size = 0;
        if (fs::exists(*validated)) {
            current_size = fs::file_size(*validated);
        }

        if (current_size + content.size() > config_.max_file_size) {
            return IoError::TooLarge;
        }

        std::ofstream file(*validated, std::ios::binary | std::ios::app);
        if (!file) {
            return IoError::PermissionDenied;
        }

        file.write(content.data(), static_cast<std::streamsize>(content.size()));
        if (!file) {
            return IoError::IoFailed;
        }

        return IoError::Success;
    } catch (...) {
        return IoError::IoFailed;
    }
}

FilesystemSandbox::Result<bool>
FilesystemSandbox::file_exists(std::string_view path) const noexcept {
    auto validated = validate_path(path, false);
    if (!validated) {
        // If validation fails due to sandbox config, the file "doesn't exist" from VM's perspective
        if (validated.error() == IoError::PathDenied ||
            validated.error() == IoError::SandboxDisabled) {
            return false;
        }
        return std::unexpected{validated.error()};
    }

    try {
        return fs::exists(*validated);
    } catch (...) {
        return std::unexpected{IoError::IoFailed};
    }
}

IoError FilesystemSandbox::delete_file(std::string_view path) {
    auto validated = validate_path(path, true);
    if (!validated) {
        return validated.error();
    }

    try {
        if (!fs::exists(*validated)) {
            return IoError::NotFound;
        }

        if (!fs::is_regular_file(*validated)) {
            return IoError::InvalidPath;
        }

        if (!fs::remove(*validated)) {
            return IoError::IoFailed;
        }

        return IoError::Success;
    } catch (const fs::filesystem_error&) {
        return IoError::PermissionDenied;
    } catch (...) {
        return IoError::IoFailed;
    }
}

// ============================================================================
// Directory Operations
// ============================================================================

IoError FilesystemSandbox::create_directory(std::string_view path, bool recursive) {
    auto validated = validate_path(path, true);
    if (!validated) {
        return validated.error();
    }

    try {
        if (fs::exists(*validated)) {
            if (fs::is_directory(*validated)) {
                return IoError::Success;  // Already exists
            }
            return IoError::AlreadyExists;  // Exists but not a directory
        }

        bool created =
            recursive ? fs::create_directories(*validated) : fs::create_directory(*validated);

        if (!created && !fs::exists(*validated)) {
            return IoError::IoFailed;
        }

        return IoError::Success;
    } catch (const fs::filesystem_error&) {
        return IoError::PermissionDenied;
    } catch (...) {
        return IoError::IoFailed;
    }
}

FilesystemSandbox::Result<std::vector<std::string>>
FilesystemSandbox::list_directory(std::string_view path) const {
    auto validated = validate_path(path, false);
    if (!validated) {
        return std::unexpected{validated.error()};
    }

    try {
        if (!fs::exists(*validated)) {
            return std::unexpected{IoError::NotFound};
        }

        if (!fs::is_directory(*validated)) {
            return std::unexpected{IoError::InvalidPath};
        }

        std::vector<std::string> entries;
        entries.reserve(100);

        for (const auto& entry : fs::directory_iterator(*validated)) {
            // Skip hidden files if not allowed
            auto name = entry.path().filename().string();
            if (!config_.allow_hidden && !name.empty() && name[0] == '.') {
                continue;
            }

            entries.push_back(name);

            // Limit number of entries
            if (entries.size() >= config_.max_dir_entries) {
                break;
            }
        }

        return entries;
    } catch (const fs::filesystem_error&) {
        return std::unexpected{IoError::PermissionDenied};
    } catch (...) {
        return std::unexpected{IoError::IoFailed};
    }
}

}  // namespace dotvm::core::io
