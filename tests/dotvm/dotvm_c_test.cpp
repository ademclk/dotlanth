/**
 * @file dotvm_c_test.cpp
 * @brief Unit tests for the DotVM C API
 */

#include <gtest/gtest.h>
#include <dotvm/dotvm_c.h>

#include <cmath>
#include <cstring>
#include <limits>

namespace {

// ============================================================================
// VM Lifecycle Tests
// ============================================================================

class DotVMCApiLifecycleTest : public ::testing::Test {};

TEST_F(DotVMCApiLifecycleTest, Create_DefaultConfig_ReturnsValidVM) {
    dotvm_vm_t* vm = dotvm_create(nullptr);
    ASSERT_NE(vm, nullptr);
    EXPECT_EQ(dotvm_get_arch(vm), 1);  // Default is Arch64
    dotvm_destroy(vm);
}

TEST_F(DotVMCApiLifecycleTest, Create_Arch32Config) {
    dotvm_config_t config = DOTVM_CONFIG_INIT;
    config.arch = 0;  // Arch32

    dotvm_vm_t* vm = dotvm_create(&config);
    ASSERT_NE(vm, nullptr);
    EXPECT_EQ(dotvm_get_arch(vm), 0);
    dotvm_destroy(vm);
}

TEST_F(DotVMCApiLifecycleTest, Create_Arch64Config) {
    dotvm_config_t config = DOTVM_CONFIG_INIT;
    config.arch = 1;  // Arch64

    dotvm_vm_t* vm = dotvm_create(&config);
    ASSERT_NE(vm, nullptr);
    EXPECT_EQ(dotvm_get_arch(vm), 1);
    dotvm_destroy(vm);
}

TEST_F(DotVMCApiLifecycleTest, Create_WithStrictOverflow) {
    dotvm_config_t config = DOTVM_CONFIG_INIT;
    config.strict_overflow = 1;

    dotvm_vm_t* vm = dotvm_create(&config);
    ASSERT_NE(vm, nullptr);
    dotvm_destroy(vm);
}

TEST_F(DotVMCApiLifecycleTest, Create_WithCfiEnabled) {
    dotvm_config_t config = DOTVM_CONFIG_INIT;
    config.cfi_enabled = 1;

    dotvm_vm_t* vm = dotvm_create(&config);
    ASSERT_NE(vm, nullptr);
    dotvm_destroy(vm);
}

TEST_F(DotVMCApiLifecycleTest, Destroy_NullVM_NoOp) {
    // Should not crash
    dotvm_destroy(nullptr);
}

TEST_F(DotVMCApiLifecycleTest, GetArch_NullVM_ReturnsNegative) {
    EXPECT_EQ(dotvm_get_arch(nullptr), -1);
}

// ============================================================================
// Register Tests
// ============================================================================

class DotVMCApiRegisterTest : public ::testing::Test {
protected:
    dotvm_vm_t* vm{nullptr};

    void SetUp() override {
        vm = dotvm_create(nullptr);
        ASSERT_NE(vm, nullptr);
    }

