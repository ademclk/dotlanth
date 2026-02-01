/// @file lock_file.cpp
/// @brief PRD-007 Lock file implementation

#include "dotvm/pkg/lock_file.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>

namespace dotvm::pkg {

namespace {

/// @brief CRC32 lookup table (IEEE polynomial)
constexpr std::array<std::uint32_t, 256> make_crc32_table() {
    std::array<std::uint32_t, 256> table{};
    for (std::uint32_t i = 0; i < 256; ++i) {
        std::uint32_t crc = i;
        for (int j = 0; j < 8; ++j) {
            if (crc & 1) {
                crc = (crc >> 1) ^ 0xEDB88320;
            } else {
                crc >>= 1;
            }
        }
        table[i] = crc;
    }
    return table;
}

constexpr auto CRC32_TABLE = make_crc32_table();

/// @brief Write a 16-bit little-endian value
void write_u16(std::vector<std::uint8_t>& buf, std::uint16_t value) {
    buf.push_back(static_cast<std::uint8_t>(value & 0xFF));
    buf.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
}

/// @brief Write a 32-bit little-endian value
void write_u32(std::vector<std::uint8_t>& buf, std::uint32_t value) {
    buf.push_back(static_cast<std::uint8_t>(value & 0xFF));
    buf.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
    buf.push_back(static_cast<std::uint8_t>((value >> 16) & 0xFF));
    buf.push_back(static_cast<std::uint8_t>((value >> 24) & 0xFF));
}

/// @brief Write a length-prefixed string
void write_string(std::vector<std::uint8_t>& buf, std::string_view str) {
    write_u16(buf, static_cast<std::uint16_t>(str.size()));
    buf.insert(buf.end(), str.begin(), str.end());
}

/// @brief Read a 16-bit little-endian value
[[nodiscard]] std::uint16_t read_u16(const std::uint8_t* data) {
    return static_cast<std::uint16_t>(data[0]) | (static_cast<std::uint16_t>(data[1]) << 8);
}

/// @brief Read a 32-bit little-endian value
[[nodiscard]] std::uint32_t read_u32(const std::uint8_t* data) {
    return static_cast<std::uint32_t>(data[0]) | (static_cast<std::uint32_t>(data[1]) << 8) |
           (static_cast<std::uint32_t>(data[2]) << 16) |
           (static_cast<std::uint32_t>(data[3]) << 24);
}

/// @brief Read a length-prefixed string
[[nodiscard]] core::Result<std::string, PackageError> read_string(const std::uint8_t*& ptr,
                                                                  const std::uint8_t* end) {
    if (ptr + 2 > end) {
        return PackageError::InvalidLockFile;
    }

    std::uint16_t len = read_u16(ptr);
    ptr += 2;

    if (ptr + len > end) {
        return PackageError::InvalidLockFile;
    }

    std::string result(reinterpret_cast<const char*>(ptr), len);
    ptr += len;
    return result;
}

}  // namespace

// ============================================================================
// CRC32 Implementation
// ============================================================================

std::uint32_t compute_crc32(std::span<const std::uint8_t> data) noexcept {
    std::uint32_t crc = 0xFFFFFFFF;
    for (std::uint8_t byte : data) {
        crc = CRC32_TABLE[(crc ^ byte) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFF;
}

// ============================================================================
// LockFile Implementation
// ============================================================================

core::Result<LockFile, PackageError> LockFile::load(const std::filesystem::path& path) noexcept {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        return PackageError::LockFileNotFound;
    }

    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        return PackageError::FileReadError;
    }

    auto size = file.tellg();
    if (size < 0) {
        return PackageError::FileReadError;
    }

    file.seekg(0, std::ios::beg);

    std::vector<std::uint8_t> data(static_cast<std::size_t>(size));
    if (!file.read(reinterpret_cast<char*>(data.data()), size)) {
        return PackageError::FileReadError;
    }

    return parse(data);
}

core::Result<LockFile, PackageError> LockFile::parse(std::span<const std::uint8_t> data) noexcept {
    // Check minimum size
    if (data.size() < LockFileConstants::HEADER_SIZE + LockFileConstants::FOOTER_SIZE) {
        return PackageError::InvalidLockFile;
    }

    const auto* ptr = data.data();
    const auto* end = data.data() + data.size();

    // Read and validate header
    std::uint32_t magic = read_u32(ptr);
    if (magic != LockFileConstants::MAGIC) {
        return PackageError::InvalidLockFile;
    }

    std::uint16_t version = read_u16(ptr + 4);
    if (version != LockFileConstants::VERSION) {
        return PackageError::LockFileVersionMismatch;
    }

    // Skip reserved (bytes 6-7)
    std::uint32_t package_count = read_u32(ptr + 8);
    std::uint32_t timestamp = read_u32(ptr + 12);

    // Verify CRC32
    std::uint32_t stored_crc = read_u32(end - LockFileConstants::FOOTER_SIZE);
    auto payload = data.subspan(0, data.size() - LockFileConstants::FOOTER_SIZE);
    std::uint32_t computed_crc = compute_crc32(payload);

    if (stored_crc != computed_crc) {
        return PackageError::InvalidLockFile;
    }

    // Parse packages
    LockFile result;
    result.timestamp_ = timestamp;

    ptr += LockFileConstants::HEADER_SIZE;
    const auto* package_end = end - LockFileConstants::FOOTER_SIZE;

    for (std::uint32_t i = 0; i < package_count; ++i) {
        if (ptr >= package_end) {
            return PackageError::InvalidLockFile;
        }

        LockedPackage pkg;

        // Read name
        auto name_result = read_string(ptr, package_end);
        if (name_result.is_err()) {
            return name_result.error();
        }
        pkg.name = std::move(name_result).value();

        // Read version
        if (ptr + 12 > package_end) {
            return PackageError::InvalidLockFile;
        }
        pkg.version.major = read_u32(ptr);
        ptr += 4;
        pkg.version.minor = read_u32(ptr);
        ptr += 4;
        pkg.version.patch = read_u32(ptr);
        ptr += 4;

        // Read checksum
        if (ptr + 32 > package_end) {
            return PackageError::InvalidLockFile;
        }
        std::memcpy(pkg.checksum.data(), ptr, 32);
        ptr += 32;

        // Read dependencies
        if (ptr + 2 > package_end) {
            return PackageError::InvalidLockFile;
        }
        std::uint16_t dep_count = read_u16(ptr);
        ptr += 2;

        for (std::uint16_t j = 0; j < dep_count; ++j) {
            auto dep_result = read_string(ptr, package_end);
            if (dep_result.is_err()) {
                return dep_result.error();
            }
            pkg.dependencies.push_back(std::move(dep_result).value());
        }

        result.packages_.push_back(std::move(pkg));
    }

    return result;
}

core::Result<void, PackageError> LockFile::save(const std::filesystem::path& path) const noexcept {
    auto data = serialize();

    std::ofstream file(path, std::ios::binary);
    if (!file) {
        return PackageError::FileWriteError;
    }

    if (!file.write(reinterpret_cast<const char*>(data.data()),
                    static_cast<std::streamsize>(data.size()))) {
        return PackageError::FileWriteError;
    }

    return core::Ok;
}

std::vector<std::uint8_t> LockFile::serialize() const noexcept {
    std::vector<std::uint8_t> data;
    data.reserve(1024);  // Pre-allocate reasonable size

    // Write header
    write_u32(data, LockFileConstants::MAGIC);
    write_u16(data, LockFileConstants::VERSION);
    write_u16(data, 0);  // Reserved
    write_u32(data, static_cast<std::uint32_t>(packages_.size()));
    write_u32(data, timestamp_);

    // Write packages
    for (const auto& pkg : packages_) {
        write_string(data, pkg.name);
        write_u32(data, pkg.version.major);
        write_u32(data, pkg.version.minor);
        write_u32(data, pkg.version.patch);
        data.insert(data.end(), pkg.checksum.begin(), pkg.checksum.end());

        write_u16(data, static_cast<std::uint16_t>(pkg.dependencies.size()));
        for (const auto& dep : pkg.dependencies) {
            write_string(data, dep);
        }
    }

    // Write CRC32 footer
    std::uint32_t crc = compute_crc32(data);
    write_u32(data, crc);

    return data;
}

void LockFile::add_package(LockedPackage package) noexcept {
    // Update existing or add new
    auto it =
        std::ranges::find_if(packages_, [&](const auto& p) { return p.name == package.name; });

    if (it != packages_.end()) {
        *it = std::move(package);
    } else {
        packages_.push_back(std::move(package));
    }
}

bool LockFile::remove_package(std::string_view name) noexcept {
    auto it = std::ranges::find_if(packages_, [&](const auto& p) { return p.name == name; });

    if (it != packages_.end()) {
        packages_.erase(it);
        return true;
    }
    return false;
}

const LockedPackage* LockFile::get_package(std::string_view name) const noexcept {
    auto it = std::ranges::find_if(packages_, [&](const auto& p) { return p.name == name; });

    return it != packages_.end() ? &*it : nullptr;
}

bool LockFile::has_package(std::string_view name) const noexcept {
    return get_package(name) != nullptr;
}

bool LockFile::verify_integrity() const noexcept {
    // Re-serialize and check if it parses correctly
    auto data = serialize();
    auto result = parse(data);
    return result.is_ok();
}

bool LockFile::matches_manifest(const PackageManifest& manifest) const noexcept {
    // Check that all manifest dependencies are locked
    for (const auto& [name, constraint] : manifest.dependencies) {
        const auto* locked = get_package(name);
        if (!locked) {
            return false;
        }
        if (!constraint.satisfies(locked->version)) {
            return false;
        }
    }
    return true;
}

void LockFile::clear() noexcept {
    packages_.clear();
    timestamp_ = 0;
}

}  // namespace dotvm::pkg
