/// @file jit_code_buffer_test.cpp
/// @brief Unit tests for JIT code buffer

#include <cstring>

#include <gtest/gtest.h>

#include "dotvm/jit/jit_code_buffer.hpp"

namespace dotvm::jit {
namespace {

// ============================================================================
// JitCodeBuffer Construction Tests
// ============================================================================

class JitCodeBufferConstructionTest : public ::testing::Test {};

TEST_F(JitCodeBufferConstructionTest, Create_ValidSize_Succeeds) {
    auto result = JitCodeBuffer::create(4096);
    ASSERT_TRUE(result.has_value());
    EXPECT_GE(result->capacity(), 4096u);
}

TEST_F(JitCodeBufferConstructionTest, Create_ZeroSize_Fails) {
    auto result = JitCodeBuffer::create(0);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), CodeBufferError::InvalidParameters);
}

TEST_F(JitCodeBufferConstructionTest, Create_RoundsUpToPageSize) {
    auto result = JitCodeBuffer::create(100);
    ASSERT_TRUE(result.has_value());
    EXPECT_GE(result->capacity(), JitCodeBuffer::page_size());
}

TEST_F(JitCodeBufferConstructionTest, Create_StartsAsWritable) {
    auto result = JitCodeBuffer::create(4096);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->is_writable());
    EXPECT_FALSE(result->is_executable());
    EXPECT_EQ(result->protection(), MemoryProtection::ReadWrite);
}

TEST_F(JitCodeBufferConstructionTest, Create_StartsEmpty) {
    auto result = JitCodeBuffer::create(4096);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->used(), 0u);
    EXPECT_TRUE(result->empty());
}

// ============================================================================
// JitCodeBuffer Move Tests
// ============================================================================

class JitCodeBufferMoveTest : public ::testing::Test {};

TEST_F(JitCodeBufferMoveTest, MoveConstruction_TransfersOwnership) {
    auto result = JitCodeBuffer::create(4096);
    ASSERT_TRUE(result.has_value());

    auto buffer1 = std::move(*result);
    auto capacity = buffer1.capacity();
    auto data_ptr = buffer1.data();

    JitCodeBuffer buffer2 = std::move(buffer1);
    EXPECT_EQ(buffer2.capacity(), capacity);
    EXPECT_EQ(buffer2.data(), data_ptr);
    EXPECT_EQ(buffer1.capacity(), 0u);
    EXPECT_EQ(buffer1.data(), nullptr);
}

TEST_F(JitCodeBufferMoveTest, MoveAssignment_TransfersOwnership) {
    auto result1 = JitCodeBuffer::create(4096);
    auto result2 = JitCodeBuffer::create(8192);
    ASSERT_TRUE(result1.has_value());
    ASSERT_TRUE(result2.has_value());

    auto buffer1 = std::move(*result1);
    auto buffer2 = std::move(*result2);

    auto capacity = buffer1.capacity();
    auto data_ptr = buffer1.data();

    buffer2 = std::move(buffer1);
    EXPECT_EQ(buffer2.capacity(), capacity);
    EXPECT_EQ(buffer2.data(), data_ptr);
}

// ============================================================================
// JitCodeBuffer Allocation Tests
// ============================================================================

class JitCodeBufferAllocationTest : public ::testing::Test {
protected:
    void SetUp() override {
        auto result = JitCodeBuffer::create(4096);
        ASSERT_TRUE(result.has_value());
        buffer_ = std::make_unique<JitCodeBuffer>(std::move(*result));
    }

    std::unique_ptr<JitCodeBuffer> buffer_;
};

TEST_F(JitCodeBufferAllocationTest, Allocate_ValidSize_ReturnsSpan) {
    auto result = buffer_->allocate(128);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->size(), 128u);
}

TEST_F(JitCodeBufferAllocationTest, Allocate_UpdatesUsed) {
    auto result = buffer_->allocate(128);
    ASSERT_TRUE(result.has_value());
    EXPECT_GE(buffer_->used(), 128u);  // May be more due to alignment
}

