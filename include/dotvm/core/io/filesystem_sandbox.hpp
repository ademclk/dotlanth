/// @file filesystem_sandbox.hpp
/// @brief Filesystem sandboxing for secure IO operations in the DotVM
///
/// Provides path validation and sandboxing to prevent unauthorized filesystem access.
/// All paths are validated against an allowlist before any IO operation.

#pragma once

#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dotvm::core::io {

// ============================================================================
// IO Error
// ============================================================================

/// @brief Error codes for IO operations
enum class IoError : std::uint8_t {
    Success = 0,       ///< Operation succeeded
    PathDenied,        ///< Path not in allowed directories
    PathTraversal,     ///< Path traversal attempt detected
    NotFound,          ///< File or directory not found
    AlreadyExists,     ///< File or directory already exists
    PermissionDenied,  ///< OS-level permission denied
    IoFailed,          ///< Generic IO failure
    InvalidPath,       ///< Path is malformed
    TooLarge,          ///< File too large
    SandboxDisabled,   ///< No sandbox configured (all operations denied)
};

/// @brief Convert IoError to string
[[nodiscard]] constexpr const char* to_string(IoError err) noexcept {
    switch (err) {
        case IoError::Success:
            return "success";
        case IoError::PathDenied:
            return "path denied";
        case IoError::PathTraversal:
            return "path traversal detected";
        case IoError::NotFound:
            return "not found";
        case IoError::AlreadyExists:
            return "already exists";
        case IoError::PermissionDenied:
            return "permission denied";
        case IoError::IoFailed:
            return "IO operation failed";
        case IoError::InvalidPath:
            return "invalid path";
        case IoError::TooLarge:
            return "file too large";
        case IoError::SandboxDisabled:
            return "sandbox disabled";
    }
    return "unknown";
}

// ============================================================================
// Filesystem Sandbox
// ============================================================================

/// @brief Sandbox for filesystem operations
///
/// Restricts filesystem access to a configurable set of allowed directories.
/// All paths are validated and canonicalized before any operation.
///
/// @code
/// FilesystemSandbox sandbox;
/// sandbox.allow_directory("/home/user/workspace");
/// sandbox.allow_read_only("/etc");
///
/// auto result = sandbox.read_file("/home/user/workspace/data.txt");
/// if (result) {
///     std::cout << *result;
/// }
/// @endcode
class FilesystemSandbox {
public:
    /// Result type for operations
    template <typename T>
    using Result = std::expected<T, IoError>;

    /// @brief Configuration for sandbox
    struct Config {
        std::size_t max_file_size = 10 * 1024 * 1024;  ///< 10MB default max
        std::size_t max_dir_entries = 10000;           ///< Max entries from dir_list
        bool follow_symlinks = false;                  ///< Follow symlinks (security risk)
        bool allow_hidden = false;                     ///< Allow hidden files (.*)
    };

    /// Default constructor - no allowed directories (all ops denied)
    FilesystemSandbox() noexcept = default;

    /// Construct with configuration
    explicit FilesystemSandbox(Config config) noexcept : config_(config) {}

    // =========================================================================
    // Sandbox Configuration
    // =========================================================================

    /// @brief Add a directory to the allowlist with read-write access
    ///
    /// @param path Absolute path to allow
    /// @return Success or error if path is invalid
    [[nodiscard]] IoError allow_directory(std::string_view path) noexcept;

    /// @brief Add a directory to the allowlist with read-only access
    ///
    /// @param path Absolute path to allow for reading
    /// @return Success or error if path is invalid
    [[nodiscard]] IoError allow_read_only(std::string_view path) noexcept;

    /// @brief Clear all allowed directories
    void clear_allowlist() noexcept;

    /// @brief Check if any directories are allowed
    [[nodiscard]] bool has_allowlist() const noexcept { return !allowed_dirs_.empty(); }

    // =========================================================================
    // Path Validation
    // =========================================================================

    /// @brief Validate and canonicalize a path
    ///
    /// @param path Path to validate
    /// @param require_write Whether write access is required
    /// @return Canonicalized path if valid, error otherwise
    [[nodiscard]] Result<std::filesystem::path>
    validate_path(std::string_view path, bool require_write = false) const noexcept;

    /// @brief Check if a path is within the sandbox
    [[nodiscard]] bool is_allowed(std::string_view path, bool require_write = false) const noexcept;

    // =========================================================================
    // File Operations
    // =========================================================================

    /// @brief Read entire file contents
    ///
    /// @param path Path to file
    /// @return File contents or error
    [[nodiscard]] Result<std::string> read_file(std::string_view path) const;

    /// @brief Write contents to file (creates or overwrites)
    ///
    /// @param path Path to file
    /// @param content Content to write
    /// @return Success or error
    [[nodiscard]] IoError write_file(std::string_view path, std::string_view content);

    /// @brief Append content to file
    ///
    /// @param path Path to file
    /// @param content Content to append
    /// @return Success or error
    [[nodiscard]] IoError append_file(std::string_view path, std::string_view content);

    /// @brief Check if file exists
    ///
    /// @param path Path to check
    /// @return True if exists, false if not, error on validation failure
    [[nodiscard]] Result<bool> file_exists(std::string_view path) const noexcept;

    /// @brief Delete a file
    ///
    /// @param path Path to file
    /// @return Success or error
    [[nodiscard]] IoError delete_file(std::string_view path);

    // =========================================================================
    // Directory Operations
    // =========================================================================

    /// @brief Create a directory
    ///
    /// @param path Path to directory
    /// @param recursive Create parent directories if needed
    /// @return Success or error
    [[nodiscard]] IoError create_directory(std::string_view path, bool recursive = false);

    /// @brief List directory contents
    ///
    /// @param path Path to directory
    /// @return List of entry names (files and subdirectories)
    [[nodiscard]] Result<std::vector<std::string>> list_directory(std::string_view path) const;

    // =========================================================================
    // Accessors
    // =========================================================================

    /// @brief Get configuration
    [[nodiscard]] const Config& config() const noexcept { return config_; }

    /// @brief Get list of allowed directories
    [[nodiscard]] const std::vector<std::filesystem::path>& allowed_directories() const noexcept {
        return allowed_dirs_;
    }

    /// @brief Get list of read-only directories
    [[nodiscard]] const std::vector<std::filesystem::path>& read_only_directories() const noexcept {
        return read_only_dirs_;
    }

private:
    /// Check if path is under any of the given directories
    [[nodiscard]] bool
    is_under_directory(const std::filesystem::path& canonical,
                       const std::vector<std::filesystem::path>& dirs) const noexcept;

    Config config_;
    std::vector<std::filesystem::path> allowed_dirs_;    ///< Read-write directories
    std::vector<std::filesystem::path> read_only_dirs_;  ///< Read-only directories
};

}  // namespace dotvm::core::io
