/// @file json_parser.cpp
/// @brief SEC-009 Minimal JSON parser implementation

#include "dotvm/core/policy/json_parser.hpp"

#include <cctype>
#include <charconv>
#include <cmath>
#include <limits>

namespace dotvm::core::policy {

// ============================================================================
// Public Interface
// ============================================================================

Result<JsonValue, PolicyErrorInfo> JsonParser::parse(std::string_view json) {
    JsonParser parser{json};
    parser.skip_whitespace();

    if (parser.at_end()) {
        return parser.make_error(PolicyError::UnexpectedEof, "Empty input");
    }

    auto result = parser.parse_value();
    if (!result) {
        return result;
    }

    parser.skip_whitespace();
    if (!parser.at_end()) {
        return parser.make_error(PolicyError::JsonSyntaxError, "Unexpected content after JSON value");
    }

    return result;
}

// ============================================================================
// Constructor
// ============================================================================

JsonParser::JsonParser(std::string_view json) noexcept : json_{json} {}

// ============================================================================
// Core Parsing Methods
// ============================================================================

Result<JsonValue, PolicyErrorInfo> JsonParser::parse_value() {
    skip_whitespace();

    if (at_end()) {
        return make_error(PolicyError::UnexpectedEof, "Unexpected end of input");
    }

    char c = peek();

    switch (c) {
        case '{':
            return parse_object();
        case '[':
            return parse_array();
        case '"':
            return parse_string();
        case 't':
        case 'f':
        case 'n':
            return parse_literal();
        case '-':
        case '0':
        case '1':
        case '2':
        case '3':
        case '4':
        case '5':
        case '6':
        case '7':
        case '8':
        case '9':
            return parse_number();
        default:
            return make_error(PolicyError::InvalidCharacter,
                              std::string{"Unexpected character '"} + c + "'");
    }
}

Result<JsonValue, PolicyErrorInfo> JsonParser::parse_object() {
    if (depth_ >= MAX_DEPTH) {
        return make_error(PolicyError::NestingTooDeep, "Object nesting too deep");
    }
    ++depth_;

    advance();  // consume '{'
    skip_whitespace();

    JsonObject object;

    if (peek() == '}') {
        advance();
        --depth_;
        return JsonValue{std::move(object)};
    }

    while (true) {
        skip_whitespace();

        // Parse key (must be a string)
        if (peek() != '"') {
            return make_error(PolicyError::JsonSyntaxError, "Expected string key in object");
        }

        auto key_result = parse_string();
        if (!key_result) {
            return key_result;
        }

        std::string key = key_result.value().as_string();

        skip_whitespace();

        // Expect colon
        if (!match(':')) {
            return make_error(PolicyError::JsonSyntaxError, "Expected ':' after object key");
        }

        skip_whitespace();

        // Parse value
        auto value_result = parse_value();
        if (!value_result) {
            return value_result;
        }

        object[std::move(key)] = std::move(value_result).value();

        skip_whitespace();

        if (match('}')) {
            break;
        }

        if (!match(',')) {
            return make_error(PolicyError::JsonSyntaxError, "Expected ',' or '}' in object");
        }
    }

    --depth_;
    return JsonValue{std::move(object)};
}

Result<JsonValue, PolicyErrorInfo> JsonParser::parse_array() {
    if (depth_ >= MAX_DEPTH) {
        return make_error(PolicyError::NestingTooDeep, "Array nesting too deep");
    }
    ++depth_;

    advance();  // consume '['
    skip_whitespace();

    JsonArray array;

    if (peek() == ']') {
        advance();
        --depth_;
        return JsonValue{std::move(array)};
    }

    while (true) {
        skip_whitespace();

        auto value_result = parse_value();
        if (!value_result) {
            return value_result;
        }

        array.push_back(std::move(value_result).value());

        skip_whitespace();

        if (match(']')) {
            break;
        }

        if (!match(',')) {
            return make_error(PolicyError::JsonSyntaxError, "Expected ',' or ']' in array");
        }
    }

    --depth_;
    return JsonValue{std::move(array)};
}

Result<JsonValue, PolicyErrorInfo> JsonParser::parse_string() {
    advance();  // consume opening '"'

    std::string result;
    result.reserve(32);  // Pre-allocate for typical strings

    while (!at_end()) {
        char c = advance();

        if (c == '"') {
            return JsonValue{std::move(result)};
        }

        if (c == '\\') {
            if (at_end()) {
                return make_error(PolicyError::UnexpectedEof, "Unterminated escape sequence");
            }

            char escaped = advance();
            switch (escaped) {
                case '"':
                    result += '"';
                    break;
                case '\\':
                    result += '\\';
                    break;
                case '/':
                    result += '/';
                    break;
                case 'b':
                    result += '\b';
                    break;
                case 'f':
                    result += '\f';
                    break;
                case 'n':
                    result += '\n';
                    break;
                case 'r':
                    result += '\r';
                    break;
                case 't':
                    result += '\t';
                    break;
                case 'u': {
                    // Parse 4 hex digits
                    if (pos_ + 4 > json_.size()) {
                        return make_error(PolicyError::InvalidEscapeSequence,
                                          "Invalid unicode escape");
                    }

                    std::string_view hex = json_.substr(pos_, 4);
                    std::uint16_t codepoint = 0;
                    auto [ptr, ec] = std::from_chars(hex.data(), hex.data() + 4, codepoint, 16);

                    if (ec != std::errc{} || ptr != hex.data() + 4) {
                        return make_error(PolicyError::InvalidEscapeSequence,
                                          "Invalid unicode escape");
                    }

                    pos_ += 4;
                    column_ += 4;

                    // Convert UTF-16 codepoint to UTF-8
                    if (codepoint < 0x80) {
                        result += static_cast<char>(codepoint);
                    } else if (codepoint < 0x800) {
                        result += static_cast<char>(0xC0 | (codepoint >> 6));
                        result += static_cast<char>(0x80 | (codepoint & 0x3F));
                    } else {
                        result += static_cast<char>(0xE0 | (codepoint >> 12));
                        result += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
                        result += static_cast<char>(0x80 | (codepoint & 0x3F));
                    }
                    break;
                }
                default:
                    return make_error(PolicyError::InvalidEscapeSequence,
                                      std::string{"Invalid escape sequence '\\"} + escaped + "'");
            }
        } else if (static_cast<unsigned char>(c) < 0x20) {
            // Control characters not allowed in strings
            return make_error(PolicyError::InvalidCharacter, "Control character in string");
        } else {
            result += c;
        }
    }

    return make_error(PolicyError::UnexpectedEof, "Unterminated string");
}

Result<JsonValue, PolicyErrorInfo> JsonParser::parse_number() {
    std::size_t start = pos_;

    // Optional negative sign
    if (peek() == '-') {
        advance();
    }

    // Integer part
    if (peek() == '0') {
        advance();
    } else if (std::isdigit(static_cast<unsigned char>(peek()))) {
        while (!at_end() && std::isdigit(static_cast<unsigned char>(peek()))) {
            advance();
        }
    } else {
        return make_error(PolicyError::InvalidNumber, "Invalid number format");
    }

    bool is_float = false;

    // Fractional part
    if (!at_end() && peek() == '.') {
        is_float = true;
        advance();

        if (at_end() || !std::isdigit(static_cast<unsigned char>(peek()))) {
            return make_error(PolicyError::InvalidNumber, "Expected digit after decimal point");
        }

        while (!at_end() && std::isdigit(static_cast<unsigned char>(peek()))) {
            advance();
        }
    }

    // Exponent part
    if (!at_end() && (peek() == 'e' || peek() == 'E')) {
        is_float = true;
        advance();

        if (!at_end() && (peek() == '+' || peek() == '-')) {
            advance();
        }

        if (at_end() || !std::isdigit(static_cast<unsigned char>(peek()))) {
            return make_error(PolicyError::InvalidNumber, "Expected digit in exponent");
        }

        while (!at_end() && std::isdigit(static_cast<unsigned char>(peek()))) {
            advance();
        }
    }

    std::string_view number_str = json_.substr(start, pos_ - start);

    if (is_float) {
        double value = 0.0;
        auto [ptr, ec] = std::from_chars(number_str.data(), number_str.data() + number_str.size(), value);

        if (ec == std::errc::result_out_of_range) {
            return make_error(PolicyError::InvalidNumber, "Number out of range");
        }
        if (ec != std::errc{}) {
            return make_error(PolicyError::InvalidNumber, "Invalid number format");
        }

        return JsonValue{value};
    } else {
        std::int64_t value = 0;
        auto [ptr, ec] = std::from_chars(number_str.data(), number_str.data() + number_str.size(), value);

        if (ec == std::errc::result_out_of_range) {
            // Try as double if too large for int64
            double dvalue = 0.0;
            auto [dptr, dec] = std::from_chars(number_str.data(), number_str.data() + number_str.size(), dvalue);
            if (dec == std::errc{}) {
                return JsonValue{dvalue};
            }
            return make_error(PolicyError::InvalidNumber, "Number out of range");
        }
        if (ec != std::errc{}) {
            return make_error(PolicyError::InvalidNumber, "Invalid number format");
        }

        return JsonValue{value};
    }
}

Result<JsonValue, PolicyErrorInfo> JsonParser::parse_literal() {
    if (json_.substr(pos_).starts_with("true")) {
        pos_ += 4;
        column_ += 4;
        return JsonValue{true};
    }

    if (json_.substr(pos_).starts_with("false")) {
        pos_ += 5;
        column_ += 5;
        return JsonValue{false};
    }

    if (json_.substr(pos_).starts_with("null")) {
        pos_ += 4;
        column_ += 4;
        return JsonValue{nullptr};
    }

    return make_error(PolicyError::JsonSyntaxError, "Invalid literal");
}

// ============================================================================
// Helper Methods
// ============================================================================

void JsonParser::skip_whitespace() noexcept {
    while (!at_end()) {
        char c = peek();
        if (c == ' ' || c == '\t' || c == '\r') {
            advance();
        } else if (c == '\n') {
            advance();
            ++line_;
            column_ = 1;
        } else {
            break;
        }
    }
}

bool JsonParser::at_end() const noexcept {
    return pos_ >= json_.size();
}

char JsonParser::peek() const noexcept {
    if (at_end()) return '\0';
    return json_[pos_];
}

char JsonParser::advance() noexcept {
    if (at_end()) return '\0';
    char c = json_[pos_++];
    if (c == '\n') {
        ++line_;
        column_ = 1;
    } else {
        ++column_;
    }
    return c;
}

bool JsonParser::match(char expected) noexcept {
    if (at_end() || peek() != expected) return false;
    advance();
    return true;
}

PolicyErrorInfo JsonParser::make_error(PolicyError code, std::string_view msg) const {
    return PolicyErrorInfo::err(code, msg, static_cast<std::uint32_t>(line_),
                                 static_cast<std::uint32_t>(column_));
}

}  // namespace dotvm::core::policy