TEST_F(JitCodeBufferAllocationTest, Allocate_ZeroSize_Fails) {
    auto result = buffer_->allocate(0);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), CodeBufferError::InvalidParameters);
}

TEST_F(JitCodeBufferAllocationTest, Allocate_ExceedsCapacity_Fails) {
    auto result = buffer_->allocate(buffer_->capacity() + 1);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), CodeBufferError::InsufficientSpace);
}

TEST_F(JitCodeBufferAllocationTest, Allocate_MultipleAllocations_Succeed) {
    auto result1 = buffer_->allocate(64);
    auto result2 = buffer_->allocate(64);
    auto result3 = buffer_->allocate(64);

    ASSERT_TRUE(result1.has_value());
    ASSERT_TRUE(result2.has_value());
    ASSERT_TRUE(result3.has_value());

    // Ensure spans don't overlap
    EXPECT_NE(result1->data(), result2->data());
    EXPECT_NE(result2->data(), result3->data());
}

TEST_F(JitCodeBufferAllocationTest, Allocate_RespectsAlignment) {
    // Allocate 1 byte to misalign
    auto result1 = buffer_->allocate(1, 1);
    ASSERT_TRUE(result1.has_value());

    // Next allocation should be 16-byte aligned
    auto result2 = buffer_->allocate(64, 16);
    ASSERT_TRUE(result2.has_value());

    auto addr = reinterpret_cast<std::uintptr_t>(result2->data());
    EXPECT_EQ(addr % 16, 0u);
}

TEST_F(JitCodeBufferAllocationTest, Allocate_WritableData) {
    auto result = buffer_->allocate(128);
    ASSERT_TRUE(result.has_value());

    // Should be able to write to allocated region
    std::memset(result->data(), 0xCC, result->size());
    EXPECT_EQ(result->data()[0], 0xCC);
    EXPECT_EQ(result->data()[127], 0xCC);
}

// ============================================================================
// JitCodeBuffer Protection Tests
// ============================================================================

class JitCodeBufferProtectionTest : public ::testing::Test {
protected:
    void SetUp() override {
        auto result = JitCodeBuffer::create(4096);
        ASSERT_TRUE(result.has_value());
        buffer_ = std::make_unique<JitCodeBuffer>(std::move(*result));
    }

    std::unique_ptr<JitCodeBuffer> buffer_;
};

TEST_F(JitCodeBufferProtectionTest, MakeExecutable_Succeeds) {
    auto result = buffer_->make_executable();
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(buffer_->is_executable());
    EXPECT_FALSE(buffer_->is_writable());
    EXPECT_EQ(buffer_->protection(), MemoryProtection::ReadExecute);
}

TEST_F(JitCodeBufferProtectionTest, MakeExecutable_AlreadyExecutable_NoOp) {
    ASSERT_TRUE(buffer_->make_executable().has_value());
    ASSERT_TRUE(buffer_->make_executable().has_value());
    EXPECT_TRUE(buffer_->is_executable());
}

TEST_F(JitCodeBufferProtectionTest, MakeWritable_AfterExecutable_Succeeds) {
    ASSERT_TRUE(buffer_->make_executable().has_value());
    auto result = buffer_->make_writable();
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(buffer_->is_writable());
    EXPECT_FALSE(buffer_->is_executable());
}

TEST_F(JitCodeBufferProtectionTest, MakeWritable_AlreadyWritable_NoOp) {
    ASSERT_TRUE(buffer_->make_writable().has_value());
    EXPECT_TRUE(buffer_->is_writable());
}

TEST_F(JitCodeBufferProtectionTest, Allocate_FailsWhenExecutable) {
    ASSERT_TRUE(buffer_->make_executable().has_value());
    auto result = buffer_->allocate(64);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), CodeBufferError::NotWritable);
}

// ============================================================================
// JitCodeBuffer Reset Tests
// ============================================================================