    void TearDown() override {
        dotvm_destroy(vm);
    }
};

TEST_F(DotVMCApiRegisterTest, SetGetRegister_Integer) {
    dotvm_value_t val = dotvm_value_int(42);
    EXPECT_EQ(dotvm_set_register(vm, 1, val), DOTVM_OK);

    dotvm_value_t result = dotvm_get_register(vm, 1);
    EXPECT_TRUE(dotvm_value_is_int(result));
    EXPECT_EQ(dotvm_value_as_int(result), 42);
}

TEST_F(DotVMCApiRegisterTest, SetGetRegister_NegativeInteger) {
    dotvm_value_t val = dotvm_value_int(-12345);
    EXPECT_EQ(dotvm_set_register(vm, 2, val), DOTVM_OK);

    dotvm_value_t result = dotvm_get_register(vm, 2);
    EXPECT_TRUE(dotvm_value_is_int(result));
    EXPECT_EQ(dotvm_value_as_int(result), -12345);
}

TEST_F(DotVMCApiRegisterTest, SetGetRegister_Float) {
    dotvm_value_t val = dotvm_value_float(3.14159);
    EXPECT_EQ(dotvm_set_register(vm, 3, val), DOTVM_OK);

    dotvm_value_t result = dotvm_get_register(vm, 3);
    EXPECT_TRUE(dotvm_value_is_float(result));
    EXPECT_DOUBLE_EQ(dotvm_value_as_float(result), 3.14159);
}

TEST_F(DotVMCApiRegisterTest, SetGetRegister_Bool_True) {
    dotvm_value_t val = dotvm_value_bool(1);
    EXPECT_EQ(dotvm_set_register(vm, 4, val), DOTVM_OK);

    dotvm_value_t result = dotvm_get_register(vm, 4);
    EXPECT_TRUE(dotvm_value_is_bool(result));
    EXPECT_EQ(dotvm_value_as_bool(result), 1);
}

TEST_F(DotVMCApiRegisterTest, SetGetRegister_Bool_False) {
    dotvm_value_t val = dotvm_value_bool(0);
    EXPECT_EQ(dotvm_set_register(vm, 5, val), DOTVM_OK);

    dotvm_value_t result = dotvm_get_register(vm, 5);
    EXPECT_TRUE(dotvm_value_is_bool(result));
    EXPECT_EQ(dotvm_value_as_bool(result), 0);
}

TEST_F(DotVMCApiRegisterTest, SetGetRegister_Nil) {
    dotvm_value_t val = dotvm_value_nil();
    EXPECT_EQ(dotvm_set_register(vm, 6, val), DOTVM_OK);

    dotvm_value_t result = dotvm_get_register(vm, 6);
    EXPECT_TRUE(dotvm_value_is_nil(result));
}

TEST_F(DotVMCApiRegisterTest, R0_AlwaysZero) {
    // Write a non-zero value to R0
    dotvm_value_t val = dotvm_value_int(42);
    dotvm_set_register(vm, 0, val);

    // R0 should still read as zero
    dotvm_value_t result = dotvm_get_register(vm, 0);
    EXPECT_TRUE(dotvm_value_is_float(result));
    EXPECT_DOUBLE_EQ(dotvm_value_as_float(result), 0.0);
}

TEST_F(DotVMCApiRegisterTest, SetRegister_NullVM_ReturnsError) {
    dotvm_value_t val = dotvm_value_int(42);
    EXPECT_EQ(dotvm_set_register(nullptr, 1, val), DOTVM_INVALID_ARG);
}

TEST_F(DotVMCApiRegisterTest, GetRegister_NullVM_ReturnsNil) {
    dotvm_value_t result = dotvm_get_register(nullptr, 1);
    EXPECT_TRUE(dotvm_value_is_nil(result));
}

TEST_F(DotVMCApiRegisterTest, AllRegisters_Accessible) {
    // Test that all 256 registers are accessible
    for (int i = 1; i < 256; ++i) {
        dotvm_value_t val = dotvm_value_int(static_cast<int64_t>(i));
        EXPECT_EQ(dotvm_set_register(vm, static_cast<uint8_t>(i), val), DOTVM_OK);
    }

    for (int i = 1; i < 256; ++i) {
        dotvm_value_t result = dotvm_get_register(vm, static_cast<uint8_t>(i));
        EXPECT_EQ(dotvm_value_as_int(result), static_cast<int64_t>(i));
    }
}

// ============================================================================
// Value Helper Tests
// ============================================================================

class DotVMCApiValueTest : public ::testing::Test {};

TEST_F(DotVMCApiValueTest, Nil_Creation) {
    dotvm_value_t val = dotvm_value_nil();
    EXPECT_TRUE(dotvm_value_is_nil(val));
    EXPECT_EQ(dotvm_value_type(val), DOTVM_TYPE_NIL);
}

TEST_F(DotVMCApiValueTest, Int_Positive) {
    dotvm_value_t val = dotvm_value_int(123456);
    EXPECT_TRUE(dotvm_value_is_int(val));
    EXPECT_FALSE(dotvm_value_is_float(val));
    EXPECT_FALSE(dotvm_value_is_nil(val));
    EXPECT_EQ(dotvm_value_type(val), DOTVM_TYPE_INTEGER);
    EXPECT_EQ(dotvm_value_as_int(val), 123456);
}

TEST_F(DotVMCApiValueTest, Int_Negative) {
    dotvm_value_t val = dotvm_value_int(-987654);
    EXPECT_TRUE(dotvm_value_is_int(val));
    EXPECT_EQ(dotvm_value_type(val), DOTVM_TYPE_INTEGER);
    EXPECT_EQ(dotvm_value_as_int(val), -987654);
}

TEST_F(DotVMCApiValueTest, Int_Zero) {
    dotvm_value_t val = dotvm_value_int(0);
    EXPECT_TRUE(dotvm_value_is_int(val));
    EXPECT_EQ(dotvm_value_as_int(val), 0);
}

TEST_F(DotVMCApiValueTest, Float_Positive) {
    dotvm_value_t val = dotvm_value_float(2.71828);
    EXPECT_TRUE(dotvm_value_is_float(val));
    EXPECT_FALSE(dotvm_value_is_int(val));
    EXPECT_EQ(dotvm_value_type(val), DOTVM_TYPE_FLOAT);
    EXPECT_DOUBLE_EQ(dotvm_value_as_float(val), 2.71828);
}

TEST_F(DotVMCApiValueTest, Float_Negative) {
    dotvm_value_t val = dotvm_value_float(-273.15);
    EXPECT_TRUE(dotvm_value_is_float(val));
    EXPECT_EQ(dotvm_value_type(val), DOTVM_TYPE_FLOAT);
    EXPECT_DOUBLE_EQ(dotvm_value_as_float(val), -273.15);
}

TEST_F(DotVMCApiValueTest, Float_Zero) {
    dotvm_value_t val = dotvm_value_float(0.0);
    EXPECT_TRUE(dotvm_value_is_float(val));
    EXPECT_DOUBLE_EQ(dotvm_value_as_float(val), 0.0);
}

TEST_F(DotVMCApiValueTest, Float_Infinity) {
    dotvm_value_t val = dotvm_value_float(std::numeric_limits<double>::infinity());
    EXPECT_TRUE(dotvm_value_is_float(val));
    EXPECT_TRUE(std::isinf(dotvm_value_as_float(val)));
}

TEST_F(DotVMCApiValueTest, Bool_True) {
    dotvm_value_t val = dotvm_value_bool(1);
    EXPECT_TRUE(dotvm_value_is_bool(val));
    EXPECT_FALSE(dotvm_value_is_nil(val));
    EXPECT_EQ(dotvm_value_type(val), DOTVM_TYPE_BOOL);
    EXPECT_EQ(dotvm_value_as_bool(val), 1);
}

TEST_F(DotVMCApiValueTest, Bool_False) {
    dotvm_value_t val = dotvm_value_bool(0);
    EXPECT_TRUE(dotvm_value_is_bool(val));
    EXPECT_EQ(dotvm_value_type(val), DOTVM_TYPE_BOOL);
    EXPECT_EQ(dotvm_value_as_bool(val), 0);
}

TEST_F(DotVMCApiValueTest, Bool_NonZeroIsTruthy) {
    // Any non-zero value should be treated as true
    dotvm_value_t val = dotvm_value_bool(42);
    EXPECT_TRUE(dotvm_value_is_bool(val));
    EXPECT_EQ(dotvm_value_as_bool(val), 1);
}

TEST_F(DotVMCApiValueTest, AsInt_WhenNotInt_ReturnsZero) {
    dotvm_value_t float_val = dotvm_value_float(3.14);
    EXPECT_EQ(dotvm_value_as_int(float_val), 0);

    dotvm_value_t nil_val = dotvm_value_nil();
    EXPECT_EQ(dotvm_value_as_int(nil_val), 0);

    dotvm_value_t bool_val = dotvm_value_bool(1);
    EXPECT_EQ(dotvm_value_as_int(bool_val), 0);
}

TEST_F(DotVMCApiValueTest, AsFloat_WhenNotFloat_ReturnsZero) {
    dotvm_value_t int_val = dotvm_value_int(42);
    EXPECT_DOUBLE_EQ(dotvm_value_as_float(int_val), 0.0);

    dotvm_value_t nil_val = dotvm_value_nil();
    EXPECT_DOUBLE_EQ(dotvm_value_as_float(nil_val), 0.0);
}

TEST_F(DotVMCApiValueTest, AsBool_WhenNotBool_ReturnsZero) {
    dotvm_value_t int_val = dotvm_value_int(1);
    EXPECT_EQ(dotvm_value_as_bool(int_val), 0);

    dotvm_value_t nil_val = dotvm_value_nil();
    EXPECT_EQ(dotvm_value_as_bool(nil_val), 0);
}

// ============================================================================
// Error Handling Tests
// ============================================================================

class DotVMCApiErrorTest : public ::testing::Test {
protected:
    dotvm_vm_t* vm{nullptr};

