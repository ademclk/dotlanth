/// @file breakpoint_manager.cpp
/// @brief TOOL-011 Debug Client - Breakpoint manager implementation

#include "dotvm/debugger/breakpoint_manager.hpp"

#include <algorithm>

namespace dotvm::debugger {

std::uint32_t BreakpointManager::set(std::size_t address) {
    std::uint32_t id = next_id_++;
    Breakpoint bp{.id = id,
                  .address = address,
                  .enabled = true,
                  .condition = {},
                  .hit_count = 0,
                  .ignore_count = 0,
                  .comment = {}};
    breakpoints_[id] = std::move(bp);
    update_address_index(id, address);
    return id;
}

std::uint32_t BreakpointManager::set_conditional(std::size_t address, std::string condition) {
    std::uint32_t id = set(address);
    breakpoints_[id].condition = std::move(condition);
    return id;
}

bool BreakpointManager::remove(std::uint32_t id) {
    auto it = breakpoints_.find(id);
    if (it == breakpoints_.end()) {
        return false;
    }
    remove_from_address_index(id, it->second.address);
    breakpoints_.erase(it);
    return true;
}

std::size_t BreakpointManager::remove_at_address(std::size_t address) {
    auto it = address_index_.find(address);
    if (it == address_index_.end()) {
        return 0;
    }

    std::size_t count = it->second.size();
    for (std::uint32_t id : it->second) {
        breakpoints_.erase(id);
    }
    address_index_.erase(it);
    return count;
}

bool BreakpointManager::enable(std::uint32_t id) {
    auto it = breakpoints_.find(id);
    if (it == breakpoints_.end()) {
        return false;
    }
    it->second.enabled = true;
    return true;
}

bool BreakpointManager::disable(std::uint32_t id) {
    auto it = breakpoints_.find(id);
    if (it == breakpoints_.end()) {
        return false;
    }
    it->second.enabled = false;
    return true;
}

bool BreakpointManager::set_condition(std::uint32_t id, std::string condition) {
    auto it = breakpoints_.find(id);
    if (it == breakpoints_.end()) {
        return false;
    }
    it->second.condition = std::move(condition);
    return true;
}

bool BreakpointManager::set_ignore_count(std::uint32_t id, std::uint32_t count) {
    auto it = breakpoints_.find(id);
    if (it == breakpoints_.end()) {
        return false;
    }
    it->second.ignore_count = count;
    return true;
}

const Breakpoint* BreakpointManager::get(std::uint32_t id) const {
    auto it = breakpoints_.find(id);
    if (it == breakpoints_.end()) {
        return nullptr;
    }
    return &it->second;
}

std::vector<const Breakpoint*> BreakpointManager::list() const {
    std::vector<const Breakpoint*> result;
    result.reserve(breakpoints_.size());
    for (const auto& [id, bp] : breakpoints_) {
        result.push_back(&bp);
    }
    // Sort by ID for consistent ordering
    std::sort(result.begin(), result.end(),
              [](const Breakpoint* a, const Breakpoint* b) { return a->id < b->id; });
    return result;
}

std::vector<const Breakpoint*> BreakpointManager::at_address(std::size_t address) const {
    std::vector<const Breakpoint*> result;
    auto it = address_index_.find(address);
    if (it == address_index_.end()) {
        return result;
    }
    for (std::uint32_t id : it->second) {
        auto bp_it = breakpoints_.find(id);
        if (bp_it != breakpoints_.end()) {
            result.push_back(&bp_it->second);
        }
    }
    return result;
}

BreakCheckResult BreakpointManager::check_simple(std::size_t address) {
    return check(address, [](const std::string&) { return true; });
}

bool BreakpointManager::has_breakpoint_at(std::size_t address) const {
    auto it = address_index_.find(address);
    if (it == address_index_.end()) {
        return false;
    }
    // Check if any are enabled
    for (std::uint32_t id : it->second) {
        auto bp_it = breakpoints_.find(id);
        if (bp_it != breakpoints_.end() && bp_it->second.enabled) {
            return true;
        }
    }
    return false;
}

std::vector<std::size_t> BreakpointManager::get_active_addresses() const {
    std::vector<std::size_t> result;
    for (const auto& [address, ids] : address_index_) {
        for (std::uint32_t id : ids) {
            auto bp_it = breakpoints_.find(id);
            if (bp_it != breakpoints_.end() && bp_it->second.enabled) {
                result.push_back(address);
                break;  // Only add each address once
            }
        }
    }
    return result;
}

void BreakpointManager::clear() {
    breakpoints_.clear();
    address_index_.clear();
}

void BreakpointManager::update_address_index(std::uint32_t id, std::size_t address) {
    address_index_[address].push_back(id);
}

void BreakpointManager::remove_from_address_index(std::uint32_t id, std::size_t address) {
    auto it = address_index_.find(address);
    if (it == address_index_.end()) {
        return;
    }
    auto& ids = it->second;
    ids.erase(std::remove(ids.begin(), ids.end(), id), ids.end());
    if (ids.empty()) {
        address_index_.erase(it);
    }
}

}  // namespace dotvm::debugger
