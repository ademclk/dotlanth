/// @file package_registry.cpp
/// @brief PRD-007 Package registry implementation

#include "dotvm/pkg/package_registry.hpp"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <sstream>

#include "dotvm/core/policy/json_parser.hpp"

namespace dotvm::pkg {

namespace {

/// @brief Convert checksum to hex string
std::string to_hex(const Checksum& checksum) {
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (std::uint8_t byte : checksum) {
        oss << std::setw(2) << static_cast<int>(byte);
    }
    return oss.str();
}

/// @brief Parse checksum from hex string
std::optional<Checksum> from_hex(std::string_view hex) {
    if (hex.length() != 64) {
        return std::nullopt;
    }

    Checksum checksum;
    for (std::size_t i = 0; i < 32; ++i) {
        auto byte_str = hex.substr(i * 2, 2);
        char* end = nullptr;
        auto value = std::strtoul(std::string(byte_str).c_str(), &end, 16);
        if (end != byte_str.data() + 2 || value > 255) {
            return std::nullopt;
        }
        checksum[i] = static_cast<std::uint8_t>(value);
    }
    return checksum;
}

/// @brief Escape a string for JSON
std::string escape_json(std::string_view str) {
    std::ostringstream oss;
    for (char c : str) {
        switch (c) {
            case '"':
                oss << "\\\"";
                break;
            case '\\':
                oss << "\\\\";
                break;
            case '\n':
                oss << "\\n";
                break;
            case '\r':
                oss << "\\r";
                break;
            case '\t':
                oss << "\\t";
                break;
            default:
                oss << c;
        }
    }
    return oss.str();
}

}  // namespace

PackageRegistry::PackageRegistry(std::filesystem::path path) noexcept : path_(std::move(path)) {}

core::Result<void, PackageError> PackageRegistry::load() noexcept {
    std::error_code ec;
    if (!std::filesystem::exists(path_, ec)) {
        // No file yet - that's OK, start with empty registry
        return core::Ok;
    }

    std::ifstream file(path_);
    if (!file) {
        return PackageError::FileReadError;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    auto json_str = buffer.str();

    auto parse_result = core::policy::JsonParser::parse(json_str);
    if (parse_result.is_err()) {
        return PackageError::RegistryCorrupted;
    }

    const auto& json = parse_result.value();
    if (!json.is_object()) {
        return PackageError::RegistryCorrupted;
    }

    const auto* packages_val = json.get("packages");
    if (!packages_val || !packages_val->is_object()) {
        return PackageError::RegistryCorrupted;
    }

    packages_.clear();

    for (const auto& [name, pkg_json] : packages_val->as_object()) {
        if (!pkg_json.is_object()) {
            continue;
        }

        RegistryEntry entry;

        // Parse version
        const auto* version_val = pkg_json.get("version");
        if (version_val && version_val->is_string()) {
            auto version_result = Version::parse(version_val->as_string());
            if (version_result.is_ok()) {
                entry.version = version_result.value();
            }
        }

        // Parse checksum
        const auto* checksum_val = pkg_json.get("checksum");
        if (checksum_val && checksum_val->is_string()) {
            if (auto checksum = from_hex(checksum_val->as_string())) {
                entry.checksum = *checksum;
            }
        }

        // Parse installedAt
        const auto* installed_at_val = pkg_json.get("installedAt");
        if (installed_at_val && installed_at_val->is_int()) {
            entry.installed_at = static_cast<std::uint32_t>(installed_at_val->as_int());
        }

        // Parse dependencies
        const auto* deps_val = pkg_json.get("dependencies");
        if (deps_val && deps_val->is_array()) {
            for (const auto& dep : deps_val->as_array()) {
                if (dep.is_string()) {
                    entry.dependencies.push_back(dep.as_string());
                }
            }
        }

        // Parse install path
        const auto* path_val = pkg_json.get("installPath");
        if (path_val && path_val->is_string()) {
            entry.install_path = path_val->as_string();
        }

        packages_[name] = std::move(entry);
    }

    return core::Ok;
}

core::Result<void, PackageError> PackageRegistry::save() const noexcept {
    std::ofstream file(path_);
    if (!file) {
        return PackageError::FileWriteError;
    }

    // Write JSON manually to avoid pulling in a serializer
    file << "{\n";
    file << "  \"packages\": {\n";

    std::size_t i = 0;
    for (const auto& [name, entry] : packages_) {
        file << "    \"" << escape_json(name) << "\": {\n";
        file << "      \"version\": \"" << entry.version.to_string() << "\",\n";
        file << "      \"checksum\": \"" << to_hex(entry.checksum) << "\",\n";
        file << "      \"installedAt\": " << entry.installed_at << ",\n";

        file << "      \"dependencies\": [";
        for (std::size_t d = 0; d < entry.dependencies.size(); ++d) {
            file << "\"" << escape_json(entry.dependencies[d]) << "\"";
            if (d + 1 < entry.dependencies.size()) {
                file << ", ";
            }
        }
        file << "],\n";

        file << "      \"installPath\": \"" << escape_json(entry.install_path.string()) << "\"\n";
        file << "    }";

        if (++i < packages_.size()) {
            file << ",";
        }
        file << "\n";
    }

    file << "  }\n";
    file << "}\n";

    if (!file) {
        return PackageError::FileWriteError;
    }

    return core::Ok;
}

bool PackageRegistry::exists() const noexcept {
    std::error_code ec;
    return std::filesystem::exists(path_, ec);
}

void PackageRegistry::add(std::string name, RegistryEntry entry) noexcept {
    packages_[std::move(name)] = std::move(entry);
}

bool PackageRegistry::remove(std::string_view name) noexcept {
    auto it = packages_.find(std::string(name));
    if (it != packages_.end()) {
        packages_.erase(it);
        return true;
    }
    return false;
}

const RegistryEntry* PackageRegistry::get(std::string_view name) const noexcept {
    auto it = packages_.find(std::string(name));
    return it != packages_.end() ? &it->second : nullptr;
}

bool PackageRegistry::has(std::string_view name) const noexcept {
    return packages_.contains(std::string(name));
}

std::vector<std::string> PackageRegistry::package_names() const noexcept {
    std::vector<std::string> names;
    names.reserve(packages_.size());
    for (const auto& [name, _] : packages_) {
        names.push_back(name);
    }
    return names;
}

std::vector<std::string> PackageRegistry::dependents_of(std::string_view name) const noexcept {
    std::vector<std::string> dependents;
    std::string name_str(name);

    for (const auto& [pkg_name, entry] : packages_) {
        if (std::ranges::find(entry.dependencies, name_str) != entry.dependencies.end()) {
            dependents.push_back(pkg_name);
        }
    }

    return dependents;
}

void PackageRegistry::clear() noexcept {
    packages_.clear();
}

}  // namespace dotvm::pkg
