#include <gtest/gtest.h>

#include <dotvm/core/memory.hpp>
#include <dotvm/core/bytecode.hpp>
#include <dotvm/core/vm_context.hpp>
#include <dotvm/core/cfi.hpp>
#include <dotvm/core/security_stats.hpp>

#include <array>
#include <limits>
#include <vector>

using namespace dotvm::core;

// ============================================================================
// Security Statistics Tests
// ============================================================================

class SecurityStatsTest : public ::testing::Test {
protected:
    SecurityStats stats;
};

TEST_F(SecurityStatsTest, InitiallyZero) {
    auto snapshot = stats.snapshot();
    EXPECT_EQ(snapshot.generation_wraparounds, 0);
    EXPECT_EQ(snapshot.bounds_violations, 0);
    EXPECT_EQ(snapshot.invalid_handle_accesses, 0);
    EXPECT_EQ(snapshot.cfi_violations, 0);
}

TEST_F(SecurityStatsTest, RecordGenerationWraparound) {
    stats.record_generation_wraparound();
    stats.record_generation_wraparound();
    EXPECT_EQ(stats.snapshot().generation_wraparounds, 2);
}

TEST_F(SecurityStatsTest, RecordBoundsViolation) {
    stats.record_bounds_violation();
    EXPECT_TRUE(stats.has_violations());
    EXPECT_EQ(stats.total_violations(), 1);
}

TEST_F(SecurityStatsTest, RecordCfiViolation) {
    stats.record_cfi_violation();
    EXPECT_TRUE(stats.has_violations());
    EXPECT_EQ(stats.snapshot().cfi_violations, 1);
}

TEST_F(SecurityStatsTest, ResetClearsAll) {
    stats.record_generation_wraparound();
    stats.record_bounds_violation();
    stats.record_cfi_violation();
    stats.record_allocation();

    stats.reset();

    EXPECT_FALSE(stats.has_violations());
    EXPECT_EQ(stats.total_violations(), 0);
    EXPECT_EQ(stats.snapshot().total_allocations, 0);
}

TEST_F(SecurityStatsTest, HasExhaustionEvents) {
    EXPECT_FALSE(stats.has_exhaustion_events());
    stats.record_allocation_limit_hit();
    EXPECT_TRUE(stats.has_exhaustion_events());
}

// ============================================================================
// Memory Bounds Violation Tests
// ============================================================================

class MemoryBoundsTest : public ::testing::Test {
protected:
    MemoryManager mem;
};

TEST_F(MemoryBoundsTest, ReadPastAllocationBoundary) {
    auto result = mem.allocate(mem_config::PAGE_SIZE);
    ASSERT_TRUE(result.has_value());
    Handle handle = *result;

    // Read at valid offset
    auto read1 = mem.read<std::uint64_t>(handle, 0);
    EXPECT_TRUE(read1.has_value());

    // Read past end
    auto read2 = mem.read<std::uint64_t>(handle, mem_config::PAGE_SIZE);
    EXPECT_FALSE(read2.has_value());
    EXPECT_EQ(read2.error(), MemoryError::BoundsViolation);

    // Read where offset + size exceeds allocation
    auto read3 = mem.read<std::uint64_t>(handle, mem_config::PAGE_SIZE - 4);
    EXPECT_FALSE(read3.has_value());
    EXPECT_EQ(read3.error(), MemoryError::BoundsViolation);
}

TEST_F(MemoryBoundsTest, WritePastAllocationBoundary) {
    auto result = mem.allocate(mem_config::PAGE_SIZE);
    ASSERT_TRUE(result.has_value());
    Handle handle = *result;

    // Write at valid offset
    auto err1 = mem.write<std::uint64_t>(handle, 0, 42);
    EXPECT_EQ(err1, MemoryError::Success);

    // Write past end
    auto err2 = mem.write<std::uint64_t>(handle, mem_config::PAGE_SIZE, 42);
    EXPECT_EQ(err2, MemoryError::BoundsViolation);
}

