/// @file string_pool.cpp
/// @brief Implementation of StringPool with SSO and interning

#include "dotvm/core/string_pool.hpp"

#include <algorithm>
#include <cstring>

namespace dotvm::core {

// ============================================================================
// StringPool Implementation
// ============================================================================

StringPool::StringPool(Config config) noexcept : config_(std::move(config)) {
    entries_.reserve(config_.initial_capacity);
    free_list_.reserve(config_.initial_capacity);
}

StringPool::Result<StringHandle> StringPool::create(std::string_view sv) noexcept {
    // Check length limit
    if (sv.size() > config_.max_string_length) {
        return std::unexpected{StringError::StringTooLong};
    }

    // Use SSO for small strings
    if (SsoString::fits(sv)) {
        return StringHandle::sso_marker(static_cast<std::uint8_t>(sv.size()));
    }

    // Allocate a slot for heap string
    auto slot_opt = allocate_slot();
    if (!slot_opt) {
        return std::unexpected{StringError::PoolFull};
    }

    std::uint32_t index = *slot_opt;
    auto& entry = entries_[index];

    try {
        entry = StringEntry::create(std::string(sv), entry.generation);
        total_bytes_ += sv.size();
    } catch (...) {
        release_slot(index);
        return std::unexpected{StringError::AllocationFailed};
    }

    return StringHandle{.index = index, .generation = entry.generation, .flags = 0};
}

StringPool::Result<StringHandle> StringPool::intern(std::string_view sv) noexcept {
    // Check length limit
    if (sv.size() > config_.max_string_length) {
        return std::unexpected{StringError::StringTooLong};
    }

    // SSO strings don't benefit from interning
    if (SsoString::fits(sv)) {
        return StringHandle::sso_marker(static_cast<std::uint8_t>(sv.size()));
    }

    // Check if interning is enabled
    if (!config_.enable_interning) {
        return create(sv);
    }

    // Look for existing interned string
    auto it = intern_map_.find(sv);
    if (it != intern_map_.end()) {
        auto& entry = entries_[it->second];
        if (entry.is_active && entry.is_interned) {
            entry.ref_count++;
            return StringHandle{.index = it->second, .generation = entry.generation, .flags = 0x02};
        }
    }

    // Create new interned string
    auto slot_opt = allocate_slot();
    if (!slot_opt) {
        return std::unexpected{StringError::PoolFull};
    }

    std::uint32_t index = *slot_opt;
    auto& entry = entries_[index];

    try {
        entry = StringEntry::create(std::string(sv), entry.generation, true);
        total_bytes_ += sv.size();

        // Add to intern map (view points to our stored string)
        intern_map_[entry.data] = index;
    } catch (...) {
        release_slot(index);
        return std::unexpected{StringError::AllocationFailed};
    }

    return StringHandle{.index = index, .generation = entry.generation, .flags = 0x02};
}

StringPool::Result<std::string_view> StringPool::get(StringHandle handle) const noexcept {
    // SSO handles don't store data in pool
    if (handle.is_sso()) {
        // Caller should use the SSO string stored separately
        return std::unexpected{StringError::InvalidHandle};
    }

    auto err = validate_handle(handle);
    if (err != StringError::Success) {
        return std::unexpected{err};
    }

    return entries_[handle.index].data;
}

bool StringPool::is_valid(StringHandle handle) const noexcept {
    if (handle.is_null()) {
        return false;
    }
    if (handle.is_sso()) {
        return handle.index <= SsoString::SSO_MAX_LEN;
    }
    return validate_handle(handle) == StringError::Success;
}

StringPool::Result<std::size_t> StringPool::length(StringHandle handle) const noexcept {
    if (handle.is_sso()) {
        return static_cast<std::size_t>(handle.index);  // SSO length stored in index
    }

    auto result = get(handle);
    if (!result) {
        return std::unexpected{result.error()};
    }
    return result->size();
}

StringPool::Result<StringHandle> StringPool::concat(StringHandle a, StringHandle b) noexcept {
    // Get first string
    std::string result;

    if (a.is_sso()) {
        // SSO strings need to be retrieved from caller's context
        // For now, return error - caller should handle SSO concatenation
        return std::unexpected{StringError::InvalidHandle};
    } else {
        auto a_result = get(a);
        if (!a_result) {
            return std::unexpected{a_result.error()};
        }
        result = std::string(*a_result);
    }

    // Append second string
    if (b.is_sso()) {
        return std::unexpected{StringError::InvalidHandle};
    } else {
        auto b_result = get(b);
        if (!b_result) {
            return std::unexpected{b_result.error()};
        }
        result += *b_result;
    }

    return create(result);
}

StringPool::Result<StringHandle> StringPool::substr(StringHandle handle, std::size_t start,
                                                    std::size_t len) noexcept {
    if (handle.is_sso()) {
        return std::unexpected{StringError::InvalidHandle};
    }

    auto result = get(handle);
    if (!result) {
        return std::unexpected{result.error()};
    }

    auto sv = *result;
    if (start > sv.size()) {
        return create("");
    }

    return create(sv.substr(start, len));
}

StringError StringPool::release(StringHandle handle) noexcept {
    if (handle.is_null() || handle.is_sso()) {
        return StringError::Success;  // Nothing to release for SSO
    }

    auto err = validate_handle(handle);
    if (err != StringError::Success) {
        return err;
    }

    auto& entry = entries_[handle.index];

    if (entry.is_interned) {
        // Decrement ref count
        if (entry.ref_count > 0) {
            entry.ref_count--;
        }
        if (entry.ref_count > 0) {
            return StringError::Success;  // Still referenced
        }
        // Remove from intern map
        intern_map_.erase(entry.data);
    }

    total_bytes_ -= entry.data.size();
    release_slot(handle.index);

    return StringError::Success;
}

void StringPool::clear() noexcept {
    entries_.clear();
    free_list_.clear();
    intern_map_.clear();
    total_bytes_ = 0;
}

std::size_t StringPool::active_count() const noexcept {
    auto count = std::count_if(entries_.begin(), entries_.end(),
                               [](const StringEntry& e) { return e.is_active; });
    return static_cast<std::size_t>(count);
}

std::size_t StringPool::total_bytes() const noexcept {
    return total_bytes_;
}

std::size_t StringPool::interned_count() const noexcept {
    return intern_map_.size();
}

std::optional<std::uint32_t> StringPool::allocate_slot() noexcept {
    if (!free_list_.empty()) {
        std::uint32_t index = free_list_.back();
        free_list_.pop_back();
        return index;
    }

    if (entries_.size() >= config_.max_strings) {
        return std::nullopt;
    }

    auto index = static_cast<std::uint32_t>(entries_.size());
    entries_.push_back(StringEntry::inactive(mem_config::INITIAL_GENERATION));
    return index;
}

void StringPool::release_slot(std::uint32_t index) noexcept {
    if (index >= entries_.size()) {
        return;
    }

    auto& entry = entries_[index];
    entry.data.clear();
    entry.data.shrink_to_fit();
    entry.is_active = false;
    entry.is_interned = false;
    entry.ref_count = 0;

    // Increment generation (with wrap)
    if (entry.generation < mem_config::MAX_GENERATION) {
        entry.generation++;
    } else {
        entry.generation = mem_config::INITIAL_GENERATION;
    }

    free_list_.push_back(index);
}

StringError StringPool::validate_handle(StringHandle handle) const noexcept {
    if (handle.is_null()) {
        return StringError::InvalidHandle;
    }
    if (handle.index >= entries_.size()) {
        return StringError::InvalidHandle;
    }

    const auto& entry = entries_[handle.index];
    if (!entry.is_active) {
        return StringError::InvalidHandle;
    }
    if (entry.generation != handle.generation) {
        return StringError::InvalidHandle;
    }

    return StringError::Success;
}

// ============================================================================
// Value Integration
// ============================================================================

/// @brief Tag bits for string values in NaN-boxing
/// We use TAG_HANDLE with markers in the lower 32-bit payload area (similar to collections)
/// This ensures is_handle() returns true for string values.
namespace string_tag {
/// Bit 30 of the 32-bit index field marks this as a string handle
inline constexpr std::uint32_t STRING_BIT = 0x4000'0000U;
/// Bit 29 marks SSO (when STRING_BIT is also set)
inline constexpr std::uint32_t SSO_BIT = 0x2000'0000U;
/// Bits 0-28 hold the actual index (up to 512M) or SSO length
inline constexpr std::uint32_t INDEX_MASK = 0x1FFF'FFFFU;
}  // namespace string_tag

Value encode_string_value(StringHandle handle, const SsoString* sso) noexcept {
    if (handle.is_sso() && sso != nullptr) {
        // For SSO, mark with STRING_BIT | SSO_BIT, length in lower bits
        std::uint32_t tagged_index = string_tag::STRING_BIT | string_tag::SSO_BIT;
        tagged_index |= static_cast<std::uint32_t>(sso->size() & string_tag::INDEX_MASK);

        std::uint64_t bits = nan_box::HANDLE_PREFIX;
        bits |= static_cast<std::uint64_t>(tagged_index);
        return Value::from_raw(bits);
    }

    // For heap strings, mark with STRING_BIT only
    std::uint32_t tagged_index = string_tag::STRING_BIT;
    tagged_index |= (handle.index & string_tag::INDEX_MASK);

    std::uint64_t bits = nan_box::HANDLE_PREFIX;
    bits |= static_cast<std::uint64_t>(tagged_index);
    // Generation in upper 16 bits of payload
    bits |= (static_cast<std::uint64_t>(handle.generation & 0xFFFF) << 32);
    return Value::from_raw(bits);
}

StringHandle decode_string_handle(Value v, SsoString* sso_out) noexcept {
    if (!is_string_value(v)) {
        return StringHandle::null();
    }

    std::uint64_t bits = v.raw_bits();
    auto tagged_index = static_cast<std::uint32_t>(bits & 0xFFFF'FFFF);

    // Check for SSO
    if ((tagged_index & string_tag::SSO_BIT) != 0) {
        auto len = static_cast<std::uint8_t>(tagged_index & 0xFF);
        if (sso_out != nullptr) {
            // SSO data not stored in value - caller must have it
        }
        return StringHandle::sso_marker(len);
    }

    // Heap string
    auto index = tagged_index & string_tag::INDEX_MASK;
    auto generation = static_cast<std::uint32_t>((bits >> 32) & 0xFFFF);

    return StringHandle{.index = index, .generation = generation, .flags = 0};
}

bool is_string_value(Value v) noexcept {
    if (!v.is_handle()) {
        return false;
    }

    std::uint64_t bits = v.raw_bits();
    auto tagged_index = static_cast<std::uint32_t>(bits & 0xFFFF'FFFF);

    // Check if STRING_BIT is set
    return (tagged_index & string_tag::STRING_BIT) != 0;
}

}  // namespace dotvm::core
