// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2025 DotLanth Project

#include <gtest/gtest.h>
#include "dotvm/jit/jit_cache.hpp"

#include <array>
#include <vector>

namespace dotvm::jit {
namespace {

class JITCacheTest : public ::testing::Test {
protected:
    // Small cache for testing eviction
    static constexpr std::size_t SMALL_CACHE_SIZE = 64 * 1024;  // 64 KB

    void SetUp() override {
        // Nothing to set up
    }

    void TearDown() override {
        // Nothing to tear down
    }

    // Create test code bytes
    static auto make_test_code(std::size_t size, std::uint8_t fill = 0xCC)
        -> std::vector<std::uint8_t> {
        return std::vector<std::uint8_t>(size, fill);
    }
};

// ============================================================================
// ExecutableMemory Tests
// ============================================================================

TEST_F(JITCacheTest, ExecutableMemoryAllocate) {
    auto alloc = ExecutableMemory::allocate(4096);

#ifdef __linux__
    EXPECT_TRUE(alloc.is_valid());
    EXPECT_NE(alloc.ptr, nullptr);
    EXPECT_GE(alloc.size, 4096u);

    // Should be page-aligned
    EXPECT_EQ(reinterpret_cast<std::uintptr_t>(alloc.ptr) % ExecutableMemory::page_size(), 0u);

    ExecutableMemory::deallocate(alloc.ptr, alloc.size);
#else
    // On unsupported platforms, allocation should fail gracefully
    EXPECT_FALSE(alloc.is_valid());
#endif
}

TEST_F(JITCacheTest, ExecutableMemoryMakeExecutable) {
    auto alloc = ExecutableMemory::allocate(4096);

#ifdef __linux__
    ASSERT_TRUE(alloc.is_valid());

    // Write some code
    std::memset(alloc.ptr, 0x90, 4096);  // NOP sled

    // Make executable
    EXPECT_TRUE(ExecutableMemory::make_executable(alloc.ptr, alloc.size));

    ExecutableMemory::deallocate(alloc.ptr, alloc.size);
#else
    EXPECT_FALSE(alloc.is_valid());
#endif
}

TEST_F(JITCacheTest, ExecutableMemoryMakeWritable) {
    auto alloc = ExecutableMemory::allocate(4096);

#ifdef __linux__
    ASSERT_TRUE(alloc.is_valid());

    // Make executable first
    EXPECT_TRUE(ExecutableMemory::make_executable(alloc.ptr, alloc.size));

    // Then make writable again
    EXPECT_TRUE(ExecutableMemory::make_writable(alloc.ptr, alloc.size));

    // Should be able to write
    std::memset(alloc.ptr, 0xCC, 4096);

    ExecutableMemory::deallocate(alloc.ptr, alloc.size);
#else
    EXPECT_FALSE(alloc.is_valid());
#endif
}

TEST_F(JITCacheTest, ExecutableMemoryPageSize) {
    auto page_size = ExecutableMemory::page_size();
    EXPECT_GT(page_size, 0u);
    // Common page sizes are 4KB or larger
    EXPECT_GE(page_size, 4096u);
}

// ============================================================================
// JITCache Basic Operations
// ============================================================================

TEST_F(JITCacheTest, DefaultConstruction) {
    JITCache cache;
    EXPECT_EQ(cache.entry_count(), 0u);
    EXPECT_EQ(cache.total_allocated(), 0u);
    EXPECT_EQ(cache.max_size(), JITCache::DEFAULT_MAX_SIZE);
}

TEST_F(JITCacheTest, CustomMaxSize) {
    JITCache cache(SMALL_CACHE_SIZE);
    EXPECT_EQ(cache.max_size(), SMALL_CACHE_SIZE);
}

TEST_F(JITCacheTest, InsertAndLookup) {
#ifdef __linux__
    JITCache cache;

    auto code = make_test_code(256);
    auto ptr = cache.insert(0x1000, code, 0x1000, 0x1010);

    ASSERT_NE(ptr, nullptr);
    EXPECT_EQ(cache.entry_count(), 1u);
    EXPECT_GT(cache.total_allocated(), 0u);

    // Lookup should return the same pointer
    auto found = cache.lookup(0x1000);
    EXPECT_EQ(found, ptr);
#else
    GTEST_SKIP() << "JIT not supported on this platform";
#endif
}

TEST_F(JITCacheTest, LookupMiss) {
    JITCache cache;

    auto found = cache.lookup(0x9999);
    EXPECT_EQ(found, nullptr);
}

TEST_F(JITCacheTest, Contains) {
#ifdef __linux__
    JITCache cache;

    EXPECT_FALSE(cache.contains(0x1000));

    auto code = make_test_code(256);
    (void)cache.insert(0x1000, code, 0x1000, 0x1010);

    EXPECT_TRUE(cache.contains(0x1000));
    EXPECT_FALSE(cache.contains(0x2000));
#else
    GTEST_SKIP() << "JIT not supported on this platform";
#endif
}

TEST_F(JITCacheTest, GetEntry) {
#ifdef __linux__
    JITCache cache;

    auto code = make_test_code(512);
    (void)cache.insert(0x2000, code, 0x2000, 0x2020);

    auto entry = cache.get_entry(0x2000);
    ASSERT_TRUE(entry.has_value());
    EXPECT_EQ(entry->function_id, 0x2000u);
    EXPECT_EQ(entry->bytecode_start, 0x2000u);
    EXPECT_EQ(entry->bytecode_end, 0x2020u);
    EXPECT_NE(entry->code_ptr, nullptr);
    EXPECT_GT(entry->code_size, 0u);

    // Non-existent entry
    auto missing = cache.get_entry(0x9999);
    EXPECT_FALSE(missing.has_value());
#else
    GTEST_SKIP() << "JIT not supported on this platform";
#endif
}

TEST_F(JITCacheTest, Remove) {
#ifdef __linux__
    JITCache cache;

    auto code = make_test_code(256);
    (void)cache.insert(0x1000, code, 0x1000, 0x1010);

    EXPECT_TRUE(cache.contains(0x1000));
    EXPECT_EQ(cache.entry_count(), 1u);

    EXPECT_TRUE(cache.remove(0x1000));
    EXPECT_FALSE(cache.contains(0x1000));
    EXPECT_EQ(cache.entry_count(), 0u);

    // Remove non-existent
    EXPECT_FALSE(cache.remove(0x9999));
#else
    GTEST_SKIP() << "JIT not supported on this platform";
#endif
}

TEST_F(JITCacheTest, Clear) {
#ifdef __linux__
    JITCache cache;

    auto code = make_test_code(256);
    (void)cache.insert(0x1000, code, 0x1000, 0x1010);
    (void)cache.insert(0x2000, code, 0x2000, 0x2010);
    (void)cache.insert(0x3000, code, 0x3000, 0x3010);

    EXPECT_EQ(cache.entry_count(), 3u);

    cache.clear();

    EXPECT_EQ(cache.entry_count(), 0u);
    EXPECT_EQ(cache.total_allocated(), 0u);
    EXPECT_FALSE(cache.contains(0x1000));
    EXPECT_FALSE(cache.contains(0x2000));
    EXPECT_FALSE(cache.contains(0x3000));
#else
    GTEST_SKIP() << "JIT not supported on this platform";
#endif
}

// ============================================================================
// JITCache Duplicate Insert
// ============================================================================

TEST_F(JITCacheTest, DuplicateInsert) {
#ifdef __linux__
    JITCache cache;

    auto code1 = make_test_code(256, 0xAA);
    auto code2 = make_test_code(256, 0xBB);

    auto ptr1 = cache.insert(0x1000, code1, 0x1000, 0x1010);
    auto ptr2 = cache.insert(0x1000, code2, 0x1000, 0x1010);

    // Should return same pointer (no re-insert)
    EXPECT_EQ(ptr1, ptr2);
    EXPECT_EQ(cache.entry_count(), 1u);
#else
    GTEST_SKIP() << "JIT not supported on this platform";
#endif
}

// ============================================================================
// JITCache LRU and Eviction
// ============================================================================

TEST_F(JITCacheTest, LRUTracking) {
#ifdef __linux__
    JITCache cache;

    auto code = make_test_code(256);
    (void)cache.insert(0x1000, code, 0x1000, 0x1010);
    (void)cache.insert(0x2000, code, 0x2000, 0x2010);
    (void)cache.insert(0x3000, code, 0x3000, 0x3010);

    // Access 0x1000 to move it to front of LRU
    (void)cache.lookup(0x1000);

    // Check execution count
    auto entry = cache.get_entry(0x1000);
    ASSERT_TRUE(entry.has_value());
    EXPECT_EQ(entry->execution_count, 1u);

    // Access again
    (void)cache.lookup(0x1000);
    entry = cache.get_entry(0x1000);
    EXPECT_EQ(entry->execution_count, 2u);
#else
    GTEST_SKIP() << "JIT not supported on this platform";
#endif
}

TEST_F(JITCacheTest, EvictionOnFull) {
#ifdef __linux__
    // Use a small cache to force eviction
    JITCache cache(16 * 1024);  // 16 KB

    // Fill with 4KB entries
    for (std::size_t i = 0; i < 10; ++i) {
        auto code = make_test_code(4096);
        (void)cache.insert(i * 0x1000, code, i * 0x1000, i * 0x1000 + 0x10);
    }

    // Some entries should have been evicted
    EXPECT_LE(cache.total_allocated(), cache.max_size());
    EXPECT_GT(cache.entry_count(), 0u);
    EXPECT_LE(cache.entry_count(), 10u);
#else
    GTEST_SKIP() << "JIT not supported on this platform";
#endif
}

TEST_F(JITCacheTest, TickClock) {
#ifdef __linux__
    JITCache cache;

    auto code = make_test_code(256);
    (void)cache.insert(0x1000, code, 0x1000, 0x1010);

    auto entry_before = cache.get_entry(0x1000);
    ASSERT_TRUE(entry_before.has_value());
    EXPECT_EQ(entry_before->age, 0u);

    cache.tick_clock(10);

    auto entry_after = cache.get_entry(0x1000);
    ASSERT_TRUE(entry_after.has_value());
    EXPECT_EQ(entry_after->age, 10u);
#else
    GTEST_SKIP() << "JIT not supported on this platform";
#endif
}

// ============================================================================
// JITCache Utilization
// ============================================================================

TEST_F(JITCacheTest, Utilization) {
#ifdef __linux__
    JITCache cache(100 * 1024);  // 100 KB

    EXPECT_DOUBLE_EQ(cache.utilization(), 0.0);

    auto code = make_test_code(4096);
    (void)cache.insert(0x1000, code, 0x1000, 0x1010);

    // Utilization should be approximately 4%
    EXPECT_GT(cache.utilization(), 0.0);
    EXPECT_LT(cache.utilization(), 100.0);
#else
    GTEST_SKIP() << "JIT not supported on this platform";
#endif
}

// ============================================================================
// JITCache Stress Test
// ============================================================================

TEST_F(JITCacheTest, StressTest) {
#ifdef __linux__
    JITCache cache(1024 * 1024);  // 1 MB

    // Insert many entries
    for (std::size_t i = 0; i < 100; ++i) {
        auto code = make_test_code(1024 + (i % 10) * 100);
        (void)cache.insert(i, code, i, i + 1);
    }

    // Random lookups
    for (std::size_t i = 0; i < 1000; ++i) {
        (void)cache.lookup(i % 100);
    }

    // Some entries should still be present
    EXPECT_GT(cache.entry_count(), 0u);
    EXPECT_LE(cache.total_allocated(), cache.max_size());
#else
    GTEST_SKIP() << "JIT not supported on this platform";
#endif
}

// ============================================================================
// CodeAllocation Tests
// ============================================================================

TEST(CodeAllocationTest, DefaultState) {
    CodeAllocation alloc;
    EXPECT_FALSE(alloc.is_valid());
    EXPECT_EQ(alloc.ptr, nullptr);
    EXPECT_EQ(alloc.size, 0u);
}

TEST(CodeAllocationTest, ValidAllocation) {
    CodeAllocation alloc{reinterpret_cast<void*>(0x1000), 4096};
    EXPECT_TRUE(alloc.is_valid());
}

TEST(CodeAllocationTest, ZeroSize) {
    CodeAllocation alloc{reinterpret_cast<void*>(0x1000), 0};
    EXPECT_FALSE(alloc.is_valid());
}

// ============================================================================
// CacheEntry Tests
// ============================================================================

TEST(CacheEntryTest, DefaultState) {
    CacheEntry entry;
    EXPECT_EQ(entry.function_id, 0u);
    EXPECT_EQ(entry.code_ptr, nullptr);
    EXPECT_EQ(entry.code_size, 0u);
    EXPECT_EQ(entry.execution_count, 0u);
    EXPECT_EQ(entry.age, 0u);
}

}  // namespace
}  // namespace dotvm::jit