TEST_F(MemoryBoundsTest, SpanReadBoundsCheck) {
    auto result = mem.allocate(mem_config::PAGE_SIZE);
    ASSERT_TRUE(result.has_value());
    Handle handle = *result;

    std::array<std::uint8_t, 16> buffer{};

    // Valid read
    auto err1 = mem.read_into(handle, 0, std::span<std::uint8_t>{buffer});
    EXPECT_EQ(err1, MemoryError::Success);

    // Read past end
    auto err2 = mem.read_into(handle, mem_config::PAGE_SIZE - 8, std::span<std::uint8_t>{buffer});
    EXPECT_EQ(err2, MemoryError::BoundsViolation);
}

TEST_F(MemoryBoundsTest, SpanWriteBoundsCheck) {
    auto result = mem.allocate(mem_config::PAGE_SIZE);
    ASSERT_TRUE(result.has_value());
    Handle handle = *result;

    std::array<std::uint8_t, 16> buffer{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};

    // Valid write
    auto err1 = mem.write_from(handle, 0, std::span<const std::uint8_t>{buffer});
    EXPECT_EQ(err1, MemoryError::Success);

    // Write past end
    auto err2 = mem.write_from(handle, mem_config::PAGE_SIZE - 8, std::span<const std::uint8_t>{buffer});
    EXPECT_EQ(err2, MemoryError::BoundsViolation);
}

// ============================================================================
// Use-After-Free Prevention Tests
// ============================================================================

class UseAfterFreeTest : public ::testing::Test {
protected:
    MemoryManager mem;
};

TEST_F(UseAfterFreeTest, AccessAfterDeallocate) {
    auto result = mem.allocate(mem_config::PAGE_SIZE);
    ASSERT_TRUE(result.has_value());
    Handle handle = *result;

    // Deallocate
    auto dealloc_err = mem.deallocate(handle);
    EXPECT_EQ(dealloc_err, MemoryError::Success);

    // Try to access - should fail with InvalidHandle
    auto read_result = mem.read<std::uint64_t>(handle, 0);
    EXPECT_FALSE(read_result.has_value());
    EXPECT_EQ(read_result.error(), MemoryError::InvalidHandle);
}

TEST_F(UseAfterFreeTest, GenerationMismatchDetection) {
    // Allocate and get handle
    auto result1 = mem.allocate(mem_config::PAGE_SIZE);
    ASSERT_TRUE(result1.has_value());
    Handle handle1 = *result1;

    // Store the original handle
    Handle old_handle = handle1;

    // Deallocate
    ASSERT_EQ(mem.deallocate(handle1), MemoryError::Success);

    // Reallocate - should reuse the slot with incremented generation
    auto result2 = mem.allocate(mem_config::PAGE_SIZE);
    ASSERT_TRUE(result2.has_value());
    Handle handle2 = *result2;

    // New handle should have same index but different generation
    EXPECT_EQ(handle2.index, old_handle.index);
    EXPECT_NE(handle2.generation, old_handle.generation);

    // Old handle should be invalid
    EXPECT_FALSE(mem.is_valid(old_handle));
    EXPECT_TRUE(mem.is_valid(handle2));
}

TEST_F(UseAfterFreeTest, DoubleDeallocateFails) {
    auto result = mem.allocate(mem_config::PAGE_SIZE);
    ASSERT_TRUE(result.has_value());
    Handle handle = *result;

    // First deallocation succeeds
    auto err1 = mem.deallocate(handle);
    EXPECT_EQ(err1, MemoryError::Success);

    // Second deallocation fails (generation mismatch)
    auto err2 = mem.deallocate(handle);
    EXPECT_EQ(err2, MemoryError::InvalidHandle);
}

// ============================================================================
// Generation Wraparound Tests
// ============================================================================

class GenerationWraparoundTest : public ::testing::Test {
protected:
    MemoryManager mem;
};

TEST_F(GenerationWraparoundTest, SecurityStatsTrackWraparound) {
    // We can't easily force wraparound in a unit test without many allocations,
    // but we can verify the stats mechanism works
    auto& stats = mem.security_stats();
    EXPECT_EQ(stats.snapshot().generation_wraparounds, 0);
}

// ============================================================================
// CFI (Control Flow Integrity) Tests
// ============================================================================

class CfiTest : public ::testing::Test {
protected:
    cfi::CfiContext cfi{cfi::CfiPolicy::strict()};
    static constexpr std::size_t CODE_SIZE = 1024;
};

