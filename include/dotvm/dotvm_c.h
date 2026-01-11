/**
 * @file dotvm_c.h
 * @brief C API for embedding the DotVM virtual machine
 *
 * This header provides a C-compatible interface for creating, configuring,
 * and executing bytecode in the DotVM runtime. All types use opaque pointers
 * for ABI stability.
 *
 * Thread Safety:
 *   - A dotvm_vm_t instance is NOT thread-safe
 *   - Each thread should have its own VM instance
 *   - The caller is responsible for synchronization if sharing VMs
 *
 * Memory Management:
 *   - VMs created with dotvm_create() must be destroyed with dotvm_destroy()
 *   - All bytecode data must remain valid until dotvm_load_bytecode() returns
 *
 * Error Handling:
 *   - All fallible functions return dotvm_result_t
 *   - Use dotvm_get_error() to retrieve error messages
 *   - Error strings are valid until the next API call on the same VM
 */

#ifndef DOTVM_C_H
#define DOTVM_C_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Export Macros
 * ============================================================================ */

#if defined(_WIN32) || defined(_WIN64)
    #ifdef DOTVM_C_EXPORTS
        #define DOTVM_API __declspec(dllexport)
    #else
        #define DOTVM_API __declspec(dllimport)
    #endif
#else
    #define DOTVM_API __attribute__((visibility("default")))
#endif

/* ============================================================================
 * Opaque Types
 * ============================================================================ */

/**
 * @brief Opaque VM instance pointer
 *
 * Represents a complete VM execution context including registers,
 * memory manager, and execution state. Must be created with dotvm_create()
 * and destroyed with dotvm_destroy().
 */
typedef struct dotvm_vm dotvm_vm_t;

/* ============================================================================
 * Result Codes
 * ============================================================================ */

/**
 * @brief Result codes for API operations
 */
typedef enum dotvm_result {
    DOTVM_OK          =  0,  /**< Operation succeeded */
    DOTVM_ERROR       = -1,  /**< Generic error (check dotvm_get_error) */
    DOTVM_OOM         = -2,  /**< Out of memory */
    DOTVM_INVALID_ARG = -3,  /**< Invalid argument (null pointer, bad index) */
    DOTVM_NOT_LOADED  = -4,  /**< Bytecode not loaded */
    DOTVM_HALTED      = -5   /**< VM has halted (normal termination) */
} dotvm_result_t;

/* ============================================================================
 * Value Type
 * ============================================================================ */

/**
 * @brief Opaque 64-bit value type
 *
 * DotVM uses NaN-boxing to store multiple types (integers, floats, bools,
 * handles, nil) in a single 64-bit value. This type is the raw representation.
 *
 * Use the dotvm_value_* helper functions to create and inspect values.
 * Direct manipulation of the bits is not recommended.
 */
typedef uint64_t dotvm_value_t;

/**
 * @brief Value type tags for runtime type inspection
 */
typedef enum dotvm_value_type {
    DOTVM_TYPE_FLOAT   = 0,  /**< IEEE 754 double-precision float */
    DOTVM_TYPE_INTEGER = 1,  /**< 48-bit signed integer */
    DOTVM_TYPE_BOOL    = 2,  /**< Boolean (true/false) */
    DOTVM_TYPE_HANDLE  = 3,  /**< Memory handle (index + generation) */
    DOTVM_TYPE_NIL     = 4,  /**< Nil/null value */
    DOTVM_TYPE_POINTER = 5   /**< Raw 48-bit pointer */
} dotvm_value_type_t;

/* ============================================================================
 * Configuration
 * ============================================================================ */

/**
 * @brief VM configuration structure
 *
 * Pass to dotvm_create() to configure the VM. All fields have sensible defaults
 * when zero-initialized. Use DOTVM_CONFIG_INIT for default values.
 */
typedef struct dotvm_config {
    uint8_t  arch;            /**< Architecture: 0=Arch32, 1=Arch64 (default) */
    uint8_t  strict_overflow; /**< If non-zero, enable strict overflow checks */
    uint8_t  cfi_enabled;     /**< If non-zero, enable Control Flow Integrity */
    uint8_t  _reserved[5];    /**< Reserved for future use (must be zero) */
    size_t   max_memory;      /**< Max allocation size (0 = use default 64MB) */
} dotvm_config_t;

/**
 * @brief Default configuration initializer
 *
 * Usage: dotvm_config_t config = DOTVM_CONFIG_INIT;
 */
