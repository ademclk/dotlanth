# DotVM Test Suite

This directory contains the comprehensive test suite for the DotVM project. Tests are written using GoogleTest and include unit tests, integration tests, property-based tests, and benchmarks.

## Directory Structure

```
tests/
├── README.md                    # This file
├── dotvm/
│   ├── core/                    # Core VM component tests
│   │   ├── crypto/              # Cryptographic operation tests
│   │   ├── dsl/                 # DSL parser tests
│   │   ├── policy/              # Policy engine tests
│   │   ├── security/            # Security subsystem tests
│   │   ├── simd/                # SIMD operation tests
│   │   ├── state/               # State management tests
│   │   └── *.cpp                # Individual component tests
│   ├── exec/                    # Execution engine tests
│   ├── jit/                     # JIT compiler tests (x86-64 only)
│   ├── cli/                     # CLI tool tests
│   └── test_utils/              # Shared test utilities
│       ├── common.hpp           # Common factory functions and helpers
│       ├── test_fixtures.hpp    # Base test fixtures
│       ├── property_testing.hpp # Property-based testing framework
│       ├── mock_register_file.hpp
│       └── mock_memory_manager.hpp
├── dsl/
│   ├── compiler/                # DSL compiler tests
│   └── stdlib/                  # Standard library tests
├── asm/                         # Assembly lexer/parser tests
└── cmake-integration/           # CMake integration tests
```

## Running Tests

### Build and Run All Tests

```bash
# Configure with tests enabled (default)
cmake --preset ci-clang-release

# Build
cmake --build --preset ci-clang-release

# Run all tests
ctest --preset ci-clang-release --output-on-failure
```

### Run Specific Tests

```bash
# Run tests matching a pattern
ctest --preset ci-clang-release -R "ValueTest"

# Run a specific test executable with filters
./build/dotvm_tests --gtest_filter="*ALU*"

# List all available tests
./build/dotvm_tests --gtest_list_tests
```

### Debug Test Failures

```bash
# Run with verbose output
ctest --preset ci-clang-release -V

# Run single test with detailed output
./build/dotvm_tests --gtest_filter="ValueTest.IntegerOperations" --gtest_break_on_failure
```

## Test Utilities

### Common Utilities (`common.hpp`)

Factory functions for creating test objects:

```cpp
#include "test_utils/common.hpp"

using namespace dotvm::test;

// Create test values
auto int_val = make_test_value(42);
auto float_val = make_test_value(3.14);
auto nil_val = make_test_nil();

// Create random values (with reproducible seed)
set_test_seed(12345);
auto rng = make_test_rng();
auto random_int = make_random_int(rng, -100, 100);

// Create test instructions
auto add = make_add_instr(1, 2, 3);  // ADD R1, R2, R3
auto program = make_test_program({add, make_nop_instr()});

// Value assertions
assert_int_value(result, 42, "Expected 42");
assert_values_equal(actual, expected);
```

### Test Fixtures (`test_fixtures.hpp`)

Base classes for common test setups:

```cpp
#include "test_utils/test_fixtures.hpp"

// VM Context testing
class MyVMTest : public VMContextFixture {
    // Inherits: ctx, set_register(), get_register(), execute()
};

TEST_F(MyVMTest, ExecuteProgram) {
    set_register(1, core::Value::from_int(10));
    auto result = execute(make_program({...}));
    EXPECT_EQ(get_register(2).as_integer(), 20);
}

// Compiler testing
class MyCompilerTest : public CompilerFixture {
    // Inherits: parse(), build_ir(), compile()
};

TEST_F(MyCompilerTest, ParseDSL) {
    auto result = parse("dot test: when true: do: nop");
    ASSERT_TRUE(result.is_ok());
}

// Architecture-parameterized tests
class MyArchTest : public ArchParameterizedTest {};

TEST_P(MyArchTest, TestBothArchitectures) {
    auto arch = GetParam().arch;  // Arch32 or Arch64
    // Test with this architecture
}

INSTANTIATE_TEST_SUITE_P(Architectures, MyArchTest,
    ::testing::ValuesIn(kArchitectures));
```

### Property-Based Testing (`property_testing.hpp`)

QuickCheck-style property testing:

