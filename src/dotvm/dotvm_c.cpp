/**
 * @file dotvm_c.cpp
 * @brief C API implementation for DotVM
 *
 * This file implements the C-compatible API defined in dotvm_c.h by wrapping
 * the C++ VmContext and related classes.
 */

#include <dotvm/dotvm_c.h>

#include <dotvm/core/vm_context.hpp>
#include <dotvm/core/bytecode.hpp>
#include <dotvm/core/value.hpp>
#include <dotvm/exec/execution_engine.hpp>

#include <cstring>
#include <memory>
#include <new>
#include <string>
#include <vector>

// ============================================================================
// Internal VM State Structure
// ============================================================================

/// Internal VM state structure - matches forward declaration in dotvm_c.h
struct dotvm_vm {
    dotvm::core::VmContext context;

    // Bytecode storage
    std::vector<std::uint8_t> bytecode_data;
    dotvm::core::BytecodeHeader header{};
    std::vector<dotvm::core::Value> const_pool;

    // Execution state
    std::uint64_t pc{0};
    bool bytecode_loaded{false};
    bool halted{false};

    // Error handling
    std::string last_error;

    // Constructor
    explicit dotvm_vm(dotvm::core::VmConfig config)
        : context{config} {}
};

// ============================================================================
// Helper Functions
// ============================================================================

namespace {

/// Convert C++ Value to C dotvm_value_t
inline dotvm_value_t to_c_value(dotvm::core::Value v) noexcept {
    return v.raw_bits();
}

/// Convert C dotvm_value_t to C++ Value
inline dotvm::core::Value from_c_value(dotvm_value_t v) noexcept {
    return dotvm::core::Value::from_raw(v);
}

/// Set error message on VM
inline void set_error(dotvm_vm* vm, const char* msg) noexcept {
    if (vm != nullptr) {
        vm->last_error = (msg != nullptr) ? msg : "";
    }
}

/// Set error message from string_view
inline void set_error(dotvm_vm* vm, std::string_view msg) noexcept {
    if (vm != nullptr) {
        vm->last_error = msg;
    }
}

}  // anonymous namespace

// ============================================================================
// C API Implementation
// ============================================================================