    void SetUp() override {
        vm = dotvm_create(nullptr);
        ASSERT_NE(vm, nullptr);
    }

    void TearDown() override {
        dotvm_destroy(vm);
    }
};

TEST_F(DotVMCApiErrorTest, GetError_NoError_ReturnsNull) {
    EXPECT_EQ(dotvm_get_error(vm), nullptr);
}

TEST_F(DotVMCApiErrorTest, GetError_NullVM_ReturnsNull) {
    EXPECT_EQ(dotvm_get_error(nullptr), nullptr);
}

TEST_F(DotVMCApiErrorTest, LoadBytecode_NullData_ReturnsError) {
    EXPECT_EQ(dotvm_load_bytecode(vm, nullptr, 0), DOTVM_INVALID_ARG);
    EXPECT_NE(dotvm_get_error(vm), nullptr);
}

TEST_F(DotVMCApiErrorTest, LoadBytecode_ZeroSize_ReturnsError) {
    uint8_t dummy = 0;
    EXPECT_EQ(dotvm_load_bytecode(vm, &dummy, 0), DOTVM_INVALID_ARG);
}

TEST_F(DotVMCApiErrorTest, LoadBytecode_NullVM_ReturnsError) {
    uint8_t data[] = {0};
    EXPECT_EQ(dotvm_load_bytecode(nullptr, data, sizeof(data)), DOTVM_INVALID_ARG);
}

TEST_F(DotVMCApiErrorTest, LoadBytecode_TooSmall_ReturnsError) {
    uint8_t data[] = {0x44, 0x4F, 0x54, 0x4D};  // Just magic bytes, incomplete
    EXPECT_EQ(dotvm_load_bytecode(vm, data, sizeof(data)), DOTVM_ERROR);
    EXPECT_NE(dotvm_get_error(vm), nullptr);
}

TEST_F(DotVMCApiErrorTest, Execute_NotLoaded_ReturnsError) {
    EXPECT_EQ(dotvm_execute(vm), DOTVM_NOT_LOADED);
}

TEST_F(DotVMCApiErrorTest, Execute_NullVM_ReturnsError) {
    EXPECT_EQ(dotvm_execute(nullptr), DOTVM_INVALID_ARG);
}

TEST_F(DotVMCApiErrorTest, Step_NotLoaded_ReturnsError) {
    EXPECT_EQ(dotvm_step(vm), DOTVM_NOT_LOADED);
}

TEST_F(DotVMCApiErrorTest, Step_NullVM_ReturnsError) {
    EXPECT_EQ(dotvm_step(nullptr), DOTVM_INVALID_ARG);
}

TEST_F(DotVMCApiErrorTest, ClearError_ClearsMessage) {
    // Trigger an error
    dotvm_load_bytecode(vm, nullptr, 0);
    EXPECT_NE(dotvm_get_error(vm), nullptr);

    // Clear error
    dotvm_clear_error(vm);
    EXPECT_EQ(dotvm_get_error(vm), nullptr);
}

TEST_F(DotVMCApiErrorTest, ClearError_NullVM_NoOp) {
    // Should not crash
    dotvm_clear_error(nullptr);
}

// ============================================================================
// Query Function Tests
// ============================================================================

class DotVMCApiQueryTest : public ::testing::Test {
protected:
    dotvm_vm_t* vm{nullptr};

