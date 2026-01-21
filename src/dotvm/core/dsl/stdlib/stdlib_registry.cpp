#include "dotvm/core/dsl/stdlib/stdlib_registry.hpp"

namespace dotvm::core::dsl::stdlib {

using capabilities::Permission;

// ============================================================================
// Singleton Implementation
// ============================================================================

StdlibRegistry& StdlibRegistry::instance() {
    static StdlibRegistry registry;
    return registry;
}

StdlibRegistry::StdlibRegistry() {
    // Initialize all built-in modules
    init_prelude();
    init_math();
    init_string();
    init_collections();
    init_io();
    init_crypto();
    init_net();
    init_time();
    init_async();
    init_control();
}

// ============================================================================
// Query Methods
// ============================================================================

const ModuleDef* StdlibRegistry::find_module(std::string_view path) const {
    auto it = modules_.find(std::string{path});
    if (it != modules_.end()) {
        return &it->second;
    }
    return nullptr;
}

bool StdlibRegistry::has_module(std::string_view path) const {
    return modules_.find(std::string{path}) != modules_.end();
}

std::vector<std::string_view> StdlibRegistry::module_paths() const {
    std::vector<std::string_view> paths;
    paths.reserve(modules_.size());
    for (const auto& [path, _] : modules_) {
        paths.emplace_back(path);
    }
    return paths;
}

std::vector<const ModuleDef*> StdlibRegistry::auto_import_modules() const {
    std::vector<const ModuleDef*> result;
    for (const auto& [_, module] : modules_) {
        if (module.auto_import) {
            result.push_back(&module);
        }
    }
    return result;
}

const FunctionDef* StdlibRegistry::resolve_qualified_call(
    std::string_view qualifier, std::string_view function_name,
    const std::unordered_map<std::string, std::string>& imported_modules) const {
    // Look up the module path from the qualifier
    auto it = imported_modules.find(std::string{qualifier});
    if (it == imported_modules.end()) {
        // Try treating qualifier as a direct module path component
        // e.g., "math" -> "std/math"
        std::string full_path = "std/" + std::string{qualifier};
        auto module = find_module(full_path);
        if (module) {
            return module->find_function(function_name);
        }
        return nullptr;
    }

    // Found the module path, look up the function
    auto module = find_module(it->second);
    if (!module) {
        return nullptr;
    }

    return module->find_function(function_name);
}

const FunctionDef* StdlibRegistry::find_by_syscall_id(std::uint16_t syscall_id) const {
    auto it = syscall_index_.find(syscall_id);
    if (it != syscall_index_.end()) {
        return it->second;
    }
    return nullptr;
}

// ============================================================================
// Registration
// ============================================================================

void StdlibRegistry::register_module(ModuleDef module) {
    // Build syscall index for this module's functions
    for (const auto& fn : module.functions) {
        if (fn.syscall_id != 0) {
            syscall_index_[fn.syscall_id] = &module.functions.back();
        }
    }

    // Store the module (must be done after indexing to get stable pointers)
    std::string path = module.path;
    modules_.emplace(std::move(path), std::move(module));

    // Re-index syscalls for the stored module (pointers were invalidated)
    auto& stored_module = modules_.at(modules_.find(path) != modules_.end()
        ? path
        : modules_.begin()->first);
    for (const auto& fn : stored_module.functions) {
        if (fn.syscall_id != 0) {
            syscall_index_[fn.syscall_id] = &fn;
        }
    }
}

// ============================================================================
// Module Initialization
// ============================================================================

void StdlibRegistry::init_prelude() {
    ModuleDef prelude{
        .path = "std/prelude",
        .required_caps = Permission::None,
        .functions = {},
        .auto_import = true,  // Prelude is auto-imported
    };

    // print(value: any) -> void
    prelude.functions.push_back(FunctionDef::variadic(
        "print", {StdlibType::Any}, StdlibType::Void, syscall_id::PRELUDE_PRINT, false));

    // assert(condition: bool, message?: string) -> void
    prelude.functions.push_back(FunctionDef::impure(
        "assert", {StdlibType::Bool}, StdlibType::Void, syscall_id::PRELUDE_ASSERT));

    // type_of(value: any) -> string
    prelude.functions.push_back(FunctionDef::pure(
        "type_of", {StdlibType::Any}, StdlibType::String, syscall_id::PRELUDE_TYPE_OF));

    // len(value: any) -> int (works on strings, lists, maps, etc.)
    prelude.functions.push_back(
        FunctionDef::pure("len", {StdlibType::Any}, StdlibType::Int, syscall_id::PRELUDE_LEN));

    // str(value: any) -> string
    prelude.functions.push_back(
        FunctionDef::pure("str", {StdlibType::Any}, StdlibType::String, syscall_id::PRELUDE_STR));

    // int(value: any) -> int
    prelude.functions.push_back(
        FunctionDef::pure("int", {StdlibType::Any}, StdlibType::Int, syscall_id::PRELUDE_INT));

    // float(value: any) -> float
    prelude.functions.push_back(FunctionDef::pure(
        "float", {StdlibType::Any}, StdlibType::Float, syscall_id::PRELUDE_FLOAT));

    // Type checking functions (inline implementations)
    prelude.functions.push_back(
        FunctionDef::inline_fn("is_int", {StdlibType::Any}, StdlibType::Bool));
    prelude.functions.push_back(
        FunctionDef::inline_fn("is_float", {StdlibType::Any}, StdlibType::Bool));
    prelude.functions.push_back(
        FunctionDef::inline_fn("is_bool", {StdlibType::Any}, StdlibType::Bool));
    prelude.functions.push_back(
        FunctionDef::inline_fn("is_string", {StdlibType::Any}, StdlibType::Bool));
    prelude.functions.push_back(
        FunctionDef::inline_fn("is_handle", {StdlibType::Any}, StdlibType::Bool));

    register_module(std::move(prelude));
}

void StdlibRegistry::init_math() {
    ModuleDef math{
        .path = "std/math",
        .required_caps = Permission::None,
        .functions = {},
        .auto_import = false,
    };

    // Basic math functions
    math.functions.push_back(
        FunctionDef::pure("abs", {StdlibType::Float}, StdlibType::Float, syscall_id::MATH_ABS));
    math.functions.push_back(FunctionDef::pure(
        "min", {StdlibType::Float, StdlibType::Float}, StdlibType::Float, syscall_id::MATH_MIN));
    math.functions.push_back(FunctionDef::pure(
        "max", {StdlibType::Float, StdlibType::Float}, StdlibType::Float, syscall_id::MATH_MAX));
    math.functions.push_back(
        FunctionDef::pure("floor", {StdlibType::Float}, StdlibType::Int, syscall_id::MATH_FLOOR));
    math.functions.push_back(
        FunctionDef::pure("ceil", {StdlibType::Float}, StdlibType::Int, syscall_id::MATH_CEIL));
    math.functions.push_back(
        FunctionDef::pure("round", {StdlibType::Float}, StdlibType::Int, syscall_id::MATH_ROUND));
    math.functions.push_back(
        FunctionDef::pure("sqrt", {StdlibType::Float}, StdlibType::Float, syscall_id::MATH_SQRT));
    math.functions.push_back(FunctionDef::pure(
        "pow", {StdlibType::Float, StdlibType::Float}, StdlibType::Float, syscall_id::MATH_POW));

    // Trigonometric functions
    math.functions.push_back(
        FunctionDef::pure("sin", {StdlibType::Float}, StdlibType::Float, syscall_id::MATH_SIN));
    math.functions.push_back(
        FunctionDef::pure("cos", {StdlibType::Float}, StdlibType::Float, syscall_id::MATH_COS));
    math.functions.push_back(
        FunctionDef::pure("tan", {StdlibType::Float}, StdlibType::Float, syscall_id::MATH_TAN));
    math.functions.push_back(
        FunctionDef::pure("asin", {StdlibType::Float}, StdlibType::Float, syscall_id::MATH_ASIN));
    math.functions.push_back(
        FunctionDef::pure("acos", {StdlibType::Float}, StdlibType::Float, syscall_id::MATH_ACOS));
    math.functions.push_back(
        FunctionDef::pure("atan", {StdlibType::Float}, StdlibType::Float, syscall_id::MATH_ATAN));
    math.functions.push_back(FunctionDef::pure(
        "atan2", {StdlibType::Float, StdlibType::Float}, StdlibType::Float, syscall_id::MATH_ATAN2));

    // Exponential/logarithmic functions
    math.functions.push_back(
        FunctionDef::pure("log", {StdlibType::Float}, StdlibType::Float, syscall_id::MATH_LOG));
    math.functions.push_back(
        FunctionDef::pure("log10", {StdlibType::Float}, StdlibType::Float, syscall_id::MATH_LOG10));
    math.functions.push_back(
        FunctionDef::pure("log2", {StdlibType::Float}, StdlibType::Float, syscall_id::MATH_LOG2));
    math.functions.push_back(
        FunctionDef::pure("exp", {StdlibType::Float}, StdlibType::Float, syscall_id::MATH_EXP));

    // Constants (inline - no syscall needed)
    math.functions.push_back(FunctionDef::inline_fn("pi", {}, StdlibType::Float));
    math.functions.push_back(FunctionDef::inline_fn("e", {}, StdlibType::Float));

    register_module(std::move(math));
}

void StdlibRegistry::init_string() {
    ModuleDef string_mod{
        .path = "std/string",
        .required_caps = Permission::None,
        .functions = {},
        .auto_import = false,
    };

    string_mod.functions.push_back(FunctionDef::pure(
        "concat", {StdlibType::String, StdlibType::String}, StdlibType::String,
        syscall_id::STRING_CONCAT));
    string_mod.functions.push_back(FunctionDef::pure(
        "split", {StdlibType::String, StdlibType::String}, StdlibType::Handle,
        syscall_id::STRING_SPLIT));
    string_mod.functions.push_back(FunctionDef::pure(
        "join", {StdlibType::Handle, StdlibType::String}, StdlibType::String,
        syscall_id::STRING_JOIN));
    string_mod.functions.push_back(FunctionDef::pure(
        "trim", {StdlibType::String}, StdlibType::String, syscall_id::STRING_TRIM));
    string_mod.functions.push_back(FunctionDef::pure(
        "upper", {StdlibType::String}, StdlibType::String, syscall_id::STRING_UPPER));
    string_mod.functions.push_back(FunctionDef::pure(
        "lower", {StdlibType::String}, StdlibType::String, syscall_id::STRING_LOWER));
    string_mod.functions.push_back(FunctionDef::pure(
        "starts_with", {StdlibType::String, StdlibType::String}, StdlibType::Bool,
        syscall_id::STRING_STARTS_WITH));
    string_mod.functions.push_back(FunctionDef::pure(
        "ends_with", {StdlibType::String, StdlibType::String}, StdlibType::Bool,
        syscall_id::STRING_ENDS_WITH));
    string_mod.functions.push_back(FunctionDef::pure(
        "contains", {StdlibType::String, StdlibType::String}, StdlibType::Bool,
        syscall_id::STRING_CONTAINS));
    string_mod.functions.push_back(FunctionDef::pure(
        "replace", {StdlibType::String, StdlibType::String, StdlibType::String}, StdlibType::String,
        syscall_id::STRING_REPLACE));
    string_mod.functions.push_back(FunctionDef::pure(
        "substr", {StdlibType::String, StdlibType::Int, StdlibType::Int}, StdlibType::String,
        syscall_id::STRING_SUBSTR));
    string_mod.functions.push_back(FunctionDef::pure(
        "len", {StdlibType::String}, StdlibType::Int, syscall_id::STRING_LEN));
    string_mod.functions.push_back(FunctionDef::pure(
        "char_at", {StdlibType::String, StdlibType::Int}, StdlibType::String,
        syscall_id::STRING_CHAR_AT));

    register_module(std::move(string_mod));
}

void StdlibRegistry::init_collections() {
    ModuleDef collections{
        .path = "std/collections",
        .required_caps = Permission::None,
        .functions = {},
        .auto_import = false,
    };

    // List operations
    collections.functions.push_back(FunctionDef::impure(
        "list_new", {}, StdlibType::Handle, syscall_id::COLLECTIONS_LIST_NEW));
    collections.functions.push_back(FunctionDef::impure(
        "list_push", {StdlibType::Handle, StdlibType::Any}, StdlibType::Void,
        syscall_id::COLLECTIONS_LIST_PUSH));
    collections.functions.push_back(FunctionDef::impure(
        "list_pop", {StdlibType::Handle}, StdlibType::Any, syscall_id::COLLECTIONS_LIST_POP));
    collections.functions.push_back(FunctionDef::pure(
        "list_get", {StdlibType::Handle, StdlibType::Int}, StdlibType::Any,
        syscall_id::COLLECTIONS_LIST_GET));
    collections.functions.push_back(FunctionDef::impure(
        "list_set", {StdlibType::Handle, StdlibType::Int, StdlibType::Any}, StdlibType::Void,
        syscall_id::COLLECTIONS_LIST_SET));
    collections.functions.push_back(FunctionDef::pure(
        "list_len", {StdlibType::Handle}, StdlibType::Int, syscall_id::COLLECTIONS_LIST_LEN));
    collections.functions.push_back(FunctionDef::impure(
        "list_clear", {StdlibType::Handle}, StdlibType::Void, syscall_id::COLLECTIONS_LIST_CLEAR));

    // Map operations
    collections.functions.push_back(
        FunctionDef::impure("map_new", {}, StdlibType::Handle, syscall_id::COLLECTIONS_MAP_NEW));
    collections.functions.push_back(FunctionDef::pure(
        "map_get", {StdlibType::Handle, StdlibType::Any}, StdlibType::Any,
        syscall_id::COLLECTIONS_MAP_GET));
    collections.functions.push_back(FunctionDef::impure(
        "map_set", {StdlibType::Handle, StdlibType::Any, StdlibType::Any}, StdlibType::Void,
        syscall_id::COLLECTIONS_MAP_SET));
    collections.functions.push_back(FunctionDef::pure(
        "map_has", {StdlibType::Handle, StdlibType::Any}, StdlibType::Bool,
        syscall_id::COLLECTIONS_MAP_HAS));
    collections.functions.push_back(FunctionDef::impure(
        "map_delete", {StdlibType::Handle, StdlibType::Any}, StdlibType::Void,
        syscall_id::COLLECTIONS_MAP_DELETE));
    collections.functions.push_back(FunctionDef::pure(
        "map_keys", {StdlibType::Handle}, StdlibType::Handle, syscall_id::COLLECTIONS_MAP_KEYS));
    collections.functions.push_back(FunctionDef::pure(
        "map_values", {StdlibType::Handle}, StdlibType::Handle, syscall_id::COLLECTIONS_MAP_VALUES));

    // Set operations
    collections.functions.push_back(
        FunctionDef::impure("set_new", {}, StdlibType::Handle, syscall_id::COLLECTIONS_SET_NEW));
    collections.functions.push_back(FunctionDef::impure(
        "set_add", {StdlibType::Handle, StdlibType::Any}, StdlibType::Void,
        syscall_id::COLLECTIONS_SET_ADD));
    collections.functions.push_back(FunctionDef::pure(
        "set_has", {StdlibType::Handle, StdlibType::Any}, StdlibType::Bool,
        syscall_id::COLLECTIONS_SET_HAS));
    collections.functions.push_back(FunctionDef::impure(
        "set_remove", {StdlibType::Handle, StdlibType::Any}, StdlibType::Void,
        syscall_id::COLLECTIONS_SET_REMOVE));
    collections.functions.push_back(FunctionDef::pure(
        "set_len", {StdlibType::Handle}, StdlibType::Int, syscall_id::COLLECTIONS_SET_LEN));

    register_module(std::move(collections));
}

void StdlibRegistry::init_io() {
    ModuleDef io{
        .path = "std/io",
        .required_caps = Permission::Filesystem,  // Requires filesystem capability
        .functions = {},
        .auto_import = false,
    };

    io.functions.push_back(FunctionDef::impure(
        "file_read", {StdlibType::String}, StdlibType::String, syscall_id::IO_FILE_READ));
    io.functions.push_back(FunctionDef::impure(
        "file_write", {StdlibType::String, StdlibType::String}, StdlibType::Bool,
        syscall_id::IO_FILE_WRITE));
    io.functions.push_back(FunctionDef::pure(
        "file_exists", {StdlibType::String}, StdlibType::Bool, syscall_id::IO_FILE_EXISTS));
    io.functions.push_back(FunctionDef::impure(
        "file_delete", {StdlibType::String}, StdlibType::Bool, syscall_id::IO_FILE_DELETE));
    io.functions.push_back(FunctionDef::impure(
        "file_append", {StdlibType::String, StdlibType::String}, StdlibType::Bool,
        syscall_id::IO_FILE_APPEND));
    io.functions.push_back(FunctionDef::impure(
        "dir_create", {StdlibType::String}, StdlibType::Bool, syscall_id::IO_DIR_CREATE));
    io.functions.push_back(FunctionDef::pure(
        "dir_list", {StdlibType::String}, StdlibType::Handle, syscall_id::IO_DIR_LIST));

    register_module(std::move(io));
}

void StdlibRegistry::init_crypto() {
    ModuleDef crypto{
        .path = "std/crypto",
        .required_caps = Permission::Crypto,  // Requires crypto capability
        .functions = {},
        .auto_import = false,
    };

    // Hashing
    crypto.functions.push_back(FunctionDef::pure(
        "hash_blake3", {StdlibType::String}, StdlibType::String, syscall_id::CRYPTO_HASH_BLAKE3));
    crypto.functions.push_back(FunctionDef::pure(
        "hash_sha256", {StdlibType::String}, StdlibType::String, syscall_id::CRYPTO_HASH_SHA256));

    // Ed25519 signatures
    crypto.functions.push_back(FunctionDef::pure(
        "sign_ed25519", {StdlibType::String, StdlibType::Handle}, StdlibType::String,
        syscall_id::CRYPTO_SIGN_ED25519));
    crypto.functions.push_back(FunctionDef::pure(
        "verify_ed25519", {StdlibType::String, StdlibType::String, StdlibType::Handle},
        StdlibType::Bool, syscall_id::CRYPTO_VERIFY_ED25519));

    // AES-256-GCM encryption
    crypto.functions.push_back(FunctionDef::pure(
        "encrypt_aes", {StdlibType::String, StdlibType::Handle}, StdlibType::String,
        syscall_id::CRYPTO_ENCRYPT_AES));
    crypto.functions.push_back(FunctionDef::pure(
        "decrypt_aes", {StdlibType::String, StdlibType::Handle}, StdlibType::String,
        syscall_id::CRYPTO_DECRYPT_AES));

    // Random bytes generation
    crypto.functions.push_back(FunctionDef::impure(
        "random_bytes", {StdlibType::Int}, StdlibType::Handle, syscall_id::CRYPTO_RANDOM_BYTES));

    register_module(std::move(crypto));
}

void StdlibRegistry::init_net() {
    ModuleDef net{
        .path = "std/net",
        .required_caps = Permission::Network,  // Requires network capability
        .functions = {},
        .auto_import = false,
    };

    net.functions.push_back(FunctionDef::impure(
        "http_get", {StdlibType::String}, StdlibType::String, syscall_id::NET_HTTP_GET));
    net.functions.push_back(FunctionDef::impure(
        "http_post", {StdlibType::String, StdlibType::String}, StdlibType::String,
        syscall_id::NET_HTTP_POST));
    net.functions.push_back(FunctionDef::impure(
        "http_put", {StdlibType::String, StdlibType::String}, StdlibType::String,
        syscall_id::NET_HTTP_PUT));
    net.functions.push_back(FunctionDef::impure(
        "http_delete", {StdlibType::String}, StdlibType::String, syscall_id::NET_HTTP_DELETE));

    register_module(std::move(net));
}

void StdlibRegistry::init_time() {
    ModuleDef time{
        .path = "std/time",
        .required_caps = Permission::None,  // Time is generally safe
        .functions = {},
        .auto_import = false,
    };

    time.functions.push_back(
        FunctionDef::impure("now", {}, StdlibType::Int, syscall_id::TIME_NOW));
    time.functions.push_back(
        FunctionDef::impure("timestamp", {}, StdlibType::Float, syscall_id::TIME_TIMESTAMP));
    time.functions.push_back(FunctionDef::pure(
        "duration", {StdlibType::Int, StdlibType::Int}, StdlibType::Int, syscall_id::TIME_DURATION));
    time.functions.push_back(
        FunctionDef::impure("sleep", {StdlibType::Int}, StdlibType::Void, syscall_id::TIME_SLEEP));
    time.functions.push_back(FunctionDef::pure(
        "format", {StdlibType::Int, StdlibType::String}, StdlibType::String,
        syscall_id::TIME_FORMAT));

    register_module(std::move(time));
}

void StdlibRegistry::init_async() {
    ModuleDef async_mod{
        .path = "std/async",
        .required_caps = Permission::None,  // Basic async primitives are safe
        .functions = {},
        .auto_import = false,
    };

    // Stub implementations for now
    async_mod.functions.push_back(FunctionDef::impure(
        "spawn", {StdlibType::Handle}, StdlibType::Handle, syscall_id::ASYNC_SPAWN));
    async_mod.functions.push_back(FunctionDef::impure(
        "channel_new", {}, StdlibType::Handle, syscall_id::ASYNC_CHANNEL_NEW));
    async_mod.functions.push_back(FunctionDef::impure(
        "channel_send", {StdlibType::Handle, StdlibType::Any}, StdlibType::Void,
        syscall_id::ASYNC_CHANNEL_SEND));
    async_mod.functions.push_back(FunctionDef::impure(
        "channel_recv", {StdlibType::Handle}, StdlibType::Any, syscall_id::ASYNC_CHANNEL_RECV));
    async_mod.functions.push_back(FunctionDef::impure(
        "await", {StdlibType::Handle}, StdlibType::Any, syscall_id::ASYNC_AWAIT));

    register_module(std::move(async_mod));
}

void StdlibRegistry::init_control() {
    ModuleDef control{
        .path = "std/control",
        .required_caps = Permission::None,
        .functions = {},
        .auto_import = false,
    };

    // These are typically handled at compile time to generate control flow
    // The syscall IDs are for runtime fallback/dynamic behavior
    control.functions.push_back(FunctionDef::impure(
        "foreach", {StdlibType::Handle, StdlibType::Handle}, StdlibType::Void,
        syscall_id::CONTROL_FOREACH));
    control.functions.push_back(FunctionDef::impure(
        "while_loop", {StdlibType::Handle, StdlibType::Handle}, StdlibType::Void,
        syscall_id::CONTROL_WHILE));
    control.functions.push_back(FunctionDef::pure(
        "match", {StdlibType::Any, StdlibType::Handle}, StdlibType::Any, syscall_id::CONTROL_MATCH));

    register_module(std::move(control));
}

}  // namespace dotvm::core::dsl::stdlib