#define DOTVM_CONFIG_INIT { 1, 0, 0, {0,0,0,0,0}, 0 }

/* ============================================================================
 * VM Lifecycle
 * ============================================================================ */

/**
 * @brief Create a new VM instance
 *
 * @param config Configuration (may be NULL for defaults)
 * @return New VM instance, or NULL on allocation failure
 *
 * The returned VM must be destroyed with dotvm_destroy().
 *
 * @note Thread-safe: Can be called from multiple threads simultaneously
 */
DOTVM_API dotvm_vm_t* dotvm_create(const dotvm_config_t* config);

/**
 * @brief Destroy a VM instance
 *
 * @param vm VM instance to destroy (may be NULL, which is a no-op)
 *
 * Frees all resources associated with the VM, including any allocated memory.
 * After this call, the vm pointer is invalid.
 *
 * @note Thread-safe: Must not be called while other threads are using the VM
 */
DOTVM_API void dotvm_destroy(dotvm_vm_t* vm);

/* ============================================================================
 * Bytecode Loading
 * ============================================================================ */

/**
 * @brief Load bytecode into the VM
 *
 * @param vm   VM instance
 * @param data Pointer to bytecode data
 * @param size Size of bytecode in bytes
 * @return DOTVM_OK on success, error code otherwise
 *
 * The bytecode is validated and copied into the VM. The data pointer does not
 * need to remain valid after this call returns.
 *
 * Replaces any previously loaded bytecode. The VM state (registers, memory)
 * is reset when new bytecode is loaded.
 *
 * @note Thread-safety: NOT thread-safe. Caller must ensure exclusive access.
 */
DOTVM_API dotvm_result_t dotvm_load_bytecode(dotvm_vm_t* vm,
                                              const uint8_t* data,
                                              size_t size);

/* ============================================================================
 * Execution
 * ============================================================================ */

/**
 * @brief Execute bytecode
 *
 * @param vm VM instance
 * @return DOTVM_OK if execution completed normally
 *         DOTVM_HALTED if VM halted (normal termination)
 *         DOTVM_NOT_LOADED if no bytecode loaded
 *         DOTVM_ERROR on execution error
 *
 * Executes from the bytecode entry point until a HALT instruction or error.
 *
 * @note Thread-safety: NOT thread-safe. Caller must ensure exclusive access.
 */
DOTVM_API dotvm_result_t dotvm_execute(dotvm_vm_t* vm);

/**
 * @brief Execute a single instruction (single-step)
 *
 * @param vm VM instance
 * @return DOTVM_OK if instruction executed
 *         DOTVM_HALTED if HALT instruction was executed
 *         Other error codes on failure
 *
 * @note Thread-safety: NOT thread-safe. Caller must ensure exclusive access.
 */
DOTVM_API dotvm_result_t dotvm_step(dotvm_vm_t* vm);

/* ============================================================================
 * Register Access
 * ============================================================================ */

/**
 * @brief Get the value of a register
 *
 * @param vm  VM instance
 * @param idx Register index (0-255)
 * @return Register value, or nil if vm is NULL or idx > 255
 *
 * Register R0 always returns zero (it is hardwired).
 *
 * @note Thread-safety: NOT thread-safe. Caller must ensure exclusive access.
 */
DOTVM_API dotvm_value_t dotvm_get_register(dotvm_vm_t* vm, uint8_t idx);

/**
 * @brief Set the value of a register
 *
 * @param vm    VM instance
 * @param idx   Register index (0-255)
 * @param value Value to set
 * @return DOTVM_OK on success, DOTVM_INVALID_ARG if vm is NULL
 *
 * Writes to R0 are silently ignored (R0 is hardwired to zero).
 * In Arch32 mode, integer values are automatically masked to 32 bits.
 *
 * @note Thread-safety: NOT thread-safe. Caller must ensure exclusive access.
 */
DOTVM_API dotvm_result_t dotvm_set_register(dotvm_vm_t* vm,
                                             uint8_t idx,
                                             dotvm_value_t value);

/* ============================================================================
 * Error Handling
 * ============================================================================ */

/**
 * @brief Get the last error message
 *
 * @param vm VM instance
 * @return Error string, or NULL if no error
 *
 * The returned string is valid until the next API call on this VM instance.
 * Returns NULL if vm is NULL or no error has occurred.
 *
 * @note Thread-safety: NOT thread-safe. Caller must ensure exclusive access.
 */
