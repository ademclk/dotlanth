#include "dotvm/exec/syscall_handler.hpp"

#include <chrono>
#include <cmath>
#include <iostream>
#include <numbers>

#include "dotvm/core/dsl/stdlib/stdlib_types.hpp"

namespace dotvm::exec {

namespace syscall_id = dotvm::core::dsl::stdlib::syscall_id;

// ============================================================================
// Global Dispatcher Instance
// ============================================================================

SyscallDispatcher& syscall_dispatcher() {
    static SyscallDispatcher instance;
    return instance;
}

// ============================================================================
// SyscallDispatcher Implementation
// ============================================================================

SyscallDispatcher::SyscallDispatcher() {
    init_prelude_handlers();
    init_math_handlers();
    init_string_handlers();
    init_collections_handlers();
    init_io_handlers();
    init_crypto_handlers();
    init_net_handlers();
    init_time_handlers();
    init_async_handlers();
    init_control_handlers();
}

SyscallResult SyscallDispatcher::dispatch(std::uint16_t syscall_id, SyscallContext& ctx,
                                          std::uint8_t dest_reg) const {
    auto it = handlers_.find(syscall_id);
    if (it == handlers_.end()) {
        return SyscallResult::InvalidSyscallId;
    }

    return it->second.handler(ctx, dest_reg);
}

bool SyscallDispatcher::has_handler(std::uint16_t syscall_id) const {
    return handlers_.find(syscall_id) != handlers_.end();
}

std::string_view SyscallDispatcher::get_name(std::uint16_t syscall_id) const {
    auto it = handlers_.find(syscall_id);
    if (it != handlers_.end()) {
        return it->second.name;
    }
    return "unknown";
}

void SyscallDispatcher::register_handler(std::uint16_t id, std::string name,
                                         SyscallHandler handler) {
    handlers_[id] = HandlerEntry{.name = std::move(name), .handler = std::move(handler)};
}

// ============================================================================
// Prelude Module Handlers
// ============================================================================

void SyscallDispatcher::init_prelude_handlers() {
    // print(value: any) -> void
    register_handler(syscall_id::PRELUDE_PRINT, "print", [](SyscallContext& ctx, std::uint8_t) {
        auto val = ctx.arg(0);

        // Print based on type
        if (val.is_integer()) {
            std::cout << val.as_integer();
        } else if (val.is_float()) {
            std::cout << val.as_float();
        } else if (val.is_bool()) {
            std::cout << (val.as_bool() ? "true" : "false");
        } else if (val.is_nil()) {
            std::cout << "nil";
        } else {
            std::cout << "<handle:" << val.raw_bits() << ">";
        }
        std::cout << std::endl;

        return SyscallResult::Success;
    });

    // assert(condition: bool) -> void
    register_handler(syscall_id::PRELUDE_ASSERT, "assert", [](SyscallContext& ctx, std::uint8_t) {
        auto condition = ctx.arg(0);
        if (!condition.as_bool()) {
            std::cerr << "Assertion failed!" << std::endl;
            // Could throw or return error
        }
        return SyscallResult::Success;
    });

    // type_of(value: any) -> string
    register_handler(syscall_id::PRELUDE_TYPE_OF, "type_of",
                     [](SyscallContext& ctx, std::uint8_t dest_reg) {
                         auto val = ctx.arg(0);

                         // Return type index as int for now (handle-based string in future)
                         std::int64_t type_id = 0;
                         if (val.is_integer())
                             type_id = 1;
                         else if (val.is_float())
                             type_id = 2;
                         else if (val.is_bool())
                             type_id = 3;
                         else if (val.is_nil())
                             type_id = 0;
                         else
                             type_id = 5;  // Handle

                         ctx.set_return(dest_reg, core::Value::from_int(type_id));
                         return SyscallResult::Success;
                     });

    // len(value: any) -> int
    register_handler(syscall_id::PRELUDE_LEN, "len",
                     [](SyscallContext& ctx, std::uint8_t dest_reg) {
                         // Placeholder - would need string/collection length
                         ctx.set_return(dest_reg, core::Value::from_int(0));
                         return SyscallResult::Success;
                     });

    // str(value: any) -> string
    register_handler(syscall_id::PRELUDE_STR, "str",
                     [](SyscallContext& ctx, std::uint8_t dest_reg) {
                         // Placeholder - would allocate string handle
                         ctx.set_return(dest_reg, core::Value::nil());
                         return SyscallResult::Success;
                     });

    // int(value: any) -> int
    register_handler(syscall_id::PRELUDE_INT, "int",
                     [](SyscallContext& ctx, std::uint8_t dest_reg) {
                         auto val = ctx.arg(0);
                         std::int64_t result = 0;

                         if (val.is_integer()) {
                             result = val.as_integer();
                         } else if (val.is_float()) {
                             result = static_cast<std::int64_t>(val.as_float());
                         } else if (val.is_bool()) {
                             result = val.as_bool() ? 1 : 0;
                         }

                         ctx.set_return(dest_reg, core::Value::from_int(result));
                         return SyscallResult::Success;
                     });

    // float(value: any) -> float
    register_handler(syscall_id::PRELUDE_FLOAT, "float",
                     [](SyscallContext& ctx, std::uint8_t dest_reg) {
                         auto val = ctx.arg(0);
                         double result = 0.0;

                         if (val.is_float()) {
                             result = val.as_float();
                         } else if (val.is_integer()) {
                             result = static_cast<double>(val.as_integer());
                         } else if (val.is_bool()) {
                             result = val.as_bool() ? 1.0 : 0.0;
                         }

                         ctx.set_return(dest_reg, core::Value::from_float(result));
                         return SyscallResult::Success;
                     });

    // Type checking functions
    register_handler(syscall_id::PRELUDE_IS_INT, "is_int",
                     [](SyscallContext& ctx, std::uint8_t dest_reg) {
                         ctx.set_return(dest_reg, core::Value::from_bool(ctx.arg(0).is_integer()));
                         return SyscallResult::Success;
                     });

    register_handler(syscall_id::PRELUDE_IS_FLOAT, "is_float",
                     [](SyscallContext& ctx, std::uint8_t dest_reg) {
                         ctx.set_return(dest_reg, core::Value::from_bool(ctx.arg(0).is_float()));
                         return SyscallResult::Success;
                     });

    register_handler(syscall_id::PRELUDE_IS_BOOL, "is_bool",
                     [](SyscallContext& ctx, std::uint8_t dest_reg) {
                         ctx.set_return(dest_reg, core::Value::from_bool(ctx.arg(0).is_bool()));
                         return SyscallResult::Success;
                     });

    register_handler(
        syscall_id::PRELUDE_IS_STRING, "is_string", [](SyscallContext& ctx, std::uint8_t dest_reg) {
            // Strings are handles with a specific tag - for now treat all handles
            auto val = ctx.arg(0);
            ctx.set_return(dest_reg, core::Value::from_bool(!val.is_integer() && !val.is_float() &&
                                                            !val.is_bool()));
            return SyscallResult::Success;
        });

    register_handler(
        syscall_id::PRELUDE_IS_HANDLE, "is_handle", [](SyscallContext& ctx, std::uint8_t dest_reg) {
            auto val = ctx.arg(0);
            ctx.set_return(dest_reg, core::Value::from_bool(!val.is_integer() && !val.is_float() &&
                                                            !val.is_bool() && !val.is_nil()));
            return SyscallResult::Success;
        });
}

// ============================================================================
// Math Module Handlers
// ============================================================================

void SyscallDispatcher::init_math_handlers() {
    // abs(x: float) -> float
    register_handler(syscall_id::MATH_ABS, "abs", [](SyscallContext& ctx, std::uint8_t dest_reg) {
        auto x = ctx.arg(0).as_float();
        ctx.set_return(dest_reg, core::Value::from_float(std::abs(x)));
        return SyscallResult::Success;
    });

    // min(a: float, b: float) -> float
    register_handler(syscall_id::MATH_MIN, "min", [](SyscallContext& ctx, std::uint8_t dest_reg) {
        auto a = ctx.arg(0).as_float();
        auto b = ctx.arg(1).as_float();
        ctx.set_return(dest_reg, core::Value::from_float(std::min(a, b)));
        return SyscallResult::Success;
    });

    // max(a: float, b: float) -> float
    register_handler(syscall_id::MATH_MAX, "max", [](SyscallContext& ctx, std::uint8_t dest_reg) {
        auto a = ctx.arg(0).as_float();
        auto b = ctx.arg(1).as_float();
        ctx.set_return(dest_reg, core::Value::from_float(std::max(a, b)));
        return SyscallResult::Success;
    });

    // floor(x: float) -> int
    register_handler(
        syscall_id::MATH_FLOOR, "floor", [](SyscallContext& ctx, std::uint8_t dest_reg) {
            auto x = ctx.arg(0).as_float();
            ctx.set_return(dest_reg,
                           core::Value::from_int(static_cast<std::int64_t>(std::floor(x))));
            return SyscallResult::Success;
        });

    // ceil(x: float) -> int
    register_handler(syscall_id::MATH_CEIL, "ceil", [](SyscallContext& ctx, std::uint8_t dest_reg) {
        auto x = ctx.arg(0).as_float();
        ctx.set_return(dest_reg, core::Value::from_int(static_cast<std::int64_t>(std::ceil(x))));
        return SyscallResult::Success;
    });

    // round(x: float) -> int
    register_handler(
        syscall_id::MATH_ROUND, "round", [](SyscallContext& ctx, std::uint8_t dest_reg) {
            auto x = ctx.arg(0).as_float();
            ctx.set_return(dest_reg,
                           core::Value::from_int(static_cast<std::int64_t>(std::round(x))));
            return SyscallResult::Success;
        });

    // sqrt(x: float) -> float
    register_handler(syscall_id::MATH_SQRT, "sqrt", [](SyscallContext& ctx, std::uint8_t dest_reg) {
        auto x = ctx.arg(0).as_float();
        ctx.set_return(dest_reg, core::Value::from_float(std::sqrt(x)));
        return SyscallResult::Success;
    });

    // pow(base: float, exp: float) -> float
    register_handler(syscall_id::MATH_POW, "pow", [](SyscallContext& ctx, std::uint8_t dest_reg) {
        auto base = ctx.arg(0).as_float();
        auto exp = ctx.arg(1).as_float();
        ctx.set_return(dest_reg, core::Value::from_float(std::pow(base, exp)));
        return SyscallResult::Success;
    });

    // Trigonometric functions
    register_handler(syscall_id::MATH_SIN, "sin", [](SyscallContext& ctx, std::uint8_t dest_reg) {
        auto x = ctx.arg(0).as_float();
        ctx.set_return(dest_reg, core::Value::from_float(std::sin(x)));
        return SyscallResult::Success;
    });

    register_handler(syscall_id::MATH_COS, "cos", [](SyscallContext& ctx, std::uint8_t dest_reg) {
        auto x = ctx.arg(0).as_float();
        ctx.set_return(dest_reg, core::Value::from_float(std::cos(x)));
        return SyscallResult::Success;
    });

    register_handler(syscall_id::MATH_TAN, "tan", [](SyscallContext& ctx, std::uint8_t dest_reg) {
        auto x = ctx.arg(0).as_float();
        ctx.set_return(dest_reg, core::Value::from_float(std::tan(x)));
        return SyscallResult::Success;
    });

    register_handler(syscall_id::MATH_ASIN, "asin", [](SyscallContext& ctx, std::uint8_t dest_reg) {
        auto x = ctx.arg(0).as_float();
        ctx.set_return(dest_reg, core::Value::from_float(std::asin(x)));
        return SyscallResult::Success;
    });

    register_handler(syscall_id::MATH_ACOS, "acos", [](SyscallContext& ctx, std::uint8_t dest_reg) {
        auto x = ctx.arg(0).as_float();
        ctx.set_return(dest_reg, core::Value::from_float(std::acos(x)));
        return SyscallResult::Success;
    });

    register_handler(syscall_id::MATH_ATAN, "atan", [](SyscallContext& ctx, std::uint8_t dest_reg) {
        auto x = ctx.arg(0).as_float();
        ctx.set_return(dest_reg, core::Value::from_float(std::atan(x)));
        return SyscallResult::Success;
    });

    register_handler(syscall_id::MATH_ATAN2, "atan2",
                     [](SyscallContext& ctx, std::uint8_t dest_reg) {
                         auto y = ctx.arg(0).as_float();
                         auto x = ctx.arg(1).as_float();
                         ctx.set_return(dest_reg, core::Value::from_float(std::atan2(y, x)));
                         return SyscallResult::Success;
                     });

    // Exponential/logarithmic
    register_handler(syscall_id::MATH_LOG, "log", [](SyscallContext& ctx, std::uint8_t dest_reg) {
        auto x = ctx.arg(0).as_float();
        ctx.set_return(dest_reg, core::Value::from_float(std::log(x)));
        return SyscallResult::Success;
    });

    register_handler(syscall_id::MATH_LOG10, "log10",
                     [](SyscallContext& ctx, std::uint8_t dest_reg) {
                         auto x = ctx.arg(0).as_float();
                         ctx.set_return(dest_reg, core::Value::from_float(std::log10(x)));
                         return SyscallResult::Success;
                     });

    register_handler(syscall_id::MATH_LOG2, "log2", [](SyscallContext& ctx, std::uint8_t dest_reg) {
        auto x = ctx.arg(0).as_float();
        ctx.set_return(dest_reg, core::Value::from_float(std::log2(x)));
        return SyscallResult::Success;
    });

    register_handler(syscall_id::MATH_EXP, "exp", [](SyscallContext& ctx, std::uint8_t dest_reg) {
        auto x = ctx.arg(0).as_float();
        ctx.set_return(dest_reg, core::Value::from_float(std::exp(x)));
        return SyscallResult::Success;
    });
}

// ============================================================================
// String Module Handlers (Stubs)
// ============================================================================

void SyscallDispatcher::init_string_handlers() {
    // String operations require memory allocation - stub for now
    register_handler(syscall_id::STRING_CONCAT, "concat",
                     [](SyscallContext& ctx, std::uint8_t dest_reg) {
                         ctx.set_return(dest_reg, core::Value::nil());
                         return SyscallResult::NotImplemented;
                     });

    register_handler(syscall_id::STRING_LEN, "string_len",
                     [](SyscallContext& ctx, std::uint8_t dest_reg) {
                         ctx.set_return(dest_reg, core::Value::from_int(0));
                         return SyscallResult::NotImplemented;
                     });
}

// ============================================================================
// Collections Module Handlers (Stubs)
// ============================================================================

void SyscallDispatcher::init_collections_handlers() {
    // Collections require heap allocation - stub for now
    register_handler(syscall_id::COLLECTIONS_LIST_NEW, "list_new",
                     [](SyscallContext& ctx, std::uint8_t dest_reg) {
                         ctx.set_return(dest_reg, core::Value::nil());
                         return SyscallResult::NotImplemented;
                     });

    register_handler(syscall_id::COLLECTIONS_MAP_NEW, "map_new",
                     [](SyscallContext& ctx, std::uint8_t dest_reg) {
                         ctx.set_return(dest_reg, core::Value::nil());
                         return SyscallResult::NotImplemented;
                     });

    register_handler(syscall_id::COLLECTIONS_SET_NEW, "set_new",
                     [](SyscallContext& ctx, std::uint8_t dest_reg) {
                         ctx.set_return(dest_reg, core::Value::nil());
                         return SyscallResult::NotImplemented;
                     });
}

// ============================================================================
// IO Module Handlers (Stubs - require Filesystem capability)
// ============================================================================

void SyscallDispatcher::init_io_handlers() {
    register_handler(syscall_id::IO_FILE_READ, "file_read",
                     [](SyscallContext& ctx, std::uint8_t dest_reg) {
                         // Check capability
                         if (!core::capabilities::has_permission(
                                 ctx.granted_caps, core::capabilities::Permission::Filesystem)) {
                             return SyscallResult::PermissionDenied;
                         }
                         ctx.set_return(dest_reg, core::Value::nil());
                         return SyscallResult::NotImplemented;
                     });

    register_handler(syscall_id::IO_FILE_WRITE, "file_write",
                     [](SyscallContext& ctx, std::uint8_t dest_reg) {
                         if (!core::capabilities::has_permission(
                                 ctx.granted_caps, core::capabilities::Permission::Filesystem)) {
                             return SyscallResult::PermissionDenied;
                         }
                         ctx.set_return(dest_reg, core::Value::from_bool(false));
                         return SyscallResult::NotImplemented;
                     });

    register_handler(syscall_id::IO_FILE_EXISTS, "file_exists",
                     [](SyscallContext& ctx, std::uint8_t dest_reg) {
                         if (!core::capabilities::has_permission(
                                 ctx.granted_caps, core::capabilities::Permission::Filesystem)) {
                             return SyscallResult::PermissionDenied;
                         }
                         ctx.set_return(dest_reg, core::Value::from_bool(false));
                         return SyscallResult::NotImplemented;
                     });
}

// ============================================================================
// Crypto Module Handlers (Stubs - require Crypto capability)
// ============================================================================

void SyscallDispatcher::init_crypto_handlers() {
    register_handler(syscall_id::CRYPTO_HASH_BLAKE3, "hash_blake3",
                     [](SyscallContext& ctx, std::uint8_t dest_reg) {
                         if (!core::capabilities::has_permission(
                                 ctx.granted_caps, core::capabilities::Permission::Crypto)) {
                             return SyscallResult::PermissionDenied;
                         }
                         ctx.set_return(dest_reg, core::Value::nil());
                         return SyscallResult::NotImplemented;
                     });

    register_handler(syscall_id::CRYPTO_HASH_SHA256, "hash_sha256",
                     [](SyscallContext& ctx, std::uint8_t dest_reg) {
                         if (!core::capabilities::has_permission(
                                 ctx.granted_caps, core::capabilities::Permission::Crypto)) {
                             return SyscallResult::PermissionDenied;
                         }
                         ctx.set_return(dest_reg, core::Value::nil());
                         return SyscallResult::NotImplemented;
                     });
}

// ============================================================================
// Network Module Handlers (Stubs - require Network capability)
// ============================================================================

void SyscallDispatcher::init_net_handlers() {
    register_handler(syscall_id::NET_HTTP_GET, "http_get",
                     [](SyscallContext& ctx, std::uint8_t dest_reg) {
                         if (!core::capabilities::has_permission(
                                 ctx.granted_caps, core::capabilities::Permission::Network)) {
                             return SyscallResult::PermissionDenied;
                         }
                         ctx.set_return(dest_reg, core::Value::nil());
                         return SyscallResult::NotImplemented;
                     });

    register_handler(syscall_id::NET_HTTP_POST, "http_post",
                     [](SyscallContext& ctx, std::uint8_t dest_reg) {
                         if (!core::capabilities::has_permission(
                                 ctx.granted_caps, core::capabilities::Permission::Network)) {
                             return SyscallResult::PermissionDenied;
                         }
                         ctx.set_return(dest_reg, core::Value::nil());
                         return SyscallResult::NotImplemented;
                     });
}

// ============================================================================
// Time Module Handlers
// ============================================================================

void SyscallDispatcher::init_time_handlers() {
    // now() -> int (milliseconds since epoch)
    register_handler(syscall_id::TIME_NOW, "now", [](SyscallContext& ctx, std::uint8_t dest_reg) {
        auto now = std::chrono::system_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch());
        ctx.set_return(dest_reg, core::Value::from_int(ms.count()));
        return SyscallResult::Success;
    });

