/* Integration test for Dotvm::CApi
 *
 * This test verifies that:
 * 1. C API header can be included from pure C
 * 2. Basic C API functions are accessible
 * 3. Linking succeeds with C linker
 */

#include <dotvm/dotvm_c.h>
#include <stdio.h>

int main(void) {
    printf("Dotvm::CApi integration test\n");

    /* Get version info */
    const char* version = dotvm_version();
    int bytecode_ver = dotvm_bytecode_version();

    printf("DotVM version: %s (bytecode version: %d)\n", version, bytecode_ver);

    /* Create default configuration */
    dotvm_config_t config = DOTVM_CONFIG_INIT;

    /* Create and destroy a VM context */
    dotvm_vm_t* vm = dotvm_create(&config);
    if (!vm) {
        fprintf(stderr, "ERROR: Failed to create VM\n");
        return 1;
    }

    printf("Created VM successfully\n");

    /* Test basic value operations */
    dotvm_value_t int_val = dotvm_value_int(42);
    if (!dotvm_value_is_int(int_val)) {
        fprintf(stderr, "ERROR: Value is not an integer\n");
        dotvm_destroy(vm);
        return 1;
    }

    int64_t extracted = dotvm_value_as_int(int_val);
    if (extracted != 42) {
        fprintf(stderr, "ERROR: Value mismatch (%ld != 42)\n", (long)extracted);
        dotvm_destroy(vm);
        return 1;
    }

    printf("Value operations work correctly\n");

    dotvm_destroy(vm);
    printf("Destroyed VM successfully\n");

    printf("C API integration test PASSED\n");
    return 0;
}
