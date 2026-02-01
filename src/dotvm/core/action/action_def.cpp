#include "dotvm/core/action/action_def.hpp"

#include <utility>

namespace dotvm::core::action {

ActionDef::ActionDef(std::string name) noexcept : name_(std::move(name)) {}

const std::string& ActionDef::name() const noexcept {
    return name_;
}
const std::string& ActionDef::description() const noexcept {
    return description_;
}

bool ActionDef::has_parameter(std::string_view name) const noexcept {
    return parameters_.contains(std::string(name));
}

const ParamDef* ActionDef::get_parameter(std::string_view name) const noexcept {
    auto it = parameters_.find(std::string(name));
    if (it == parameters_.end())
        return nullptr;
    return &it->second;
}

std::vector<std::string> ActionDef::parameter_names() const noexcept {
    std::vector<std::string> names;
    names.reserve(parameters_.size());
    for (const auto& [name, _] : parameters_) {
        names.push_back(name);
    }
    return names;
}

std::size_t ActionDef::parameter_count() const noexcept {
    return parameters_.size();
}
security::Permission ActionDef::required_permissions() const noexcept {
    return required_permissions_;
}
std::uint64_t ActionDef::handler_offset() const noexcept {
    return handler_offset_;
}
std::uint32_t ActionDef::max_calls_per_minute() const noexcept {
    return max_calls_per_minute_;
}

ActionDefBuilder::ActionDefBuilder(std::string name) noexcept : action_(std::move(name)) {}

ActionDefBuilder& ActionDefBuilder::with_description(std::string description) noexcept {
    action_.description_ = std::move(description);
    return *this;
}

ActionDefBuilder&
ActionDefBuilder::with_required_permissions(security::Permission permissions) noexcept {
    action_.required_permissions_ = permissions;
    return *this;
}

ActionDefBuilder& ActionDefBuilder::with_handler_offset(std::uint64_t offset) noexcept {
    action_.handler_offset_ = offset;
    return *this;
}

ActionDefBuilder&
ActionDefBuilder::with_max_calls_per_minute(std::uint32_t max_calls_per_minute) noexcept {
    action_.max_calls_per_minute_ = max_calls_per_minute;
    return *this;
}

ActionDefBuilder::Result<void> ActionDefBuilder::add_parameter(ParamDef parameter) noexcept {
    if (action_.name_.empty())
        return ActionError::InvalidActionName;
    if (parameter.name.empty())
        return ActionError::InvalidParameter;
    if (action_.parameters_.contains(parameter.name))
        return ActionError::InvalidParameter;
    if (parameter.default_value.has_value() &&
        !schema::is_compatible(*parameter.default_value, parameter.type)) {
        return ActionError::ParameterTypeMismatch;
    }
    std::string param_name = parameter.name;
    action_.parameters_[param_name] = std::move(parameter);
    return {};
}

ActionDefBuilder& ActionDefBuilder::try_add_parameter(ParamDef parameter) noexcept {
    (void)add_parameter(std::move(parameter));
    return *this;
}

ActionDef ActionDefBuilder::build() && noexcept {
    return std::move(action_);
}
ActionDef ActionDefBuilder::build() const& noexcept {
    return action_;
}

}  // namespace dotvm::core::action
