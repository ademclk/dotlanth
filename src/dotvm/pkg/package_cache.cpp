/// @file package_cache.cpp
/// @brief PRD-007 Package cache implementation

#include "dotvm/pkg/package_cache.hpp"

#include <algorithm>
#include <cstdlib>
#include <fstream>

#include "dotvm/core/crypto/sha256.hpp"

namespace dotvm::pkg {

CacheConfig CacheConfig::defaults() {
    CacheConfig config;

    // Use ~/.dotpkg as the default root directory
    if (const char* home = std::getenv("HOME")) {
        config.root_dir = std::filesystem::path(home) / ".dotpkg";
    } else {
        config.root_dir = std::filesystem::temp_directory_path() / ".dotpkg";
    }

    return config;
}

PackageCache::PackageCache(CacheConfig config) : config_(std::move(config)) {}

core::Result<void, PackageError> PackageCache::initialize() noexcept {
    std::error_code ec;

    // Create the directory structure
    auto packages_dir = config_.root_dir / "cache" / "packages";
    std::filesystem::create_directories(packages_dir, ec);

    if (ec) {
        return PackageError::DirectoryCreateError;
    }

    initialized_ = true;
    return core::Ok;
}

bool PackageCache::is_valid() const noexcept {
    return initialized_ && std::filesystem::exists(config_.root_dir / "cache" / "packages");
}

std::filesystem::path PackageCache::package_path(std::string_view name,
                                                 const Version& version) const noexcept {
    return config_.root_dir / "cache" / "packages" / name / version.to_string();
}

core::Result<CachedPackage, PackageError>
PackageCache::add_from_source(const PackageSource& source,
                              const PackageManifest& manifest) noexcept {
    if (source.is_archive) {
        // TODO: Implement archive extraction
        return PackageError::InvalidArchive;
    }
    return add_from_directory(source.path, manifest);
}

core::Result<CachedPackage, PackageError>
PackageCache::add_from_directory(const std::filesystem::path& source_dir,
                                 const PackageManifest& manifest) noexcept {
    if (!initialized_) {
        return PackageError::CacheNotFound;
    }

    std::error_code ec;

    if (!std::filesystem::exists(source_dir, ec)) {
        return PackageError::DirectoryNotFound;
    }

    // Create the destination directory
    auto dest_dir = package_path(manifest.name, manifest.version);
    std::filesystem::create_directories(dest_dir, ec);
    if (ec) {
        return PackageError::DirectoryCreateError;
    }

    // Copy the package contents
    auto contents_dir = dest_dir / "contents";
    std::filesystem::create_directories(contents_dir, ec);

    // Copy all files from source to contents
    for (const auto& entry : std::filesystem::recursive_directory_iterator(source_dir, ec)) {
        if (ec) {
            break;
        }

        auto relative = std::filesystem::relative(entry.path(), source_dir, ec);
        if (ec) {
            continue;
        }

        auto dest_path = contents_dir / relative;

        if (entry.is_directory()) {
            std::filesystem::create_directories(dest_path, ec);
        } else if (entry.is_regular_file()) {
            std::filesystem::create_directories(dest_path.parent_path(), ec);
            std::filesystem::copy_file(entry.path(), dest_path,
                                       std::filesystem::copy_options::overwrite_existing, ec);
        }
    }

    if (ec) {
        return PackageError::CacheWriteError;
    }

    // Compute and store checksum
    auto checksum_result = compute_directory_checksum(contents_dir);
    if (checksum_result.is_err()) {
        return checksum_result.error();
    }

    auto checksum = checksum_result.value();
    auto write_result = write_checksum(dest_dir, checksum);
    if (write_result.is_err()) {
        return write_result.error();
    }

    // Compute size
    std::uint64_t size = 0;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(contents_dir, ec)) {
        if (entry.is_regular_file()) {
            size += entry.file_size(ec);
        }
    }

    // Create cached package info
    CachedPackage cached;
    cached.name = manifest.name;
    cached.version = manifest.version;
    cached.path = dest_dir;
    cached.checksum = checksum;
    cached.size = size;
    cached.cached_at = static_cast<std::uint32_t>(
        std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()));

    return cached;
}