```cpp
#include "test_utils/property_testing.hpp"

using namespace dotvm::test::property;

// Basic property test
TEST(MathProperties, AdditionCommutative) {
    forAll<int64_t, int64_t>([](int64_t a, int64_t b) {
        return a + b == b + a;
    }, 100);  // Run 100 random cases
}

// Custom generator with range
TEST(MathProperties, MultiplicationInRange) {
    Generator<int64_t> small_ints{-100, 100};
    forAll<int64_t, int64_t>([](int64_t a, int64_t b) {
        auto product = a * b;
        return product >= -10000 && product <= 10000;
    }, 100, small_ints, small_ints);
}

// String properties
TEST(StringProperties, ConcatLength) {
    forAll<std::string, std::string>([](const std::string& a, const std::string& b) {
        return (a + b).length() == a.length() + b.length();
    }, 50);
}

// Reproducible failures
TEST(ReproducibleTest, WithSeed) {
    set_seed(12345);  // Set seed for reproducibility
    forAll<int64_t>([](int64_t x) {
        return x * 2 / 2 == x;  // May fail due to overflow
    }, 100);
}

// Using property helpers
TEST(Properties, CommutativeAddition) {
    forAll<int, int>([](int a, int b) {
        return isCommutative([](int x, int y) { return x + y; }, a, b);
    }, 100);
}
```

### Mock Objects

```cpp
#include "test_utils/mock_register_file.hpp"
#include "test_utils/mock_memory_manager.hpp"

// Mock register file with access logging
MockRegisterFile regs;
regs.write(1, core::Value::from_int(42));
auto val = regs.read(1);

EXPECT_EQ(regs.total_accesses(), 2);
EXPECT_EQ(regs.write_count(1), 1);

// Mock memory manager with failure injection
MockMemoryManager mem;
mem.fail_next_allocate(core::MemoryError::AllocationFailed);

auto result = mem.allocate(4096);
EXPECT_FALSE(result.has_value());
```

## Adding New Tests

### 1. Create Test File

Create a new `.cpp` file in the appropriate directory:

```cpp
// tests/dotvm/core/my_component_test.cpp
#include <gtest/gtest.h>
#include "dotvm/core/my_component.hpp"
#include "test_utils/common.hpp"

namespace dotvm::test {
namespace {

class MyComponentTest : public ::testing::Test {
protected:
    // Test fixtures and setup
};

TEST_F(MyComponentTest, BasicFunctionality) {
    // Test code
}

} // namespace
} // namespace dotvm::test
```

### 2. Add to CMakeLists.txt

Add the test file to `CMakeLists.txt`:

```cmake
add_executable(dotvm_tests
    # ... existing tests ...
    tests/dotvm/core/my_component_test.cpp
)
```

### 3. Run and Verify

```bash
cmake --build --preset ci-clang-release
ctest --preset ci-clang-release -R "MyComponent"
```

## Best Practices

### Test Organization

- **One test file per component**: Keep tests focused on a single component
- **Use fixtures for shared setup**: Avoid code duplication with test fixtures
- **Name tests descriptively**: Use `ComponentName_WhatItTests` format

### Property Testing

- **Start small**: Begin with simple properties, add complexity as needed
- **Use appropriate generators**: Create custom generators for domain types
- **Shrink failures**: Use `runPropertyWithShrink` to find minimal failing cases
- **Record seeds**: Always log the seed on failure for reproducibility

### Performance

- **Avoid slow operations in tight loops**: Cache expensive computations
- **Use parameterized tests**: Test multiple inputs efficiently
- **Mark slow tests**: Use `TEST_F(SlowTests, ...)` naming convention

### Debugging

- **Use `--gtest_break_on_failure`**: Break into debugger on failure
- **Set `DOTVM_TEST_SEED=12345`**: Reproduce random test failures
- **Use `SCOPED_TRACE`**: Add context to nested assertions

## Continuous Integration

Tests are run automatically in CI on:
- Every pull request
- Merges to main branch
- Release builds

CI configurations:
- `ci-clang-release`: Clang compiler, Release build
- `ci-gcc-release`: GCC compiler, Release build
- `ci-clang-debug`: Clang compiler, Debug build with sanitizers

## Coverage

Generate coverage reports (requires `-DDOTVM_ENABLE_COVERAGE=ON`):

```bash
cmake -B build -DDOTVM_ENABLE_COVERAGE=ON
cmake --build build
ctest --test-dir build
# Generate report with lcov or similar
```
