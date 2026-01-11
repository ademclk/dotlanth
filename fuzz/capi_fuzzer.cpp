/**
 * @file capi_fuzzer.cpp
 * @brief LibFuzzer harness for the C API
 *
 * This fuzzer tests the C API bytecode loading and validation against
 * arbitrary input to find crashes, hangs, and memory errors.
 *
 * Build with: -fsanitize=fuzzer,address
 * Run with: ./capi_fuzzer corpus/capi -max_total_time=300
 */

#include <dotvm/dotvm_c.h>

#include <cstddef>
#include <cstdint>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    // Create VM with secure default configuration
    dotvm_config_t config = DOTVM_CONFIG_INIT;
    config.cfi_enabled = 1;
    config.strict_overflow = 1;

    dotvm_vm_t* vm = dotvm_create(&config);
    if (vm == nullptr) {
        // Allocation failure - not a bug
        return 0;
    }

    // Try to load the fuzzed bytecode
    dotvm_result_t result = dotvm_load_bytecode(vm, data, size);

    // If loading succeeded, exercise more API surface
    if (result == DOTVM_OK) {
        // Try to step (execution not yet implemented, but tests API paths)
        [[maybe_unused]] auto step_result = dotvm_step(vm);

        // Read some registers
        for (int i = 0; i < 16; ++i) {
            [[maybe_unused]] auto val = dotvm_get_register(vm, static_cast<uint8_t>(i));
        }

        // Query functions
        [[maybe_unused]] auto arch = dotvm_get_arch(vm);
        [[maybe_unused]] auto pc = dotvm_get_pc(vm);
        [[maybe_unused]] auto loaded = dotvm_is_loaded(vm);

        // Reset and try again
        dotvm_reset(vm);
    }

    // Always check error handling path
    [[maybe_unused]] const char* error = dotvm_get_error(vm);
    dotvm_clear_error(vm);

    // Clean up
    dotvm_destroy(vm);

    return 0;
}