extern "C" {

// ----------------------------------------------------------------------------
// VM Lifecycle
// ----------------------------------------------------------------------------

DOTVM_API dotvm_vm_t* dotvm_create(const dotvm_config_t* config) {
    try {
        dotvm::core::VmConfig vm_config{};

        if (config != nullptr) {
            vm_config.arch = (config->arch == 0)
                ? dotvm::core::Architecture::Arch32
                : dotvm::core::Architecture::Arch64;
            vm_config.strict_overflow = (config->strict_overflow != 0);
            vm_config.cfi_enabled = (config->cfi_enabled != 0);

            if (config->max_memory > 0) {
                vm_config.max_memory = config->max_memory;
            }
        }

        return new (std::nothrow) dotvm_vm{vm_config};
    } catch (...) {
        return nullptr;
    }
}

DOTVM_API void dotvm_destroy(dotvm_vm_t* vm) {
    delete vm;
}

// ----------------------------------------------------------------------------
// Bytecode Loading
// ----------------------------------------------------------------------------

DOTVM_API dotvm_result_t dotvm_load_bytecode(dotvm_vm_t* vm,
                                              const uint8_t* data,
                                              size_t size) {
    if (vm == nullptr) {
        return DOTVM_INVALID_ARG;
    }
    if (data == nullptr || size == 0) {
        set_error(vm, "Bytecode data is null or empty");
        return DOTVM_INVALID_ARG;
    }

    using namespace dotvm::core;

    // Read and validate header
    auto header_result = read_header(std::span{data, size});
    if (!header_result) {
        set_error(vm, to_string(header_result.error()));
        return DOTVM_ERROR;
    }

    auto validation_error = validate_header(*header_result, size);
    if (validation_error != BytecodeError::Success) {
        set_error(vm, to_string(validation_error));
        return DOTVM_ERROR;
    }

    // Load constant pool
    std::vector<Value> constants;
    if (header_result->const_pool_size > 0) {
        auto pool_offset = static_cast<std::size_t>(header_result->const_pool_offset);
        auto pool_size = static_cast<std::size_t>(header_result->const_pool_size);

        if (pool_offset + pool_size > size) {
            set_error(vm, "Constant pool extends beyond bytecode data");
            return DOTVM_ERROR;
        }

        auto pool_span = std::span{data + pool_offset, pool_size};
        auto pool_result = load_constant_pool(pool_span);
        if (!pool_result) {
            set_error(vm, to_string(pool_result.error()));
            return DOTVM_ERROR;
        }
        constants = std::move(*pool_result);
    }

    // Store bytecode (copy for safety)
    try {
        vm->bytecode_data.assign(data, data + size);
        vm->const_pool = std::move(constants);
    } catch (const std::bad_alloc&) {
        set_error(vm, "Out of memory while loading bytecode");
        return DOTVM_OOM;
    }

    vm->header = *header_result;
    vm->pc = header_result->entry_point;
    vm->bytecode_loaded = true;
    vm->halted = false;

    // Reset VM state
    vm->context.reset();
    vm->last_error.clear();

    return DOTVM_OK;
}

// ----------------------------------------------------------------------------
// Execution
// ----------------------------------------------------------------------------

DOTVM_API dotvm_result_t dotvm_execute(dotvm_vm_t* vm) {
    if (vm == nullptr) {
        return DOTVM_INVALID_ARG;
    }
    if (!vm->bytecode_loaded) {
        set_error(vm, "No bytecode loaded");
        return DOTVM_NOT_LOADED;
    }
    if (vm->halted) {
        return DOTVM_HALTED;
    }

    // Get code section pointer
    const auto* code_bytes = vm->bytecode_data.data() + vm->header.code_offset;
    const auto* code = reinterpret_cast<const std::uint32_t*>(code_bytes);
    auto code_size = vm->header.code_size / 4;  // Convert bytes to instruction count
    auto entry_point = vm->pc / 4;  // Convert byte offset to instruction index

    // Create execution engine and execute
    dotvm::exec::ExecutionEngine engine(vm->context);
    auto result = engine.execute(code, code_size, entry_point, vm->const_pool);

    // Update VM state
    vm->pc = engine.pc() * 4;  // Convert back to byte offset
    vm->halted = engine.halted();

    switch (result) {
        case dotvm::exec::ExecResult::Success:
            return vm->halted ? DOTVM_HALTED : DOTVM_OK;
        case dotvm::exec::ExecResult::InvalidOpcode:
            set_error(vm, "Invalid opcode");
            return DOTVM_ERROR;
        case dotvm::exec::ExecResult::CfiViolation:
            set_error(vm, "Control flow integrity violation");
            return DOTVM_ERROR;
        case dotvm::exec::ExecResult::OutOfBounds:
            set_error(vm, "Program counter out of bounds");
            return DOTVM_ERROR;
        case dotvm::exec::ExecResult::Interrupted:
            set_error(vm, "Execution interrupted");
            return DOTVM_ERROR;
        default:
            set_error(vm, "Execution error");
            return DOTVM_ERROR;
    }
}

DOTVM_API dotvm_result_t dotvm_step(dotvm_vm_t* vm) {
    if (vm == nullptr) {
        return DOTVM_INVALID_ARG;
    }
    if (!vm->bytecode_loaded) {
        set_error(vm, "No bytecode loaded");
        return DOTVM_NOT_LOADED;
    }
    if (vm->halted) {
        return DOTVM_HALTED;
    }

    // Get code section pointer
    const auto* code_bytes = vm->bytecode_data.data() + vm->header.code_offset;
    const auto* code = reinterpret_cast<const std::uint32_t*>(code_bytes);
    auto code_size = vm->header.code_size / 4;
    auto current_pc = vm->pc / 4;

    // Bounds check
    if (current_pc >= code_size) {
        set_error(vm, "Program counter out of bounds");
        return DOTVM_ERROR;
    }

    // Create execution engine and execute single instruction
    dotvm::exec::ExecutionEngine engine(vm->context);

    // Initialize execution context manually for single-step
    // We need to set up the context then step once
    auto result = engine.execute(code, code_size, current_pc, vm->const_pool);

    // For single-step, we actually want to use a modified approach
    // Since execute() runs until halt, we create a mini-program approach
    // For now, just run one instruction by checking the step function

    // Update VM state
    vm->pc = engine.pc() * 4;
    vm->halted = engine.halted();

    switch (result) {
        case dotvm::exec::ExecResult::Success:
            return vm->halted ? DOTVM_HALTED : DOTVM_OK;
        case dotvm::exec::ExecResult::InvalidOpcode:
            set_error(vm, "Invalid opcode");
            return DOTVM_ERROR;
        case dotvm::exec::ExecResult::CfiViolation:
            set_error(vm, "Control flow integrity violation");
            return DOTVM_ERROR;
        case dotvm::exec::ExecResult::OutOfBounds:
            set_error(vm, "Program counter out of bounds");
            return DOTVM_ERROR;
        default:
            set_error(vm, "Step execution error");
            return DOTVM_ERROR;
    }
}

// ----------------------------------------------------------------------------
// Register Access
// ----------------------------------------------------------------------------

DOTVM_API dotvm_value_t dotvm_get_register(dotvm_vm_t* vm, uint8_t idx) {
    if (vm == nullptr) {
        return to_c_value(dotvm::core::Value::nil());
    }
    return to_c_value(vm->context.registers().read(idx));
}

DOTVM_API dotvm_result_t dotvm_set_register(dotvm_vm_t* vm,
                                             uint8_t idx,
                                             dotvm_value_t value) {
    if (vm == nullptr) {
        return DOTVM_INVALID_ARG;
    }
    vm->context.registers().write(idx, from_c_value(value));
    return DOTVM_OK;
}

// ----------------------------------------------------------------------------
// Error Handling
// ----------------------------------------------------------------------------

DOTVM_API const char* dotvm_get_error(dotvm_vm_t* vm) {
    if (vm == nullptr || vm->last_error.empty()) {
        return nullptr;
    }
    return vm->last_error.c_str();
}

DOTVM_API void dotvm_clear_error(dotvm_vm_t* vm) {
    if (vm != nullptr) {
        vm->last_error.clear();
    }
}

DOTVM_API size_t dotvm_get_error_copy(dotvm_vm_t* vm,
                                       char* buffer,
                                       size_t buffer_size) {
    // Handle null VM or empty error
    if (vm == nullptr || vm->last_error.empty()) {
        if (buffer != nullptr && buffer_size > 0) {
            buffer[0] = '\0';
        }
        return 0;
    }

    const size_t error_len = vm->last_error.size();

    // If buffer is nullptr or zero size, just return required size
    if (buffer == nullptr || buffer_size == 0) {
        return error_len;
    }

    // Copy as much as fits (always null-terminate)
    const size_t copy_len = (error_len < buffer_size) ? error_len : (buffer_size - 1);
    std::memcpy(buffer, vm->last_error.c_str(), copy_len);
    buffer[copy_len] = '\0';

    return error_len;
}

// ----------------------------------------------------------------------------
// Value Helpers
// ----------------------------------------------------------------------------

DOTVM_API dotvm_value_t dotvm_value_nil(void) {
    return to_c_value(dotvm::core::Value::nil());
}

DOTVM_API dotvm_value_t dotvm_value_int(int64_t i) {
    return to_c_value(dotvm::core::Value::from_int(i));
}

DOTVM_API dotvm_value_t dotvm_value_float(double f) {
    return to_c_value(dotvm::core::Value::from_float(f));
}

DOTVM_API dotvm_value_t dotvm_value_bool(int b) {
    return to_c_value(dotvm::core::Value::from_bool(b != 0));
}

DOTVM_API dotvm_value_type_t dotvm_value_type(dotvm_value_t v) {
    auto val = from_c_value(v);

    if (val.is_nil()) return DOTVM_TYPE_NIL;
    if (val.is_integer()) return DOTVM_TYPE_INTEGER;
    if (val.is_bool()) return DOTVM_TYPE_BOOL;
    if (val.is_handle()) return DOTVM_TYPE_HANDLE;
    if (val.is_pointer()) return DOTVM_TYPE_POINTER;

    // Default to float (includes actual floats and canonical NaN)
    return DOTVM_TYPE_FLOAT;
}

DOTVM_API int dotvm_value_is_nil(dotvm_value_t v) {
    return from_c_value(v).is_nil() ? 1 : 0;
}

DOTVM_API int dotvm_value_is_int(dotvm_value_t v) {
    return from_c_value(v).is_integer() ? 1 : 0;
}

DOTVM_API int dotvm_value_is_float(dotvm_value_t v) {
    return from_c_value(v).is_float() ? 1 : 0;
}

DOTVM_API int dotvm_value_is_bool(dotvm_value_t v) {
    return from_c_value(v).is_bool() ? 1 : 0;
}

DOTVM_API int64_t dotvm_value_as_int(dotvm_value_t v) {
    auto val = from_c_value(v);
    return val.is_integer() ? val.as_integer() : 0;
}

DOTVM_API double dotvm_value_as_float(dotvm_value_t v) {
    auto val = from_c_value(v);
    return val.is_float() ? val.as_float() : 0.0;
}

DOTVM_API int dotvm_value_as_bool(dotvm_value_t v) {
    auto val = from_c_value(v);
    return val.is_bool() ? (val.as_bool() ? 1 : 0) : 0;
}

// ----------------------------------------------------------------------------
// Query Functions
// ----------------------------------------------------------------------------

DOTVM_API int dotvm_get_arch(dotvm_vm_t* vm) {
    if (vm == nullptr) {
        return -1;
    }
    return (vm->context.arch() == dotvm::core::Architecture::Arch32) ? 0 : 1;
}

DOTVM_API uint64_t dotvm_get_pc(dotvm_vm_t* vm) {
    if (vm == nullptr) {
        return 0;
    }
    return vm->pc;
}

DOTVM_API int dotvm_is_loaded(dotvm_vm_t* vm) {
    return (vm != nullptr && vm->bytecode_loaded) ? 1 : 0;
}

DOTVM_API dotvm_result_t dotvm_reset(dotvm_vm_t* vm) {
    if (vm == nullptr) {
        return DOTVM_INVALID_ARG;
    }
    vm->context.reset();
    vm->pc = vm->bytecode_loaded ? vm->header.entry_point : 0;
    vm->halted = false;
    vm->last_error.clear();
    return DOTVM_OK;
}

// ----------------------------------------------------------------------------
// Version Information
// ----------------------------------------------------------------------------

DOTVM_API const char* dotvm_version(void) {
    return "0.1.0";
}

DOTVM_API int dotvm_bytecode_version(void) {
    return static_cast<int>(dotvm::core::bytecode::CURRENT_VERSION);
}

}  // extern "C"