std::optional<CachedPackage> PackageCache::get(std::string_view name,
                                               const Version& version) const noexcept {
    auto pkg_dir = package_path(name, version);

    std::error_code ec;
    if (!std::filesystem::exists(pkg_dir, ec)) {
        return std::nullopt;
    }

    CachedPackage cached;
    cached.name = std::string(name);
    cached.version = version;
    cached.path = pkg_dir;

    // Read checksum if available
    if (auto checksum = read_stored_checksum(pkg_dir)) {
        cached.checksum = *checksum;
    }

    // Compute size
    auto contents_dir = pkg_dir / "contents";
    if (std::filesystem::exists(contents_dir, ec)) {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(contents_dir, ec)) {
            if (entry.is_regular_file()) {
                cached.size += entry.file_size(ec);
            }
        }
    }

    return cached;
}

bool PackageCache::has(std::string_view name, const Version& version) const noexcept {
    std::error_code ec;
    return std::filesystem::exists(package_path(name, version), ec);
}

core::Result<bool, PackageError> PackageCache::remove(std::string_view name,
                                                      const Version& version) noexcept {
    auto pkg_dir = package_path(name, version);

    std::error_code ec;
    if (!std::filesystem::exists(pkg_dir, ec)) {
        return false;
    }

    std::filesystem::remove_all(pkg_dir, ec);
    if (ec) {
        return PackageError::CacheCleanupError;
    }

    // Also remove the package name directory if it's now empty
    auto name_dir = pkg_dir.parent_path();
    if (std::filesystem::is_empty(name_dir, ec) && !ec) {
        std::filesystem::remove(name_dir, ec);
    }

    return true;
}

core::Result<std::size_t, PackageError> PackageCache::remove_all(std::string_view name) noexcept {
    auto name_dir = config_.root_dir / "cache" / "packages" / name;

    std::error_code ec;
    if (!std::filesystem::exists(name_dir, ec)) {
        return std::size_t{0};
    }

    std::size_t count = 0;
    for (const auto& entry : std::filesystem::directory_iterator(name_dir, ec)) {
        if (entry.is_directory()) {
            ++count;
        }
    }

    std::filesystem::remove_all(name_dir, ec);
    if (ec) {
        return PackageError::CacheCleanupError;
    }

    return count;
}

std::vector<Version> PackageCache::list_versions(std::string_view name) const noexcept {
    std::vector<Version> versions;

    auto name_dir = config_.root_dir / "cache" / "packages" / name;

    std::error_code ec;
    if (!std::filesystem::exists(name_dir, ec)) {
        return versions;
    }

    for (const auto& entry : std::filesystem::directory_iterator(name_dir, ec)) {
        if (entry.is_directory()) {
            auto version_str = entry.path().filename().string();
            auto result = Version::parse(version_str);
            if (result.is_ok()) {
                versions.push_back(result.value());
            }
        }
    }

    // Sort versions in ascending order
    std::ranges::sort(versions);

    return versions;
}

std::vector<CachedPackage> PackageCache::list_all() const noexcept {
    std::vector<CachedPackage> packages;

    auto packages_dir = config_.root_dir / "cache" / "packages";

    std::error_code ec;
    if (!std::filesystem::exists(packages_dir, ec)) {
        return packages;
    }

    for (const auto& name_entry : std::filesystem::directory_iterator(packages_dir, ec)) {
        if (!name_entry.is_directory()) {
            continue;
        }

        auto name = name_entry.path().filename().string();

        for (const auto& version_entry :
             std::filesystem::directory_iterator(name_entry.path(), ec)) {
            if (!version_entry.is_directory()) {
                continue;
            }

            auto version_str = version_entry.path().filename().string();
            auto version_result = Version::parse(version_str);
            if (version_result.is_ok()) {
                if (auto pkg = get(name, version_result.value())) {
                    packages.push_back(std::move(*pkg));
                }
            }
        }
    }

    return packages;
}

std::uint64_t PackageCache::total_size() const noexcept {
    std::uint64_t total = 0;

    for (const auto& pkg : list_all()) {
        total += pkg.size;
    }

    return total;
}

