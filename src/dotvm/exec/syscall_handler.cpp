#include "dotvm/exec/syscall_handler.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <numbers>
#include <random>
#include <sstream>

#include "dotvm/core/collections/registry.hpp"
#include "dotvm/core/crypto/aes256gcm.hpp"
#include "dotvm/core/crypto/blake3.hpp"
#include "dotvm/core/crypto/ed25519.hpp"
#include "dotvm/core/crypto/sha256.hpp"
#include "dotvm/core/dsl/stdlib/stdlib_types.hpp"
#include "dotvm/core/io/filesystem_sandbox.hpp"
#include "dotvm/core/string_pool.hpp"

namespace dotvm::exec {

namespace syscall_id = dotvm::core::dsl::stdlib::syscall_id;

// ============================================================================
// SyscallContext Helper Methods
// ============================================================================

std::string_view SyscallContext::get_string_arg(std::size_t index) const {
    if (strings == nullptr) {
        return {};
    }

    auto val = arg(index);
    if (!core::is_string_value(val)) {
        return {};
    }

    auto handle = core::decode_string_handle(val);
    if (handle.is_null()) {
        return {};
    }

    // For heap strings, get from pool
    if (!handle.is_sso()) {
        auto result = strings->get(handle);
        if (result) {
            return *result;
        }
    }

    // SSO strings would need separate handling
    return {};
}

SyscallResult SyscallContext::return_string(std::uint8_t dest_reg, std::string_view str) {
    if (strings == nullptr) {
        set_return(dest_reg, core::Value::nil());
        return SyscallResult::AllocationFailed;
    }

    auto result = strings->create(str);
    if (!result) {
        set_return(dest_reg, core::Value::nil());
        return SyscallResult::AllocationFailed;
    }

    set_return(dest_reg, core::encode_string_value(*result));
    return SyscallResult::Success;
}

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
// String Module Handlers (GROUP-C)
// ============================================================================

namespace {

/// Helper to get string from a Value, using memory manager for heap strings
std::string get_string_from_value(SyscallContext& ctx, core::Value val) {
    if (val.is_handle()) {
        // Check if it's a string handle
        if (core::is_string_value(val)) {
            auto handle = core::decode_string_handle(val);
            if (!handle.is_null() && !handle.is_sso() && ctx.strings != nullptr) {
                auto result = ctx.strings->get(handle);
                if (result) {
                    return std::string(*result);
                }
            }
        }
        // Fallback: try to read from memory as raw bytes
        auto h = val.as_handle();
        auto size_result = ctx.vm_ctx.memory().get_size(h);
        if (size_result) {
            std::string str(*size_result, '\0');
            auto err = ctx.vm_ctx.memory().read_bytes(h, 0, str.data(), str.size());
            if (err == core::MemoryError::Success) {
                // Trim null terminator if present
                auto pos = str.find('\0');
                if (pos != std::string::npos) {
                    str.resize(pos);
                }
                return str;
            }
        }
    }
    return {};
}

/// Helper to create a string and return its handle
core::Value create_string_value(SyscallContext& ctx, std::string_view str) {
    if (ctx.strings != nullptr) {
        auto result = ctx.strings->create(str);
        if (result) {
            return core::encode_string_value(*result);
        }
    }

    // Fallback: allocate in memory manager
    auto alloc_result = ctx.vm_ctx.memory().allocate(str.size() + 1);
    if (!alloc_result) {
        return core::Value::nil();
    }

    auto h = *alloc_result;
    auto err = ctx.vm_ctx.memory().write_bytes(h, 0, str.data(), str.size());
    if (err != core::MemoryError::Success) {
        (void)ctx.vm_ctx.memory().deallocate(h);
        return core::Value::nil();
    }

    // Write null terminator
    char null_char = '\0';
    (void)ctx.vm_ctx.memory().write_bytes(h, str.size(), &null_char, 1);

    return core::Value::from_handle(h);
}

}  // namespace

void SyscallDispatcher::init_string_handlers() {
    // concat(a: string, b: string) -> string
    register_handler(syscall_id::STRING_CONCAT, "concat",
                     [](SyscallContext& ctx, std::uint8_t dest_reg) {
                         auto a = get_string_from_value(ctx, ctx.arg(0));
                         auto b = get_string_from_value(ctx, ctx.arg(1));
                         std::string result = a + b;
                         ctx.set_return(dest_reg, create_string_value(ctx, result));
                         return SyscallResult::Success;
                     });

    // len(s: string) -> int
    register_handler(
        syscall_id::STRING_LEN, "string_len", [](SyscallContext& ctx, std::uint8_t dest_reg) {
            auto s = get_string_from_value(ctx, ctx.arg(0));
            ctx.set_return(dest_reg, core::Value::from_int(static_cast<std::int64_t>(s.size())));
            return SyscallResult::Success;
        });

    // split(s: string, delim: string) -> list (returns handle to list)
    register_handler(
        syscall_id::STRING_SPLIT, "split", [](SyscallContext& ctx, std::uint8_t dest_reg) {
            auto* registry = ctx.collections;
            if (registry == nullptr) {
                ctx.set_return(dest_reg, core::Value::nil());
                return SyscallResult::InvalidArgument;
            }

            auto s = get_string_from_value(ctx, ctx.arg(0));
            auto delim = get_string_from_value(ctx, ctx.arg(1));

            auto list_handle = registry->create_list();
            if (!list_handle) {
                ctx.set_return(dest_reg, core::Value::nil());
                return SyscallResult::AllocationFailed;
            }

            auto list = registry->get_list(*list_handle);
            if (!list) {
                ctx.set_return(dest_reg, core::Value::nil());
                return SyscallResult::AllocationFailed;
            }

            if (delim.empty()) {
                // Split by character
                for (char c : s) {
                    (*list)->push(create_string_value(ctx, std::string_view(&c, 1)));
                }
            } else {
                std::size_t start = 0;
                std::size_t pos;
                while ((pos = s.find(delim, start)) != std::string::npos) {
                    (*list)->push(create_string_value(ctx, s.substr(start, pos - start)));
                    start = pos + delim.size();
                }
                (*list)->push(create_string_value(ctx, s.substr(start)));
            }

            ctx.set_return(dest_reg, core::collections::encode_collection_value(*list_handle));
            return SyscallResult::Success;
        });

    // join(list: handle, sep: string) -> string
    register_handler(syscall_id::STRING_JOIN, "join",
                     [](SyscallContext& ctx, std::uint8_t dest_reg) {
                         auto* registry = ctx.collections;
                         if (registry == nullptr) {
                             ctx.set_return(dest_reg, core::Value::nil());
                             return SyscallResult::InvalidArgument;
                         }

                         auto list_handle = core::collections::decode_collection_handle(ctx.arg(0));
                         if (list_handle.is_null() ||
                             list_handle.type != core::collections::CollectionType::List) {
                             ctx.set_return(dest_reg, core::Value::nil());
                             return SyscallResult::InvalidArgument;
                         }

                         auto sep = get_string_from_value(ctx, ctx.arg(1));
                         auto list = registry->get_list(list_handle);
                         if (!list) {
                             ctx.set_return(dest_reg, core::Value::nil());
                             return SyscallResult::InvalidArgument;
                         }

                         std::string result;
                         bool first = true;
                         for (const auto& val : **list) {
                             if (!first)
                                 result += sep;
                             first = false;
                             result += get_string_from_value(ctx, val);
                         }

                         ctx.set_return(dest_reg, create_string_value(ctx, result));
                         return SyscallResult::Success;
                     });

    // trim(s: string) -> string
    register_handler(syscall_id::STRING_TRIM, "trim",
                     [](SyscallContext& ctx, std::uint8_t dest_reg) {
                         auto s = get_string_from_value(ctx, ctx.arg(0));

                         // Trim leading whitespace
                         auto start = s.find_first_not_of(" \t\n\r\f\v");
                         if (start == std::string::npos) {
                             ctx.set_return(dest_reg, create_string_value(ctx, ""));
                             return SyscallResult::Success;
                         }

                         // Trim trailing whitespace
                         auto end = s.find_last_not_of(" \t\n\r\f\v");
                         std::string result = s.substr(start, end - start + 1);

                         ctx.set_return(dest_reg, create_string_value(ctx, result));
                         return SyscallResult::Success;
                     });

    // upper(s: string) -> string
    register_handler(syscall_id::STRING_UPPER, "upper",
                     [](SyscallContext& ctx, std::uint8_t dest_reg) {
                         auto s = get_string_from_value(ctx, ctx.arg(0));
                         std::transform(s.begin(), s.end(), s.begin(),
                                        [](unsigned char c) { return std::toupper(c); });
                         ctx.set_return(dest_reg, create_string_value(ctx, s));
                         return SyscallResult::Success;
                     });

    // lower(s: string) -> string
    register_handler(syscall_id::STRING_LOWER, "lower",
                     [](SyscallContext& ctx, std::uint8_t dest_reg) {
                         auto s = get_string_from_value(ctx, ctx.arg(0));
                         std::transform(s.begin(), s.end(), s.begin(),
                                        [](unsigned char c) { return std::tolower(c); });
                         ctx.set_return(dest_reg, create_string_value(ctx, s));
                         return SyscallResult::Success;
                     });

    // starts_with(s: string, prefix: string) -> bool
    register_handler(syscall_id::STRING_STARTS_WITH, "starts_with",
                     [](SyscallContext& ctx, std::uint8_t dest_reg) {
                         auto s = get_string_from_value(ctx, ctx.arg(0));
                         auto prefix = get_string_from_value(ctx, ctx.arg(1));

                         bool result =
                             s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
                         ctx.set_return(dest_reg, core::Value::from_bool(result));
                         return SyscallResult::Success;
                     });

    // ends_with(s: string, suffix: string) -> bool
    register_handler(
        syscall_id::STRING_ENDS_WITH, "ends_with", [](SyscallContext& ctx, std::uint8_t dest_reg) {
            auto s = get_string_from_value(ctx, ctx.arg(0));
            auto suffix = get_string_from_value(ctx, ctx.arg(1));

            bool result = s.size() >= suffix.size() &&
                          s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
            ctx.set_return(dest_reg, core::Value::from_bool(result));
            return SyscallResult::Success;
        });

    // contains(s: string, substr: string) -> bool
    register_handler(syscall_id::STRING_CONTAINS, "contains",
                     [](SyscallContext& ctx, std::uint8_t dest_reg) {
                         auto s = get_string_from_value(ctx, ctx.arg(0));
                         auto substr = get_string_from_value(ctx, ctx.arg(1));

                         bool result = s.find(substr) != std::string::npos;
                         ctx.set_return(dest_reg, core::Value::from_bool(result));
                         return SyscallResult::Success;
                     });

    // replace(s: string, from: string, to: string) -> string
    register_handler(syscall_id::STRING_REPLACE, "replace",
                     [](SyscallContext& ctx, std::uint8_t dest_reg) {
                         auto s = get_string_from_value(ctx, ctx.arg(0));
                         auto from = get_string_from_value(ctx, ctx.arg(1));
                         auto to = get_string_from_value(ctx, ctx.arg(2));

                         if (from.empty()) {
                             ctx.set_return(dest_reg, create_string_value(ctx, s));
                             return SyscallResult::Success;
                         }

                         std::string result;
                         result.reserve(s.size());

                         std::size_t pos = 0;
                         std::size_t prev = 0;
                         while ((pos = s.find(from, prev)) != std::string::npos) {
                             result.append(s, prev, pos - prev);
                             result.append(to);
                             prev = pos + from.size();
                         }
                         result.append(s, prev, s.size() - prev);

                         ctx.set_return(dest_reg, create_string_value(ctx, result));
                         return SyscallResult::Success;
                     });

    // substr(s: string, start: int, len: int) -> string
    register_handler(
        syscall_id::STRING_SUBSTR, "substr", [](SyscallContext& ctx, std::uint8_t dest_reg) {
            auto s = get_string_from_value(ctx, ctx.arg(0));
            auto start = ctx.arg(1).as_integer();
            auto len = ctx.arg(2).as_integer();

            if (start < 0 || static_cast<std::size_t>(start) >= s.size()) {
                ctx.set_return(dest_reg, create_string_value(ctx, ""));
                return SyscallResult::Success;
            }

            auto actual_start = static_cast<std::size_t>(start);
            auto actual_len = len < 0 ? std::string::npos : static_cast<std::size_t>(len);

            std::string result = s.substr(actual_start, actual_len);
            ctx.set_return(dest_reg, create_string_value(ctx, result));
            return SyscallResult::Success;
        });

    // char_at(s: string, idx: int) -> string (single character)
    register_handler(syscall_id::STRING_CHAR_AT, "char_at",
                     [](SyscallContext& ctx, std::uint8_t dest_reg) {
                         auto s = get_string_from_value(ctx, ctx.arg(0));
                         auto idx = ctx.arg(1).as_integer();

                         if (idx < 0 || static_cast<std::size_t>(idx) >= s.size()) {
                             ctx.set_return(dest_reg, create_string_value(ctx, ""));
                             return SyscallResult::Success;
                         }

                         std::string result(1, s[static_cast<std::size_t>(idx)]);
                         ctx.set_return(dest_reg, create_string_value(ctx, result));
                         return SyscallResult::Success;
                     });
}

// ============================================================================
// Collections Module Handlers (GROUP-C)
// ============================================================================

namespace {

/// Helper to get collection registry from context
core::collections::CollectionRegistry* get_collections(SyscallContext& ctx) {
    return ctx.collections;
}

/// Helper to decode a collection handle from a value
core::collections::CollectionHandle get_collection_handle(core::Value val) {
    return core::collections::decode_collection_handle(val);
}

}  // namespace

void SyscallDispatcher::init_collections_handlers() {
    // ===== List Operations =====

    // list_new() -> handle
    register_handler(syscall_id::COLLECTIONS_LIST_NEW, "list_new",
                     [](SyscallContext& ctx, std::uint8_t dest_reg) {
                         auto* registry = get_collections(ctx);
                         if (registry == nullptr) {
                             ctx.set_return(dest_reg, core::Value::nil());
                             return SyscallResult::AllocationFailed;
                         }

                         auto result = registry->create_list();
                         if (!result) {
                             ctx.set_return(dest_reg, core::Value::nil());
                             return SyscallResult::AllocationFailed;
                         }

                         ctx.set_return(dest_reg,
                                        core::collections::encode_collection_value(*result));
                         return SyscallResult::Success;
                     });

    // list_push(list: handle, val: any) -> void
    register_handler(syscall_id::COLLECTIONS_LIST_PUSH, "list_push",
                     [](SyscallContext& ctx, std::uint8_t dest_reg) {
                         auto* registry = get_collections(ctx);
                         if (registry == nullptr) {
                             ctx.set_return(dest_reg, core::Value::nil());
                             return SyscallResult::InvalidArgument;
                         }

                         auto handle = get_collection_handle(ctx.arg(0));
                         auto val = ctx.arg(1);

                         auto list_result = registry->get_list(handle);
                         if (!list_result) {
                             ctx.set_return(dest_reg, core::Value::nil());
                             return SyscallResult::InvalidArgument;
                         }

                         (*list_result)->push(val);
                         ctx.set_return(dest_reg, core::Value::nil());
                         return SyscallResult::Success;
                     });

    // list_pop(list: handle) -> any
    register_handler(syscall_id::COLLECTIONS_LIST_POP, "list_pop",
                     [](SyscallContext& ctx, std::uint8_t dest_reg) {
                         auto* registry = get_collections(ctx);
                         if (registry == nullptr) {
                             ctx.set_return(dest_reg, core::Value::nil());
                             return SyscallResult::InvalidArgument;
                         }

                         auto handle = get_collection_handle(ctx.arg(0));
                         auto list_result = registry->get_list(handle);
                         if (!list_result) {
                             ctx.set_return(dest_reg, core::Value::nil());
                             return SyscallResult::InvalidArgument;
                         }

                         ctx.set_return(dest_reg, (*list_result)->pop());
                         return SyscallResult::Success;
                     });

    // list_get(list: handle, index: int) -> any
    register_handler(syscall_id::COLLECTIONS_LIST_GET, "list_get",
                     [](SyscallContext& ctx, std::uint8_t dest_reg) {
                         auto* registry = get_collections(ctx);
                         if (registry == nullptr) {
                             ctx.set_return(dest_reg, core::Value::nil());
                             return SyscallResult::InvalidArgument;
                         }

                         auto handle = get_collection_handle(ctx.arg(0));
                         auto index = ctx.arg(1).as_integer();

                         auto list_result = registry->get_list(handle);
                         if (!list_result) {
                             ctx.set_return(dest_reg, core::Value::nil());
                             return SyscallResult::InvalidArgument;
                         }

                         if (index < 0) {
                             ctx.set_return(dest_reg, core::Value::nil());
                             return SyscallResult::Success;
                         }

                         ctx.set_return(dest_reg,
                                        (*list_result)->get(static_cast<std::size_t>(index)));
                         return SyscallResult::Success;
                     });

    // list_set(list: handle, index: int, val: any) -> bool
    register_handler(syscall_id::COLLECTIONS_LIST_SET, "list_set",
                     [](SyscallContext& ctx, std::uint8_t dest_reg) {
                         auto* registry = get_collections(ctx);
                         if (registry == nullptr) {
                             ctx.set_return(dest_reg, core::Value::from_bool(false));
                             return SyscallResult::InvalidArgument;
                         }

                         auto handle = get_collection_handle(ctx.arg(0));
                         auto index = ctx.arg(1).as_integer();
                         auto val = ctx.arg(2);

                         auto list_result = registry->get_list(handle);
                         if (!list_result) {
                             ctx.set_return(dest_reg, core::Value::from_bool(false));
                             return SyscallResult::InvalidArgument;
                         }

                         if (index < 0) {
                             ctx.set_return(dest_reg, core::Value::from_bool(false));
                             return SyscallResult::Success;
                         }

                         bool success = (*list_result)->set(static_cast<std::size_t>(index), val);
                         ctx.set_return(dest_reg, core::Value::from_bool(success));
                         return SyscallResult::Success;
                     });

    // list_len(list: handle) -> int
    register_handler(syscall_id::COLLECTIONS_LIST_LEN, "list_len",
                     [](SyscallContext& ctx, std::uint8_t dest_reg) {
                         auto* registry = get_collections(ctx);
                         if (registry == nullptr) {
                             ctx.set_return(dest_reg, core::Value::from_int(0));
                             return SyscallResult::InvalidArgument;
                         }

                         auto handle = get_collection_handle(ctx.arg(0));
                         auto list_result = registry->get_list(handle);
                         if (!list_result) {
                             ctx.set_return(dest_reg, core::Value::from_int(0));
                             return SyscallResult::InvalidArgument;
                         }

                         ctx.set_return(dest_reg, core::Value::from_int(static_cast<std::int64_t>(
                                                      (*list_result)->size())));
                         return SyscallResult::Success;
                     });

    // list_clear(list: handle) -> void
    register_handler(syscall_id::COLLECTIONS_LIST_CLEAR, "list_clear",
                     [](SyscallContext& ctx, std::uint8_t dest_reg) {
                         auto* registry = get_collections(ctx);
                         if (registry == nullptr) {
                             ctx.set_return(dest_reg, core::Value::nil());
                             return SyscallResult::InvalidArgument;
                         }

                         auto handle = get_collection_handle(ctx.arg(0));
                         auto list_result = registry->get_list(handle);
                         if (!list_result) {
                             ctx.set_return(dest_reg, core::Value::nil());
                             return SyscallResult::InvalidArgument;
                         }

                         (*list_result)->clear();
                         ctx.set_return(dest_reg, core::Value::nil());
                         return SyscallResult::Success;
                     });

    // ===== Map Operations =====

    // map_new() -> handle
    register_handler(
        syscall_id::COLLECTIONS_MAP_NEW, "map_new", [](SyscallContext& ctx, std::uint8_t dest_reg) {
            auto* registry = get_collections(ctx);
            if (registry == nullptr) {
                ctx.set_return(dest_reg, core::Value::nil());
                return SyscallResult::AllocationFailed;
            }

            auto result = registry->create_map();
            if (!result) {
                ctx.set_return(dest_reg, core::Value::nil());
                return SyscallResult::AllocationFailed;
            }

            ctx.set_return(dest_reg, core::collections::encode_collection_value(*result));
            return SyscallResult::Success;
        });

    // map_get(map: handle, key: any) -> any
    register_handler(syscall_id::COLLECTIONS_MAP_GET, "map_get",
                     [](SyscallContext& ctx, std::uint8_t dest_reg) {
                         auto* registry = get_collections(ctx);
                         if (registry == nullptr) {
                             ctx.set_return(dest_reg, core::Value::nil());
                             return SyscallResult::InvalidArgument;
                         }

                         auto handle = get_collection_handle(ctx.arg(0));
                         auto key = ctx.arg(1);

                         auto map_result = registry->get_map(handle);
                         if (!map_result) {
                             ctx.set_return(dest_reg, core::Value::nil());
                             return SyscallResult::InvalidArgument;
                         }

                         ctx.set_return(dest_reg, (*map_result)->get(key));
                         return SyscallResult::Success;
                     });

    // map_set(map: handle, key: any, val: any) -> void
    register_handler(syscall_id::COLLECTIONS_MAP_SET, "map_set",
                     [](SyscallContext& ctx, std::uint8_t dest_reg) {
                         auto* registry = get_collections(ctx);
                         if (registry == nullptr) {
                             ctx.set_return(dest_reg, core::Value::nil());
                             return SyscallResult::InvalidArgument;
                         }

                         auto handle = get_collection_handle(ctx.arg(0));
                         auto key = ctx.arg(1);
                         auto val = ctx.arg(2);

                         auto map_result = registry->get_map(handle);
                         if (!map_result) {
                             ctx.set_return(dest_reg, core::Value::nil());
                             return SyscallResult::InvalidArgument;
                         }

                         (*map_result)->set(key, val);
                         ctx.set_return(dest_reg, core::Value::nil());
                         return SyscallResult::Success;
                     });

    // map_has(map: handle, key: any) -> bool
    register_handler(syscall_id::COLLECTIONS_MAP_HAS, "map_has",
                     [](SyscallContext& ctx, std::uint8_t dest_reg) {
                         auto* registry = get_collections(ctx);
                         if (registry == nullptr) {
                             ctx.set_return(dest_reg, core::Value::from_bool(false));
                             return SyscallResult::InvalidArgument;
                         }

                         auto handle = get_collection_handle(ctx.arg(0));
                         auto key = ctx.arg(1);

                         auto map_result = registry->get_map(handle);
                         if (!map_result) {
                             ctx.set_return(dest_reg, core::Value::from_bool(false));
                             return SyscallResult::InvalidArgument;
                         }

                         ctx.set_return(dest_reg, core::Value::from_bool((*map_result)->has(key)));
                         return SyscallResult::Success;
                     });

    // map_delete(map: handle, key: any) -> bool
    register_handler(syscall_id::COLLECTIONS_MAP_DELETE, "map_delete",
                     [](SyscallContext& ctx, std::uint8_t dest_reg) {
                         auto* registry = get_collections(ctx);
                         if (registry == nullptr) {
                             ctx.set_return(dest_reg, core::Value::from_bool(false));
                             return SyscallResult::InvalidArgument;
                         }

                         auto handle = get_collection_handle(ctx.arg(0));
                         auto key = ctx.arg(1);

                         auto map_result = registry->get_map(handle);
                         if (!map_result) {
                             ctx.set_return(dest_reg, core::Value::from_bool(false));
                             return SyscallResult::InvalidArgument;
                         }

                         bool removed = (*map_result)->remove(key);
                         ctx.set_return(dest_reg, core::Value::from_bool(removed));
                         return SyscallResult::Success;
                     });

    // map_keys(map: handle) -> list_handle
    register_handler(syscall_id::COLLECTIONS_MAP_KEYS, "map_keys",
                     [](SyscallContext& ctx, std::uint8_t dest_reg) {
                         auto* registry = get_collections(ctx);
                         if (registry == nullptr) {
                             ctx.set_return(dest_reg, core::Value::nil());
                             return SyscallResult::InvalidArgument;
                         }

                         auto handle = get_collection_handle(ctx.arg(0));
                         auto map_result = registry->get_map(handle);
                         if (!map_result) {
                             ctx.set_return(dest_reg, core::Value::nil());
                             return SyscallResult::InvalidArgument;
                         }

                         // Create a list with the keys
                         auto list_handle = registry->create_list();
                         if (!list_handle) {
                             ctx.set_return(dest_reg, core::Value::nil());
                             return SyscallResult::AllocationFailed;
                         }

                         auto list = registry->get_list(*list_handle);
                         if (!list) {
                             ctx.set_return(dest_reg, core::Value::nil());
                             return SyscallResult::AllocationFailed;
                         }

                         for (const auto& [k, v] : **map_result) {
                             (*list)->push(k);
                         }

                         ctx.set_return(dest_reg,
                                        core::collections::encode_collection_value(*list_handle));
                         return SyscallResult::Success;
                     });

    // map_values(map: handle) -> list_handle
    register_handler(syscall_id::COLLECTIONS_MAP_VALUES, "map_values",
                     [](SyscallContext& ctx, std::uint8_t dest_reg) {
                         auto* registry = get_collections(ctx);
                         if (registry == nullptr) {
                             ctx.set_return(dest_reg, core::Value::nil());
                             return SyscallResult::InvalidArgument;
                         }

                         auto handle = get_collection_handle(ctx.arg(0));
                         auto map_result = registry->get_map(handle);
                         if (!map_result) {
                             ctx.set_return(dest_reg, core::Value::nil());
                             return SyscallResult::InvalidArgument;
                         }

                         // Create a list with the values
                         auto list_handle = registry->create_list();
                         if (!list_handle) {
                             ctx.set_return(dest_reg, core::Value::nil());
                             return SyscallResult::AllocationFailed;
                         }

                         auto list = registry->get_list(*list_handle);
                         if (!list) {
                             ctx.set_return(dest_reg, core::Value::nil());
                             return SyscallResult::AllocationFailed;
                         }

                         for (const auto& [k, v] : **map_result) {
                             (*list)->push(v);
                         }

                         ctx.set_return(dest_reg,
                                        core::collections::encode_collection_value(*list_handle));
                         return SyscallResult::Success;
                     });

    // ===== Set Operations =====

    // set_new() -> handle
    register_handler(
        syscall_id::COLLECTIONS_SET_NEW, "set_new", [](SyscallContext& ctx, std::uint8_t dest_reg) {
            auto* registry = get_collections(ctx);
            if (registry == nullptr) {
                ctx.set_return(dest_reg, core::Value::nil());
                return SyscallResult::AllocationFailed;
            }

            auto result = registry->create_set();
            if (!result) {
                ctx.set_return(dest_reg, core::Value::nil());
                return SyscallResult::AllocationFailed;
            }

            ctx.set_return(dest_reg, core::collections::encode_collection_value(*result));
            return SyscallResult::Success;
        });

    // set_add(set: handle, val: any) -> bool (true if added, false if already present)
    register_handler(syscall_id::COLLECTIONS_SET_ADD, "set_add",
                     [](SyscallContext& ctx, std::uint8_t dest_reg) {
                         auto* registry = get_collections(ctx);
                         if (registry == nullptr) {
                             ctx.set_return(dest_reg, core::Value::from_bool(false));
                             return SyscallResult::InvalidArgument;
                         }

                         auto handle = get_collection_handle(ctx.arg(0));
                         auto val = ctx.arg(1);

                         auto set_result = registry->get_set(handle);
                         if (!set_result) {
                             ctx.set_return(dest_reg, core::Value::from_bool(false));
                             return SyscallResult::InvalidArgument;
                         }

                         bool added = (*set_result)->add(val);
                         ctx.set_return(dest_reg, core::Value::from_bool(added));
                         return SyscallResult::Success;
                     });

    // set_has(set: handle, val: any) -> bool
    register_handler(syscall_id::COLLECTIONS_SET_HAS, "set_has",
                     [](SyscallContext& ctx, std::uint8_t dest_reg) {
                         auto* registry = get_collections(ctx);
                         if (registry == nullptr) {
                             ctx.set_return(dest_reg, core::Value::from_bool(false));
                             return SyscallResult::InvalidArgument;
                         }

                         auto handle = get_collection_handle(ctx.arg(0));
                         auto val = ctx.arg(1);

                         auto set_result = registry->get_set(handle);
                         if (!set_result) {
                             ctx.set_return(dest_reg, core::Value::from_bool(false));
                             return SyscallResult::InvalidArgument;
                         }

                         ctx.set_return(dest_reg, core::Value::from_bool((*set_result)->has(val)));
                         return SyscallResult::Success;
                     });

    // set_remove(set: handle, val: any) -> bool
    register_handler(syscall_id::COLLECTIONS_SET_REMOVE, "set_remove",
                     [](SyscallContext& ctx, std::uint8_t dest_reg) {
                         auto* registry = get_collections(ctx);
                         if (registry == nullptr) {
                             ctx.set_return(dest_reg, core::Value::from_bool(false));
                             return SyscallResult::InvalidArgument;
                         }

                         auto handle = get_collection_handle(ctx.arg(0));
                         auto val = ctx.arg(1);

                         auto set_result = registry->get_set(handle);
                         if (!set_result) {
                             ctx.set_return(dest_reg, core::Value::from_bool(false));
                             return SyscallResult::InvalidArgument;
                         }

                         bool removed = (*set_result)->remove(val);
                         ctx.set_return(dest_reg, core::Value::from_bool(removed));
                         return SyscallResult::Success;
                     });

    // set_len(set: handle) -> int
    register_handler(
        syscall_id::COLLECTIONS_SET_LEN, "set_len", [](SyscallContext& ctx, std::uint8_t dest_reg) {
            auto* registry = get_collections(ctx);
            if (registry == nullptr) {
                ctx.set_return(dest_reg, core::Value::from_int(0));
                return SyscallResult::InvalidArgument;
            }

            auto handle = get_collection_handle(ctx.arg(0));
            auto set_result = registry->get_set(handle);
            if (!set_result) {
                ctx.set_return(dest_reg, core::Value::from_int(0));
                return SyscallResult::InvalidArgument;
            }

            ctx.set_return(dest_reg,
                           core::Value::from_int(static_cast<std::int64_t>((*set_result)->size())));
            return SyscallResult::Success;
        });
}

// ============================================================================
// IO Module Handlers (GROUP-C)
// ============================================================================

namespace {

/// Convert IoError to SyscallResult
SyscallResult io_error_to_syscall_result(core::io::IoError err) {
    switch (err) {
        case core::io::IoError::Success:
            return SyscallResult::Success;
        case core::io::IoError::PathDenied:
        case core::io::IoError::PermissionDenied:
        case core::io::IoError::SandboxDisabled:
            return SyscallResult::PermissionDenied;
        case core::io::IoError::PathTraversal:
        case core::io::IoError::InvalidPath:
            return SyscallResult::InvalidArgument;
        case core::io::IoError::NotFound:
        case core::io::IoError::AlreadyExists:
        case core::io::IoError::TooLarge:
        case core::io::IoError::IoFailed:
            return SyscallResult::IoError;
    }
    return SyscallResult::IoError;
}

}  // namespace

void SyscallDispatcher::init_io_handlers() {
    // file_read(path: string) -> string (file contents)
    register_handler(syscall_id::IO_FILE_READ, "file_read",
                     [](SyscallContext& ctx, std::uint8_t dest_reg) {
                         // Check capability
                         if (!core::capabilities::has_permission(
                                 ctx.granted_caps, core::capabilities::Permission::Filesystem)) {
                             ctx.set_return(dest_reg, core::Value::nil());
                             return SyscallResult::PermissionDenied;
                         }

                         auto* fs = ctx.filesystem;
                         if (fs == nullptr) {
                             ctx.set_return(dest_reg, core::Value::nil());
                             return SyscallResult::PermissionDenied;
                         }

                         auto path = get_string_from_value(ctx, ctx.arg(0));
                         auto result = fs->read_file(path);

                         if (!result) {
                             ctx.set_return(dest_reg, core::Value::nil());
                             return io_error_to_syscall_result(result.error());
                         }

                         ctx.set_return(dest_reg, create_string_value(ctx, *result));
                         return SyscallResult::Success;
                     });

    // file_write(path: string, content: string) -> bool
    register_handler(
        syscall_id::IO_FILE_WRITE, "file_write", [](SyscallContext& ctx, std::uint8_t dest_reg) {
            if (!core::capabilities::has_permission(ctx.granted_caps,
                                                    core::capabilities::Permission::Filesystem)) {
                ctx.set_return(dest_reg, core::Value::from_bool(false));
                return SyscallResult::PermissionDenied;
            }

            auto* fs = ctx.filesystem;
            if (fs == nullptr) {
                ctx.set_return(dest_reg, core::Value::from_bool(false));
                return SyscallResult::PermissionDenied;
            }

            auto path = get_string_from_value(ctx, ctx.arg(0));
            auto content = get_string_from_value(ctx, ctx.arg(1));

            auto err = fs->write_file(path, content);
            ctx.set_return(dest_reg, core::Value::from_bool(err == core::io::IoError::Success));

            if (err != core::io::IoError::Success) {
                return io_error_to_syscall_result(err);
            }
            return SyscallResult::Success;
        });

    // file_exists(path: string) -> bool
    register_handler(syscall_id::IO_FILE_EXISTS, "file_exists",
                     [](SyscallContext& ctx, std::uint8_t dest_reg) {
                         if (!core::capabilities::has_permission(
                                 ctx.granted_caps, core::capabilities::Permission::Filesystem)) {
                             ctx.set_return(dest_reg, core::Value::from_bool(false));
                             return SyscallResult::PermissionDenied;
                         }

                         auto* fs = ctx.filesystem;
                         if (fs == nullptr) {
                             ctx.set_return(dest_reg, core::Value::from_bool(false));
                             return SyscallResult::PermissionDenied;
                         }

                         auto path = get_string_from_value(ctx, ctx.arg(0));
                         auto result = fs->file_exists(path);

                         if (!result) {
                             ctx.set_return(dest_reg, core::Value::from_bool(false));
                             return SyscallResult::Success;  // Not an error, just doesn't exist
                         }

                         ctx.set_return(dest_reg, core::Value::from_bool(*result));
                         return SyscallResult::Success;
                     });

    // file_delete(path: string) -> bool
    register_handler(
        syscall_id::IO_FILE_DELETE, "file_delete", [](SyscallContext& ctx, std::uint8_t dest_reg) {
            if (!core::capabilities::has_permission(ctx.granted_caps,
                                                    core::capabilities::Permission::Filesystem)) {
                ctx.set_return(dest_reg, core::Value::from_bool(false));
                return SyscallResult::PermissionDenied;
            }

            auto* fs = ctx.filesystem;
            if (fs == nullptr) {
                ctx.set_return(dest_reg, core::Value::from_bool(false));
                return SyscallResult::PermissionDenied;
            }

            auto path = get_string_from_value(ctx, ctx.arg(0));
            auto err = fs->delete_file(path);

            ctx.set_return(dest_reg, core::Value::from_bool(err == core::io::IoError::Success));

            if (err != core::io::IoError::Success && err != core::io::IoError::NotFound) {
                return io_error_to_syscall_result(err);
            }
            return SyscallResult::Success;
        });

    // file_append(path: string, content: string) -> bool
    register_handler(
        syscall_id::IO_FILE_APPEND, "file_append", [](SyscallContext& ctx, std::uint8_t dest_reg) {
            if (!core::capabilities::has_permission(ctx.granted_caps,
                                                    core::capabilities::Permission::Filesystem)) {
                ctx.set_return(dest_reg, core::Value::from_bool(false));
                return SyscallResult::PermissionDenied;
            }

            auto* fs = ctx.filesystem;
            if (fs == nullptr) {
                ctx.set_return(dest_reg, core::Value::from_bool(false));
                return SyscallResult::PermissionDenied;
            }

            auto path = get_string_from_value(ctx, ctx.arg(0));
            auto content = get_string_from_value(ctx, ctx.arg(1));

            auto err = fs->append_file(path, content);
            ctx.set_return(dest_reg, core::Value::from_bool(err == core::io::IoError::Success));

            if (err != core::io::IoError::Success) {
                return io_error_to_syscall_result(err);
            }
            return SyscallResult::Success;
        });

    // dir_create(path: string) -> bool
    register_handler(
        syscall_id::IO_DIR_CREATE, "dir_create", [](SyscallContext& ctx, std::uint8_t dest_reg) {
            if (!core::capabilities::has_permission(ctx.granted_caps,
                                                    core::capabilities::Permission::Filesystem)) {
                ctx.set_return(dest_reg, core::Value::from_bool(false));
                return SyscallResult::PermissionDenied;
            }

            auto* fs = ctx.filesystem;
            if (fs == nullptr) {
                ctx.set_return(dest_reg, core::Value::from_bool(false));
                return SyscallResult::PermissionDenied;
            }

            auto path = get_string_from_value(ctx, ctx.arg(0));
            auto err = fs->create_directory(path, true);  // recursive by default

            ctx.set_return(dest_reg, core::Value::from_bool(err == core::io::IoError::Success));

            if (err != core::io::IoError::Success) {
                return io_error_to_syscall_result(err);
            }
            return SyscallResult::Success;
        });

    // dir_list(path: string) -> list_handle (list of filenames)
    register_handler(
        syscall_id::IO_DIR_LIST, "dir_list", [](SyscallContext& ctx, std::uint8_t dest_reg) {
            if (!core::capabilities::has_permission(ctx.granted_caps,
                                                    core::capabilities::Permission::Filesystem)) {
                ctx.set_return(dest_reg, core::Value::nil());
                return SyscallResult::PermissionDenied;
            }

            auto* fs = ctx.filesystem;
            if (fs == nullptr) {
                ctx.set_return(dest_reg, core::Value::nil());
                return SyscallResult::PermissionDenied;
            }

            auto* registry = get_collections(ctx);
            if (registry == nullptr) {
                ctx.set_return(dest_reg, core::Value::nil());
                return SyscallResult::AllocationFailed;
            }

            auto path = get_string_from_value(ctx, ctx.arg(0));
            auto result = fs->list_directory(path);

            if (!result) {
                ctx.set_return(dest_reg, core::Value::nil());
                return io_error_to_syscall_result(result.error());
            }

            // Create a list with the filenames
            auto list_handle = registry->create_list();
            if (!list_handle) {
                ctx.set_return(dest_reg, core::Value::nil());
                return SyscallResult::AllocationFailed;
            }

            auto list = registry->get_list(*list_handle);
            if (!list) {
                ctx.set_return(dest_reg, core::Value::nil());
                return SyscallResult::AllocationFailed;
            }

            for (const auto& name : *result) {
                (*list)->push(create_string_value(ctx, name));
            }

            ctx.set_return(dest_reg, core::collections::encode_collection_value(*list_handle));
            return SyscallResult::Success;
        });
}

// ============================================================================
// Crypto Module Handlers (GROUP-C)
// ============================================================================

namespace {

/// Read bytes from a handle (string or raw memory)
std::vector<std::uint8_t> read_bytes_from_handle(SyscallContext& ctx, core::Value val) {
    if (!val.is_handle()) {
        // Try to get it as a string
        auto str = get_string_from_value(ctx, val);
        return std::vector<std::uint8_t>(str.begin(), str.end());
    }

    auto h = val.as_handle();
    auto size_result = ctx.vm_ctx.memory().get_size(h);
    if (!size_result) {
        return {};
    }

    std::vector<std::uint8_t> data(*size_result);
    auto err = ctx.vm_ctx.memory().read_bytes(h, 0, data.data(), data.size());
    if (err != core::MemoryError::Success) {
        return {};
    }
    return data;
}

/// Write bytes to a new allocation and return as handle value
core::Value write_bytes_as_handle(SyscallContext& ctx, std::span<const std::uint8_t> data) {
    if (data.empty()) {
        return core::Value::nil();
    }

    auto result = ctx.vm_ctx.memory().allocate(data.size());
    if (!result) {
        return core::Value::nil();
    }

    auto h = *result;
    auto err = ctx.vm_ctx.memory().write_bytes(h, 0, data.data(), data.size());
    if (err != core::MemoryError::Success) {
        (void)ctx.vm_ctx.memory().deallocate(h);
        return core::Value::nil();
    }

    return core::Value::from_handle(h);
}

/// Convert bytes to hex string
std::string bytes_to_hex(std::span<const std::uint8_t> data) {
    static constexpr char hex_chars[] = "0123456789abcdef";
    std::string result;
    result.reserve(data.size() * 2);
    for (auto byte : data) {
        result += hex_chars[(byte >> 4) & 0x0F];
        result += hex_chars[byte & 0x0F];
    }
    return result;
}

}  // namespace

void SyscallDispatcher::init_crypto_handlers() {
    // hash_blake3(data: handle) -> string (hex digest)
    register_handler(syscall_id::CRYPTO_HASH_BLAKE3, "hash_blake3",
                     [](SyscallContext& ctx, std::uint8_t dest_reg) {
                         if (!core::capabilities::has_permission(
                                 ctx.granted_caps, core::capabilities::Permission::Crypto)) {
                             return SyscallResult::PermissionDenied;
                         }

                         auto data = read_bytes_from_handle(ctx, ctx.arg(0));
                         auto digest =
                             core::crypto::Blake3::hash(std::span<const std::uint8_t>{data});
                         std::string hex = bytes_to_hex(std::span<const std::uint8_t>{digest});

                         ctx.set_return(dest_reg, create_string_value(ctx, hex));
                         return SyscallResult::Success;
                     });

    // hash_sha256(data: handle) -> string (hex digest)
    register_handler(syscall_id::CRYPTO_HASH_SHA256, "hash_sha256",
                     [](SyscallContext& ctx, std::uint8_t dest_reg) {
                         if (!core::capabilities::has_permission(
                                 ctx.granted_caps, core::capabilities::Permission::Crypto)) {
                             return SyscallResult::PermissionDenied;
                         }

                         auto data = read_bytes_from_handle(ctx, ctx.arg(0));
                         auto digest =
                             core::crypto::Sha256::hash(std::span<const std::uint8_t>{data});
                         std::string hex = bytes_to_hex(std::span<const std::uint8_t>{digest});

                         ctx.set_return(dest_reg, create_string_value(ctx, hex));
                         return SyscallResult::Success;
                     });

    // sign_ed25519(message: handle, private_key: handle) -> handle (signature bytes)
    register_handler(
        syscall_id::CRYPTO_SIGN_ED25519, "sign_ed25519",
        [](SyscallContext& ctx, std::uint8_t dest_reg) {
            if (!core::capabilities::has_permission(ctx.granted_caps,
                                                    core::capabilities::Permission::Crypto)) {
                return SyscallResult::PermissionDenied;
            }

            auto message = read_bytes_from_handle(ctx, ctx.arg(0));
            auto key_data = read_bytes_from_handle(ctx, ctx.arg(1));

            if (key_data.size() != core::crypto::Ed25519::PRIVATE_KEY_SIZE) {
                ctx.set_return(dest_reg, core::Value::nil());
                return SyscallResult::InvalidArgument;
            }

            core::crypto::Ed25519::PrivateKey key;
            std::copy_n(key_data.begin(), key.size(), key.begin());

            auto signature =
                core::crypto::Ed25519::sign(std::span<const std::uint8_t>{message}, key);

            ctx.set_return(dest_reg,
                           write_bytes_as_handle(ctx, std::span<const std::uint8_t>{signature}));
            return SyscallResult::Success;
        });

    // verify_ed25519(message: handle, signature: handle, public_key: handle) -> bool
    register_handler(syscall_id::CRYPTO_VERIFY_ED25519, "verify_ed25519",
                     [](SyscallContext& ctx, std::uint8_t dest_reg) {
                         if (!core::capabilities::has_permission(
                                 ctx.granted_caps, core::capabilities::Permission::Crypto)) {
                             return SyscallResult::PermissionDenied;
                         }

                         auto message = read_bytes_from_handle(ctx, ctx.arg(0));
                         auto sig_data = read_bytes_from_handle(ctx, ctx.arg(1));
                         auto key_data = read_bytes_from_handle(ctx, ctx.arg(2));

                         if (sig_data.size() != core::crypto::Ed25519::SIGNATURE_SIZE ||
                             key_data.size() != core::crypto::Ed25519::PUBLIC_KEY_SIZE) {
                             ctx.set_return(dest_reg, core::Value::from_bool(false));
                             return SyscallResult::InvalidArgument;
                         }

                         core::crypto::Ed25519::Signature sig;
                         core::crypto::Ed25519::PublicKey key;
                         std::copy_n(sig_data.begin(), sig.size(), sig.begin());
                         std::copy_n(key_data.begin(), key.size(), key.begin());

                         bool valid = core::crypto::Ed25519::verify(
                             std::span<const std::uint8_t>{message}, sig, key);

                         ctx.set_return(dest_reg, core::Value::from_bool(valid));
                         return SyscallResult::Success;
                     });

    // encrypt_aes(plaintext: handle, key: handle) -> handle (ciphertext)
    register_handler(
        syscall_id::CRYPTO_ENCRYPT_AES, "encrypt_aes",
        [](SyscallContext& ctx, std::uint8_t dest_reg) {
            if (!core::capabilities::has_permission(ctx.granted_caps,
                                                    core::capabilities::Permission::Crypto)) {
                return SyscallResult::PermissionDenied;
            }

            auto plaintext = read_bytes_from_handle(ctx, ctx.arg(0));
            auto key_data = read_bytes_from_handle(ctx, ctx.arg(1));

            if (key_data.size() != core::crypto::Aes256Gcm::KEY_SIZE) {
                ctx.set_return(dest_reg, core::Value::nil());
                return SyscallResult::InvalidArgument;
            }

            core::crypto::Aes256Gcm::Key key;
            std::copy_n(key_data.begin(), key.size(), key.begin());

            auto ciphertext =
                core::crypto::Aes256Gcm::encrypt(std::span<const std::uint8_t>{plaintext}, key);

            ctx.set_return(dest_reg,
                           write_bytes_as_handle(ctx, std::span<const std::uint8_t>{ciphertext}));
            return SyscallResult::Success;
        });

    // decrypt_aes(ciphertext: handle, key: handle) -> handle (plaintext)
    register_handler(
        syscall_id::CRYPTO_DECRYPT_AES, "decrypt_aes",
        [](SyscallContext& ctx, std::uint8_t dest_reg) {
            if (!core::capabilities::has_permission(ctx.granted_caps,
                                                    core::capabilities::Permission::Crypto)) {
                return SyscallResult::PermissionDenied;
            }

            auto ciphertext = read_bytes_from_handle(ctx, ctx.arg(0));
            auto key_data = read_bytes_from_handle(ctx, ctx.arg(1));

            if (key_data.size() != core::crypto::Aes256Gcm::KEY_SIZE) {
                ctx.set_return(dest_reg, core::Value::nil());
                return SyscallResult::InvalidArgument;
            }

            if (ciphertext.size() <
                core::crypto::Aes256Gcm::NONCE_SIZE + core::crypto::Aes256Gcm::TAG_SIZE) {
                ctx.set_return(dest_reg, core::Value::nil());
                return SyscallResult::InvalidArgument;
            }

            core::crypto::Aes256Gcm::Key key;
            std::copy_n(key_data.begin(), key.size(), key.begin());

            auto plaintext =
                core::crypto::Aes256Gcm::decrypt(std::span<const std::uint8_t>{ciphertext}, key);

            // Empty result indicates authentication failure
            if (plaintext.empty() && !ciphertext.empty()) {
                ctx.set_return(dest_reg, core::Value::nil());
                return SyscallResult::InvalidArgument;
            }

            ctx.set_return(dest_reg,
                           write_bytes_as_handle(ctx, std::span<const std::uint8_t>{plaintext}));
            return SyscallResult::Success;
        });

    // random_bytes(n: int) -> handle (random bytes)
    register_handler(syscall_id::CRYPTO_RANDOM_BYTES, "random_bytes",
                     [](SyscallContext& ctx, std::uint8_t dest_reg) {
                         if (!core::capabilities::has_permission(
                                 ctx.granted_caps, core::capabilities::Permission::Crypto)) {
                             return SyscallResult::PermissionDenied;
                         }

                         auto n = ctx.arg(0).as_integer();
                         if (n <= 0 || n > 1024 * 1024) {  // Limit to 1MB
                             ctx.set_return(dest_reg, core::Value::nil());
                             return SyscallResult::InvalidArgument;
                         }

                         std::vector<std::uint8_t> bytes(static_cast<std::size_t>(n));

                         // Use random_device for cryptographic quality randomness
                         std::random_device rd;
                         for (auto& byte : bytes) {
                             byte = static_cast<std::uint8_t>(rd() & 0xFF);
                         }

                         ctx.set_return(dest_reg, write_bytes_as_handle(
                                                      ctx, std::span<const std::uint8_t>{bytes}));
                         return SyscallResult::Success;
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

    // time_format(timestamp_ms: int, format: string) -> string
    register_handler(
        syscall_id::TIME_FORMAT, "time_format", [](SyscallContext& ctx, std::uint8_t dest_reg) {
            auto ms = ctx.arg(0).as_integer();
            auto fmt = get_string_from_value(ctx, ctx.arg(1));

            auto tp = std::chrono::system_clock::time_point{std::chrono::milliseconds{ms}};
            auto tt = std::chrono::system_clock::to_time_t(tp);
            std::tm tm_buf{};
#ifdef _WIN32
            gmtime_s(&tm_buf, &tt);
#else
                         gmtime_r(&tt, &tm_buf);
#endif

            std::string format_str = fmt.empty() ? "%Y-%m-%dT%H:%M:%SZ" : std::string(fmt);
            std::ostringstream oss;
            oss << std::put_time(&tm_buf, format_str.c_str());

            ctx.set_return(dest_reg, create_string_value(ctx, oss.str()));
            return SyscallResult::Success;
        });

    // time_duration(start_ms: int, end_ms: int) -> int
    register_handler(syscall_id::TIME_DURATION, "time_duration",
                     [](SyscallContext& ctx, std::uint8_t dest_reg) {
                         auto start = ctx.arg(0).as_integer();
                         auto end = ctx.arg(1).as_integer();
                         ctx.set_return(dest_reg, core::Value::from_int(end - start));
                         return SyscallResult::Success;
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
