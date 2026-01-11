#pragma once

/// @file result.hpp
/// @brief Enhanced Result type for exception-free error handling
///
/// Provides a modern Result<T, E> type with monadic operations,
/// enabling functional-style error handling without exceptions.

#include <functional>
#include <type_traits>
#include <utility>
#include <variant>

namespace dotvm::core {

/// @brief Tag type for constructing Result in success state
struct Ok_t {
    explicit constexpr Ok_t() noexcept = default;
};

/// @brief Tag type for constructing Result in error state
struct Err_t {
    explicit constexpr Err_t() noexcept = default;
};

/// @brief Tag value for explicit success construction
inline constexpr Ok_t Ok{};

/// @brief Tag value for explicit error construction
inline constexpr Err_t Err{};

/// @brief Generic Result type for exception-free error handling
///
/// Result<T, E> represents either a successful value of type T, or an error
/// of type E. It provides monadic operations for chaining computations.
///
/// @tparam T The success value type
/// @tparam E The error type
///
/// @example
/// ```cpp
/// Result<int, MemoryError> divide(int a, int b) {
///     if (b == 0) return MemoryError::InvalidSize;
///     return a / b;
/// }
///
/// auto result = divide(10, 2)
///     .map([](int x) { return x * 2; })
///     .value_or(-1);  // Returns 10
/// ```
template<typename T, typename E>
class Result {
public:
    using value_type = T;
    using error_type = E;

    // =========================================================================
    // Constructors
    // =========================================================================

    /// @brief Construct a success result from a value
    constexpr Result(T value) noexcept(std::is_nothrow_move_constructible_v<T>)
        : data_{std::in_place_index<0>, std::move(value)} {}

    /// @brief Construct an error result
    constexpr Result(E error) noexcept(std::is_nothrow_move_constructible_v<E>)
        : data_{std::in_place_index<1>, std::move(error)} {}

    /// @brief Construct success explicitly with Ok tag
    template<typename U>
        requires std::convertible_to<U, T>
    constexpr Result(Ok_t, U&& value) noexcept(std::is_nothrow_constructible_v<T, U&&>)
        : data_{std::in_place_index<0>, std::forward<U>(value)} {}

    /// @brief Construct error explicitly with Err tag
    template<typename U>
        requires std::convertible_to<U, E>
    constexpr Result(Err_t, U&& error) noexcept(std::is_nothrow_constructible_v<E, U&&>)
        : data_{std::in_place_index<1>, std::forward<U>(error)} {}

    // =========================================================================
    // State Queries
    // =========================================================================

    /// @brief Check if the result contains a success value
    [[nodiscard]] constexpr bool is_ok() const noexcept {
        return data_.index() == 0;
    }

    /// @brief Check if the result contains an error
    [[nodiscard]] constexpr bool is_err() const noexcept {
        return data_.index() == 1;
    }

    /// @brief Explicit boolean conversion (true if success)
    [[nodiscard]] constexpr explicit operator bool() const noexcept {
        return is_ok();
    }

    // =========================================================================
    // Value Access
    // =========================================================================

    /// @brief Get the success value (const lvalue reference)
    /// @pre is_ok() must be true
    [[nodiscard]] constexpr const T& value() const& noexcept {
        return std::get<0>(data_);
    }

    /// @brief Get the success value (lvalue reference)
    /// @pre is_ok() must be true
    [[nodiscard]] constexpr T& value() & noexcept {
        return std::get<0>(data_);
    }

    /// @brief Get the success value (rvalue reference)
    /// @pre is_ok() must be true
    [[nodiscard]] constexpr T&& value() && noexcept {
        return std::get<0>(std::move(data_));
    }

    /// @brief Get the error value
    /// @pre is_err() must be true
    [[nodiscard]] constexpr E error() const noexcept {
        return std::get<1>(data_);
    }

    /// @brief Get the value or a default
    [[nodiscard]] constexpr T value_or(T default_value) const& noexcept {
        if (is_ok()) {
            return value();
        }
        return default_value;
    }

    /// @brief Get the value or a default (move semantics)
    [[nodiscard]] constexpr T value_or(T default_value) && noexcept {
        if (is_ok()) {
            return std::move(*this).value();
        }
        return default_value;
    }

    // =========================================================================
    // Monadic Operations
    // =========================================================================

    /// @brief Transform the success value with a function
    ///
    /// If this Result is Ok, applies f to the value and returns a new Result
    /// containing the transformed value. If this Result is Err, returns
    /// a new Result with the same error.
    ///
    /// @param f Function to apply to the success value
    /// @return Result<U, E> where U is the return type of f
    template<typename F>
    [[nodiscard]] constexpr auto map(F&& f) const& -> Result<std::invoke_result_t<F, const T&>, E> {
        using U = std::invoke_result_t<F, const T&>;
        if (is_ok()) {
            return Result<U, E>{std::invoke(std::forward<F>(f), value())};
        }
        return Result<U, E>{error()};
    }

