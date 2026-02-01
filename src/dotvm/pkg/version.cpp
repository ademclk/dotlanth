/// @file version.cpp
/// @brief PRD-007 Semantic version parsing implementation

#include "dotvm/pkg/version.hpp"

#include <charconv>
#include <sstream>

namespace dotvm::pkg {

namespace {

/// @brief Check if a character is valid in a prerelease identifier
[[nodiscard]] constexpr bool is_valid_prerelease_char(char c) noexcept {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' ||
           c == '-';
}

/// @brief Parse a numeric version component
/// @param str String view to parse
/// @param error_code Error to return on failure
/// @return Parsed value on success, error on failure
[[nodiscard]] core::Result<std::uint32_t, PackageError>
parse_component(std::string_view str, PackageError error_code) noexcept {
    if (str.empty()) {
        return error_code;
    }

    // Leading zeros are not allowed in SemVer (except for "0" itself)
    if (str.length() > 1 && str[0] == '0') {
        return error_code;
    }

    // Check all characters are digits
    for (char c : str) {
        if (c < '0' || c > '9') {
            return error_code;
        }
    }

    std::uint32_t value = 0;
    auto [ptr, ec] = std::from_chars(str.data(), str.data() + str.size(), value);

    if (ec == std::errc::result_out_of_range) {
        return PackageError::VersionOverflow;
    }
    if (ec != std::errc{} || ptr != str.data() + str.size()) {
        return error_code;
    }

    return value;
}

}  // namespace

core::Result<Version, PackageError> Version::parse(std::string_view version_str) noexcept {
    if (version_str.empty()) {
        return PackageError::EmptyVersion;
    }

    Version result;

    // Find first dot (separates major from minor)
    auto first_dot = version_str.find('.');
    if (first_dot == std::string_view::npos) {
        return PackageError::InvalidVersionFormat;
    }

    // Find second dot (separates minor from patch)
    auto second_dot = version_str.find('.', first_dot + 1);
    if (second_dot == std::string_view::npos) {
        return PackageError::InvalidVersionFormat;
    }

    // Extract major
    auto major_str = version_str.substr(0, first_dot);
    auto major_result = parse_component(major_str, PackageError::InvalidMajorVersion);
    if (major_result.is_err()) {
        return major_result.error();
    }
    result.major = major_result.value();

    // Extract minor
    auto minor_str = version_str.substr(first_dot + 1, second_dot - first_dot - 1);
    auto minor_result = parse_component(minor_str, PackageError::InvalidMinorVersion);
    if (minor_result.is_err()) {
        return minor_result.error();
    }
    result.minor = minor_result.value();

    // Find prerelease separator or end of patch
    auto remaining = version_str.substr(second_dot + 1);
    auto prerelease_sep = remaining.find('-');

    std::string_view patch_str;
    if (prerelease_sep == std::string_view::npos) {
        patch_str = remaining;
    } else {
        patch_str = remaining.substr(0, prerelease_sep);
    }

    // Extract patch
    auto patch_result = parse_component(patch_str, PackageError::InvalidPatchVersion);
    if (patch_result.is_err()) {
        return patch_result.error();
    }
    result.patch = patch_result.value();

    // Extract prerelease if present
    if (prerelease_sep != std::string_view::npos) {
        auto prerelease_str = remaining.substr(prerelease_sep + 1);
        if (prerelease_str.empty()) {
            return PackageError::InvalidPrerelease;
        }

        // Validate prerelease characters
        for (char c : prerelease_str) {
            if (!is_valid_prerelease_char(c)) {
                return PackageError::InvalidPrerelease;
            }
        }

        result.prerelease = std::string(prerelease_str);
    }

    return result;
}

std::string Version::to_string() const {
    std::ostringstream oss;
    oss << major << '.' << minor << '.' << patch;
    if (!prerelease.empty()) {
        oss << '-' << prerelease;
    }
    return oss.str();
}

}  // namespace dotvm::pkg
