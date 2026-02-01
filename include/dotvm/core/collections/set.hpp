/// @file set.hpp
/// @brief Hash set of Values for the DotVM
///
/// Provides a type-safe hash set that stores unique NaN-boxed Values.

#pragma once

#include <cstdint>
#include <unordered_set>
#include <vector>

#include "dotvm/core/value.hpp"
#include "map.hpp"  // For ValueHash, ValueEqual

namespace dotvm::core::collections {

/// @brief Hash set of unique Values
///
/// A hash set that stores unique Values. Uses the same hash and equality
/// functions as DotMap.
class DotSet {
public:
    using SetType = std::unordered_set<Value, ValueHash, ValueEqual>;

    /// Default constructor - empty set
    DotSet() noexcept = default;

    /// Construct with reserved capacity
    explicit DotSet(std::size_t initial_capacity) { data_.reserve(initial_capacity); }

    /// Add value to set
    /// @param val Value to add
    /// @return true if value was added (not already present)
    bool add(Value val) {
        auto [it, inserted] = data_.insert(val);
        return inserted;
    }

    /// Check if value exists in set
    /// @param val Value to check
    /// @return true if value is in set
    [[nodiscard]] bool has(Value val) const noexcept { return data_.find(val) != data_.end(); }

    /// Remove value from set
    /// @param val Value to remove
    /// @return true if value was removed
    bool remove(Value val) { return data_.erase(val) > 0; }

    /// Get all values as a vector
    /// @return Vector of all values
    [[nodiscard]] std::vector<Value> to_vector() const {
        std::vector<Value> result;
        result.reserve(data_.size());
        for (const auto& v : data_) {
            result.push_back(v);
        }
        return result;
    }

    /// Get set size
    [[nodiscard]] std::size_t size() const noexcept { return data_.size(); }

    /// Check if empty
    [[nodiscard]] bool empty() const noexcept { return data_.empty(); }

    /// Clear all elements
    void clear() noexcept { data_.clear(); }

    /// Iterator support
    [[nodiscard]] auto begin() const noexcept { return data_.begin(); }
    [[nodiscard]] auto end() const noexcept { return data_.end(); }

    // Set operations

    /// Union with another set
    /// @param other Set to union with
    /// @return New set containing all elements from both
    [[nodiscard]] DotSet union_with(const DotSet& other) const {
        DotSet result = *this;
        for (const auto& v : other) {
            result.add(v);
        }
        return result;
    }

    /// Intersection with another set
    /// @param other Set to intersect with
    /// @return New set containing only elements in both
    [[nodiscard]] DotSet intersection_with(const DotSet& other) const {
        DotSet result;
        for (const auto& v : *this) {
            if (other.has(v)) {
                result.add(v);
            }
        }
        return result;
    }

    /// Difference with another set
    /// @param other Set to subtract
    /// @return New set containing elements in this but not other
    [[nodiscard]] DotSet difference_with(const DotSet& other) const {
        DotSet result;
        for (const auto& v : *this) {
            if (!other.has(v)) {
                result.add(v);
            }
        }
        return result;
    }

private:
    SetType data_;
};

}  // namespace dotvm::core::collections
