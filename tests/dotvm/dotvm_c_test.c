/**
 * @file dotvm_c_test.c
 * @brief Pure C compilation test for DotVM C API
 *
 * This file validates that the C API header is C-compatible.
 * It must compile as pure C (not C++) to ensure FFI compatibility.
 */

#include <dotvm/dotvm_c.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Simple assertion macro for C */
#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "FAILED: %s\n  at %s:%d\n", msg, __FILE__, __LINE__); \
            return 1; \
        } \
    } while (0)

#define TEST_ASSERT_EQ_INT(a, b, msg) \
    do { \
        if ((a) != (b)) { \
            fprintf(stderr, "FAILED: %s\n  expected %d, got %d\n  at %s:%d\n", \
                    msg, (int)(b), (int)(a), __FILE__, __LINE__); \
            return 1; \
        } \
    } while (0)

#define TEST_ASSERT_EQ_I64(a, b, msg) \
    do { \
        if ((a) != (b)) { \
            fprintf(stderr, "FAILED: %s\n  expected %lld, got %lld\n  at %s:%d\n", \
                    msg, (long long)(b), (long long)(a), __FILE__, __LINE__); \
            return 1; \
        } \
    } while (0)

/* Test VM creation and destruction */
static int test_vm_lifecycle(void) {
    dotvm_vm_t* vm = dotvm_create(NULL);
    TEST_ASSERT(vm != NULL, "dotvm_create should return valid VM");

    int arch = dotvm_get_arch(vm);
    TEST_ASSERT_EQ_INT(arch, 1, "Default architecture should be Arch64 (1)");

    dotvm_destroy(vm);

    /* Should not crash on NULL */
    dotvm_destroy(NULL);

    printf("  [PASS] test_vm_lifecycle\n");
    return 0;
}

/* Test VM creation with config */
static int test_vm_config(void) {
    dotvm_config_t config = DOTVM_CONFIG_INIT;
    config.arch = 0;  /* Arch32 */

    dotvm_vm_t* vm = dotvm_create(&config);
    TEST_ASSERT(vm != NULL, "dotvm_create with config should return valid VM");

    int arch = dotvm_get_arch(vm);
    TEST_ASSERT_EQ_INT(arch, 0, "Architecture should be Arch32 (0)");

    dotvm_destroy(vm);

    printf("  [PASS] test_vm_config\n");
    return 0;
}

/* Test value creation */
static int test_value_nil(void) {
    dotvm_value_t nil_val = dotvm_value_nil();

    TEST_ASSERT(dotvm_value_is_nil(nil_val), "nil value should be nil");
    TEST_ASSERT_EQ_INT(dotvm_value_type(nil_val), DOTVM_TYPE_NIL, "nil type should be DOTVM_TYPE_NIL");

    printf("  [PASS] test_value_nil\n");
    return 0;
}

/* Test integer values */
static int test_value_int(void) {
    dotvm_value_t int_val = dotvm_value_int(42);

    TEST_ASSERT(dotvm_value_is_int(int_val), "int value should be int");
    TEST_ASSERT(!dotvm_value_is_float(int_val), "int value should not be float");
    TEST_ASSERT(!dotvm_value_is_nil(int_val), "int value should not be nil");
    TEST_ASSERT_EQ_INT(dotvm_value_type(int_val), DOTVM_TYPE_INTEGER, "int type should be DOTVM_TYPE_INTEGER");
    TEST_ASSERT_EQ_I64(dotvm_value_as_int(int_val), 42, "int value should be 42");

    /* Test negative integer */
    dotvm_value_t neg_val = dotvm_value_int(-12345);
    TEST_ASSERT(dotvm_value_is_int(neg_val), "negative int should be int");
    TEST_ASSERT_EQ_I64(dotvm_value_as_int(neg_val), -12345, "negative int should be -12345");

    printf("  [PASS] test_value_int\n");
    return 0;
}

/* Test float values */
static int test_value_float(void) {
    dotvm_value_t float_val = dotvm_value_float(3.14);

    TEST_ASSERT(dotvm_value_is_float(float_val), "float value should be float");
    TEST_ASSERT(!dotvm_value_is_int(float_val), "float value should not be int");
    TEST_ASSERT_EQ_INT(dotvm_value_type(float_val), DOTVM_TYPE_FLOAT, "float type should be DOTVM_TYPE_FLOAT");

    /* Check value is approximately correct (floating point comparison) */
    double f = dotvm_value_as_float(float_val);
    TEST_ASSERT(f > 3.13 && f < 3.15, "float value should be approximately 3.14");

    printf("  [PASS] test_value_float\n");
    return 0;
}

/* Test boolean values */
static int test_value_bool(void) {
    dotvm_value_t true_val = dotvm_value_bool(1);
    dotvm_value_t false_val = dotvm_value_bool(0);

    TEST_ASSERT(dotvm_value_is_bool(true_val), "true should be bool");
    TEST_ASSERT(dotvm_value_is_bool(false_val), "false should be bool");

    TEST_ASSERT_EQ_INT(dotvm_value_as_bool(true_val), 1, "true should be 1");
    TEST_ASSERT_EQ_INT(dotvm_value_as_bool(false_val), 0, "false should be 0");

    printf("  [PASS] test_value_bool\n");
    return 0;
}

