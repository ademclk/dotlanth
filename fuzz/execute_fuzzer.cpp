/**
 * @file execute_fuzzer.cpp
 * @brief LibFuzzer harness for the execution engine
 *
 * This fuzzer tests the VM execution engine against arbitrary bytecode
 * to find crashes, hangs, and memory errors.
 *
 * Target: ExecutionEngine::execute()
 *
 * SECURITY: Uses sandboxed configuration with tight resource limits:
 * - max_instructions: 10,000 (prevent infinite loops)
 * - max_allocations: 100 (prevent memory exhaustion)
 * - max_total_memory: 1MB (tight memory cap)
 * - max_call_depth: 64 (prevent stack overflow)
 *
 * Build with: -fsanitize=fuzzer,address
 * Run with: ./execute_fuzzer corpus/execute -max_total_time=300
 */

#include <dotvm/core/bytecode.hpp>
#include <dotvm/core/vm_context.hpp>
#include <dotvm/exec/execution_engine.hpp>

#include <cstddef>
#include <cstdint>
#include <span>

using namespace dotvm::core;
using namespace dotvm::exec;

namespace {

/// Create a tightly sandboxed VM configuration for fuzzing
///
/// We use stricter limits than VmConfig::sandboxed() to ensure the fuzzer
/// can explore many inputs without hanging.
VmConfig make_fuzz_config() noexcept {
    VmConfig config = VmConfig::sandboxed();

    // Tighten resource limits for fuzzing
    config.resource_limits.max_instructions = 10'000;
    config.resource_limits.max_allocations = 100;
    config.resource_limits.max_total_memory = 1024 * 1024;  // 1MB
    config.resource_limits.max_call_depth = 64;
    config.resource_limits.max_backward_jumps = 1'000;

    // Disable JIT to reduce complexity during fuzzing
    config.jit_enabled = false;

    // Keep SIMD disabled for determinism
    config.simd_enabled = false;

    return config;
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    // Reject empty input or input too small for a header
    if (size < sizeof(BytecodeHeader)) {
        return 0;
    }

    // Limit input size to prevent excessive memory usage
    // Bytecode files are typically small for embedded VMs
    constexpr std::size_t MAX_INPUT_SIZE = 256 * 1024;  // 256KB
    if (size > MAX_INPUT_SIZE) {
        return 0;
    }

    // First, validate the bytecode header
    // We only attempt execution if the header is valid to focus fuzzing
    // on the execution engine rather than header parsing
    auto header_result = read_header(std::span{data, size});
    if (!header_result) {
        // Invalid header - not a bug, just malformed input
        return 0;
    }

    auto validation_error = validate_header(*header_result, size);
    if (validation_error != BytecodeError::Success) {
        // Validation failure - expected for fuzz input
        return 0;
    }

    // Header is valid - attempt execution with sandboxed config
    VmContext ctx{make_fuzz_config()};
    ExecutionEngine engine{ctx};

    // Calculate code pointer and size from header
    // The code section starts after the header
    const auto* code_start =
        reinterpret_cast<const std::uint32_t*>(data + sizeof(BytecodeHeader));
    const std::size_t code_bytes = size - sizeof(BytecodeHeader);
    const std::size_t code_size = code_bytes / sizeof(std::uint32_t);

    if (code_size == 0) {
        // No code to execute
        return 0;
    }

    // Create empty constant pool (we don't parse it for simplicity)
    std::span<const Value> const_pool;

    // Execute - result doesn't matter, only crashes
    [[maybe_unused]] auto result = engine.execute(code_start, code_size, 0, const_pool);

    return 0;
}