    // timestamp() -> float (seconds since epoch)
    register_handler(syscall_id::TIME_TIMESTAMP, "timestamp",
                     [](SyscallContext& ctx, std::uint8_t dest_reg) {
                         auto now = std::chrono::system_clock::now();
                         auto duration = now.time_since_epoch();
                         auto secs = std::chrono::duration<double>(duration).count();
                         ctx.set_return(dest_reg, core::Value::from_float(secs));
                         return SyscallResult::Success;
                     });

    // sleep(ms: int) -> void
    register_handler(syscall_id::TIME_SLEEP, "sleep", [](SyscallContext& ctx, std::uint8_t) {
        // Note: sleep is impure and affects execution time
        // In a sandboxed environment, this should be limited
        return SyscallResult::NotImplemented;  // Disable for safety
    });
}

// ============================================================================
// Async Module Handlers (Stubs)
// ============================================================================

void SyscallDispatcher::init_async_handlers() {
    register_handler(syscall_id::ASYNC_SPAWN, "spawn",
                     [](SyscallContext& ctx, std::uint8_t dest_reg) {
                         ctx.set_return(dest_reg, core::Value::nil());
                         return SyscallResult::NotImplemented;
                     });

    register_handler(syscall_id::ASYNC_CHANNEL_NEW, "channel_new",
                     [](SyscallContext& ctx, std::uint8_t dest_reg) {
                         ctx.set_return(dest_reg, core::Value::nil());
                         return SyscallResult::NotImplemented;
                     });
}

// ============================================================================
// Control Module Handlers (Stubs)
// ============================================================================

void SyscallDispatcher::init_control_handlers() {
    // Control flow is typically handled at compile time
    // These are fallbacks for dynamic usage
    register_handler(syscall_id::CONTROL_FOREACH, "foreach", [](SyscallContext& ctx, std::uint8_t) {
        return SyscallResult::NotImplemented;
    });

    register_handler(
        syscall_id::CONTROL_WHILE, "while_loop",
        [](SyscallContext& ctx, std::uint8_t) { return SyscallResult::NotImplemented; });
}

}  // namespace dotvm::exec
