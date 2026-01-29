/// @file property_testing_test.cpp
/// @brief Tests for the property-based testing framework
///
/// Verifies that the property testing utilities work correctly.

#include <cstdint>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "common.hpp"
#include "property_testing.hpp"

namespace dotvm::test::property {
namespace {

// ============================================================================
// Generator Tests
// ============================================================================

TEST(PropertyTestingGenerator, IntegerGenerator) {
    Generator<int64_t> gen{-100, 100};
    auto rng = make_rng();

    for (int i = 0; i < 100; ++i) {
        int64_t val = gen.generate(rng);
        EXPECT_GE(val, -100);
        EXPECT_LE(val, 100);
    }
}

TEST(PropertyTestingGenerator, FloatGenerator) {
    Generator<double> gen{-10.0, 10.0};
    auto rng = make_rng();

    for (int i = 0; i < 100; ++i) {
        double val = gen.generate(rng);
        EXPECT_GE(val, -10.0);
        EXPECT_LE(val, 10.0);
    }
}

TEST(PropertyTestingGenerator, BoolGenerator) {
    Generator<bool> gen{};
    auto rng = make_rng();

    int true_count = 0;
    int false_count = 0;

    for (int i = 0; i < 1000; ++i) {
        bool val = gen.generate(rng);
        if (val)
            ++true_count;
        else
            ++false_count;
    }

    // Both should be generated (with high probability)
    EXPECT_GT(true_count, 100);
    EXPECT_GT(false_count, 100);
}

TEST(PropertyTestingGenerator, StringGenerator) {
    Generator<std::string> gen{5, 20};
    auto rng = make_rng();

    for (int i = 0; i < 100; ++i) {
        std::string val = gen.generate(rng);
        EXPECT_GE(val.size(), 5);
        EXPECT_LE(val.size(), 20);
    }
}

TEST(PropertyTestingGenerator, VectorGenerator) {
    Generator<std::vector<int>> gen{Generator<int>{0, 100}, 1, 10};
    auto rng = make_rng();

    for (int i = 0; i < 50; ++i) {
        std::vector<int> val = gen.generate(rng);
        EXPECT_GE(val.size(), 1);
        EXPECT_LE(val.size(), 10);

        for (int elem : val) {
            EXPECT_GE(elem, 0);
            EXPECT_LE(elem, 100);
        }
    }
}

// ============================================================================
// Seed Control Tests
// ============================================================================

TEST(PropertyTestingSeed, ReproducibleResults) {
    set_seed(12345);
    auto rng1 = make_rng();
    Generator<int64_t> gen{};

    std::vector<int64_t> first_run;
    for (int i = 0; i < 10; ++i) {
        first_run.push_back(gen.generate(rng1));
    }

    // Reset and regenerate
    set_seed(12345);
    auto rng2 = make_rng();

    std::vector<int64_t> second_run;
    for (int i = 0; i < 10; ++i) {
        second_run.push_back(gen.generate(rng2));
    }

    EXPECT_EQ(first_run, second_run);
}

// ============================================================================
// runProperty Tests
// ============================================================================

TEST(PropertyTestingRunProperty, PassingPropertySingleArg) {
    set_seed(42);
    auto result = runProperty<int64_t>([](int64_t x) { return x + 0 == x; },  // Identity property
                                       100);

    EXPECT_TRUE(result.passed);
    EXPECT_EQ(result.tests_run, 100);
}

TEST(PropertyTestingRunProperty, FailingPropertySingleArg) {
    set_seed(42);
    auto result = runProperty<int64_t>([](int64_t x) { return x < 50; },  // Will fail for x >= 50
                                       1000, Generator<int64_t>{0, 100});

    EXPECT_FALSE(result.passed);
    EXPECT_LE(result.tests_run, 1000);
}

TEST(PropertyTestingRunProperty, PassingPropertyTwoArgs) {
    set_seed(42);
    auto result = runProperty<int64_t, int64_t>(
        [](int64_t a, int64_t b) { return a + b == b + a; },  // Commutativity
        100, Generator<int64_t>{-1000, 1000}, Generator<int64_t>{-1000, 1000});

    EXPECT_TRUE(result.passed);
    EXPECT_EQ(result.tests_run, 100);
}

TEST(PropertyTestingRunProperty, PassingPropertyThreeArgs) {
    set_seed(42);
    // Associativity of addition (may fail for overflow, use small range)
    auto result = runProperty<int32_t, int32_t, int32_t>(
        [](int32_t a, int32_t b, int32_t c) {
            // Use int64_t to avoid overflow
            int64_t sum1 = static_cast<int64_t>(a) + b + c;
            int64_t sum2 = a + (static_cast<int64_t>(b) + c);
            return sum1 == sum2;
        },
        100, Generator<int32_t>{-1000, 1000}, Generator<int32_t>{-1000, 1000},
        Generator<int32_t>{-1000, 1000});

    EXPECT_TRUE(result.passed);
    EXPECT_EQ(result.tests_run, 100);
}

// ============================================================================
// forAll Integration Tests
// ============================================================================

TEST(PropertyTestingForAll, AdditionCommutative) {
    set_seed(42);
    forAll<int32_t, int32_t>(
        [](int32_t a, int32_t b) {
            return static_cast<int64_t>(a) + b == static_cast<int64_t>(b) + a;
        },
        100, Generator<int32_t>{-1000000, 1000000}, Generator<int32_t>{-1000000, 1000000});
}

TEST(PropertyTestingForAll, MultiplicationByZero) {
    set_seed(42);
    forAll<int64_t>([](int64_t x) { return x * 0 == 0; }, 100);
}

TEST(PropertyTestingForAll, StringConcatLength) {
    set_seed(42);
    forAll<std::string, std::string>(
        [](const std::string& a, const std::string& b) {
            return (a + b).length() == a.length() + b.length();
        },
        50, Generator<std::string>{0, 50}, Generator<std::string>{0, 50});
}

// ============================================================================
// Shrinking Tests
// ============================================================================

TEST(PropertyTestingShrink, IntegerShrinking) {
    auto shrunk = shrink(100);

    EXPECT_FALSE(shrunk.empty());
    // Should contain 0
    EXPECT_NE(std::find(shrunk.begin(), shrunk.end(), 0), shrunk.end());
    // Should contain intermediate values from binary shrinking
    EXPECT_NE(std::find(shrunk.begin(), shrunk.end(), 50), shrunk.end());
}

TEST(PropertyTestingShrink, NegativeIntegerShrinking) {
    auto shrunk = shrink(-100);

    EXPECT_FALSE(shrunk.empty());
    // Should contain 0
    EXPECT_NE(std::find(shrunk.begin(), shrunk.end(), 0), shrunk.end());
    // Should contain positive version
    EXPECT_NE(std::find(shrunk.begin(), shrunk.end(), 100), shrunk.end());
}

TEST(PropertyTestingShrink, ZeroNoShrink) {
    auto shrunk = shrink(0);
    EXPECT_TRUE(shrunk.empty());
}

TEST(PropertyTestingShrink, StringShrinking) {
    auto shrunk = shrink(std::string("hello"));

    EXPECT_FALSE(shrunk.empty());
    // Should contain empty string
    EXPECT_NE(std::find(shrunk.begin(), shrunk.end(), ""), shrunk.end());
    // Should contain substrings
    bool has_shorter = false;
    for (const auto& s : shrunk) {
        if (s.size() < 5)
            has_shorter = true;
    }
    EXPECT_TRUE(has_shorter);
}

// ============================================================================
// Property Helper Tests
// ============================================================================

TEST(PropertyTestingHelpers, IsCommutative) {
    auto add = [](int a, int b) { return a + b; };
    auto sub = [](int a, int b) { return a - b; };

    EXPECT_TRUE(isCommutative(add, 5, 3));
    EXPECT_FALSE(isCommutative(sub, 5, 3));  // 5-3 != 3-5
}

TEST(PropertyTestingHelpers, IsAssociative) {
    auto add = [](int a, int b) { return a + b; };

    EXPECT_TRUE(isAssociative(add, 1, 2, 3));
}

TEST(PropertyTestingHelpers, HasIdentity) {
    auto add = [](int a, int b) { return a + b; };
    auto mul = [](int a, int b) { return a * b; };

    EXPECT_TRUE(hasIdentity(add, 5, 0));
    EXPECT_TRUE(hasIdentity(mul, 5, 1));
}

TEST(PropertyTestingHelpers, IsIdempotent) {
    auto identity = [](int x) { return x; };
    auto square = [](int x) { return x * x; };

    EXPECT_TRUE(isIdempotent(identity, 5));
    EXPECT_FALSE(isIdempotent(square, 2));  // square(square(2)) = 16 != 4
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST(PropertyTestingEdgeCases, EmptyStringGenerator) {
    Generator<std::string> gen{0, 0};
    auto rng = make_rng();

    for (int i = 0; i < 10; ++i) {
        std::string val = gen.generate(rng);
        EXPECT_TRUE(val.empty());
    }
}

TEST(PropertyTestingEdgeCases, SingleValueRange) {
    Generator<int> gen{42, 42};
    auto rng = make_rng();

    for (int i = 0; i < 10; ++i) {
        int val = gen.generate(rng);
        EXPECT_EQ(val, 42);
    }
}

TEST(PropertyTestingEdgeCases, EmptyVectorGenerator) {
    Generator<std::vector<int>> gen{Generator<int>{}, 0, 0};
    auto rng = make_rng();

    for (int i = 0; i < 10; ++i) {
        std::vector<int> val = gen.generate(rng);
        EXPECT_TRUE(val.empty());
    }
}

}  // namespace
}  // namespace dotvm::test::property