    void SetUp() override {
        vm = dotvm_create(nullptr);
        ASSERT_NE(vm, nullptr);
    }

    void TearDown() override {
        dotvm_destroy(vm);
    }
};

TEST_F(DotVMCApiQueryTest, IsLoaded_Initially_ReturnsFalse) {
    EXPECT_EQ(dotvm_is_loaded(vm), 0);
}

TEST_F(DotVMCApiQueryTest, IsLoaded_NullVM_ReturnsFalse) {
    EXPECT_EQ(dotvm_is_loaded(nullptr), 0);
}

TEST_F(DotVMCApiQueryTest, GetPC_Initially_ReturnsZero) {
    EXPECT_EQ(dotvm_get_pc(vm), 0u);
}

TEST_F(DotVMCApiQueryTest, GetPC_NullVM_ReturnsZero) {
    EXPECT_EQ(dotvm_get_pc(nullptr), 0u);
}

TEST_F(DotVMCApiQueryTest, Reset_NullVM_ReturnsError) {
    EXPECT_EQ(dotvm_reset(nullptr), DOTVM_INVALID_ARG);
}

TEST_F(DotVMCApiQueryTest, Reset_ClearsRegisters) {
    // Set a register
    dotvm_set_register(vm, 1, dotvm_value_int(42));
    EXPECT_EQ(dotvm_value_as_int(dotvm_get_register(vm, 1)), 42);

    // Reset
    EXPECT_EQ(dotvm_reset(vm), DOTVM_OK);

    // Register should be cleared (zero)
    dotvm_value_t result = dotvm_get_register(vm, 1);
    EXPECT_TRUE(dotvm_value_is_float(result));
    EXPECT_DOUBLE_EQ(dotvm_value_as_float(result), 0.0);
}

// ============================================================================
// Version Tests
// ============================================================================

TEST(DotVMCApiVersionTest, Version_ReturnsNonNull) {
    const char* version = dotvm_version();
    ASSERT_NE(version, nullptr);
    EXPECT_GT(strlen(version), 0u);
}

TEST(DotVMCApiVersionTest, Version_ContainsExpectedFormat) {
    const char* version = dotvm_version();
    EXPECT_STREQ(version, "0.1.0");
}

TEST(DotVMCApiVersionTest, BytecodeVersion_Returns26) {
    EXPECT_EQ(dotvm_bytecode_version(), 26);
}

// ============================================================================
// Config Initializer Test
// ============================================================================

TEST(DotVMCApiConfigTest, ConfigInit_HasCorrectDefaults) {
    dotvm_config_t config = DOTVM_CONFIG_INIT;

    EXPECT_EQ(config.arch, 1);           // Arch64
    EXPECT_EQ(config.strict_overflow, 0);
    EXPECT_EQ(config.cfi_enabled, 0);
    EXPECT_EQ(config.max_memory, 0u);    // 0 means use default
}

}  // namespace
