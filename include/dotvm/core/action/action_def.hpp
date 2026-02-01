#pragma once

/// @file action_def.hpp
/// @brief DEP-005 Action definition structures and builders
///
/// Defines ParamDef and ActionDef for the Action system, along with
/// fluent builders for constructing immutable ActionDef instances.

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "dotvm/core/action/action_error.hpp"
#include "dotvm/core/result.hpp"
#include "dotvm/core/schema/property_type.hpp"
#include "dotvm/core/schema/validator.hpp"
#include "dotvm/core/security/permission.hpp"
#include "dotvm/core/value.hpp"

namespace dotvm::core::action {

// ============================================================================
// ParamDef
// ============================================================================

/// @brief Definition of a single action parameter
struct ParamDef {
    std::string name;
    schema::PropertyType type{schema::PropertyType::String};
    bool required{false};
    std::optional<Value> default_value{std::nullopt};
    std::string description{};
    std::vector<schema::Validator> validators{};

    [[nodiscard]] bool operator==(const ParamDef& other) const noexcept {
        return name == other.name && type == other.type && required == other.required &&
               default_value == other.default_value && description == other.description &&
               validators == other.validators;
    }
};

// ============================================================================
// ParamDefBuilder
// ============================================================================

class ParamDefBuilder {
public:
    explicit ParamDefBuilder(std::string name) noexcept { def_.name = std::move(name); }

    ParamDefBuilder& with_type(schema::PropertyType type) noexcept {
        def_.type = type;
        return *this;
    }

    ParamDefBuilder& required(bool is_required = true) noexcept {
        def_.required = is_required;
        return *this;
    }

    ParamDefBuilder& with_default(Value value) noexcept {
        def_.default_value = value;
        return *this;
    }

    ParamDefBuilder& with_description(std::string description) noexcept {
        def_.description = std::move(description);
        return *this;
    }

    ParamDefBuilder& add_validator(schema::Validator validator) noexcept {
        def_.validators.push_back(std::move(validator));
        return *this;
    }

    [[nodiscard]] ParamDef build() const noexcept { return def_; }

private:
    ParamDef def_;
};

// ============================================================================
// ActionDef
// ============================================================================

class ActionDef {
public:
    using ParamMap = std::map<std::string, ParamDef>;

    explicit ActionDef(std::string name) noexcept;

    [[nodiscard]] const std::string& name() const noexcept;
    [[nodiscard]] const std::string& description() const noexcept;
    [[nodiscard]] bool has_parameter(std::string_view name) const noexcept;
    [[nodiscard]] const ParamDef* get_parameter(std::string_view name) const noexcept;
    [[nodiscard]] std::vector<std::string> parameter_names() const noexcept;
    [[nodiscard]] std::size_t parameter_count() const noexcept;
    [[nodiscard]] security::Permission required_permissions() const noexcept;
    [[nodiscard]] std::uint64_t handler_offset() const noexcept;
    [[nodiscard]] std::uint32_t max_calls_per_minute() const noexcept;

private:
    friend class ActionDefBuilder;

    std::string name_;
    std::string description_;
    ParamMap parameters_;
    security::Permission required_permissions_{security::Permission::None};
    std::uint64_t handler_offset_{0};
    std::uint32_t max_calls_per_minute_{0};
};

// ============================================================================
// ActionDefBuilder
// ============================================================================

class ActionDefBuilder {
public:
    template <typename T>
    using Result = ::dotvm::core::Result<T, ActionError>;

    explicit ActionDefBuilder(std::string name) noexcept;

    ActionDefBuilder& with_description(std::string description) noexcept;
    ActionDefBuilder& with_required_permissions(security::Permission permissions) noexcept;
    ActionDefBuilder& with_handler_offset(std::uint64_t offset) noexcept;
    ActionDefBuilder& with_max_calls_per_minute(std::uint32_t max_calls_per_minute) noexcept;
    Result<void> add_parameter(ParamDef parameter) noexcept;
    ActionDefBuilder& try_add_parameter(ParamDef parameter) noexcept;

    [[nodiscard]] ActionDef build() && noexcept;
    [[nodiscard]] ActionDef build() const& noexcept;

private:
    ActionDef action_;
};

}  // namespace dotvm::core::action
