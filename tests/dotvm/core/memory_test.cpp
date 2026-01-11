#include <dotvm/core/memory.hpp>
#include <dotvm/core/value.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <vector>

namespace dotvm::core {
namespace {

// ============================================================================
// Configuration Tests
// ============================================================================

TEST(MemoryConfigTest, PageSizeConstants) {
    EXPECT_EQ(mem_config::PAGE_SIZE, 4096);
    EXPECT_EQ(mem_config::PAGE_SHIFT, 12);
    EXPECT_EQ(mem_config::PAGE_MASK, 4095);
    EXPECT_EQ(1ULL << mem_config::PAGE_SHIFT, mem_config::PAGE_SIZE);
}

TEST(MemoryConfigTest, AllocationLimits) {
    EXPECT_EQ(mem_config::MAX_ALLOCATION_SIZE, 64 * 1024 * 1024);
    EXPECT_EQ(mem_config::MIN_ALLOCATION_SIZE, mem_config::PAGE_SIZE);
}

TEST(MemoryConfigTest, HandleConstants) {
    EXPECT_EQ(mem_config::INVALID_INDEX, 0xFFFF'FFFFU);
    EXPECT_EQ(mem_config::INITIAL_GENERATION, 1);
    EXPECT_EQ(mem_config::MAX_GENERATION, 0xFFFFU);
}

TEST(MemoryConfigTest, AlignToPage) {
    EXPECT_EQ(align_to_page(0), 0);
    EXPECT_EQ(align_to_page(1), 4096);
    EXPECT_EQ(align_to_page(4095), 4096);
    EXPECT_EQ(align_to_page(4096), 4096);
    EXPECT_EQ(align_to_page(4097), 8192);
    EXPECT_EQ(align_to_page(8192), 8192);
}

TEST(MemoryConfigTest, IsPageAligned) {
    EXPECT_TRUE(is_page_aligned(0));
    EXPECT_TRUE(is_page_aligned(4096));
    EXPECT_TRUE(is_page_aligned(8192));
    EXPECT_FALSE(is_page_aligned(1));
    EXPECT_FALSE(is_page_aligned(4095));
    EXPECT_FALSE(is_page_aligned(4097));
}

TEST(MemoryConfigTest, IsValidAllocationSize) {
    EXPECT_FALSE(is_valid_allocation_size(0));
    EXPECT_TRUE(is_valid_allocation_size(1));
    EXPECT_TRUE(is_valid_allocation_size(4096));
    EXPECT_TRUE(is_valid_allocation_size(mem_config::MAX_ALLOCATION_SIZE));
    EXPECT_FALSE(is_valid_allocation_size(mem_config::MAX_ALLOCATION_SIZE + 1));
}

TEST(MemoryConfigTest, PagesForSize) {
    EXPECT_EQ(pages_for_size(0), 0);
    EXPECT_EQ(pages_for_size(1), 1);
    EXPECT_EQ(pages_for_size(4096), 1);
    EXPECT_EQ(pages_for_size(4097), 2);
    EXPECT_EQ(pages_for_size(8192), 2);
}

TEST(MemoryConfigTest, ConstexprFunctions) {
    // Verify functions are constexpr
    constexpr auto aligned = align_to_page(100);
    constexpr auto page_aligned = is_page_aligned(4096);
    constexpr auto valid_size = is_valid_allocation_size(1000);
    constexpr auto pages = pages_for_size(5000);

    EXPECT_EQ(aligned, 4096);
    EXPECT_TRUE(page_aligned);
    EXPECT_TRUE(valid_size);
    EXPECT_EQ(pages, 2);
}

// ============================================================================
// HandleEntry Tests
// ============================================================================

TEST(HandleEntryTest, SizeConstraint) {
    EXPECT_LE(sizeof(HandleEntry), 32);
}

TEST(HandleEntryTest, CreateActiveEntry) {
    int dummy = 42;
    auto entry = HandleEntry::create(&dummy, 4096, 5);

    EXPECT_EQ(entry.ptr, &dummy);
    EXPECT_EQ(entry.size, 4096);
    EXPECT_EQ(entry.generation, 5);
    EXPECT_TRUE(entry.is_active);
}

TEST(HandleEntryTest, CreateInactiveEntry) {
    auto entry = HandleEntry::inactive(10);

    EXPECT_EQ(entry.ptr, nullptr);
    EXPECT_EQ(entry.size, 0);
    EXPECT_EQ(entry.generation, 10);
    EXPECT_FALSE(entry.is_active);
}

TEST(HandleEntryTest, Equality) {
    int dummy = 42;
    auto entry1 = HandleEntry::create(&dummy, 4096, 1);
    auto entry2 = HandleEntry::create(&dummy, 4096, 1);
    auto entry3 = HandleEntry::create(&dummy, 4096, 2);

    EXPECT_EQ(entry1, entry2);
    EXPECT_NE(entry1, entry3);
}

// ============================================================================
// HandleTable Tests
// ============================================================================

class HandleTableTest : public ::testing::Test {
protected:
    HandleTable table;
};

TEST_F(HandleTableTest, InitialState) {
    EXPECT_EQ(table.capacity(), 0);
    EXPECT_EQ(table.free_count(), 0);
    EXPECT_EQ(table.active_count(), 0);
}

TEST_F(HandleTableTest, AllocateSlot) {
    auto index = table.allocate_slot();

    EXPECT_NE(index, mem_config::INVALID_INDEX);
    EXPECT_EQ(index, 0);
    EXPECT_EQ(table.capacity(), 1);
    EXPECT_EQ(table.active_count(), 1);
    EXPECT_EQ(table.free_count(), 0);
}

TEST_F(HandleTableTest, AllocateMultipleSlots) {
    auto idx0 = table.allocate_slot();
    auto idx1 = table.allocate_slot();
    auto idx2 = table.allocate_slot();

    EXPECT_EQ(idx0, 0);
    EXPECT_EQ(idx1, 1);
    EXPECT_EQ(idx2, 2);
    EXPECT_EQ(table.capacity(), 3);
    EXPECT_EQ(table.active_count(), 3);
}

TEST_F(HandleTableTest, ReleaseSlot) {
    auto index = table.allocate_slot();
    auto& entry = table[index];
    auto old_gen = entry.generation;

    table.release_slot(index);

    EXPECT_FALSE(entry.is_active);
    EXPECT_EQ(entry.ptr, nullptr);
    EXPECT_EQ(entry.size, 0);
    EXPECT_EQ(entry.generation, old_gen + 1);
    EXPECT_EQ(table.free_count(), 1);
    EXPECT_EQ(table.active_count(), 0);
}

TEST_F(HandleTableTest, ReuseSlot) {
    auto idx0 = table.allocate_slot();
    table[idx0].generation = 5;
    table.release_slot(idx0);

    auto idx1 = table.allocate_slot();

    EXPECT_EQ(idx1, idx0);  // Same slot reused
    EXPECT_EQ(table[idx1].generation, 6);  // Generation incremented
    EXPECT_TRUE(table[idx1].is_active);
}

TEST_F(HandleTableTest, ValidHandle) {
    auto index = table.allocate_slot();
    auto& entry = table[index];

    Handle valid{.index = index, .generation = entry.generation};
    Handle wrong_gen{.index = index, .generation = entry.generation + 1};
    Handle wrong_idx{.index = 999, .generation = 1};

    EXPECT_TRUE(table.is_valid_handle(valid));
    EXPECT_FALSE(table.is_valid_handle(wrong_gen));
    EXPECT_FALSE(table.is_valid_handle(wrong_idx));
}

TEST_F(HandleTableTest, GenerationWrapAround) {
    auto index = table.allocate_slot();
    table[index].generation = mem_config::MAX_GENERATION;

    table.release_slot(index);

    EXPECT_EQ(table[index].generation, mem_config::INITIAL_GENERATION);
}

// ============================================================================
// MemoryManager Tests - Fixture
// ============================================================================

class MemoryManagerTest : public ::testing::Test {
protected:
    MemoryManager mm;
};

// ============================================================================
// Allocation Tests
// ============================================================================

TEST_F(MemoryManagerTest, AllocateMinimumSize) {
    auto [h, err] = mm.allocate(1);

    EXPECT_EQ(err, MemoryError::Success);
    EXPECT_NE(h.index, mem_config::INVALID_INDEX);

    auto [size, size_err] = mm.get_size(h);
    EXPECT_EQ(size_err, MemoryError::Success);
    EXPECT_EQ(size, mem_config::PAGE_SIZE);  // Rounded up to page
}

TEST_F(MemoryManagerTest, AllocateExactPageSize) {
    auto [h, err] = mm.allocate(4096);

    EXPECT_EQ(err, MemoryError::Success);

    auto [size, size_err] = mm.get_size(h);
    EXPECT_EQ(size, 4096);
}

TEST_F(MemoryManagerTest, AllocateRoundsUpToPage) {
    auto [h, err] = mm.allocate(5000);

    EXPECT_EQ(err, MemoryError::Success);

    auto [size, size_err] = mm.get_size(h);
    EXPECT_EQ(size, 8192);  // Rounded up to 2 pages
}

TEST_F(MemoryManagerTest, AllocateZeroSizeFails) {
    auto [h, err] = mm.allocate(0);

    EXPECT_EQ(err, MemoryError::InvalidSize);
    EXPECT_EQ(h.index, mem_config::INVALID_INDEX);
}

TEST_F(MemoryManagerTest, AllocateTooLargeFails) {
    auto [h, err] = mm.allocate(mem_config::MAX_ALLOCATION_SIZE + 1);

    EXPECT_EQ(err, MemoryError::InvalidSize);
}

TEST_F(MemoryManagerTest, AllocateMaxSize) {
    auto [h, err] = mm.allocate(mem_config::MAX_ALLOCATION_SIZE);

    EXPECT_EQ(err, MemoryError::Success);

    (void)mm.deallocate(h);  // Clean up large allocation
}

TEST_F(MemoryManagerTest, AllocatedPtrIsPageAligned) {
    auto [h, err] = mm.allocate(4096);
    ASSERT_EQ(err, MemoryError::Success);

    auto [ptr, ptr_err] = mm.get_ptr(h);
    EXPECT_EQ(ptr_err, MemoryError::Success);
    EXPECT_TRUE(is_address_page_aligned(reinterpret_cast<std::uintptr_t>(ptr)));
}

TEST_F(MemoryManagerTest, AllocateMultipleHandles) {
    std::vector<Handle> handles;

    for (int i = 0; i < 10; ++i) {
        auto [h, err] = mm.allocate(4096);
        ASSERT_EQ(err, MemoryError::Success);
        handles.push_back(h);
    }

    EXPECT_EQ(mm.active_allocations(), 10);
    EXPECT_EQ(mm.total_allocated_bytes(), 10 * 4096);

    // All handles should be valid and have unique pointers
    std::vector<void*> ptrs;
    for (auto h : handles) {
        auto [ptr, err] = mm.get_ptr(h);
        EXPECT_EQ(err, MemoryError::Success);
        EXPECT_NE(ptr, nullptr);
        ptrs.push_back(ptr);
    }

    // Verify all pointers are unique
    std::sort(ptrs.begin(), ptrs.end());
    auto last = std::unique(ptrs.begin(), ptrs.end());
    EXPECT_EQ(last, ptrs.end());
}

// ============================================================================
// Deallocation Tests
// ============================================================================

TEST_F(MemoryManagerTest, DeallocateValidHandle) {
    auto [h, alloc_err] = mm.allocate(4096);
    ASSERT_EQ(alloc_err, MemoryError::Success);

    auto err = mm.deallocate(h);

    EXPECT_EQ(err, MemoryError::Success);
    EXPECT_EQ(mm.active_allocations(), 0);
    EXPECT_EQ(mm.total_allocated_bytes(), 0);
}

TEST_F(MemoryManagerTest, DeallocateInvalidHandleFails) {
    auto invalid = MemoryManager::invalid_handle();
    auto err = mm.deallocate(invalid);

    EXPECT_EQ(err, MemoryError::InvalidHandle);
}

TEST_F(MemoryManagerTest, DoubleDeallocateFails) {
    auto [h, alloc_err] = mm.allocate(4096);
    ASSERT_EQ(alloc_err, MemoryError::Success);

    auto err1 = mm.deallocate(h);
    EXPECT_EQ(err1, MemoryError::Success);

    auto err2 = mm.deallocate(h);
    EXPECT_EQ(err2, MemoryError::InvalidHandle);
}

TEST_F(MemoryManagerTest, DeallocateIncrementsGeneration) {
    auto [h1, err1] = mm.allocate(4096);
    ASSERT_EQ(err1, MemoryError::Success);
    auto gen1 = h1.generation;

    (void)mm.deallocate(h1);

    // Allocate again (reuses slot)
    auto [h2, err2] = mm.allocate(4096);
    ASSERT_EQ(err2, MemoryError::Success);

    // Should have same index but different generation
    EXPECT_EQ(h2.index, h1.index);
    EXPECT_NE(h2.generation, gen1);
}

// ============================================================================
// Generation Counter Tests
// ============================================================================

TEST_F(MemoryManagerTest, GenerationPreventsUseAfterFree) {
    auto [h1, err1] = mm.allocate(4096);
    ASSERT_EQ(err1, MemoryError::Success);

    // Write some data
    EXPECT_EQ(mm.write<int>(h1, 0, 42), MemoryError::Success);

    // Save old handle info
    Handle old_handle = h1;

    // Deallocate
    EXPECT_EQ(mm.deallocate(h1), MemoryError::Success);

    // Old handle should now be invalid
    EXPECT_FALSE(mm.is_valid(old_handle));

    // Attempts to use old handle should fail
    auto [val, read_err] = mm.read<int>(old_handle, 0);
    EXPECT_EQ(read_err, MemoryError::InvalidHandle);

    auto [ptr, ptr_err] = mm.get_ptr(old_handle);
    EXPECT_EQ(ptr_err, MemoryError::InvalidHandle);
}

TEST_F(MemoryManagerTest, OldHandleInvalidAfterReallocation) {
    auto [h1, err1] = mm.allocate(4096);
    ASSERT_EQ(err1, MemoryError::Success);
    Handle old = h1;

    (void)mm.deallocate(h1);

    auto [h2, err2] = mm.allocate(4096);
    ASSERT_EQ(err2, MemoryError::Success);

    // New handle is valid
    EXPECT_TRUE(mm.is_valid(h2));

    // Old handle is still invalid (even if same index)
    EXPECT_FALSE(mm.is_valid(old));
}

// ============================================================================
// Pointer Access Tests
// ============================================================================

TEST_F(MemoryManagerTest, GetPtrValidHandle) {
    auto [h, err] = mm.allocate(4096);
    ASSERT_EQ(err, MemoryError::Success);

    auto [ptr, ptr_err] = mm.get_ptr(h);

    EXPECT_EQ(ptr_err, MemoryError::Success);
    EXPECT_NE(ptr, nullptr);
}

TEST_F(MemoryManagerTest, GetPtrInvalidHandleFails) {
    auto [ptr, err] = mm.get_ptr(MemoryManager::invalid_handle());

    EXPECT_EQ(err, MemoryError::InvalidHandle);
    EXPECT_EQ(ptr, nullptr);
}

TEST_F(MemoryManagerTest, GetPtrConstCorrectness) {
    auto [h, err] = mm.allocate(4096);
    ASSERT_EQ(err, MemoryError::Success);

    const MemoryManager& const_mm = mm;
    auto [ptr, ptr_err] = const_mm.get_ptr(h);

    EXPECT_EQ(ptr_err, MemoryError::Success);
    EXPECT_NE(ptr, nullptr);
}

// ============================================================================
// Read/Write Tests
// ============================================================================

TEST_F(MemoryManagerTest, ReadWriteInt) {
    auto [h, err] = mm.allocate(4096);
    ASSERT_EQ(err, MemoryError::Success);

    EXPECT_EQ(mm.write<int>(h, 0, 12345), MemoryError::Success);

    auto [val, read_err] = mm.read<int>(h, 0);
    EXPECT_EQ(read_err, MemoryError::Success);
    EXPECT_EQ(val, 12345);
}

TEST_F(MemoryManagerTest, ReadWriteDouble) {
    auto [h, err] = mm.allocate(4096);
    ASSERT_EQ(err, MemoryError::Success);

    EXPECT_EQ(mm.write<double>(h, 0, 3.14159), MemoryError::Success);

    auto [val, read_err] = mm.read<double>(h, 0);
    EXPECT_EQ(read_err, MemoryError::Success);
    EXPECT_DOUBLE_EQ(val, 3.14159);
}

TEST_F(MemoryManagerTest, ReadWriteStruct) {
    struct Point {
        int x, y, z;
        bool operator==(const Point&) const = default;
    };

    auto [h, err] = mm.allocate(4096);
    ASSERT_EQ(err, MemoryError::Success);

    Point p{10, 20, 30};
    EXPECT_EQ(mm.write<Point>(h, 0, p), MemoryError::Success);

    auto [val, read_err] = mm.read<Point>(h, 0);
    EXPECT_EQ(read_err, MemoryError::Success);
    EXPECT_EQ(val, p);
}

TEST_F(MemoryManagerTest, ReadWriteAtOffset) {
    auto [h, err] = mm.allocate(4096);
    ASSERT_EQ(err, MemoryError::Success);

    // Write at different offsets
    EXPECT_EQ(mm.write<int>(h, 0, 100), MemoryError::Success);
    EXPECT_EQ(mm.write<int>(h, 100, 200), MemoryError::Success);
    EXPECT_EQ(mm.write<int>(h, 1000, 300), MemoryError::Success);

    // Read back
    auto [v1, e1] = mm.read<int>(h, 0);
    auto [v2, e2] = mm.read<int>(h, 100);
    auto [v3, e3] = mm.read<int>(h, 1000);

    EXPECT_EQ(v1, 100);
    EXPECT_EQ(v2, 200);
    EXPECT_EQ(v3, 300);
}

TEST_F(MemoryManagerTest, ReadWriteAtBoundary) {
    auto [h, err] = mm.allocate(4096);
    ASSERT_EQ(err, MemoryError::Success);

    // Write at the last valid position for an int
    std::size_t offset = 4096 - sizeof(int);
    EXPECT_EQ(mm.write<int>(h, offset, 999), MemoryError::Success);

    auto [val, read_err] = mm.read<int>(h, offset);
    EXPECT_EQ(read_err, MemoryError::Success);
    EXPECT_EQ(val, 999);
}

TEST_F(MemoryManagerTest, ReadOutOfBoundsFails) {
    auto [h, err] = mm.allocate(4096);
    ASSERT_EQ(err, MemoryError::Success);

    // Try to read past the end
    auto [val, read_err] = mm.read<int>(h, 4096);
    EXPECT_EQ(read_err, MemoryError::BoundsViolation);

    // Try to read with offset that would overflow
    auto [val2, read_err2] = mm.read<int>(h, 4093);  // 4093 + 4 > 4096
    EXPECT_EQ(read_err2, MemoryError::BoundsViolation);
}

TEST_F(MemoryManagerTest, WriteOutOfBoundsFails) {
    auto [h, err] = mm.allocate(4096);
    ASSERT_EQ(err, MemoryError::Success);

    EXPECT_EQ(mm.write<int>(h, 4096, 123), MemoryError::BoundsViolation);
    EXPECT_EQ(mm.write<int>(h, 4093, 123), MemoryError::BoundsViolation);
}

// ============================================================================
// Bulk Operations Tests
// ============================================================================

TEST_F(MemoryManagerTest, ReadWriteBytes) {
    auto [h, err] = mm.allocate(4096);
    ASSERT_EQ(err, MemoryError::Success);

    std::array<char, 100> src;
    for (std::size_t i = 0; i < 100; ++i) src[i] = static_cast<char>(i);

    EXPECT_EQ(mm.write_bytes(h, 0, src.data(), src.size()), MemoryError::Success);

    std::array<char, 100> dst{};
    EXPECT_EQ(mm.read_bytes(h, 0, dst.data(), dst.size()), MemoryError::Success);

    EXPECT_EQ(src, dst);
}

TEST_F(MemoryManagerTest, ReadWriteBytesPartial) {
    auto [h, err] = mm.allocate(4096);
    ASSERT_EQ(err, MemoryError::Success);

    std::array<char, 50> src;
    for (std::size_t i = 0; i < 50; ++i) src[i] = static_cast<char>(i + 10);

    // Write at offset 100
    EXPECT_EQ(mm.write_bytes(h, 100, src.data(), src.size()), MemoryError::Success);

    std::array<char, 50> dst{};
    EXPECT_EQ(mm.read_bytes(h, 100, dst.data(), dst.size()), MemoryError::Success);

    EXPECT_EQ(src, dst);
}

TEST_F(MemoryManagerTest, ReadWriteBytesBoundsCheck) {
    auto [h, err] = mm.allocate(4096);
    ASSERT_EQ(err, MemoryError::Success);

    std::array<char, 100> buf{};

    // Try to write past the end
    EXPECT_EQ(mm.write_bytes(h, 4000, buf.data(), 100), MemoryError::BoundsViolation);
    EXPECT_EQ(mm.read_bytes(h, 4000, buf.data(), 100), MemoryError::BoundsViolation);

    // Null/zero operations should succeed
    EXPECT_EQ(mm.write_bytes(h, 0, nullptr, 0), MemoryError::Success);
    EXPECT_EQ(mm.read_bytes(h, 0, nullptr, 0), MemoryError::Success);
}

// ============================================================================
// Statistics Tests
// ============================================================================

TEST_F(MemoryManagerTest, ActiveAllocationsCount) {
    EXPECT_EQ(mm.active_allocations(), 0);

    auto [h1, e1] = mm.allocate(4096);
    EXPECT_EQ(mm.active_allocations(), 1);

    auto [h2, e2] = mm.allocate(4096);
    EXPECT_EQ(mm.active_allocations(), 2);

    (void)mm.deallocate(h1);
    EXPECT_EQ(mm.active_allocations(), 1);

    (void)mm.deallocate(h2);
    EXPECT_EQ(mm.active_allocations(), 0);
}

TEST_F(MemoryManagerTest, TotalAllocatedBytes) {
    EXPECT_EQ(mm.total_allocated_bytes(), 0);

    auto [h1, e1] = mm.allocate(1000);  // Rounds to 4096
    EXPECT_EQ(mm.total_allocated_bytes(), 4096);

    auto [h2, e2] = mm.allocate(5000);  // Rounds to 8192
    (void)e2;  // Suppress unused warning
    EXPECT_EQ(mm.total_allocated_bytes(), 4096 + 8192);

    (void)mm.deallocate(h1);
    EXPECT_EQ(mm.total_allocated_bytes(), 8192);
}

TEST_F(MemoryManagerTest, MaxAllocationSize) {
    EXPECT_EQ(mm.max_allocation_size(), mem_config::MAX_ALLOCATION_SIZE);

    MemoryManager custom_mm(1024 * 1024);  // 1MB limit
    EXPECT_EQ(custom_mm.max_allocation_size(), 1024 * 1024);

    // Should reject allocations over custom limit
    auto [h, err] = custom_mm.allocate(2 * 1024 * 1024);
    EXPECT_EQ(err, MemoryError::InvalidSize);
}

// ============================================================================
// Free List Tests
// ============================================================================

TEST_F(MemoryManagerTest, FreeListReusesSlots) {
    // Allocate and deallocate to build up free list
    std::vector<Handle> handles;
    for (int i = 0; i < 5; ++i) {
        auto [h, err] = mm.allocate(4096);
        ASSERT_EQ(err, MemoryError::Success);
        handles.push_back(h);
    }

    // Track indices before deallocation
    std::vector<std::uint32_t> indices;
    for (auto h : handles) {
        indices.push_back(h.index);
    }

    // Deallocate all
    for (auto h : handles) {
        (void)mm.deallocate(h);
    }

    // Re-allocate - should reuse same indices (in LIFO order)
    for (std::size_t i = 5; i > 0; --i) {
        auto [h, err] = mm.allocate(4096);
        ASSERT_EQ(err, MemoryError::Success);
        EXPECT_EQ(h.index, indices[i - 1]);
    }
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST_F(MemoryManagerTest, AllocateDeallocateCycle) {
    for (int cycle = 0; cycle < 100; ++cycle) {
        auto [h, err] = mm.allocate(4096);
        ASSERT_EQ(err, MemoryError::Success);

        EXPECT_EQ(mm.write<int>(h, 0, cycle), MemoryError::Success);

        auto [val, read_err] = mm.read<int>(h, 0);
        EXPECT_EQ(val, cycle);

        EXPECT_EQ(mm.deallocate(h), MemoryError::Success);
    }

    EXPECT_EQ(mm.active_allocations(), 0);
    EXPECT_EQ(mm.total_allocated_bytes(), 0);
}

TEST_F(MemoryManagerTest, ManySmallAllocations) {
    constexpr std::size_t count = 100;
    std::vector<Handle> handles;

    for (std::size_t i = 0; i < count; ++i) {
        auto [h, err] = mm.allocate(4096);
        ASSERT_EQ(err, MemoryError::Success);
        handles.push_back(h);

        // Write identifier
        EXPECT_EQ(mm.write<int>(h, 0, static_cast<int>(i)), MemoryError::Success);
    }

    // Verify all
    for (std::size_t i = 0; i < count; ++i) {
        auto [val, err] = mm.read<int>(handles[i], 0);
        EXPECT_EQ(val, static_cast<int>(i));
    }

    // Clean up
    for (auto h : handles) {
        (void)mm.deallocate(h);
    }
}

TEST_F(MemoryManagerTest, AlternatingAllocDealloc) {
    Handle h1, h2;

    for (int i = 0; i < 50; ++i) {
        auto [ha, ea] = mm.allocate(4096);
        ASSERT_EQ(ea, MemoryError::Success);
        h1 = ha;

        auto [hb, eb] = mm.allocate(4096);
        ASSERT_EQ(eb, MemoryError::Success);
        h2 = hb;

        (void)mm.deallocate(h1);
        // h2 still valid
        EXPECT_TRUE(mm.is_valid(h2));

        (void)mm.deallocate(h2);
    }
}

// ============================================================================
// Integration with Value/Handle
// ============================================================================

TEST_F(MemoryManagerTest, HandleStoredInValue) {
    auto [h, err] = mm.allocate(4096);
    ASSERT_EQ(err, MemoryError::Success);

    // Store handle in a Value
    Value v = Value::from_handle(h);

    EXPECT_TRUE(v.is_handle());
    EXPECT_EQ(v.type(), ValueType::Handle);

    // Extract and verify
    Handle extracted = v.as_handle();
    EXPECT_EQ(extracted.index, h.index);
    // Note: generation is truncated to 16 bits in Value encoding
    EXPECT_EQ(extracted.generation & 0xFFFF, h.generation & 0xFFFF);

    // Should still be usable
    EXPECT_TRUE(mm.is_valid(extracted));
}

TEST_F(MemoryManagerTest, HandleFromValueRoundTrip) {
    auto [h, err] = mm.allocate(4096);
    ASSERT_EQ(err, MemoryError::Success);

    // Write data using original handle
    EXPECT_EQ(mm.write<int>(h, 0, 999), MemoryError::Success);

    // Round-trip through Value
    Value v = Value::from_handle(h);
    Handle h2 = v.as_handle();

    // Read data using extracted handle
    auto [val, read_err] = mm.read<int>(h2, 0);
    EXPECT_EQ(read_err, MemoryError::Success);
    EXPECT_EQ(val, 999);
}

TEST_F(MemoryManagerTest, InvalidHandleConstant) {
    Handle invalid = MemoryManager::invalid_handle();

    EXPECT_EQ(invalid.index, mem_config::INVALID_INDEX);
    EXPECT_EQ(invalid.generation, 0);
    EXPECT_FALSE(mm.is_valid(invalid));
}

// ============================================================================
// Destructor Test
// ============================================================================

TEST(MemoryManagerDestructorTest, FreesAllAllocations) {
    // This test verifies no memory leaks (run with sanitizers)
    {
        MemoryManager mm;

        for (int i = 0; i < 10; ++i) {
            auto [h, err] = mm.allocate(4096);
            ASSERT_EQ(err, MemoryError::Success);
            // Don't deallocate - destructor should handle it
        }

        EXPECT_EQ(mm.active_allocations(), 10);
        // mm goes out of scope here
    }
    // If sanitizers are enabled, they would catch leaks here
}

} // namespace
} // namespace dotvm::core
