/**
 * @file dsl_fuzzer.cpp
 * @brief LibFuzzer harness for the DSL parser
 *
 * This fuzzer tests the DSL parsing and lexing logic against arbitrary input
 * to find crashes, hangs, and memory errors in the recursive descent parser.
 *
 * Target: DslParser::parse(std::string_view)
 *
 * Build with: -fsanitize=fuzzer,address
 * Run with: ./dsl_fuzzer corpus/dsl -max_total_time=300
 */

#include <dotvm/core/dsl/parser.hpp>

#include <cstddef>
#include <cstdint>
#include <string_view>

using namespace dotvm::core::dsl;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    // Reject empty input - nothing useful to parse
    if (size == 0) {
        return 0;
    }

    // Limit input size to prevent excessive memory usage during parsing.
    // DSL source files are typically small, so 64KB is generous.
    constexpr std::size_t MAX_INPUT_SIZE = 64 * 1024;
    if (size > MAX_INPUT_SIZE) {
        return 0;
    }

    // Create string_view from fuzz input
    // Note: fuzz input may contain embedded nulls or non-UTF8 bytes
    std::string_view source{reinterpret_cast<const char*>(data), size};

    // Parse the input - we don't care about the result, only crashes
    // The parser uses panic-mode recovery, so it should handle any input
    [[maybe_unused]] auto result = DslParser::parse(source);

    // Result is either success with a DslModule or error with DslErrorList
    // Both outcomes are valid for arbitrary input

    return 0;
}