/* Test register access */
static int test_register_access(void) {
    dotvm_vm_t* vm = dotvm_create(NULL);
    TEST_ASSERT(vm != NULL, "VM creation failed");

    /* Set register */
    dotvm_value_t val = dotvm_value_int(42);
    dotvm_result_t result = dotvm_set_register(vm, 1, val);
    TEST_ASSERT_EQ_INT(result, DOTVM_OK, "set_register should succeed");

    /* Get register */
    dotvm_value_t read_val = dotvm_get_register(vm, 1);
    TEST_ASSERT(dotvm_value_is_int(read_val), "read value should be int");
    TEST_ASSERT_EQ_I64(dotvm_value_as_int(read_val), 42, "read value should be 42");

    dotvm_destroy(vm);

    printf("  [PASS] test_register_access\n");
    return 0;
}

/* Test R0 is hardwired to zero */
static int test_r0_hardwired(void) {
    dotvm_vm_t* vm = dotvm_create(NULL);
    TEST_ASSERT(vm != NULL, "VM creation failed");

    /* Try to write to R0 */
    dotvm_value_t val = dotvm_value_int(42);
    dotvm_set_register(vm, 0, val);

    /* R0 should still be zero */
    dotvm_value_t r0_val = dotvm_get_register(vm, 0);
    TEST_ASSERT(dotvm_value_is_float(r0_val), "R0 should be zero (float)");

    double f = dotvm_value_as_float(r0_val);
    TEST_ASSERT(f == 0.0, "R0 should be 0.0");

    dotvm_destroy(vm);

    printf("  [PASS] test_r0_hardwired\n");
    return 0;
}

/* Test error handling */
static int test_error_handling(void) {
    dotvm_vm_t* vm = dotvm_create(NULL);
    TEST_ASSERT(vm != NULL, "VM creation failed");

    /* Initially no error */
    const char* err = dotvm_get_error(vm);
    TEST_ASSERT(err == NULL, "initially no error");

    /* Trigger error with invalid bytecode */
    dotvm_result_t result = dotvm_load_bytecode(vm, NULL, 0);
    TEST_ASSERT_EQ_INT(result, DOTVM_INVALID_ARG, "null bytecode should return DOTVM_INVALID_ARG");

    err = dotvm_get_error(vm);
    TEST_ASSERT(err != NULL, "error message should be set");

    /* Clear error */
    dotvm_clear_error(vm);
    err = dotvm_get_error(vm);
    TEST_ASSERT(err == NULL, "error should be cleared");

    dotvm_destroy(vm);

    printf("  [PASS] test_error_handling\n");
    return 0;
}

/* Test execution without bytecode */
static int test_execute_not_loaded(void) {
    dotvm_vm_t* vm = dotvm_create(NULL);
    TEST_ASSERT(vm != NULL, "VM creation failed");

    TEST_ASSERT_EQ_INT(dotvm_is_loaded(vm), 0, "initially not loaded");

    dotvm_result_t result = dotvm_execute(vm);
    TEST_ASSERT_EQ_INT(result, DOTVM_NOT_LOADED, "execute should fail without bytecode");

    dotvm_destroy(vm);

    printf("  [PASS] test_execute_not_loaded\n");
    return 0;
}

/* Test version functions */
static int test_version(void) {
    const char* version = dotvm_version();
    TEST_ASSERT(version != NULL, "version should not be NULL");
    TEST_ASSERT(strlen(version) > 0, "version should not be empty");

    int bytecode_version = dotvm_bytecode_version();
    TEST_ASSERT_EQ_INT(bytecode_version, 26, "bytecode version should be 26");

    printf("  [PASS] test_version\n");
    return 0;
}

/* Test reset function */
static int test_reset(void) {
    dotvm_vm_t* vm = dotvm_create(NULL);
    TEST_ASSERT(vm != NULL, "VM creation failed");

    /* Set a register */
    dotvm_set_register(vm, 1, dotvm_value_int(42));
    TEST_ASSERT_EQ_I64(dotvm_value_as_int(dotvm_get_register(vm, 1)), 42, "register should be 42");

    /* Reset */
    dotvm_result_t result = dotvm_reset(vm);
    TEST_ASSERT_EQ_INT(result, DOTVM_OK, "reset should succeed");

    /* Register should be cleared */
    dotvm_value_t r1_val = dotvm_get_register(vm, 1);
    TEST_ASSERT(dotvm_value_is_float(r1_val), "register should be zero after reset");

    dotvm_destroy(vm);

    printf("  [PASS] test_reset\n");
    return 0;
}

int main(void) {
    int failures = 0;

    printf("DotVM C API Compatibility Tests\n");
    printf("================================\n\n");

    failures += test_vm_lifecycle();
    failures += test_vm_config();
    failures += test_value_nil();
    failures += test_value_int();
    failures += test_value_float();
    failures += test_value_bool();
    failures += test_register_access();
    failures += test_r0_hardwired();
    failures += test_error_handling();
    failures += test_execute_not_loaded();
    failures += test_version();
    failures += test_reset();

    printf("\n================================\n");
    if (failures == 0) {
        printf("All tests passed!\n");
        printf("DotVM C API version: %s\n", dotvm_version());
        return 0;
    } else {
        printf("%d test(s) FAILED\n", failures);
        return 1;
    }
}
