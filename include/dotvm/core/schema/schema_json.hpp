#pragma once

/// @file schema_json.hpp
/// @brief DEP-003 JSON serialization for schema definitions
///
/// Provides import/export functionality for object type schemas using JSON format.
/// Uses the existing JsonParser from the policy module.

#include <sstream>
#include <string>
#include <string_view>

#include "dotvm/core/policy/json_parser.hpp"
#include "dotvm/core/result.hpp"
#include "link_def.hpp"
#include "migration.hpp"
#include "object_type.hpp"
#include "property_type.hpp"
#include "schema_error.hpp"
#include "schema_registry.hpp"
#include "validator.hpp"

namespace dotvm::core::schema {

// Use JsonValue from policy module
using policy::JsonArray;
using policy::JsonObject;
using policy::JsonParser;
using policy::JsonValue;

// ============================================================================
// JSON Schema Version
// ============================================================================

/// @brief Current schema JSON format version
inline constexpr std::string_view SCHEMA_JSON_VERSION = "dotvm-schema/1.0";

// ============================================================================
// Validator Serialization
// ============================================================================

/// @brief Convert a RangeValidator to JSON
[[nodiscard]] inline JsonValue range_validator_to_json(const RangeValidator& v) noexcept {
    JsonObject obj;
    obj["type"] = "range";

    if (v.min.has_value()) {
        obj["min"] = *v.min;
        obj["min_inclusive"] = v.min_inclusive;
    }
    if (v.max.has_value()) {
        obj["max"] = *v.max;
        obj["max_inclusive"] = v.max_inclusive;
    }

    return obj;
}

/// @brief Convert a RegexValidator to JSON
[[nodiscard]] inline JsonValue regex_validator_to_json(const RegexValidator& v) noexcept {
    JsonObject obj;
    obj["type"] = "regex";
    obj["pattern"] = v.pattern;
    return obj;
}

/// @brief Convert an EnumValidator to JSON
[[nodiscard]] inline JsonValue enum_validator_to_json(const EnumValidator& v) noexcept {
    JsonObject obj;
    obj["type"] = "enum";

    JsonArray values;
    for (const auto& val : v.allowed_values) {
        values.push_back(val);
    }
    obj["values"] = std::move(values);

    return obj;
}

/// @brief Convert a RequiredValidator to JSON
[[nodiscard]] inline JsonValue required_validator_to_json(const RequiredValidator&) noexcept {
    JsonObject obj;
    obj["type"] = "required";
    return obj;
}

/// @brief Convert any Validator to JSON
[[nodiscard]] inline JsonValue validator_to_json(const Validator& v) noexcept {
    return std::visit(
        [](const auto& validator) -> JsonValue {
            using T = std::decay_t<decltype(validator)>;
            if constexpr (std::is_same_v<T, RangeValidator>) {
                return range_validator_to_json(validator);
            } else if constexpr (std::is_same_v<T, RegexValidator>) {
                return regex_validator_to_json(validator);
            } else if constexpr (std::is_same_v<T, EnumValidator>) {
                return enum_validator_to_json(validator);
            } else if constexpr (std::is_same_v<T, RequiredValidator>) {
                return required_validator_to_json(validator);
            } else {
                return nullptr;
            }
        },
        v);
}

/// @brief Parse a Validator from JSON
[[nodiscard]] inline Result<Validator, SchemaError>
validator_from_json(const JsonValue& json) noexcept {
    if (!json.is_object()) {
        return SchemaError::ValidationFailed;
    }

    const auto* type_val = json.get("type");
    if (!type_val || !type_val->is_string()) {
        return SchemaError::ValidationFailed;
    }

    const auto& type = type_val->as_string();

    if (type == "range") {
        RangeValidator v;
        if (const auto* min_val = json.get("min")) {
            if (min_val->is_number()) {
                v.min = min_val->as_float();
            }
        }
        if (const auto* max_val = json.get("max")) {
            if (max_val->is_number()) {
                v.max = max_val->as_float();
            }
        }
        if (const auto* min_inc = json.get("min_inclusive")) {
            if (min_inc->is_bool()) {
                v.min_inclusive = min_inc->as_bool();
            }
        }
        if (const auto* max_inc = json.get("max_inclusive")) {
            if (max_inc->is_bool()) {
                v.max_inclusive = max_inc->as_bool();
            }
        }
        return Validator{v};
    }

    if (type == "regex") {
        const auto* pattern_val = json.get("pattern");
        if (!pattern_val || !pattern_val->is_string()) {
            return SchemaError::ValidationFailed;
        }
        auto result = RegexValidator::create(pattern_val->as_string());
        if (result.is_err()) {
            return result.error();
        }
        return Validator{result.value()};
    }

    if (type == "enum") {
        const auto* values_val = json.get("values");
        if (!values_val || !values_val->is_array()) {
            return SchemaError::ValidationFailed;
        }
        EnumValidator v;
        for (const auto& val : values_val->as_array()) {
            if (val.is_string()) {
                v.allowed_values.push_back(val.as_string());
            }
        }
        return Validator{v};
    }

    if (type == "required") {
        return Validator{RequiredValidator{}};
    }

    return SchemaError::ValidationFailed;
}

// ============================================================================
// PropertyDef Serialization
// ============================================================================

/// @brief Convert PropertyType to JSON string
[[nodiscard]] inline std::string property_type_to_string(PropertyType type) noexcept {
    return std::string(to_string(type));
}

/// @brief Parse PropertyType from JSON string
[[nodiscard]] inline Result<PropertyType, SchemaError>
property_type_from_string(std::string_view str) noexcept {
    if (str == "Int64")
        return PropertyType::Int64;
    if (str == "Float64")
        return PropertyType::Float64;
    if (str == "Boolean")
        return PropertyType::Boolean;
    if (str == "String")
        return PropertyType::String;
    if (str == "DateTime")
        return PropertyType::DateTime;
    if (str == "Handle")
        return PropertyType::Handle;
    return SchemaError::InvalidPropertyType;
}

/// @brief Convert a ValidatedProperty to JSON
[[nodiscard]] inline JsonValue validated_property_to_json(const ValidatedProperty& vp) noexcept {
    JsonObject obj;
    obj["name"] = vp.definition.name;
    obj["type"] = property_type_to_string(vp.definition.type);
    obj["required"] = vp.definition.required;

    if (vp.definition.default_value.has_value()) {
        const auto& val = *vp.definition.default_value;
        if (val.is_nil()) {
            obj["default"] = nullptr;
        } else if (val.is_integer()) {
            obj["default"] = val.as_integer();
        } else if (val.is_float()) {
            obj["default"] = val.as_float();
        } else if (val.is_bool()) {
            obj["default"] = val.as_bool();
        }
    }

    if (!vp.definition.target_type.empty()) {
        obj["target_type"] = vp.definition.target_type;
    }

    if (!vp.validators.empty()) {
        JsonArray validators;
        for (const auto& v : vp.validators) {
            // Skip RequiredValidator in JSON (it's implied by "required": true)
            if (!std::holds_alternative<RequiredValidator>(v)) {
                validators.push_back(validator_to_json(v));
            }
        }
        if (!validators.empty()) {
            obj["validators"] = std::move(validators);
        }
    }

    return obj;
}

// ============================================================================
// LinkDef Serialization
// ============================================================================

/// @brief Convert Cardinality to JSON string
[[nodiscard]] inline std::string cardinality_to_string(Cardinality c) noexcept {
    return std::string(to_string(c));
}

/// @brief Parse Cardinality from JSON string
[[nodiscard]] inline Result<Cardinality, SchemaError>
cardinality_from_string(std::string_view str) noexcept {
    if (str == "OneToOne")
        return Cardinality::OneToOne;
    if (str == "OneToMany")
        return Cardinality::OneToMany;
    if (str == "ManyToMany")
        return Cardinality::ManyToMany;
    return SchemaError::InvalidPropertyType;
}

/// @brief Convert a LinkDef to JSON
[[nodiscard]] inline JsonValue link_def_to_json(const LinkDef& link) noexcept {
    JsonObject obj;
    obj["name"] = link.name;
    obj["target_type"] = link.target_type;
    obj["cardinality"] = cardinality_to_string(link.cardinality);
    obj["required"] = link.required;

    if (!link.inverse_link.empty()) {
        obj["inverse_link"] = link.inverse_link;
    }

    return obj;
}

// ============================================================================
// ObjectType Serialization
// ============================================================================

/// @brief Convert an ObjectType to JSON
[[nodiscard]] inline JsonValue object_type_to_json(const ObjectType& type) noexcept {
    JsonObject obj;
    obj["name"] = type.name();

    // Properties
    JsonArray properties;
    for (const auto& prop_name : type.property_names()) {
        const auto* prop = type.get_property(prop_name);
        if (prop != nullptr) {
            properties.push_back(validated_property_to_json(*prop));
        }
    }
    if (!properties.empty()) {
        obj["properties"] = std::move(properties);
    }

    // Links
    JsonArray links;
    for (const auto& link_name : type.link_names()) {
        const auto* link = type.get_link(link_name);
        if (link != nullptr) {
            links.push_back(link_def_to_json(*link));
        }
    }
    if (!links.empty()) {
        obj["links"] = std::move(links);
    }

    return obj;
}

/// @brief Parse an ObjectType from JSON
[[nodiscard]] inline Result<ObjectType, SchemaError>
object_type_from_json(const JsonValue& json) noexcept {
    if (!json.is_object()) {
        return SchemaError::ValidationFailed;
    }

    const auto* name_val = json.get("name");
    if (!name_val || !name_val->is_string()) {
        return SchemaError::InvalidTypeName;
    }

    ObjectTypeBuilder builder(name_val->as_string());

    // Parse properties
    if (const auto* props_val = json.get("properties")) {
        if (props_val->is_array()) {
            for (const auto& prop_json : props_val->as_array()) {
                if (!prop_json.is_object()) {
                    continue;
                }

                const auto* pname = prop_json.get("name");
                const auto* ptype = prop_json.get("type");

                if (!pname || !pname->is_string() || !ptype || !ptype->is_string()) {
                    continue;
                }

                auto type_result = property_type_from_string(ptype->as_string());
                if (type_result.is_err()) {
                    continue;
                }

                PropertyDef def;
                def.name = pname->as_string();
                def.type = type_result.value();

                if (const auto* req = prop_json.get("required")) {
                    if (req->is_bool()) {
                        def.required = req->as_bool();
                    }
                }

                if (const auto* target = prop_json.get("target_type")) {
                    if (target->is_string()) {
                        def.target_type = target->as_string();
                    }
                }

                // Parse validators
                std::vector<Validator> validators;
                if (const auto* vals = prop_json.get("validators")) {
                    if (vals->is_array()) {
                        for (const auto& v : vals->as_array()) {
                            auto v_result = validator_from_json(v);
                            if (v_result.is_ok()) {
                                validators.push_back(v_result.value());
                            }
                        }
                    }
                }

                auto add_result = builder.add_property(std::move(def), std::move(validators));
                if (add_result.is_err()) {
                    return add_result.error();
                }
            }
        }
    }

    // Parse links
    if (const auto* links_val = json.get("links")) {
        if (links_val->is_array()) {
            for (const auto& link_json : links_val->as_array()) {
                if (!link_json.is_object()) {
                    continue;
                }

                const auto* lname = link_json.get("name");
                const auto* ltarget = link_json.get("target_type");

                if (!lname || !lname->is_string() || !ltarget || !ltarget->is_string()) {
                    continue;
                }

                LinkDef link;
                link.name = lname->as_string();
                link.target_type = ltarget->as_string();

                if (const auto* card = link_json.get("cardinality")) {
                    if (card->is_string()) {
                        auto card_result = cardinality_from_string(card->as_string());
                        if (card_result.is_ok()) {
                            link.cardinality = card_result.value();
                        }
                    }
                }

                if (const auto* req = link_json.get("required")) {
                    if (req->is_bool()) {
                        link.required = req->as_bool();
                    }
                }

                if (const auto* inv = link_json.get("inverse_link")) {
                    if (inv->is_string()) {
                        link.inverse_link = inv->as_string();
                    }
                }

                auto add_result = builder.add_link(std::move(link));
                if (add_result.is_err()) {
                    return add_result.error();
                }
            }
        }
    }

    return std::move(builder).build();
}

// ============================================================================
// Full Schema Serialization
// ============================================================================

/// @brief Schema export structure
struct SchemaExport {
    std::string schema_version{std::string(SCHEMA_JSON_VERSION)};
    MigrationVersion version{0, 0, 0};
    std::vector<ObjectType> types;
};

// Forward declaration for serialization
[[nodiscard]] inline std::string serialize_json_value(const JsonValue& val) noexcept;

/// @brief Convert a full schema export to JSON string
[[nodiscard]] inline std::string export_schema_json(const SchemaExport& schema) noexcept {
    std::ostringstream out;

    out << "{\n";
    out << "  \"$schema\": \"" << schema.schema_version << "\",\n";
    out << "  \"version\": \"" << schema.version.to_string() << "\",\n";
    out << "  \"types\": [\n";

    bool first = true;
    for (const auto& type : schema.types) {
        if (!first) {
            out << ",\n";
        }
        first = false;

        // Simple inline JSON for type
        auto json = object_type_to_json(type);
        out << "    " << serialize_json_value(json);
    }

    out << "\n  ]\n";
    out << "}\n";

    return out.str();
}

/// @brief Serialize a JsonValue to string (simple implementation)
[[nodiscard]] inline std::string serialize_json_value(const JsonValue& val) noexcept {
    std::ostringstream out;

    if (val.is_null()) {
        out << "null";
    } else if (val.is_bool()) {
        out << (val.as_bool() ? "true" : "false");
    } else if (val.is_int()) {
        out << val.as_int();
    } else if (val.is_float()) {
        out << val.as_float();
    } else if (val.is_string()) {
        out << "\"";
        for (char c : val.as_string()) {
            switch (c) {
                case '"':
                    out << "\\\"";
                    break;
                case '\\':
                    out << "\\\\";
                    break;
                case '\n':
                    out << "\\n";
                    break;
                case '\r':
                    out << "\\r";
                    break;
                case '\t':
                    out << "\\t";
                    break;
                default:
                    out << c;
            }
        }
        out << "\"";
    } else if (val.is_array()) {
        out << "[";
        bool first = true;
        for (const auto& elem : val.as_array()) {
            if (!first) {
                out << ", ";
            }
            first = false;
            out << serialize_json_value(elem);
        }
        out << "]";
    } else if (val.is_object()) {
        out << "{";
        bool first = true;
        for (const auto& [key, value] : val.as_object()) {
            if (!first) {
                out << ", ";
            }
            first = false;
            out << "\"" << key << "\": " << serialize_json_value(value);
        }
        out << "}";
    }

    return out.str();
}

/// @brief Import schema from JSON string
[[nodiscard]] inline Result<SchemaExport, SchemaError>
import_schema_json(std::string_view json) noexcept {
    auto parse_result = JsonParser::parse(json);
    if (parse_result.is_err()) {
        return SchemaError::ValidationFailed;
    }

    const auto& root = parse_result.value();
    if (!root.is_object()) {
        return SchemaError::ValidationFailed;
    }

    SchemaExport schema;

    // Parse $schema
    if (const auto* schema_val = root.get("$schema")) {
        if (schema_val->is_string()) {
            schema.schema_version = schema_val->as_string();
        }
    }

    // Parse version
    if (const auto* version_val = root.get("version")) {
        if (version_val->is_string()) {
            auto v_result = MigrationVersion::parse(version_val->as_string());
            if (v_result.is_ok()) {
                schema.version = v_result.value();
            }
        }
    }

    // Parse types
    if (const auto* types_val = root.get("types")) {
        if (types_val->is_array()) {
            for (const auto& type_json : types_val->as_array()) {
                auto type_result = object_type_from_json(type_json);
                if (type_result.is_ok()) {
                    schema.types.push_back(std::move(type_result).value());
                }
            }
        }
    }

    return schema;
}

/// @brief Export a SchemaRegistry to JSON
[[nodiscard]] inline std::string export_registry_json(const SchemaRegistry& registry,
                                                      MigrationVersion version) noexcept {
    SchemaExport schema;
    schema.version = version;

    for (const auto& name : registry.type_names()) {
        auto type_result = registry.get_type(name);
        if (type_result.is_ok()) {
            schema.types.push_back(*type_result.value());
        }
    }

    return export_schema_json(schema);
}

/// @brief Import JSON schema into a SchemaRegistry
[[nodiscard]] inline Result<MigrationVersion, SchemaError>
import_registry_json(SchemaRegistry& registry, std::string_view json) noexcept {
    auto schema_result = import_schema_json(json);
    if (schema_result.is_err()) {
        return schema_result.error();
    }

    auto schema = std::move(schema_result).value();

    for (auto& type : schema.types) {
        auto reg_result = registry.register_type(std::move(type));
        if (reg_result.is_err()) {
            return reg_result.error();
        }
    }

    return schema.version;
}

}  // namespace dotvm::core::schema
