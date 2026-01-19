#pragma once

/// @file json_parser.hpp
/// @brief SEC-009 Minimal JSON parser for policy definitions
///
/// A custom recursive descent JSON parser with no external dependencies.
/// Supports the JSON subset needed for policy DSL parsing.

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "dotvm/core/policy/policy_error.hpp"
#include "dotvm/core/result.hpp"

namespace dotvm::core::policy {

// Forward declaration
class JsonValue;

/// @brief JSON array type
using JsonArray = std::vector<JsonValue>;

/// @brief JSON object type (preserves insertion order is not guaranteed)
using JsonObject = std::map<std::string, JsonValue>;

/// @brief JSON value types
using JsonVariant = std::variant<std::nullptr_t,  // null
                                 bool,            // boolean
                                 std::int64_t,    // integer
                                 double,          // floating point
                                 std::string,     // string
                                 JsonArray,       // array
                                 JsonObject       // object
                                 >;

/// @brief JSON value container
///
/// Represents any JSON value: null, boolean, number, string, array, or object.
class JsonValue {
public:
    /// @brief Value type enumeration
    enum class Type : std::uint8_t {
        Null,
        Bool,
        Int,
        Float,
        String,
        Array,
        Object,
    };

    // ========== Constructors ==========

    /// Default construct as null
    JsonValue() noexcept : data_{nullptr} {}

    /// Construct from null
    JsonValue(std::nullptr_t) noexcept : data_{nullptr} {}

    /// Construct from boolean
    JsonValue(bool value) noexcept : data_{value} {}

    /// Construct from integer
    JsonValue(std::int64_t value) noexcept : data_{value} {}

    /// Construct from int (promotes to int64)
    JsonValue(int value) noexcept : data_{static_cast<std::int64_t>(value)} {}

    /// Construct from double
    JsonValue(double value) noexcept : data_{value} {}

    /// Construct from string
    JsonValue(std::string value) noexcept : data_{std::move(value)} {}

    /// Construct from C-string
    JsonValue(const char* value) : data_{std::string{value}} {}

    /// Construct from array
    JsonValue(JsonArray value) noexcept : data_{std::move(value)} {}

    /// Construct from object
    JsonValue(JsonObject value) noexcept : data_{std::move(value)} {}

    // ========== Type Queries ==========

    /// Get the value type
    [[nodiscard]] Type type() const noexcept { return static_cast<Type>(data_.index()); }

    [[nodiscard]] bool is_null() const noexcept {
        return std::holds_alternative<std::nullptr_t>(data_);
    }
    [[nodiscard]] bool is_bool() const noexcept { return std::holds_alternative<bool>(data_); }
    [[nodiscard]] bool is_int() const noexcept {
        return std::holds_alternative<std::int64_t>(data_);
    }
    [[nodiscard]] bool is_float() const noexcept { return std::holds_alternative<double>(data_); }
    [[nodiscard]] bool is_number() const noexcept { return is_int() || is_float(); }
    [[nodiscard]] bool is_string() const noexcept {
        return std::holds_alternative<std::string>(data_);
    }
    [[nodiscard]] bool is_array() const noexcept {
        return std::holds_alternative<JsonArray>(data_);
    }
    [[nodiscard]] bool is_object() const noexcept {
        return std::holds_alternative<JsonObject>(data_);
    }

    // ========== Value Access ==========

    /// Get as boolean (undefined behavior if not bool)
    [[nodiscard]] bool as_bool() const noexcept { return std::get<bool>(data_); }

    /// Get as integer (undefined behavior if not int)
    [[nodiscard]] std::int64_t as_int() const noexcept { return std::get<std::int64_t>(data_); }

    /// Get as double (works for both int and float)
    [[nodiscard]] double as_float() const noexcept {
        if (is_int()) {
            return static_cast<double>(std::get<std::int64_t>(data_));
        }
        return std::get<double>(data_);
    }

    /// Get as string (undefined behavior if not string)
    [[nodiscard]] const std::string& as_string() const noexcept {
        return std::get<std::string>(data_);
    }