    /// @brief Transform the success value with a function (move semantics)
    template<typename F>
    [[nodiscard]] constexpr auto map(F&& f) && -> Result<std::invoke_result_t<F, T&&>, E> {
        using U = std::invoke_result_t<F, T&&>;
        if (is_ok()) {
            return Result<U, E>{std::invoke(std::forward<F>(f), std::move(*this).value())};
        }
        return Result<U, E>{error()};
    }

    /// @brief Transform the error value with a function
    ///
    /// If this Result is Err, applies f to the error and returns a new Result
    /// containing the transformed error. If this Result is Ok, returns
    /// a new Result with the same value.
    template<typename F>
    [[nodiscard]] constexpr auto map_err(F&& f) const& -> Result<T, std::invoke_result_t<F, E>> {
        using U = std::invoke_result_t<F, E>;
        if (is_err()) {
            return Result<T, U>{Err, std::invoke(std::forward<F>(f), error())};
        }
        return Result<T, U>{Ok, value()};
    }

    /// @brief Chain computations that return Result
    ///
    /// If this Result is Ok, applies f to the value. f must return a Result.
    /// If this Result is Err, returns a new Result with the same error.
    ///
    /// @param f Function returning Result<U, E>
    /// @return The result of f, or the original error
    template<typename F>
    [[nodiscard]] constexpr auto and_then(F&& f) const& -> std::invoke_result_t<F, const T&> {
        if (is_ok()) {
            return std::invoke(std::forward<F>(f), value());
        }
        return std::invoke_result_t<F, const T&>{error()};
    }

    /// @brief Chain computations that return Result (move semantics)
    template<typename F>
    [[nodiscard]] constexpr auto and_then(F&& f) && -> std::invoke_result_t<F, T&&> {
        if (is_ok()) {
            return std::invoke(std::forward<F>(f), std::move(*this).value());
        }
        return std::invoke_result_t<F, T&&>{error()};
    }

    /// @brief Provide a fallback if this Result is an error
    ///
    /// If this Result is Err, applies f to the error. f must return a Result<T, F>.
    /// If this Result is Ok, returns a copy of this Result.
    template<typename F>
    [[nodiscard]] constexpr auto or_else(F&& f) const& -> std::invoke_result_t<F, E> {
        if (is_err()) {
            return std::invoke(std::forward<F>(f), error());
        }
        return std::invoke_result_t<F, E>{value()};
    }

    // =========================================================================
    // Inspection
    // =========================================================================

    /// @brief Execute a function if this Result is Ok
    template<typename F>
    constexpr const Result& inspect(F&& f) const& {
        if (is_ok()) {
            std::invoke(std::forward<F>(f), value());
        }
        return *this;
    }

    /// @brief Execute a function if this Result is Err
    template<typename F>
    constexpr const Result& inspect_err(F&& f) const& {
        if (is_err()) {
            std::invoke(std::forward<F>(f), error());
        }
        return *this;
    }

private:
    std::variant<T, E> data_;
};

/// @brief Specialization for void success type
template<typename E>
class Result<void, E> {
public:
    using value_type = void;
    using error_type = E;

    /// @brief Construct a success result
    constexpr Result() noexcept : data_{std::in_place_index<0>, std::monostate{}} {}

    /// @brief Construct success explicitly with Ok tag
    constexpr Result(Ok_t) noexcept : data_{std::in_place_index<0>, std::monostate{}} {}

    /// @brief Construct an error result
    constexpr Result(E error) noexcept(std::is_nothrow_move_constructible_v<E>)
        : data_{std::in_place_index<1>, std::move(error)} {}

    /// @brief Construct error explicitly with Err tag
    template<typename U>
        requires std::convertible_to<U, E>
    constexpr Result(Err_t, U&& error) noexcept(std::is_nothrow_constructible_v<E, U&&>)
        : data_{std::in_place_index<1>, std::forward<U>(error)} {}

    [[nodiscard]] constexpr bool is_ok() const noexcept {
        return data_.index() == 0;
    }

    [[nodiscard]] constexpr bool is_err() const noexcept {
        return data_.index() == 1;
    }

    [[nodiscard]] constexpr explicit operator bool() const noexcept {
        return is_ok();
    }

    [[nodiscard]] constexpr E error() const noexcept {
        return std::get<1>(data_);
    }

    template<typename F>
    [[nodiscard]] constexpr auto and_then(F&& f) const -> std::invoke_result_t<F> {
        if (is_ok()) {
            return std::invoke(std::forward<F>(f));
        }
        return std::invoke_result_t<F>{error()};
    }

private:
    std::variant<std::monostate, E> data_;
};

}  // namespace dotvm::core