TEST_F(CfiTest, ValidInstructionPasses) {
    // PC at 0, aligned, within bounds
    EXPECT_TRUE(cfi.validate_instruction(0, 0x00000000, CODE_SIZE));
    EXPECT_FALSE(cfi.has_violation());
}

TEST_F(CfiTest, PCOutOfBoundsFails) {
    EXPECT_FALSE(cfi.validate_instruction(static_cast<std::uint32_t>(CODE_SIZE), 0, CODE_SIZE));
    EXPECT_TRUE(cfi.has_violation());
    EXPECT_EQ(cfi.last_violation(), cfi::CfiViolation::JumpOutOfBounds);
}

TEST_F(CfiTest, MisalignedPCFails) {
    // PC = 1 is not 4-byte aligned
    cfi.reset();
    EXPECT_FALSE(cfi.validate_instruction(1, 0, CODE_SIZE));
    EXPECT_TRUE(cfi.has_violation());
    EXPECT_EQ(cfi.last_violation(), cfi::CfiViolation::InvalidJumpTarget);
}

TEST_F(CfiTest, ValidJumpPasses) {
    EXPECT_TRUE(cfi.validate_jump(0, 16, CODE_SIZE));
    EXPECT_FALSE(cfi.has_violation());
}

TEST_F(CfiTest, JumpOutOfBoundsFails) {
    EXPECT_FALSE(cfi.validate_jump(0, static_cast<std::uint32_t>(CODE_SIZE + 4), CODE_SIZE));
    EXPECT_EQ(cfi.last_violation(), cfi::CfiViolation::JumpOutOfBounds);
}

TEST_F(CfiTest, BackwardJumpLimitEnforced) {
    cfi::CfiPolicy policy;
    policy.max_backward_jumps = 3;
    cfi::CfiContext limited_cfi{policy};

    // Forward jumps don't count
    EXPECT_TRUE(limited_cfi.validate_jump(0, 16, CODE_SIZE));

    // Backward jumps count
    EXPECT_TRUE(limited_cfi.validate_jump(100, 96, CODE_SIZE));  // 1
    EXPECT_TRUE(limited_cfi.validate_jump(100, 92, CODE_SIZE));  // 2
    EXPECT_TRUE(limited_cfi.validate_jump(100, 88, CODE_SIZE));  // 3
    EXPECT_FALSE(limited_cfi.validate_jump(100, 84, CODE_SIZE)); // 4 - exceeds limit

    EXPECT_EQ(limited_cfi.last_violation(), cfi::CfiViolation::BackwardJumpLimit);
}

TEST_F(CfiTest, CallStackOverflowDetected) {
    cfi::CfiPolicy policy;
    policy.max_call_depth = 3;
    cfi::CfiContext limited_cfi{policy};

    EXPECT_TRUE(limited_cfi.push_call(4));
    EXPECT_TRUE(limited_cfi.push_call(8));
    EXPECT_TRUE(limited_cfi.push_call(12));
    EXPECT_FALSE(limited_cfi.push_call(16));  // Exceeds limit

    EXPECT_EQ(limited_cfi.last_violation(), cfi::CfiViolation::CallStackOverflow);
}

TEST_F(CfiTest, ReturnMismatchDetected) {
    EXPECT_EQ(cfi.pop_call(), std::nullopt);
    EXPECT_EQ(cfi.last_violation(), cfi::CfiViolation::ReturnMismatch);
}

TEST_F(CfiTest, MatchedCallReturnPasses) {
    EXPECT_TRUE(cfi.push_call(100));
    EXPECT_TRUE(cfi.push_call(200));

    auto ret1 = cfi.pop_call();
    EXPECT_TRUE(ret1.has_value());
    EXPECT_EQ(*ret1, 200);

    auto ret2 = cfi.pop_call();
    EXPECT_TRUE(ret2.has_value());
    EXPECT_EQ(*ret2, 100);
}

