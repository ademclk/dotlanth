/// @file watch_manager.cpp
/// @brief TOOL-011 Debug Client - Watch manager implementation

#include "dotvm/debugger/watch_manager.hpp"

#include <algorithm>

namespace dotvm::debugger {

std::uint32_t WatchManager::set(core::Handle handle, std::size_t offset, std::size_t size,
                                WatchType type) {
    std::uint32_t id = next_id_++;
    Watchpoint wp{.id = id,
                  .handle = handle,
                  .offset = offset,
                  .size = size,
                  .type = type,
                  .enabled = true,
                  .hit_count = 0,
                  .comment = {},
                  .previous_value = {}};
    watchpoints_[id] = std::move(wp);
    return id;
}

bool WatchManager::remove(std::uint32_t id) {
    return watchpoints_.erase(id) > 0;
}

bool WatchManager::enable(std::uint32_t id) {
    auto it = watchpoints_.find(id);
    if (it == watchpoints_.end()) {
        return false;
    }
    it->second.enabled = true;
    return true;
}

bool WatchManager::disable(std::uint32_t id) {
    auto it = watchpoints_.find(id);
    if (it == watchpoints_.end()) {
        return false;
    }
    it->second.enabled = false;
    return true;
}

const Watchpoint* WatchManager::get(std::uint32_t id) const {
    auto it = watchpoints_.find(id);
    if (it == watchpoints_.end()) {
        return nullptr;
    }
    return &it->second;
}

std::vector<const Watchpoint*> WatchManager::list() const {
    std::vector<const Watchpoint*> result;
    result.reserve(watchpoints_.size());
    for (const auto& [id, wp] : watchpoints_) {
        result.push_back(&wp);
    }
    // Sort by ID for consistent ordering
    std::sort(result.begin(), result.end(),
              [](const Watchpoint* a, const Watchpoint* b) { return a->id < b->id; });
    return result;
}

WatchCheckResult WatchManager::check(const MemoryReader& reader) {
    for (auto& [id, wp] : watchpoints_) {
        if (!wp.enabled) {
            continue;
        }

        // Skip if we don't have a cached value yet
        if (wp.previous_value.empty()) {
            continue;
        }

        // Read current value
        std::vector<std::uint8_t> current_value = reader(wp.handle, wp.offset, wp.size);

        // Compare
        if (current_value != wp.previous_value) {
            wp.hit_count++;
            wp.previous_value = std::move(current_value);
            return {true, wp.id};
        }
    }

    return {};
}

void WatchManager::update_cached_values(const MemoryReader& reader) {
    for (auto& [id, wp] : watchpoints_) {
        if (!wp.enabled) {
            continue;
        }
        wp.previous_value = reader(wp.handle, wp.offset, wp.size);
    }
}

void WatchManager::clear() {
    watchpoints_.clear();
}

}  // namespace dotvm::debugger
