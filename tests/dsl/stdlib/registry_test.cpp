/// @file registry_test.cpp
/// @brief DSL-004 Standard Library Registry Tests

#include <set>

#include <gtest/gtest.h>

#include "dotvm/core/capabilities/capability.hpp"
#include "dotvm/core/dsl/stdlib/module_def.hpp"
#include "dotvm/core/dsl/stdlib/stdlib_registry.hpp"
#include "dotvm/core/dsl/stdlib/stdlib_types.hpp"

using namespace dotvm::core::dsl::stdlib;
using namespace dotvm::core::capabilities;

class StdlibRegistryTest : public ::testing::Test {
protected:
    const StdlibRegistry& registry = StdlibRegistry::instance();

    // Helper to find a function in a module
    const FunctionDef* find_function(std::string_view module_path, std::string_view fn_name) const {
        const auto* module = registry.find_module(module_path);
        if (!module) {
            return nullptr;
        }
        for (const auto& fn : module->functions) {
            if (fn.name == fn_name) {
                return &fn;
            }
        }
        return nullptr;
    }
};

// ============================================================================
// Module Registration Tests
// ============================================================================

TEST_F(StdlibRegistryTest, PreludeModuleExists) {
    const auto* module = registry.find_module("std/prelude");
    ASSERT_NE(module, nullptr);
    EXPECT_EQ(module->path, "std/prelude");
    EXPECT_TRUE(module->auto_import);
    EXPECT_EQ(module->required_caps, Permission::None);
}

TEST_F(StdlibRegistryTest, MathModuleExists) {
    const auto* module = registry.find_module("std/math");
    ASSERT_NE(module, nullptr);
    EXPECT_EQ(module->path, "std/math");
    EXPECT_FALSE(module->auto_import);
    EXPECT_EQ(module->required_caps, Permission::None);
}

TEST_F(StdlibRegistryTest, StringModuleExists) {
    const auto* module = registry.find_module("std/string");
    ASSERT_NE(module, nullptr);
    EXPECT_EQ(module->path, "std/string");
    EXPECT_EQ(module->required_caps, Permission::None);
}

TEST_F(StdlibRegistryTest, CollectionsModuleExists) {
    const auto* module = registry.find_module("std/collections");
    ASSERT_NE(module, nullptr);
    EXPECT_EQ(module->path, "std/collections");
    EXPECT_EQ(module->required_caps, Permission::None);
}

TEST_F(StdlibRegistryTest, IoModuleRequiresFilesystem) {
    const auto* module = registry.find_module("std/io");
    ASSERT_NE(module, nullptr);
    EXPECT_EQ(module->path, "std/io");
    EXPECT_EQ(module->required_caps, Permission::Filesystem);
}

TEST_F(StdlibRegistryTest, CryptoModuleRequiresCrypto) {
    const auto* module = registry.find_module("std/crypto");
    ASSERT_NE(module, nullptr);
    EXPECT_EQ(module->path, "std/crypto");
    EXPECT_EQ(module->required_caps, Permission::Crypto);
}

TEST_F(StdlibRegistryTest, NetModuleRequiresNetwork) {
    const auto* module = registry.find_module("std/net");
    ASSERT_NE(module, nullptr);
    EXPECT_EQ(module->path, "std/net");
    EXPECT_EQ(module->required_caps, Permission::Network);
}

TEST_F(StdlibRegistryTest, TimeModuleExists) {
    const auto* module = registry.find_module("std/time");
    ASSERT_NE(module, nullptr);
    EXPECT_EQ(module->path, "std/time");
    EXPECT_EQ(module->required_caps, Permission::None);
}

TEST_F(StdlibRegistryTest, AsyncModuleExists) {
    const auto* module = registry.find_module("std/async");
    ASSERT_NE(module, nullptr);
    EXPECT_EQ(module->path, "std/async");
    EXPECT_EQ(module->required_caps, Permission::None);
}

TEST_F(StdlibRegistryTest, ControlModuleExists) {
    const auto* module = registry.find_module("std/control");
    ASSERT_NE(module, nullptr);
    EXPECT_EQ(module->path, "std/control");
    EXPECT_EQ(module->required_caps, Permission::None);
}

TEST_F(StdlibRegistryTest, UnknownModuleReturnsNull) {
    const auto* module = registry.find_module("std/nonexistent");
    EXPECT_EQ(module, nullptr);
}

TEST_F(StdlibRegistryTest, HasModuleReturnsCorrectly) {
    EXPECT_TRUE(registry.has_module("std/prelude"));
    EXPECT_TRUE(registry.has_module("std/math"));
    EXPECT_FALSE(registry.has_module("std/nonexistent"));
}

// ============================================================================
// Function Registration Tests
// ============================================================================

