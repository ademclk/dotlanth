#pragma once

/// @file property_testing.hpp
/// @brief Lightweight property-based testing framework for DotVM
///
/// Provides tools for property-based testing with random input generation
/// and integration with GoogleTest. Inspired by QuickCheck-style testing.
///
/// Example usage:
/// @code
/// TEST(ValueTest, PropertyAdditionCommutative) {
///     forAll<int64_t, int64_t>([](int64_t a, int64_t b) {
///         return a + b == b + a;
///     }, 100);  // Run 100 random cases
/// }
/// @endcode

#include <cstdint>
#include <functional>
#include <limits>
#include <random>
#include <string>
#include <type_traits>
#include <vector>

#include <gtest/gtest.h>

namespace dotvm::test::property {

// ============================================================================
// Random Seed Management
// ============================================================================

/// @brief Thread-local random seed for reproducibility
/// Set via set_seed() before running property tests
inline thread_local std::uint64_t g_seed = std::random_device{}();

/// @brief Set the random seed for reproducibility
/// @param seed The seed value to use
inline void set_seed(std::uint64_t seed) noexcept {
    g_seed = seed;
}

/// @brief Get the current random seed
/// @return The current seed value
[[nodiscard]] inline std::uint64_t get_seed() noexcept {
    return g_seed;
}

/// @brief Get a seeded random engine
/// @return A mt19937_64 engine seeded with the current global seed
[[nodiscard]] inline std::mt19937_64 make_rng() {
    return std::mt19937_64{g_seed};
}

// ============================================================================
// Generator Traits
// ============================================================================

/// @brief Trait for generating random values of type T
/// Specialize this for custom types
template <typename T, typename Enable = void>
struct Generator {
    static_assert(sizeof(T) == 0, "No Generator specialization for this type");
};

/// @brief Generator for integral types
template <typename T>
struct Generator<T, std::enable_if_t<std::is_integral_v<T>>> {
    using value_type = T;

    T min_val;
    T max_val;

    Generator(T min = std::numeric_limits<T>::min(), T max = std::numeric_limits<T>::max())
        : min_val(min), max_val(max) {}

    [[nodiscard]] T generate(std::mt19937_64& rng) const {
        if constexpr (std::is_same_v<T, bool>) {
            std::uniform_int_distribution<int> dist(0, 1);
            return dist(rng) != 0;
        } else {
            std::uniform_int_distribution<T> dist(min_val, max_val);
            return dist(rng);
        }
    }

    [[nodiscard]] std::string describe(const T& val) const {
        if constexpr (std::is_same_v<T, bool>) {
            return val ? "true" : "false";
        } else {
            return std::to_string(val);
        }
    }
};

/// @brief Generator for floating-point types
template <typename T>
struct Generator<T, std::enable_if_t<std::is_floating_point_v<T>>> {
    using value_type = T;

    T min_val;
    T max_val;

    Generator(T min = T(-1e10), T max = T(1e10)) : min_val(min), max_val(max) {}

    [[nodiscard]] T generate(std::mt19937_64& rng) const {
        std::uniform_real_distribution<T> dist(min_val, max_val);
        return dist(rng);
    }

    [[nodiscard]] std::string describe(const T& val) const { return std::to_string(val); }
};

/// @brief Generator for strings
template <>
struct Generator<std::string> {
    using value_type = std::string;

    std::size_t min_len;
    std::size_t max_len;
    std::string charset;

    Generator(std::size_t min = 0, std::size_t max = 100,
              std::string chars = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789")
        : min_len(min), max_len(max), charset(std::move(chars)) {}

    [[nodiscard]] std::string generate(std::mt19937_64& rng) const {
        std::uniform_int_distribution<std::size_t> len_dist(min_len, max_len);
        std::uniform_int_distribution<std::size_t> char_dist(0, charset.size() - 1);

        std::size_t len = len_dist(rng);
        std::string result;
        result.reserve(len);

        for (std::size_t i = 0; i < len; ++i) {
            result += charset[char_dist(rng)];
        }
        return result;
    }

    [[nodiscard]] std::string describe(const std::string& val) const {
        if (val.size() <= 20) {
            return "\"" + val + "\"";
        }
        return "\"" + val.substr(0, 17) + "...\" (len=" + std::to_string(val.size()) + ")";
    }
};

/// @brief Generator for vectors of any type with a generator
template <typename T>
struct Generator<std::vector<T>> {
    using value_type = std::vector<T>;

    Generator<T> element_gen;
    std::size_t min_size;
    std::size_t max_size;

    Generator(Generator<T> elem_gen = {}, std::size_t min = 0, std::size_t max = 50)
        : element_gen(std::move(elem_gen)), min_size(min), max_size(max) {}

