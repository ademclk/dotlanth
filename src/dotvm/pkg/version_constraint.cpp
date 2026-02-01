/// @file version_constraint.cpp
/// @brief PRD-007 Version constraint implementation

#include "dotvm/pkg/version_constraint.hpp"

#include <sstream>

namespace dotvm::pkg {

namespace {

/// @brief Skip leading whitespace
[[nodiscard]] std::string_view trim_start(std::string_view str) noexcept {
    while (!str.empty() && (str[0] == ' ' || str[0] == '\t')) {
        str.remove_prefix(1);
    }
    return str;
}

}  // namespace

core::Result<VersionConstraint, PackageError> VersionConstraint::parse(
    std::string_view constraint_str) noexcept {
    constraint_str = trim_start(constraint_str);

    if (constraint_str.empty()) {
        return PackageError::EmptyConstraint;
    }

    VersionConstraint result;

    // Detect operator prefix
    if (constraint_str.starts_with(">=")) {
        result.op = ConstraintOp::GreaterEq;
        constraint_str.remove_prefix(2);
    } else if (constraint_str.starts_with("<=")) {
        result.op = ConstraintOp::LessEq;
        constraint_str.remove_prefix(2);
    } else if (constraint_str.starts_with('>')) {
        result.op = ConstraintOp::Greater;
        constraint_str.remove_prefix(1);
    } else if (constraint_str.starts_with('<')) {
        result.op = ConstraintOp::Less;
        constraint_str.remove_prefix(1);
    } else if (constraint_str.starts_with('^')) {
        result.op = ConstraintOp::Caret;
        constraint_str.remove_prefix(1);
    } else if (constraint_str.starts_with('~')) {
        result.op = ConstraintOp::Tilde;
        constraint_str.remove_prefix(1);
    } else {
        result.op = ConstraintOp::Exact;
    }

    // Skip whitespace after operator
    constraint_str = trim_start(constraint_str);

    if (constraint_str.empty()) {
        return PackageError::InvalidConstraint;
    }

    // Parse the version part
    auto version_result = Version::parse(constraint_str);
    if (version_result.is_err()) {
        return version_result.error();
    }

    result.version = std::move(version_result).value();
    return result;
}

bool VersionConstraint::satisfies(const Version& v) const noexcept {
    switch (op) {
        case ConstraintOp::Exact:
            return v == version;

        case ConstraintOp::Greater:
            return v > version;

        case ConstraintOp::GreaterEq:
            return v >= version;

        case ConstraintOp::Less:
            return v < version;

        case ConstraintOp::LessEq:
            return v <= version;

        case ConstraintOp::Caret: {
            // Caret allows changes that do not modify the left-most non-zero element
            // ^1.2.3 := >=1.2.3 <2.0.0-0
            // ^0.2.3 := >=0.2.3 <0.3.0-0
            // ^0.0.3 := >=0.0.3 <0.0.4-0

            if (v < version) {
                return false;
            }

            if (version.major != 0) {
                // ^X.Y.Z where X > 0: same major version
                return v.major == version.major;
            }
            if (version.minor != 0) {
                // ^0.Y.Z where Y > 0: same major and minor
                return v.major == 0 && v.minor == version.minor;
            }
            // ^0.0.Z: exact patch match
            return v.major == 0 && v.minor == 0 && v.patch == version.patch;
        }

        case ConstraintOp::Tilde: {
            // Tilde allows patch-level changes
            // ~1.2.3 := >=1.2.3 <1.3.0-0

            if (v < version) {
                return false;
            }

            // Same major and minor
            return v.major == version.major && v.minor == version.minor;
        }
    }

    return false;  // Should never reach here
}

std::string VersionConstraint::to_string() const {
    std::ostringstream oss;
    oss << pkg::to_string(op) << version.to_string();
    return oss.str();
}

}  // namespace dotvm::pkg