DOTVM_API const char* dotvm_get_error(dotvm_vm_t* vm);

/**
 * @brief Clear the last error
 *
 * @param vm VM instance
 *
 * @note Thread-safety: NOT thread-safe. Caller must ensure exclusive access.
 */
DOTVM_API void dotvm_clear_error(dotvm_vm_t* vm);

/* ============================================================================
 * Value Helper Functions
 * ============================================================================ */

/**
 * @brief Create a nil value
 * @return Nil value
 */
DOTVM_API dotvm_value_t dotvm_value_nil(void);

/**
 * @brief Create an integer value
 * @param i Integer value (48-bit signed, truncated if out of range)
 * @return Integer value
 */
DOTVM_API dotvm_value_t dotvm_value_int(int64_t i);

/**
 * @brief Create a float value
 * @param f Float value
 * @return Float value
 */
DOTVM_API dotvm_value_t dotvm_value_float(double f);

/**
 * @brief Create a boolean value
 * @param b Boolean (0 = false, non-zero = true)
 * @return Boolean value
 */
DOTVM_API dotvm_value_t dotvm_value_bool(int b);

/**
 * @brief Get the type of a value
 * @param v Value to inspect
 * @return Value type tag
 */
DOTVM_API dotvm_value_type_t dotvm_value_type(dotvm_value_t v);

/**
 * @brief Check if value is nil
 * @param v Value to check
 * @return Non-zero if nil, zero otherwise
 */
DOTVM_API int dotvm_value_is_nil(dotvm_value_t v);

/**
 * @brief Check if value is an integer
 * @param v Value to check
 * @return Non-zero if integer, zero otherwise
 */
DOTVM_API int dotvm_value_is_int(dotvm_value_t v);

/**
 * @brief Check if value is a float
 * @param v Value to check
 * @return Non-zero if float, zero otherwise
 */
DOTVM_API int dotvm_value_is_float(dotvm_value_t v);

/**
 * @brief Check if value is a boolean
 * @param v Value to check
 * @return Non-zero if bool, zero otherwise
 */
DOTVM_API int dotvm_value_is_bool(dotvm_value_t v);

/**
 * @brief Extract integer from value
 * @param v Value (must be integer type)
 * @return Integer value (sign-extended from 48 bits)
 * @note Returns 0 if value is not an integer
 */
DOTVM_API int64_t dotvm_value_as_int(dotvm_value_t v);

/**
 * @brief Extract float from value
 * @param v Value (must be float type)
 * @return Float value
 * @note Returns 0.0 if value is not a float
 */
DOTVM_API double dotvm_value_as_float(dotvm_value_t v);

/**
 * @brief Extract boolean from value
 * @param v Value (must be bool type)
 * @return 0 for false, 1 for true
 * @note Returns 0 if value is not a bool
 */
DOTVM_API int dotvm_value_as_bool(dotvm_value_t v);

/* ============================================================================
 * Query Functions
 * ============================================================================ */

/**
 * @brief Get the VM architecture
 * @param vm VM instance
 * @return 0 for Arch32, 1 for Arch64, -1 if vm is NULL
 */
DOTVM_API int dotvm_get_arch(dotvm_vm_t* vm);

/**
 * @brief Get the program counter
 * @param vm VM instance
 * @return Current program counter value, or 0 if vm is NULL
 */
DOTVM_API uint64_t dotvm_get_pc(dotvm_vm_t* vm);

/**
 * @brief Check if bytecode is loaded
 * @param vm VM instance
 * @return Non-zero if bytecode is loaded, zero otherwise
 */
DOTVM_API int dotvm_is_loaded(dotvm_vm_t* vm);

/**
 * @brief Reset VM state (registers, PC) without unloading bytecode
 * @param vm VM instance
 * @return DOTVM_OK on success, DOTVM_INVALID_ARG if vm is NULL
 */
DOTVM_API dotvm_result_t dotvm_reset(dotvm_vm_t* vm);

/* ============================================================================
 * Version Information
 * ============================================================================ */

/**
 * @brief Get the library version string
 * @return Version string (e.g., "0.1.0")
 */
DOTVM_API const char* dotvm_version(void);

/**
 * @brief Get the bytecode format version
 * @return Bytecode version number (currently 26)
 */
DOTVM_API int dotvm_bytecode_version(void);

#ifdef __cplusplus
}
#endif

#endif /* DOTVM_C_H */
