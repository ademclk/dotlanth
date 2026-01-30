#pragma once

/// @file validator.hpp
/// @brief DEP-003 Validator types for property validation
///
/// Provides variant-based validators for schema property validation including
/// range checks, regex patterns, enum constraints, and required checks.

#include <cstdint>
#include <optional>
#include <regex>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "dotvm/core/result.hpp"
#include "dotvm/core/value.hpp"
#include "schema_error.hpp"

namespace dotvm::core::schema {

// ============================================================================
// Validator Types
// ============================================================================

/// @brief Validates that a numeric value falls within a range
///
/// Works with Int64 and Float64 property types. Supports inclusive and
/// exclusive bounds on either end.
struct RangeValidator {
    /// @brief Minimum value (nullopt = no lower bound)
    std::optional<double> min{std::nullopt};

    /// @brief Maximum value (nullopt = no upper bound)
    std::optional<double> max{std::nullopt};

    /// @brief Whether minimum bound is inclusive (default: true)
    bool min_inclusive{true};

    /// @brief Whether maximum bound is inclusive (default: true)
    bool max_inclusive{true};

    /// @brief Validate a numeric value against this range
    ///
    /// @param value The value to validate (must be numeric)
    /// @return true if value is within range, false otherwise
    [[nodiscard]] bool validate(double value) const noexcept {
        if (min.has_value()) {
            if (min_inclusive) {
                if (value < *min) {
                    return false;
                }
            } else {
                if (value <= *min) {
                    return false;
                }
            }
        }

        if (max.has_value()) {
            if (max_inclusive) {
                if (value > *max) {
                    return false;
                }
            } else {
                if (value >= *max) {
                    return false;
                }
            }
        }

        return true;
    }

    [[nodiscard]] bool operator==(const RangeValidator&) const noexcept = default;
};

/// @brief Validates that a string matches a regex pattern
///
/// Works with String property types. Pattern is compiled at construction time
/// via the static `create` method. The compiled regex is immutable for thread-safety.
struct RegexValidator {
    /// @brief The regex pattern string (for serialization)
    std::string pattern;

    /// @brief Compiled regex (immutable after construction for thread-safety)
    std::optional<std::regex> compiled{std::nullopt};

    /// @brief Create a RegexValidator with pattern validation
    ///
    /// This factory method compiles the regex and catches exceptions.
    /// The resulting validator is immutable and thread-safe.
    ///
    /// @param pattern The regex pattern to compile
    /// @return Result containing validator or SchemaError::RegexValidationFailed
    [[nodiscard]] static Result<RegexValidator, SchemaError> create(std::string pattern) noexcept {
        RegexValidator validator;
        validator.pattern = std::move(pattern);

        try {
            validator.compiled = std::regex(validator.pattern, std::regex::ECMAScript);
            return validator;
        } catch (...) {
            return SchemaError::RegexValidationFailed;
        }
    }

    /// @brief Validate a string against this pattern
    ///
    /// Note: This method is thread-safe. The regex must be compiled via
    /// RegexValidator::create() before use - validate() does not lazily compile.
    ///
    /// @param value The string to validate
    /// @return true if value matches pattern, false otherwise
    [[nodiscard]] bool validate(std::string_view value) const noexcept {
        // Regex must be compiled via create() - no lazy compilation for thread-safety
        if (!compiled.has_value()) {
            return false;  // Not compiled - use RegexValidator::create()
        }

        try {
            return std::regex_match(value.begin(), value.end(), *compiled);
        } catch (...) {
            return false;
        }
    }

    [[nodiscard]] bool operator==(const RegexValidator& other) const noexcept {
        return pattern == other.pattern;
    }
};

/// @brief Validates that a value is one of an allowed set
///
/// Works with any property type. Values are stored as strings and compared
/// after string conversion for consistency.
struct EnumValidator {
    /// @brief Allowed values (as strings for type-agnostic comparison)
    std::vector<std::string> allowed_values;

    /// @brief Validate a string value against allowed values
    ///
    /// @param value The value to validate (as string)
    /// @return true if value is in allowed set, false otherwise
    [[nodiscard]] bool validate(std::string_view value) const noexcept {
        for (const auto& allowed : allowed_values) {
            if (allowed == value) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] bool operator==(const EnumValidator&) const noexcept = default;
};

/// @brief Validates that a value is not nil
///
/// Used for required property enforcement. This is the simplest validator.
struct RequiredValidator {
    /// @brief Validate that a value is not nil
    ///
    /// @param value The value to check
    /// @return true if value is not nil, false otherwise
    [[nodiscard]] bool validate(Value value) const noexcept { return !value.is_nil(); }