TEST_F(CfiTest, ResetClearsState) {
    [[maybe_unused]] auto push_ok = cfi.push_call(100);
    [[maybe_unused]] auto jump_ok = cfi.validate_jump(0, static_cast<std::uint32_t>(CODE_SIZE + 100), CODE_SIZE);  // Force violation

    EXPECT_TRUE(cfi.has_violation());
    EXPECT_EQ(cfi.call_depth(), 1);

    cfi.reset();

    EXPECT_FALSE(cfi.has_violation());
    EXPECT_EQ(cfi.call_depth(), 0);
    EXPECT_EQ(cfi.backward_jump_count(), 0);
}

// ============================================================================
// Bytecode Execution Constraint Tests
// ============================================================================

class BytecodeConstraintTest : public ::testing::Test {
protected:
    static constexpr std::uint64_t CODE_SIZE = 1024;
};

TEST_F(BytecodeConstraintTest, ValidPCPasses) {
    EXPECT_EQ(validate_pc(0, CODE_SIZE), BytecodeError::Success);
    EXPECT_EQ(validate_pc(4, CODE_SIZE), BytecodeError::Success);
    EXPECT_EQ(validate_pc(CODE_SIZE - 4, CODE_SIZE), BytecodeError::Success);
}

TEST_F(BytecodeConstraintTest, PCOutOfBoundsFails) {
    EXPECT_EQ(validate_pc(CODE_SIZE, CODE_SIZE), BytecodeError::EntryPointOutOfBounds);
    EXPECT_EQ(validate_pc(CODE_SIZE + 100, CODE_SIZE), BytecodeError::EntryPointOutOfBounds);
}

TEST_F(BytecodeConstraintTest, MisalignedPCFails) {
    EXPECT_EQ(validate_pc(1, CODE_SIZE), BytecodeError::EntryPointNotAligned);
    EXPECT_EQ(validate_pc(2, CODE_SIZE), BytecodeError::EntryPointNotAligned);
    EXPECT_EQ(validate_pc(3, CODE_SIZE), BytecodeError::EntryPointNotAligned);
    EXPECT_EQ(validate_pc(5, CODE_SIZE), BytecodeError::EntryPointNotAligned);
}

TEST_F(BytecodeConstraintTest, ValidConstIndexPasses) {
    EXPECT_TRUE(is_valid_const_index(0, 10));
    EXPECT_TRUE(is_valid_const_index(9, 10));
}

TEST_F(BytecodeConstraintTest, InvalidConstIndexFails) {
    EXPECT_FALSE(is_valid_const_index(10, 10));
    EXPECT_FALSE(is_valid_const_index(100, 10));
}

TEST_F(BytecodeConstraintTest, ValidJumpTargetPasses) {
    EXPECT_EQ(validate_jump_target(100, -96, CODE_SIZE), BytecodeError::Success);  // 100 - 96 = 4
    EXPECT_EQ(validate_jump_target(0, 100, CODE_SIZE), BytecodeError::Success);
}

TEST_F(BytecodeConstraintTest, JumpTargetUnderflowFails) {
    EXPECT_EQ(validate_jump_target(0, -4, CODE_SIZE), BytecodeError::EntryPointOutOfBounds);
}

TEST_F(BytecodeConstraintTest, JumpTargetOverflowFails) {
    EXPECT_EQ(validate_jump_target(CODE_SIZE - 4, 100, CODE_SIZE), BytecodeError::EntryPointOutOfBounds);
}

// ============================================================================
// VmContext Security Configuration Tests
// ============================================================================

class VmContextSecurityTest : public ::testing::Test {
protected:
};

TEST_F(VmContextSecurityTest, DefaultConfigNoCfi) {
    VmContext ctx;
    EXPECT_FALSE(ctx.cfi_enabled());
}

TEST_F(VmContextSecurityTest, SecureConfigEnablesCfi) {
    VmContext ctx{VmConfig::secure()};
    EXPECT_TRUE(ctx.cfi_enabled());
}

TEST_F(VmContextSecurityTest, SecurityStatsAccessible) {
    VmContext ctx;
    auto& stats = ctx.security_stats();
    EXPECT_EQ(stats.snapshot().total_allocations, 0);
}

TEST_F(VmContextSecurityTest, CfiContextAccessibleWhenEnabled) {
    VmContext ctx{VmConfig::secure()};
    ASSERT_TRUE(ctx.cfi_enabled());

    // Should not throw
    auto& cfi = ctx.cfi();
    EXPECT_FALSE(cfi.has_violation());
}