TEST_F(StdlibRegistryTest, PreludeHasPrintFunction) {
    const auto* fn = find_function("std/prelude", "print");
    ASSERT_NE(fn, nullptr);
    EXPECT_EQ(fn->name, "print");
    EXPECT_EQ(fn->syscall_id, syscall_id::PRELUDE_PRINT);
    EXPECT_EQ(fn->return_type, StdlibType::Void);
    EXPECT_TRUE(fn->is_variadic);
}

TEST_F(StdlibRegistryTest, PreludeHasAssertFunction) {
    const auto* fn = find_function("std/prelude", "assert");
    ASSERT_NE(fn, nullptr);
    EXPECT_EQ(fn->name, "assert");
    EXPECT_EQ(fn->syscall_id, syscall_id::PRELUDE_ASSERT);
    ASSERT_EQ(fn->params.size(), 1);
    EXPECT_EQ(fn->params[0], StdlibType::Bool);
}

TEST_F(StdlibRegistryTest, PreludeHasLenFunction) {
    const auto* fn = find_function("std/prelude", "len");
    ASSERT_NE(fn, nullptr);
    EXPECT_EQ(fn->name, "len");
    EXPECT_EQ(fn->syscall_id, syscall_id::PRELUDE_LEN);
    EXPECT_EQ(fn->return_type, StdlibType::Int);
}

TEST_F(StdlibRegistryTest, MathHasSqrtFunction) {
    const auto* fn = find_function("std/math", "sqrt");
    ASSERT_NE(fn, nullptr);
    EXPECT_EQ(fn->name, "sqrt");
    EXPECT_EQ(fn->syscall_id, syscall_id::MATH_SQRT);
    EXPECT_EQ(fn->return_type, StdlibType::Float);
    EXPECT_TRUE(fn->is_pure);
    ASSERT_EQ(fn->params.size(), 1);
    EXPECT_EQ(fn->params[0], StdlibType::Float);
}

TEST_F(StdlibRegistryTest, MathHasPiConstant) {
    const auto* fn = find_function("std/math", "pi");
    ASSERT_NE(fn, nullptr);
    EXPECT_EQ(fn->name, "pi");
    EXPECT_EQ(fn->syscall_id, 0);  // Inline constant
    EXPECT_EQ(fn->return_type, StdlibType::Float);
    EXPECT_TRUE(fn->params.empty());
}

TEST_F(StdlibRegistryTest, StringHasConcatFunction) {
    const auto* fn = find_function("std/string", "concat");
    ASSERT_NE(fn, nullptr);
    EXPECT_EQ(fn->name, "concat");
    EXPECT_EQ(fn->syscall_id, syscall_id::STRING_CONCAT);
    EXPECT_EQ(fn->return_type, StdlibType::String);
}

TEST_F(StdlibRegistryTest, IoHasFileReadFunction) {
    const auto* fn = find_function("std/io", "file_read");
    ASSERT_NE(fn, nullptr);
    EXPECT_EQ(fn->name, "file_read");
    EXPECT_EQ(fn->syscall_id, syscall_id::IO_FILE_READ);
    EXPECT_EQ(fn->return_type, StdlibType::String);
}

TEST_F(StdlibRegistryTest, CryptoHasBlake3Function) {
    const auto* fn = find_function("std/crypto", "hash_blake3");
    ASSERT_NE(fn, nullptr);
    EXPECT_EQ(fn->name, "hash_blake3");
    EXPECT_EQ(fn->syscall_id, syscall_id::CRYPTO_HASH_BLAKE3);
    EXPECT_EQ(fn->return_type, StdlibType::String);
}

TEST_F(StdlibRegistryTest, NetHasHttpGetFunction) {
    const auto* fn = find_function("std/net", "http_get");
    ASSERT_NE(fn, nullptr);
    EXPECT_EQ(fn->name, "http_get");
    EXPECT_EQ(fn->syscall_id, syscall_id::NET_HTTP_GET);
    EXPECT_EQ(fn->return_type, StdlibType::String);
}

TEST_F(StdlibRegistryTest, UnknownFunctionReturnsNull) {
    const auto* fn = find_function("std/prelude", "nonexistent");
    EXPECT_EQ(fn, nullptr);
}

TEST_F(StdlibRegistryTest, FunctionInUnknownModuleReturnsNull) {
    const auto* fn = find_function("std/nonexistent", "print");
    EXPECT_EQ(fn, nullptr);
}

// ============================================================================
// Module Listing Tests
// ============================================================================

TEST_F(StdlibRegistryTest, ModulePathsReturnsAllModules) {
    auto paths = registry.module_paths();
    EXPECT_GE(paths.size(), 10);  // At least 10 modules

    // Verify all expected modules are present
    std::vector<std::string> expected = {
        "std/prelude", "std/math", "std/string", "std/collections", "std/io",
        "std/crypto",  "std/net",  "std/time",   "std/async",       "std/control"};

    for (const auto& path : expected) {
        auto it = std::find(paths.begin(), paths.end(), path);
        EXPECT_NE(it, paths.end()) << "Module " << path << " not found in module_paths()";
    }
}