std::size_t PackageCache::package_count() const noexcept {
    return list_all().size();
}

core::Result<bool, PackageError> PackageCache::verify(std::string_view name,
                                                      const Version& version) const noexcept {
    auto pkg = get(name, version);
    if (!pkg) {
        return PackageError::PackageNotFound;
    }

    auto stored = read_stored_checksum(pkg->path);
    if (!stored) {
        return false;
    }

    auto computed_result = compute_directory_checksum(pkg->path / "contents");
    if (computed_result.is_err()) {
        return computed_result.error();
    }

    return *stored == computed_result.value();
}

std::vector<std::pair<std::string, Version>> PackageCache::verify_all() const noexcept {
    std::vector<std::pair<std::string, Version>> failed;

    for (const auto& pkg : list_all()) {
        auto result = verify(pkg.name, pkg.version);
        if (result.is_err() || !result.value()) {
            failed.emplace_back(pkg.name, pkg.version);
        }
    }

    return failed;
}

core::Result<std::size_t, PackageError> PackageCache::cleanup() noexcept {
    auto failed = verify_all();
    std::size_t removed = 0;

    for (const auto& [name, version] : failed) {
        auto result = remove(name, version);
        if (result.is_ok() && result.value()) {
            ++removed;
        }
    }

    return removed;
}

core::Result<void, PackageError> PackageCache::clear() noexcept {
    auto packages_dir = config_.root_dir / "cache" / "packages";

    std::error_code ec;
    std::filesystem::remove_all(packages_dir, ec);
    if (ec) {
        return PackageError::CacheCleanupError;
    }

    // Recreate the empty packages directory
    std::filesystem::create_directories(packages_dir, ec);
    if (ec) {
        return PackageError::DirectoryCreateError;
    }

    return core::Ok;
}

core::Result<Checksum, PackageError>
PackageCache::compute_directory_checksum(const std::filesystem::path& dir) const noexcept {
    core::crypto::Sha256 hasher;

    std::error_code ec;

    // Sort entries for deterministic ordering
    std::vector<std::filesystem::path> entries;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(dir, ec)) {
        if (entry.is_regular_file()) {
            entries.push_back(entry.path());
        }
    }
    std::ranges::sort(entries);

    for (const auto& path : entries) {
        // Hash relative path
        auto rel = std::filesystem::relative(path, dir, ec);
        auto rel_str = rel.string();
        hasher.update(
            std::span{reinterpret_cast<const std::uint8_t*>(rel_str.data()), rel_str.size()});

        // Hash file contents
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            return PackageError::FileReadError;
        }

        std::array<char, 8192> buffer{};
        while (file.read(buffer.data(), buffer.size()) || file.gcount() > 0) {
            hasher.update(std::span{reinterpret_cast<const std::uint8_t*>(buffer.data()),
                                    static_cast<std::size_t>(file.gcount())});
        }
    }

    auto hash = hasher.finalize();
    Checksum checksum;
    std::copy(hash.begin(), hash.end(), checksum.begin());
    return checksum;
}

std::optional<Checksum>
PackageCache::read_stored_checksum(const std::filesystem::path& pkg_dir) const noexcept {
    auto checksum_file = pkg_dir / ".checksum";

    std::ifstream file(checksum_file, std::ios::binary);
    if (!file) {
        return std::nullopt;
    }

    Checksum checksum;
    file.read(reinterpret_cast<char*>(checksum.data()), checksum.size());

    if (file.gcount() != static_cast<std::streamsize>(checksum.size())) {
        return std::nullopt;
    }

    return checksum;
}

core::Result<void, PackageError>
PackageCache::write_checksum(const std::filesystem::path& pkg_dir,
                             const Checksum& checksum) const noexcept {
    auto checksum_file = pkg_dir / ".checksum";

    std::ofstream file(checksum_file, std::ios::binary);
    if (!file) {
        return PackageError::FileWriteError;
    }

    file.write(reinterpret_cast<const char*>(checksum.data()), checksum.size());
    if (!file) {
        return PackageError::FileWriteError;
    }

    return core::Ok;
}

}  // namespace dotvm::pkg
