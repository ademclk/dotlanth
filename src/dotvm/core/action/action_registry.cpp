#include "dotvm/core/action/action_registry.hpp"

namespace dotvm::core::action {

ActionRegistry::Result<void> ActionRegistry::register_action(ActionDef action) noexcept {
    std::unique_lock lock(mutex_);

    if (action.name().empty()) {
        return ActionError::InvalidActionName;
    }

    if (actions_.size() >= config_.max_actions) {
        return ActionError::MaxActionsExceeded;
    }

    if (actions_.contains(action.name())) {
        return ActionError::ActionAlreadyExists;
    }

    std::string action_name = action.name();
    actions_[action_name] = std::make_shared<ActionDef>(std::move(action));
    return {};
}

ActionRegistry::Result<void> ActionRegistry::unregister_action(const std::string& name) noexcept {
    std::unique_lock lock(mutex_);

    auto it = actions_.find(name);
    if (it == actions_.end()) {
        return ActionError::ActionNotFound;
    }

    actions_.erase(it);
    return {};
}

bool ActionRegistry::has_action(const std::string& name) const noexcept {
    std::shared_lock lock(mutex_);
    return actions_.contains(name);
}

ActionRegistry::Result<std::shared_ptr<const ActionDef>>
ActionRegistry::get_action(const std::string& name) const noexcept {
    std::shared_lock lock(mutex_);

    auto it = actions_.find(name);
    if (it == actions_.end()) {
        return ActionError::ActionNotFound;
    }
    return std::const_pointer_cast<const ActionDef>(it->second);
}

std::vector<std::string> ActionRegistry::action_names() const noexcept {
    std::shared_lock lock(mutex_);

    std::vector<std::string> names;
    names.reserve(actions_.size());
    for (const auto& [name, _] : actions_) {
        names.push_back(name);
    }
    return names;
}

std::size_t ActionRegistry::action_count() const noexcept {
    std::shared_lock lock(mutex_);
    return actions_.size();
}

void ActionRegistry::clear() noexcept {
    std::unique_lock lock(mutex_);
    actions_.clear();
}

}  // namespace dotvm::core::action
