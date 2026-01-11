/**
 * @file bytecode_fuzzer.cpp
 * @brief LibFuzzer harness for bytecode parsing
 *
 * This fuzzer tests the bytecode parsing and validation logic against
 * arbitrary input to find crashes, hangs, and memory errors.
 *
 * Build with: -fsanitize=fuzzer,address
 * Run with: ./bytecode_fuzzer corpus/bytecode -max_total_time=300
 */

#include <dotvm/core/bytecode.hpp>

#include <cstddef>
#include <cstdint>
#include <span>

using namespace dotvm::core;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    // Reject empty input early
    if (size == 0) {
        return 0;
    }

    // Test header parsing - this should never crash regardless of input
    auto header_result = read_header(std::span{data, size});
    if (!header_result) {
        // Invalid header is expected for fuzz input - not a bug
        return 0;
    }

    // Test header validation - checks bounds, alignment, etc.
    auto validation_error = validate_header(*header_result, size);
    if (validation_error != BytecodeError::Success) {
        // Validation failure is expected for malformed input
        return 0;
    }

    // If we get here, the header passed all validation
    // Test constant pool parsing if present
    if (header_result->const_pool_size > 0) {
        auto pool_offset = static_cast<std::size_t>(header_result->const_pool_offset);
        auto pool_size = static_cast<std::size_t>(header_result->const_pool_size);

        // Double-check bounds (validation should have caught this)
        if (pool_offset + pool_size <= size) {
            auto pool_span = std::span{data + pool_offset, pool_size};
            [[maybe_unused]] auto pool_result = load_constant_pool(pool_span);
            // Result doesn't matter - we're testing for crashes
        }
    }

    return 0;
}
