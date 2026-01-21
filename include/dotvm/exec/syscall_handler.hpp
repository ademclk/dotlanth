#pragma once

/// @file syscall_handler.hpp
/// @brief DSL-004 SYSCALL dispatch handler for stdlib functions
///
/// Dispatches SYSCALL instructions to appropriate stdlib function implementations.
/// Each syscall ID maps to a specific stdlib function with its own implementation.

#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <unordered_map>

#include "dotvm/core/capabilities/capability.hpp"
#include "dotvm/core/register_file.hpp"
#include "dotvm/core/value.hpp"
#include "dotvm/core/vm_context.hpp"

namespace dotvm::exec {

/// @brief Result of a syscall execution
enum class SyscallResult : std::uint8_t {
    Success = 0,       ///< Syscall completed successfully
    InvalidSyscallId,  ///< Unknown syscall ID
    PermissionDenied,  ///< Missing required capability
    InvalidArgument,   ///< Invalid argument value
    AllocationFailed,  ///< Memory allocation failed
    IoError,           ///< I/O operation failed
    NotImplemented,    ///< Syscall not yet implemented
};

/// @brief Context passed to syscall handlers
struct SyscallContext {
    core::VmContext& vm_ctx;                      ///< Full VM context
    core::ArchRegisterFile& regs;                 ///< Register file for args/return
    core::capabilities::Permission granted_caps;  ///< Capabilities granted at compile time
    std::span<const core::Value> const_pool;      ///< Constant pool

    /// Read argument from register (R1-R6 calling convention)
    [[nodiscard]] core::Value arg(std::size_t index) const {
        return regs.read(static_cast<std::uint8_t>(1 + index));
    }

    /// Set return value in destination register
    void set_return(std::uint8_t dest_reg, core::Value val) { regs.write(dest_reg, val); }
};

/// @brief Type for syscall handler functions
using SyscallHandler = std::function<SyscallResult(SyscallContext&, std::uint8_t dest_reg)>;

/// @brief Central syscall dispatcher for stdlib functions
///
/// Manages the mapping from syscall IDs to handler functions and dispatches
/// incoming SYSCALL instructions to the appropriate handler.
class SyscallDispatcher {
public:
    SyscallDispatcher();

    /// @brief Dispatch a syscall
    ///
    /// @param syscall_id The 16-bit syscall identifier
    /// @param ctx Syscall context with VM state
    /// @param dest_reg Destination register for return value
    /// @return Result of the syscall execution
    [[nodiscard]] SyscallResult dispatch(std::uint16_t syscall_id, SyscallContext& ctx,
                                         std::uint8_t dest_reg) const;

    /// @brief Check if a syscall ID is registered
    [[nodiscard]] bool has_handler(std::uint16_t syscall_id) const;

    /// @brief Get the name of a syscall (for debugging)
    [[nodiscard]] std::string_view get_name(std::uint16_t syscall_id) const;

private:
    /// Initialize all syscall handlers
    void init_prelude_handlers();
    void init_math_handlers();
    void init_string_handlers();
    void init_collections_handlers();
    void init_io_handlers();
    void init_crypto_handlers();
    void init_net_handlers();
    void init_time_handlers();
    void init_async_handlers();
    void init_control_handlers();

    /// Register a handler
    void register_handler(std::uint16_t id, std::string name, SyscallHandler handler);

    /// Handler storage
    struct HandlerEntry {
        std::string name;
        SyscallHandler handler;
    };
    std::unordered_map<std::uint16_t, HandlerEntry> handlers_;
};

/// @brief Global syscall dispatcher instance
[[nodiscard]] SyscallDispatcher& syscall_dispatcher();

}  // namespace dotvm::exec