// ============================================================================
// Integer Range Tests
// ============================================================================

class IntegerRangeTest : public ::testing::Test {
protected:
};

TEST_F(IntegerRangeTest, ValidIntegerRange) {
    EXPECT_TRUE(is_valid_value_int(0));
    EXPECT_TRUE(is_valid_value_int(1));
    EXPECT_TRUE(is_valid_value_int(-1));
    EXPECT_TRUE(is_valid_value_int(MAX_VALUE_INT));
    EXPECT_TRUE(is_valid_value_int(MIN_VALUE_INT));
}

TEST_F(IntegerRangeTest, OutOfRangePositive) {
    EXPECT_FALSE(is_valid_value_int(MAX_VALUE_INT + 1));
}

TEST_F(IntegerRangeTest, OutOfRangeNegative) {
    EXPECT_FALSE(is_valid_value_int(MIN_VALUE_INT - 1));
}

// ============================================================================
// DoS Resistance Tests
// ============================================================================

class DoSResistanceTest : public ::testing::Test {
protected:
};

TEST_F(DoSResistanceTest, ConstantPoolBombPrevention) {
    // Create bytecode with too many constants
    std::vector<std::uint8_t> pool_data(8);

    // Write entry count that exceeds MAX_CONST_POOL_ENTRIES
    endian::write_u32_le(pool_data.data(), bytecode::MAX_CONST_POOL_ENTRIES + 1);

    auto result = load_constant_pool(std::span{pool_data});
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), BytecodeError::TooManyConstants);
}

TEST_F(DoSResistanceTest, FileSizeLimitEnforcement) {
    BytecodeHeader header{};
    header.magic = bytecode::MAGIC_BYTES;
    header.version = bytecode::CURRENT_VERSION;
    header.arch = Architecture::Arch64;
    header.flags = bytecode::FLAG_NONE;

    // File size exceeds limit
    auto err = validate_header(header, bytecode::MAX_FILE_SIZE + 1);
    EXPECT_EQ(err, BytecodeError::FileTooLarge);
}

TEST_F(DoSResistanceTest, HandleTableExhaustionGraceful) {
    MemoryManager mem;
    std::vector<Handle> handles;

    // Allocate until we can't anymore (or hit a reasonable limit)
    constexpr std::size_t MAX_TEST_ALLOCATIONS = 10000;
    for (std::size_t i = 0; i < MAX_TEST_ALLOCATIONS; ++i) {
        auto result = mem.allocate(mem_config::PAGE_SIZE);
        if (!result.has_value()) {
            // Expected: either AllocationFailed or HandleTableFull
            auto err = result.error();
            EXPECT_TRUE(err == MemoryError::AllocationFailed ||
                       err == MemoryError::HandleTableFull);
            break;
        }
        handles.push_back(*result);
    }

    // Clean up
    for (auto h : handles) {
        [[maybe_unused]] auto err = mem.deallocate(h);
    }
}

// ============================================================================
// Resource Limits Tests
// ============================================================================

class ResourceLimitsTest : public ::testing::Test {};

TEST_F(ResourceLimitsTest, UnlimitedPolicyHasNoLimits) {
    auto limits = ResourceLimits::unlimited();
    EXPECT_EQ(limits.max_instructions, 0u);
    EXPECT_EQ(limits.max_allocations, 0u);
    EXPECT_EQ(limits.max_total_memory, 0u);
    EXPECT_EQ(limits.max_call_depth, 0u);
    EXPECT_EQ(limits.max_backward_jumps, 0u);
    EXPECT_FALSE(limits.has_limits());
}

TEST_F(ResourceLimitsTest, RestrictedPolicyHasReasonableDefaults) {
    auto limits = ResourceLimits::restricted();
    EXPECT_GT(limits.max_instructions, 0u);
    EXPECT_GT(limits.max_allocations, 0u);
    EXPECT_GT(limits.max_total_memory, 0u);
    EXPECT_GT(limits.max_call_depth, 0u);
    EXPECT_GT(limits.max_backward_jumps, 0u);
    EXPECT_TRUE(limits.has_limits());
}

