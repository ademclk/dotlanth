/**
 * @file asm_fuzzer.cpp
 * @brief LibFuzzer harness for the assembly parser
 *
 * This fuzzer tests the assembly parsing and lexing logic against arbitrary
 * input to find crashes, hangs, and memory errors.
 *
 * Target: AsmParser::parse()
 *
 * Build with: -fsanitize=fuzzer,address
 * Run with: ./asm_fuzzer corpus/asm -max_total_time=300
 */

#include <dotvm/core/asm/asm_parser.hpp>

#include <cstddef>
#include <cstdint>
#include <string_view>

using namespace dotvm::core::asm_;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    // Reject empty input - nothing useful to parse
    if (size == 0) {
        return 0;
    }

    // Limit input size to prevent excessive memory usage during parsing.
    // Assembly files are typically small, so 64KB is generous.
    constexpr std::size_t MAX_INPUT_SIZE = 64 * 1024;
    if (size > MAX_INPUT_SIZE) {
        return 0;
    }

    // Create string_view from fuzz input
    // Note: fuzz input may contain embedded nulls or non-UTF8 bytes
    std::string_view source{reinterpret_cast<const char*>(data), size};

    // Configure parser to NOT process includes - we don't want to hit the
    // filesystem during fuzzing, and include processing could lead to
    // path traversal issues with arbitrary input
    AsmParserConfig config{
        .include_base_dir = "",
        .max_include_depth = 0,
        .process_includes = false,
    };

    // Create parser and parse input
    AsmParser parser{source, config};
    [[maybe_unused]] auto result = parser.parse();

    // Result contains program (possibly partial) and errors
    // Both outcomes are valid for arbitrary input

    return 0;
}