    [[nodiscard]] std::vector<T> generate(std::mt19937_64& rng) const {
        std::uniform_int_distribution<std::size_t> size_dist(min_size, max_size);
        std::size_t size = size_dist(rng);

        std::vector<T> result;
        result.reserve(size);

        for (std::size_t i = 0; i < size; ++i) {
            result.push_back(element_gen.generate(rng));
        }
        return result;
    }

    [[nodiscard]] std::string describe(const std::vector<T>& val) const {
        std::string result = "[";
        for (std::size_t i = 0; i < val.size() && i < 5; ++i) {
            if (i > 0)
                result += ", ";
            result += element_gen.describe(val[i]);
        }
        if (val.size() > 5) {
            result += ", ... (" + std::to_string(val.size()) + " elements)";
        }
        result += "]";
        return result;
    }
};

// ============================================================================
// Property Test Result
// ============================================================================

/// @brief Result of a property test run
struct PropertyResult {
    bool passed{true};
    std::size_t tests_run{0};
    std::size_t test_failed_at{0};
    std::string failure_description;
    std::uint64_t seed_used{0};

    [[nodiscard]] explicit operator bool() const noexcept { return passed; }
};

// ============================================================================
// Core forAll Implementation
// ============================================================================

/// @brief Run a property test with one argument
/// @tparam T The type to generate
/// @param property The property function to test (returns bool)
/// @param num_tests Number of random test cases to generate
/// @param gen Optional custom generator
/// @return PropertyResult with test outcome
template <typename T, typename F>
[[nodiscard]] PropertyResult runProperty(F&& property, std::size_t num_tests,
                                         Generator<T> gen = {}) {
    PropertyResult result;
    result.seed_used = g_seed;

    auto rng = make_rng();

    for (std::size_t i = 0; i < num_tests; ++i) {
        T value = gen.generate(rng);
        result.tests_run = i + 1;

        if (!property(value)) {
            result.passed = false;
            result.test_failed_at = i;
            result.failure_description =
                "Failed at test " + std::to_string(i) + " with input: " + gen.describe(value);
            return result;
        }
    }

    return result;
}

/// @brief Run a property test with two arguments
template <typename T1, typename T2, typename F>
[[nodiscard]] PropertyResult runProperty(F&& property, std::size_t num_tests,
                                         Generator<T1> gen1 = {}, Generator<T2> gen2 = {}) {
    PropertyResult result;
    result.seed_used = g_seed;

    auto rng = make_rng();

    for (std::size_t i = 0; i < num_tests; ++i) {
        T1 val1 = gen1.generate(rng);
        T2 val2 = gen2.generate(rng);
        result.tests_run = i + 1;

        if (!property(val1, val2)) {
            result.passed = false;
            result.test_failed_at = i;
            result.failure_description = "Failed at test " + std::to_string(i) + " with inputs: (" +
                                         gen1.describe(val1) + ", " + gen2.describe(val2) + ")";
            return result;
        }
    }

    return result;
}

/// @brief Run a property test with three arguments
template <typename T1, typename T2, typename T3, typename F>
[[nodiscard]] PropertyResult runProperty(F&& property, std::size_t num_tests,
                                         Generator<T1> gen1 = {}, Generator<T2> gen2 = {},
                                         Generator<T3> gen3 = {}) {
    PropertyResult result;
    result.seed_used = g_seed;

    auto rng = make_rng();

    for (std::size_t i = 0; i < num_tests; ++i) {
        T1 val1 = gen1.generate(rng);
        T2 val2 = gen2.generate(rng);
        T3 val3 = gen3.generate(rng);
        result.tests_run = i + 1;

        if (!property(val1, val2, val3)) {
            result.passed = false;
            result.test_failed_at = i;
            result.failure_description = "Failed at test " + std::to_string(i) + " with inputs: (" +
                                         gen1.describe(val1) + ", " + gen2.describe(val2) + ", " +
                                         gen3.describe(val3) + ")";
            return result;
        }
    }

    return result;
}

// ============================================================================
// GoogleTest Integration - forAll macros
// ============================================================================

/// @brief Run a property test with one argument and assert success
template <typename T, typename F>
void forAll(F&& property, std::size_t num_tests = 100, Generator<T> gen = {}) {
    auto result = runProperty<T>(std::forward<F>(property), num_tests, std::move(gen));
    EXPECT_TRUE(result.passed) << result.failure_description << "\nSeed: " << result.seed_used
                               << " (use property::set_seed(" << result.seed_used
                               << ") to reproduce)";
}

/// @brief Run a property test with two arguments and assert success
template <typename T1, typename T2, typename F>
void forAll(F&& property, std::size_t num_tests = 100, Generator<T1> gen1 = {},
            Generator<T2> gen2 = {}) {
    auto result =
        runProperty<T1, T2>(std::forward<F>(property), num_tests, std::move(gen1), std::move(gen2));
    EXPECT_TRUE(result.passed) << result.failure_description << "\nSeed: " << result.seed_used
                               << " (use property::set_seed(" << result.seed_used
                               << ") to reproduce)";
}

/// @brief Run a property test with three arguments and assert success
template <typename T1, typename T2, typename T3, typename F>
void forAll(F&& property, std::size_t num_tests = 100, Generator<T1> gen1 = {},
            Generator<T2> gen2 = {}, Generator<T3> gen3 = {}) {
    auto result = runProperty<T1, T2, T3>(std::forward<F>(property), num_tests, std::move(gen1),
                                          std::move(gen2), std::move(gen3));
    EXPECT_TRUE(result.passed) << result.failure_description << "\nSeed: " << result.seed_used
                               << " (use property::set_seed(" << result.seed_used
                               << ") to reproduce)";
}

// ============================================================================
// Shrinking Support (Basic)
// ============================================================================

/// @brief Shrink an integer toward zero
template <typename T>
    requires std::is_integral_v<T>
[[nodiscard]] std::vector<T> shrink(T value) {
    std::vector<T> results;

    if (value == T{0}) {
        return results;
    }

    // Try zero first
    results.push_back(T{0});

    // Binary shrinking toward zero
    T current = value;
    while (current != T{0}) {
        current = current / T{2};
        if (current != value) {
            results.push_back(current);
        }
    }

    // If negative, also try positive
    if constexpr (std::is_signed_v<T>) {
        if (value < T{0}) {
            results.push_back(-value);
        }
    }

    return results;
}

/// @brief Shrink a string by removing characters
[[nodiscard]] inline std::vector<std::string> shrink(const std::string& value) {
    std::vector<std::string> results;

    if (value.empty()) {
        return results;
    }

    // Empty string
    results.emplace_back();

    // Remove each character
    for (std::size_t i = 0; i < value.size(); ++i) {
        std::string s = value.substr(0, i) + value.substr(i + 1);
        results.push_back(std::move(s));
    }

    // Take halves
    if (value.size() > 1) {
        results.push_back(value.substr(0, value.size() / 2));
        results.push_back(value.substr(value.size() / 2));
    }

    return results;
}

/// @brief Run property with shrinking on failure
template <typename T, typename F>
[[nodiscard]] PropertyResult runPropertyWithShrink(F&& property, std::size_t num_tests,
                                                   Generator<T> gen = {},
                                                   std::size_t max_shrinks = 100) {
    auto result = runProperty<T>(std::forward<F>(property), num_tests, gen);

    if (result.passed) {
        return result;
    }

    // Re-run to get the failing value
    auto rng = make_rng();
    T failing_value{};

    for (std::size_t i = 0; i <= result.test_failed_at; ++i) {
        failing_value = gen.generate(rng);
    }

    // Try to shrink
    T smallest_failing = failing_value;
    std::size_t shrink_attempts = 0;

    while (shrink_attempts < max_shrinks) {
        auto shrunk_values = shrink(smallest_failing);
        bool found_smaller = false;

        for (const auto& s : shrunk_values) {
            if (!property(s)) {
                smallest_failing = s;
                found_smaller = true;
                break;
            }
        }

        if (!found_smaller) {
            break;
        }
        ++shrink_attempts;
    }

    result.failure_description = "Failed with shrunk input: " + gen.describe(smallest_failing) +
                                 " (original: " + gen.describe(failing_value) + ", " +
                                 std::to_string(shrink_attempts) + " shrink steps)";

    return result;
}

// ============================================================================
// Common Property Helpers
// ============================================================================

/// @brief Check that a function is idempotent: f(f(x)) == f(x)
template <typename T, typename F>
[[nodiscard]] bool isIdempotent(F&& func, const T& value) {
    auto first = func(value);
    auto second = func(first);
    return first == second;
}

/// @brief Check that an operation is commutative: f(a, b) == f(b, a)
template <typename T, typename F>
[[nodiscard]] bool isCommutative(F&& func, const T& a, const T& b) {
    return func(a, b) == func(b, a);
}

/// @brief Check that an operation is associative: f(f(a, b), c) == f(a, f(b, c))
template <typename T, typename F>
[[nodiscard]] bool isAssociative(F&& func, const T& a, const T& b, const T& c) {
    return func(func(a, b), c) == func(a, func(b, c));
}

/// @brief Check that e is an identity element: f(a, e) == a
template <typename T, typename F>
[[nodiscard]] bool hasIdentity(F&& func, const T& a, const T& identity) {
    return func(a, identity) == a;
}

/// @brief Check that an operation distributes: f(a, g(b, c)) == g(f(a, b), f(a, c))
template <typename T, typename F, typename G>
[[nodiscard]] bool isDistributive(F&& f, G&& g, const T& a, const T& b, const T& c) {
    return f(a, g(b, c)) == g(f(a, b), f(a, c));
}

}  // namespace dotvm::test::property