class JitCodeBufferResetTest : public ::testing::Test {
protected:
    void SetUp() override {
        auto result = JitCodeBuffer::create(4096);
        ASSERT_TRUE(result.has_value());
        buffer_ = std::make_unique<JitCodeBuffer>(std::move(*result));
    }

    std::unique_ptr<JitCodeBuffer> buffer_;
};

TEST_F(JitCodeBufferResetTest, Reset_ClearsUsed) {
    ASSERT_TRUE(buffer_->allocate(128).has_value());
    EXPECT_GT(buffer_->used(), 0u);

    ASSERT_TRUE(buffer_->reset().has_value());
    EXPECT_EQ(buffer_->used(), 0u);
    EXPECT_TRUE(buffer_->empty());
}

TEST_F(JitCodeBufferResetTest, Reset_PreservesCapacity) {
    auto capacity = buffer_->capacity();
    ASSERT_TRUE(buffer_->allocate(128).has_value());
    ASSERT_TRUE(buffer_->reset().has_value());
    EXPECT_EQ(buffer_->capacity(), capacity);
}

TEST_F(JitCodeBufferResetTest, Reset_FailsWhenExecutable) {
    ASSERT_TRUE(buffer_->make_executable().has_value());
    auto result = buffer_->reset();
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), CodeBufferError::NotWritable);
}

TEST_F(JitCodeBufferResetTest, Reset_AllowsNewAllocations) {
    ASSERT_TRUE(buffer_->allocate(buffer_->capacity() - 64).has_value());
    ASSERT_TRUE(buffer_->reset().has_value());

    // Should be able to allocate full capacity again
    auto result = buffer_->allocate(buffer_->capacity() - 64);
    ASSERT_TRUE(result.has_value());
}

// ============================================================================
// WritableGuard Tests
// ============================================================================

class WritableGuardTest : public ::testing::Test {
protected:
    void SetUp() override {
        auto result = JitCodeBuffer::create(4096);
        ASSERT_TRUE(result.has_value());
        buffer_ = std::make_unique<JitCodeBuffer>(std::move(*result));
    }

    std::unique_ptr<JitCodeBuffer> buffer_;
};

TEST_F(WritableGuardTest, Guard_OnWritableBuffer_Succeeds) {
    WritableGuard guard(*buffer_);
    EXPECT_TRUE(guard.success());
    EXPECT_TRUE(buffer_->is_writable());
}

TEST_F(WritableGuardTest, Guard_OnExecutableBuffer_MakesWritable) {
    ASSERT_TRUE(buffer_->make_executable().has_value());
    {
        WritableGuard guard(*buffer_);
        EXPECT_TRUE(guard.success());
        EXPECT_TRUE(buffer_->is_writable());
    }
    // Guard destructor should restore executable state
    EXPECT_TRUE(buffer_->is_executable());
}

TEST_F(WritableGuardTest, Guard_AllowsWritesDuringScope) {
    ASSERT_TRUE(buffer_->make_executable().has_value());
    {
        WritableGuard guard(*buffer_);
        ASSERT_TRUE(guard.success());

        // Should be able to write during guard
        auto result = buffer_->allocate(64);
        EXPECT_TRUE(result.has_value());
    }
}

// ============================================================================
// Page Size Tests
// ============================================================================

class PageSizeTest : public ::testing::Test {};

TEST_F(PageSizeTest, PageSize_ReturnsPositiveValue) {
    auto size = JitCodeBuffer::page_size();
    EXPECT_GT(size, 0u);
}

TEST_F(PageSizeTest, PageSize_IsPowerOfTwo) {
    auto size = JitCodeBuffer::page_size();
    EXPECT_EQ(size & (size - 1), 0u);
}

TEST_F(PageSizeTest, PageSize_IsAtLeast4K) {
    EXPECT_GE(JitCodeBuffer::page_size(), 4096u);
}

}  // namespace
}  // namespace dotvm::jit
