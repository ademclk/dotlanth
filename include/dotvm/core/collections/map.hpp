/// @file map.hpp
/// @brief Hash map with Value keys for the DotVM
///
/// Provides a type-safe hash map that uses NaN-boxed Values as both keys and values.

#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <unordered_map>
#include <vector>

#include "dotvm/core/value.hpp"

namespace dotvm::core::collections {

/// @brief Hash function for Value keys
struct ValueHash {
    std::size_t operator()(Value v) const noexcept {
        // Use raw bits as hash - works well for most value types
        return std::hash<std::uint64_t>{}(v.raw_bits());
    }
};

/// @brief Equality comparison for Value keys
struct ValueEqual {
    bool operator()(Value a, Value b) const noexcept {
        // For integer and bool, compare directly
        if (a.is_integer() && b.is_integer()) {
            return a.as_integer() == b.as_integer();
        }
        if (a.is_bool() && b.is_bool()) {
            return a.as_bool() == b.as_bool();
        }
        if (a.is_nil() && b.is_nil()) {
            return true;
        }
        // For floats, handle NaN (NaN != NaN, but we want same bits to match)
        if (a.is_float() && b.is_float()) {
            return a.raw_bits() == b.raw_bits();
        }
        // For handles, compare by raw bits (same handle = same object)
        if (a.is_handle() && b.is_handle()) {
            return a.raw_bits() == b.raw_bits();
        }
        // Different types are not equal
        return false;
    }
};

/// @brief Hash map from Value keys to Value values
///
/// A hash map that supports any Value type as key. Integer, bool, and handle
/// keys are recommended; float keys may have precision issues.
class DotMap {
public:
    using MapType = std::unordered_map<Value, Value, ValueHash, ValueEqual>;

    /// Default constructor - empty map
    DotMap() noexcept = default;

    /// Construct with reserved capacity
    explicit DotMap(std::size_t initial_capacity) { data_.reserve(initial_capacity); }

    /// Get value for key
    /// @param key The key to look up
    /// @return Value if found, nil otherwise
    [[nodiscard]] Value get(Value key) const noexcept {
        auto it = data_.find(key);
        if (it == data_.end()) {
            return Value::nil();
        }
        return it->second;
    }

    /// Get value for key with bounds checking
    /// @param key The key to look up
    /// @return Value if found, nullopt otherwise
    [[nodiscard]] std::optional<Value> get_checked(Value key) const noexcept {
        auto it = data_.find(key);
        if (it == data_.end()) {
            return std::nullopt;
        }
        return it->second;
    }

    /// Set value for key
    /// @param key The key
    /// @param val The value to set
    void set(Value key, Value val) { data_[key] = val; }

    /// Check if key exists
    /// @param key The key to check
    /// @return true if key exists
    [[nodiscard]] bool has(Value key) const noexcept { return data_.find(key) != data_.end(); }

    /// Delete key
    /// @param key The key to delete
    /// @return true if key existed and was deleted
    bool remove(Value key) { return data_.erase(key) > 0; }

    /// Get all keys
    /// @return Vector of keys
    [[nodiscard]] std::vector<Value> keys() const {
        std::vector<Value> result;
        result.reserve(data_.size());
        for (const auto& [k, v] : data_) {
            result.push_back(k);
        }
        return result;
    }

    /// Get all values
    /// @return Vector of values
    [[nodiscard]] std::vector<Value> values() const {
        std::vector<Value> result;
        result.reserve(data_.size());
        for (const auto& [k, v] : data_) {
            result.push_back(v);
        }
        return result;
    }

    /// Get map size
    [[nodiscard]] std::size_t size() const noexcept { return data_.size(); }

    /// Check if empty
    [[nodiscard]] bool empty() const noexcept { return data_.empty(); }

    /// Clear all entries
    void clear() noexcept { data_.clear(); }

    /// Iterator support
    [[nodiscard]] auto begin() const noexcept { return data_.begin(); }
    [[nodiscard]] auto end() const noexcept { return data_.end(); }
    [[nodiscard]] auto begin() noexcept { return data_.begin(); }
    [[nodiscard]] auto end() noexcept { return data_.end(); }

private:
    MapType data_;
};

}  // namespace dotvm::core::collections
