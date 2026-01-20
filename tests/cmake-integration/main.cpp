// Integration test for Dotvm::Core
//
// This simple test verifies that:
// 1. Headers can be included
// 2. Basic types and functions are accessible
// 3. Linking succeeds

#include <dotvm/dotvm.hpp>
#include <iostream>

int main() {
    std::cout << "Dotvm::Core integration test\n";

    // Test that we can create basic DotVM types
    dotvm::core::RegisterFile regs;

    // Test value creation
    auto val = dotvm::core::Value::from_int(42);

    // Verify value type
    if (!val.is_integer()) {
        std::cerr << "ERROR: Value is not an integer\n";
        return 1;
    }

    // Test register write and read
    regs.write(1, val);
    auto read_val = regs.read(1);
    if (!read_val.is_integer()) {
        std::cerr << "ERROR: Read value is not an integer\n";
        return 1;
    }

    std::cout << "Created RegisterFile and Value successfully\n";

#ifdef DOTVM_SIMD_ENABLED
    std::cout << "SIMD support: enabled\n";
#else
    std::cout << "SIMD support: disabled\n";
#endif

#ifdef DOTVM_JIT_ENABLED
    std::cout << "JIT support: enabled\n";
#else
    std::cout << "JIT support: disabled\n";
#endif

    std::cout << "Integration test PASSED\n";
    return 0;
}