TEST_F(StdlibRegistryTest, AutoImportModulesReturnsPrelude) {
    auto auto_imports = registry.auto_import_modules();
    EXPECT_FALSE(auto_imports.empty());

    // Prelude should be in auto-imports
    auto it = std::find_if(auto_imports.begin(), auto_imports.end(),
                           [](const ModuleDef* m) { return m->path == "std/prelude"; });
    EXPECT_NE(it, auto_imports.end()) << "std/prelude should be in auto_import_modules()";
}

// ============================================================================
// Syscall ID Tests
// ============================================================================

TEST_F(StdlibRegistryTest, SyscallIdsAreUnique) {
    std::set<std::uint16_t> seen_ids;
    auto paths = registry.module_paths();

    for (const auto& path : paths) {
        const auto* module = registry.find_module(path);
        ASSERT_NE(module, nullptr);

        for (const auto& fn : module->functions) {
            if (fn.syscall_id != 0) {  // Skip inline functions
                auto [it, inserted] = seen_ids.insert(fn.syscall_id);
                EXPECT_TRUE(inserted) << "Duplicate syscall ID: " << fn.syscall_id
                                      << " for function " << fn.name << " in " << module->path;
            }
        }
    }
}

TEST_F(StdlibRegistryTest, SyscallIdsFollowModuleRanges) {
    // Verify syscall IDs are within expected module ranges
    auto verify_range = [this](std::string_view path, std::uint16_t min, std::uint16_t max) {
        const auto* module = registry.find_module(path);
        ASSERT_NE(module, nullptr) << "Module " << path << " not found";

        for (const auto& fn : module->functions) {
            if (fn.syscall_id != 0) {
                EXPECT_GE(fn.syscall_id, min)
                    << "Function " << fn.name << " in " << path << " has syscall_id below range";
                EXPECT_LE(fn.syscall_id, max)
                    << "Function " << fn.name << " in " << path << " has syscall_id above range";
            }
        }
    };

    verify_range("std/prelude", 0x0001, 0x00FF);
    verify_range("std/io", 0x0100, 0x01FF);
    verify_range("std/crypto", 0x0200, 0x02FF);
    verify_range("std/net", 0x0300, 0x03FF);
    verify_range("std/time", 0x0400, 0x04FF);
    verify_range("std/collections", 0x0500, 0x05FF);
    verify_range("std/string", 0x0600, 0x06FF);
    verify_range("std/math", 0x0700, 0x07FF);
    verify_range("std/async", 0x0800, 0x08FF);
    verify_range("std/control", 0x0900, 0x09FF);
}

TEST_F(StdlibRegistryTest, FindBySyscallIdWorks) {
    // Look up print function by syscall ID
    const auto* fn = registry.find_by_syscall_id(syscall_id::PRELUDE_PRINT);
    ASSERT_NE(fn, nullptr);
    EXPECT_EQ(fn->name, "print");

    // Look up math.sqrt by syscall ID
    const auto* sqrt_fn = registry.find_by_syscall_id(syscall_id::MATH_SQRT);
    ASSERT_NE(sqrt_fn, nullptr);
    EXPECT_EQ(sqrt_fn->name, "sqrt");

    // Unknown syscall ID returns nullptr
    const auto* unknown = registry.find_by_syscall_id(0xFFFF);
    EXPECT_EQ(unknown, nullptr);
}

// ============================================================================
// Qualified Call Resolution Tests
// ============================================================================

TEST_F(StdlibRegistryTest, ResolveQualifiedCallMathSqrt) {
    std::unordered_map<std::string, std::string> imports = {{"math", "std/math"}};

    const auto* fn = registry.resolve_qualified_call("math", "sqrt", imports);
    ASSERT_NE(fn, nullptr);
    EXPECT_EQ(fn->name, "sqrt");
    EXPECT_EQ(fn->syscall_id, syscall_id::MATH_SQRT);
}

TEST_F(StdlibRegistryTest, ResolveQualifiedCallStringUpper) {
    std::unordered_map<std::string, std::string> imports = {{"string", "std/string"}};

    const auto* fn = registry.resolve_qualified_call("string", "upper", imports);
    ASSERT_NE(fn, nullptr);
    EXPECT_EQ(fn->name, "upper");
}

TEST_F(StdlibRegistryTest, ResolveQualifiedCallUnknownQualifier) {
    std::unordered_map<std::string, std::string> imports = {{"math", "std/math"}};

    const auto* fn = registry.resolve_qualified_call("unknown", "sqrt", imports);
    EXPECT_EQ(fn, nullptr);
}

TEST_F(StdlibRegistryTest, ResolveQualifiedCallUnknownFunction) {
    std::unordered_map<std::string, std::string> imports = {{"math", "std/math"}};

    const auto* fn = registry.resolve_qualified_call("math", "nonexistent", imports);
    EXPECT_EQ(fn, nullptr);
}