TEST_F(ResourceLimitsTest, RestrictedLimitsAreReasonable) {
    auto limits = ResourceLimits::restricted();
    // Should be at least 100K instructions (reasonable minimum)
    EXPECT_GE(limits.max_instructions, 100'000u);
    // Should be at least 100 allocations
    EXPECT_GE(limits.max_allocations, 100u);
    // Should be at least 1MB
    EXPECT_GE(limits.max_total_memory, 1024u * 1024u);
}

TEST_F(ResourceLimitsTest, EqualityComparison) {
    auto l1 = ResourceLimits::restricted();
    auto l2 = ResourceLimits::restricted();
    auto l3 = ResourceLimits::unlimited();

    EXPECT_EQ(l1, l2);
    EXPECT_NE(l1, l3);
}

// ============================================================================
// Sandboxed Configuration Tests
// ============================================================================

class SandboxedConfigTest : public ::testing::Test {};

TEST_F(SandboxedConfigTest, SandboxedConfigCombinesSecurityFeatures) {
    auto config = VmConfig::sandboxed();

    // Should have CFI enabled
    EXPECT_TRUE(config.cfi_enabled);

    // Should have strict overflow
    EXPECT_TRUE(config.strict_overflow);

    // Should have resource limits
    EXPECT_TRUE(config.resource_limits.has_limits());
    EXPECT_GT(config.resource_limits.max_instructions, 0u);
}

TEST_F(SandboxedConfigTest, SandboxedConfigHasReducedMemory) {
    auto sandboxed = VmConfig::sandboxed();
    auto normal = VmConfig::arch64();

    // Sandboxed should have smaller memory limit
    EXPECT_LT(sandboxed.max_memory, normal.max_memory);
}

TEST_F(SandboxedConfigTest, VmContextWithSandboxedConfig) {
    VmContext ctx{VmConfig::sandboxed()};

    EXPECT_TRUE(ctx.cfi_enabled());
    EXPECT_TRUE(ctx.config().strict_overflow);
    EXPECT_TRUE(ctx.config().resource_limits.has_limits());
}

// ============================================================================
// Security Event Tests
// ============================================================================

class SecurityEventTest : public ::testing::Test {};

TEST_F(SecurityEventTest, EventNameReturnsValidStrings) {
    EXPECT_STREQ(event_name(SecurityEvent::GenerationWraparound), "GenerationWraparound");
    EXPECT_STREQ(event_name(SecurityEvent::BoundsViolation), "BoundsViolation");
    EXPECT_STREQ(event_name(SecurityEvent::InvalidHandleAccess), "InvalidHandleAccess");
    EXPECT_STREQ(event_name(SecurityEvent::CfiViolation), "CfiViolation");
    EXPECT_STREQ(event_name(SecurityEvent::AllocationLimitHit), "AllocationLimitHit");
    EXPECT_STREQ(event_name(SecurityEvent::HandleTableExhaustion), "HandleTableExhaustion");
    EXPECT_STREQ(event_name(SecurityEvent::InstructionLimitHit), "InstructionLimitHit");
    EXPECT_STREQ(event_name(SecurityEvent::MemoryLimitHit), "MemoryLimitHit");
}

// ============================================================================
// Security Event Callback Tests
// ============================================================================

namespace {
struct CallbackState {
    int call_count = 0;
    SecurityEvent last_event{};
    const char* last_context = nullptr;
};

void test_callback(SecurityEvent event, const char* context, void* user_data) {
    auto* state = static_cast<CallbackState*>(user_data);
    state->call_count++;
    state->last_event = event;
    state->last_context = context;
}
}  // namespace

class SecurityEventCallbackTest : public ::testing::Test {
protected:
    SecurityStats stats;
    CallbackState state;

    void SetUp() override {
        stats.set_event_callback(test_callback, &state);
    }
};

TEST_F(SecurityEventCallbackTest, CallbackIsRegistered) {
    EXPECT_TRUE(stats.has_event_callback());
}

TEST_F(SecurityEventCallbackTest, NoCallbackByDefault) {
    SecurityStats fresh_stats;
    EXPECT_FALSE(fresh_stats.has_event_callback());
}

TEST_F(SecurityEventCallbackTest, CallbackCanBeCleared) {
    stats.set_event_callback(nullptr);
    EXPECT_FALSE(stats.has_event_callback());
}