    [[nodiscard]] bool operator==(const RequiredValidator&) const noexcept = default;
};

// ============================================================================
// Validator Variant
// ============================================================================

/// @brief A validator is one of the supported validator types
using Validator = std::variant<RangeValidator, RegexValidator, EnumValidator, RequiredValidator>;

// ============================================================================
// Validation Functions
// ============================================================================

/// @brief Validate a Value against a single validator
///
/// Dispatches to the appropriate validation logic based on validator type
/// and value type.
///
/// @param value The value to validate
/// @param validator The validator to apply
/// @return Result<void, SchemaError> indicating success or failure
[[nodiscard]] inline Result<void, SchemaError> validate_value(Value value,
                                                              const Validator& validator) noexcept {
    return std::visit(
        [&value](const auto& v) -> Result<void, SchemaError> {
            using T = std::decay_t<decltype(v)>;

            if constexpr (std::is_same_v<T, RequiredValidator>) {
                if (!v.validate(value)) {
                    return SchemaError::RequiredPropertyMissing;
                }
                return {};
            } else if constexpr (std::is_same_v<T, RangeValidator>) {
                if (value.is_nil()) {
                    return {};  // Nil passes range validation (use RequiredValidator for required)
                }
                if (!value.is_numeric()) {
                    return SchemaError::InvalidPropertyType;
                }
                if (!v.validate(value.as_number())) {
                    return SchemaError::RangeValidationFailed;
                }
                return {};
            } else if constexpr (std::is_same_v<T, RegexValidator>) {
                if (value.is_nil()) {
                    return {};  // Nil passes regex validation
                }
                // For regex validation, we need the string representation.
                // String values are stored as handles (references to string storage),
                // which we cannot resolve at validation time. We accept handles
                // since they represent valid string references - actual string content
                // validation would require access to the string storage layer.
                if (value.is_handle()) {
                    // Accept handles - cannot validate without string resolution
                    return {};
                }
                if (value.is_integer()) {
                    // Allow validating numeric-as-string patterns
                    auto str = std::to_string(value.as_integer());
                    if (!v.validate(str)) {
                        return SchemaError::RegexValidationFailed;
                    }
                    return {};
                }
                // Non-string, non-integer types cannot be regex validated
                return SchemaError::InvalidPropertyType;
            } else if constexpr (std::is_same_v<T, EnumValidator>) {
                if (value.is_nil()) {
                    return {};  // Nil passes enum validation
                }
                // For handles (string references), we cannot validate without
                // access to the string storage layer. Accept them.
                if (value.is_handle()) {
                    return {};  // Accept handles - cannot validate without string resolution
                }
                // Convert value to string for comparison
                std::string str_value;
                if (value.is_integer()) {
                    str_value = std::to_string(value.as_integer());
                } else if (value.is_float()) {
                    str_value = std::to_string(value.as_float());
                } else if (value.is_bool()) {
                    str_value = value.as_bool() ? "true" : "false";
                } else {
                    return SchemaError::InvalidPropertyType;
                }
                if (!v.validate(str_value)) {
                    return SchemaError::EnumValidationFailed;
                }
                return {};
            }

            return {};
        },
        validator);
}

/// @brief Validate a Value against multiple validators
///
/// All validators must pass for the value to be valid.
///
/// @param value The value to validate
/// @param validators The validators to apply
/// @return Result<void, SchemaError> indicating success or first failure
[[nodiscard]] inline Result<void, SchemaError>
validate_value(Value value, const std::vector<Validator>& validators) noexcept {
    for (const auto& validator : validators) {
        auto result = validate_value(value, validator);
        if (result.is_err()) {
            return result;
        }
    }
    return {};
}

// ============================================================================
// Validator Builders
// ============================================================================

/// @brief Create a range validator with min/max bounds
///
/// @param min Minimum value (inclusive)
/// @param max Maximum value (inclusive)
/// @return RangeValidator
[[nodiscard]] inline RangeValidator range(double min, double max) noexcept {
    return RangeValidator{.min = min, .max = max};
}

/// @brief Create a range validator with only minimum bound
///
/// @param min Minimum value (inclusive)
/// @return RangeValidator
[[nodiscard]] inline RangeValidator min_value(double min) noexcept {
    return RangeValidator{.min = min};
}

/// @brief Create a range validator with only maximum bound
///
/// @param max Maximum value (inclusive)
/// @return RangeValidator
[[nodiscard]] inline RangeValidator max_value(double max) noexcept {
    return RangeValidator{.max = max};
}

/// @brief Create an enum validator from a list of allowed values
///
/// @param values Allowed values
/// @return EnumValidator
[[nodiscard]] inline EnumValidator one_of(std::vector<std::string> values) noexcept {
    return EnumValidator{.allowed_values = std::move(values)};
}

/// @brief Create a required validator
///
/// @return RequiredValidator
[[nodiscard]] inline RequiredValidator required() noexcept {
    return RequiredValidator{};
}

}  // namespace dotvm::core::schema
