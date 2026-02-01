/// @file package.cpp
/// @brief PRD-007 Package types implementation

#include "dotvm/pkg/package.hpp"

#include <fstream>
#include <iomanip>
#include <sstream>

#include "dotvm/core/policy/json_parser.hpp"

namespace dotvm::pkg {

// ============================================================================
// PackageManifest Implementation
// ============================================================================

core::Result<PackageManifest, PackageError> PackageManifest::from_json(
    std::string_view json) noexcept {
    auto parse_result = core::policy::JsonParser::parse(json);
    if (parse_result.is_err()) {
        return PackageError::InvalidManifest;
    }

    const auto& root = parse_result.value();
    if (!root.is_object()) {
        return PackageError::InvalidManifest;
    }

    PackageManifest manifest;

    // Parse name (required)
    const auto* name_val = root.get("name");
    if (!name_val || !name_val->is_string()) {
        return PackageError::MissingPackageName;
    }
    manifest.name = name_val->as_string();

    if (!is_valid_package_name(manifest.name)) {
        return PackageError::InvalidPackageName;
    }

    // Parse version (required)
    const auto* version_val = root.get("version");
    if (!version_val || !version_val->is_string()) {
        return PackageError::MissingPackageVersion;
    }

    auto version_result = Version::parse(version_val->as_string());
    if (version_result.is_err()) {
        return version_result.error();
    }
    manifest.version = version_result.value();

    // Parse description (optional)
    const auto* desc_val = root.get("description");
    if (desc_val && desc_val->is_string()) {
        manifest.description = desc_val->as_string();
    }

    // Parse dependencies (optional)
    const auto* deps_val = root.get("dependencies");
    if (deps_val && deps_val->is_object()) {
        for (const auto& [dep_name, dep_constraint] : deps_val->as_object()) {
            if (!dep_constraint.is_string()) {
                continue;
            }

            auto constraint_result = VersionConstraint::parse(dep_constraint.as_string());
            if (constraint_result.is_ok()) {
                manifest.dependencies.emplace(dep_name, constraint_result.value());
            }
        }
    }

    return manifest;
}

core::Result<PackageManifest, PackageError> PackageManifest::from_file(
    const std::filesystem::path& path) noexcept {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        return PackageError::ManifestNotFound;
    }

    std::ifstream file(path);
    if (!file) {
        return PackageError::FileReadError;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();

    return from_json(buffer.str());
}

std::string PackageManifest::to_json() const {
    std::ostringstream oss;
    oss << "{\n";
    oss << "  \"name\": \"" << name << "\",\n";
    oss << "  \"version\": \"" << version.to_string() << "\"";

    if (!description.empty()) {
        oss << ",\n  \"description\": \"" << description << "\"";
    }

    if (!dependencies.empty()) {
        oss << ",\n  \"dependencies\": {\n";
        std::size_t i = 0;
        for (const auto& [dep_name, constraint] : dependencies) {
            oss << "    \"" << dep_name << "\": \"" << constraint.to_string() << "\"";
            if (++i < dependencies.size()) {
                oss << ",";
            }
            oss << "\n";
        }
        oss << "  }";
    }

    oss << "\n}\n";
    return oss.str();
}

core::Result<void, PackageError> PackageManifest::save(
    const std::filesystem::path& path) const noexcept {
    std::ofstream file(path);
    if (!file) {
        return PackageError::FileWriteError;
    }

    file << to_json();
    if (!file) {
        return PackageError::FileWriteError;
    }

    return core::Ok;
}

// ============================================================================
// PackageSource Implementation
// ============================================================================

bool PackageSource::is_valid() const noexcept {
    std::error_code ec;

    if (!std::filesystem::exists(path, ec)) {
        return false;
    }

    if (is_archive) {
        return std::filesystem::is_regular_file(path, ec);
    }

    // For directories, check for dotpkg.json
    if (std::filesystem::is_directory(path, ec)) {
        return std::filesystem::exists(path / "dotpkg.json", ec);
    }

    return false;
}

// ============================================================================
// Utility Functions
// ============================================================================

bool is_valid_package_name(std::string_view name) noexcept {
    if (name.empty() || name.length() > 128) {
        return false;
    }

    // Must start with a letter
    if (!std::isalpha(static_cast<unsigned char>(name[0]))) {
        return false;
    }

    // Must not start or end with hyphen
    if (name.front() == '-' || name.back() == '-') {
        return false;
    }

    // Check all characters
    for (char c : name) {
        if (!std::islower(static_cast<unsigned char>(c)) &&
            !std::isdigit(static_cast<unsigned char>(c)) && c != '-') {
            return false;
        }
    }

    return true;
}

std::string checksum_to_hex(const Checksum& checksum) noexcept {
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (std::uint8_t byte : checksum) {
        oss << std::setw(2) << static_cast<int>(byte);
    }
    return oss.str();
}

core::Result<Checksum, PackageError> checksum_from_hex(std::string_view hex) noexcept {
    if (hex.length() != 64) {
        return PackageError::ChecksumMismatch;
    }

    Checksum checksum;
    for (std::size_t i = 0; i < 32; ++i) {
        auto byte_str = hex.substr(i * 2, 2);
        char* end = nullptr;
        auto value = std::strtoul(std::string(byte_str).c_str(), &end, 16);
        if (end != byte_str.data() + 2 || value > 255) {
            return PackageError::ChecksumMismatch;
        }
        checksum[i] = static_cast<std::uint8_t>(value);
    }

    return checksum;
}

}  // namespace dotvm::pkg
