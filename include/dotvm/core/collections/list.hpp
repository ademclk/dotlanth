/// @file list.hpp
/// @brief Dynamic list (vector) of Values for the DotVM
///
/// Provides a type-safe, bounds-checked dynamic array that stores VM Values.

#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "dotvm/core/value.hpp"

namespace dotvm::core::collections {

/// @brief Dynamic list of Values
///
/// A growable array that stores NaN-boxed Values. Provides O(1) random access
/// and amortized O(1) push/pop at the end.
class DotList {
public:
    /// Default constructor - empty list
    DotList() noexcept = default;

    /// Construct with reserved capacity
    explicit DotList(std::size_t initial_capacity) { data_.reserve(initial_capacity); }

    /// Get element at index
    /// @param index Zero-based index
    /// @return Value at index, or nil if out of bounds
    [[nodiscard]] Value get(std::size_t index) const noexcept {
        if (index >= data_.size()) {
            return Value::nil();
        }
        return data_[index];
    }

    /// Get element at index with bounds checking
    /// @param index Zero-based index
    /// @return Value if valid index, nullopt otherwise
    [[nodiscard]] std::optional<Value> get_checked(std::size_t index) const noexcept {
        if (index >= data_.size()) {
            return std::nullopt;
        }
        return data_[index];
    }

    /// Set element at index
    /// @param index Zero-based index
    /// @param val Value to set
    /// @return true if successful, false if out of bounds
    bool set(std::size_t index, Value val) noexcept {
        if (index >= data_.size()) {
            return false;
        }
        data_[index] = val;
        return true;
    }

    /// Push value to end of list
    /// @param val Value to push
    void push(Value val) { data_.push_back(val); }

    /// Pop value from end of list
    /// @return Popped value, or nil if empty
    [[nodiscard]] Value pop() noexcept {
        if (data_.empty()) {
            return Value::nil();
        }
        auto val = data_.back();
        data_.pop_back();
        return val;
    }

    /// Insert value at index (shifts subsequent elements)
    /// @param index Position to insert at
    /// @param val Value to insert
    /// @return true if successful
    bool insert(std::size_t index, Value val) {
        if (index > data_.size()) {
            return false;
        }
        data_.insert(data_.begin() + static_cast<std::ptrdiff_t>(index), val);
        return true;
    }

    /// Remove value at index (shifts subsequent elements)
    /// @param index Position to remove
    /// @return Removed value, or nil if out of bounds
    [[nodiscard]] Value remove(std::size_t index) {
        if (index >= data_.size()) {
            return Value::nil();
        }
        auto val = data_[index];
        data_.erase(data_.begin() + static_cast<std::ptrdiff_t>(index));
        return val;
    }

    /// Get list size
    [[nodiscard]] std::size_t size() const noexcept { return data_.size(); }

    /// Check if empty
    [[nodiscard]] bool empty() const noexcept { return data_.empty(); }

    /// Clear all elements
    void clear() noexcept { data_.clear(); }

    /// Reserve capacity
    void reserve(std::size_t capacity) { data_.reserve(capacity); }

    /// Get capacity
    [[nodiscard]] std::size_t capacity() const noexcept { return data_.capacity(); }

    /// Get raw data pointer (for iteration)
    [[nodiscard]] const Value* data() const noexcept { return data_.data(); }

    /// Iterator support
    [[nodiscard]] auto begin() const noexcept { return data_.begin(); }
    [[nodiscard]] auto end() const noexcept { return data_.end(); }
    [[nodiscard]] auto begin() noexcept { return data_.begin(); }
    [[nodiscard]] auto end() noexcept { return data_.end(); }

private:
    std::vector<Value> data_;
};

}  // namespace dotvm::core::collections