    /// Get as array (undefined behavior if not array)
    [[nodiscard]] const JsonArray& as_array() const noexcept { return std::get<JsonArray>(data_); }

    /// Get as mutable array
    [[nodiscard]] JsonArray& as_array() noexcept { return std::get<JsonArray>(data_); }

    /// Get as object (undefined behavior if not object)
    [[nodiscard]] const JsonObject& as_object() const noexcept {
        return std::get<JsonObject>(data_);
    }

    /// Get as mutable object
    [[nodiscard]] JsonObject& as_object() noexcept { return std::get<JsonObject>(data_); }

    // ========== Object Access ==========

    /// Check if object contains a key
    [[nodiscard]] bool contains(std::string_view key) const {
        if (!is_object())
            return false;
        const auto& obj = as_object();
        return obj.find(std::string{key}) != obj.end();
    }

    /// Get value at key (returns null if not found or not object)
    [[nodiscard]] const JsonValue& operator[](std::string_view key) const {
        static const JsonValue null_value;
        if (!is_object())
            return null_value;
        const auto& obj = as_object();
        auto it = obj.find(std::string{key});
        if (it == obj.end())
            return null_value;
        return it->second;
    }

    /// Get optional value at key
    [[nodiscard]] const JsonValue* get(std::string_view key) const {
        if (!is_object())
            return nullptr;
        const auto& obj = as_object();
        auto it = obj.find(std::string{key});
        if (it == obj.end())
            return nullptr;
        return &it->second;
    }

    // ========== Array Access ==========

    /// Get array size (0 if not array)
    [[nodiscard]] std::size_t size() const noexcept {
        if (is_array())
            return as_array().size();
        if (is_object())
            return as_object().size();
        return 0;
    }

    /// Get value at index (returns null if out of bounds or not array)
    [[nodiscard]] const JsonValue& operator[](std::size_t index) const {
        static const JsonValue null_value;
        if (!is_array())
            return null_value;
        const auto& arr = as_array();
        if (index >= arr.size())
            return null_value;
        return arr[index];
    }

private:
    JsonVariant data_;
};

// ============================================================================
// JSON Parser
// ============================================================================

/// @brief Minimal recursive descent JSON parser
///
/// Parses JSON text into JsonValue. Supports:
/// - All JSON types: null, boolean, number, string, array, object
/// - UTF-8 strings with escape sequences
/// - Integer and floating-point numbers
///
/// Limitations:
/// - Maximum nesting depth of 64
/// - No streaming support
/// - Numbers limited to int64 or double range
class JsonParser {
public:
    /// Maximum nesting depth to prevent stack overflow
    static constexpr std::size_t MAX_DEPTH = 64;

    /// @brief Parse JSON text
    ///
    /// @param json JSON text to parse
    /// @return Parsed JsonValue or error
    [[nodiscard]] static Result<JsonValue, PolicyErrorInfo> parse(std::string_view json);

private:
    explicit JsonParser(std::string_view json) noexcept;

    // Parser state
    std::string_view json_;
    std::size_t pos_{0};
    std::size_t line_{1};
    std::size_t column_{1};
    std::size_t depth_{0};

    // Core parsing methods
    [[nodiscard]] Result<JsonValue, PolicyErrorInfo> parse_value();
    [[nodiscard]] Result<JsonValue, PolicyErrorInfo> parse_object();
    [[nodiscard]] Result<JsonValue, PolicyErrorInfo> parse_array();
    [[nodiscard]] Result<JsonValue, PolicyErrorInfo> parse_string();
    [[nodiscard]] Result<JsonValue, PolicyErrorInfo> parse_number();
    [[nodiscard]] Result<JsonValue, PolicyErrorInfo> parse_literal();

    // Helper methods
    void skip_whitespace() noexcept;
    [[nodiscard]] bool at_end() const noexcept;
    [[nodiscard]] char peek() const noexcept;
    char advance() noexcept;  // Often called for side effect only
    bool match(char expected) noexcept;

    // Error helpers
    [[nodiscard]] PolicyErrorInfo make_error(PolicyError code, std::string_view msg = "") const;
};

}  // namespace dotvm::core::policy
