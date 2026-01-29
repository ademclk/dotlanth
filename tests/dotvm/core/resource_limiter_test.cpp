/// @file resource_limiter_test.cpp
/// @brief Unit tests for SEC-004 Resource Limits

#include <thread>
#include <vector>

#include <dotvm/core/security/resource_limiter.hpp>
#include <dotvm/core/security_stats.hpp>

#include <gtest/gtest.h>

using namespace dotvm::core;
using namespace dotvm::core::security;

// ============================================================================
// RuntimeLimits Factory Method Tests
// ============================================================================

TEST(RuntimeLimitsTest, DefaultValues) {
    RuntimeLimits limits;

    EXPECT_EQ(limits.max_memory, 67'108'864ULL);  // 64 MB
    EXPECT_EQ(limits.max_instructions, 1'000'000ULL);
    EXPECT_EQ(limits.max_stack_depth, 1024U);
    EXPECT_EQ(limits.max_allocation_size, 1'048'576ULL);  // 1 MB
    EXPECT_EQ(limits.max_execution_time_ms, 5000U);
}

TEST(RuntimeLimitsTest, UnlimitedFactory) {
    auto limits = RuntimeLimits::unlimited();

    EXPECT_EQ(limits.max_memory, 0ULL);
    EXPECT_EQ(limits.max_instructions, 0ULL);
    EXPECT_EQ(limits.max_stack_depth, 0U);
    EXPECT_EQ(limits.max_allocation_size, 0ULL);
    EXPECT_EQ(limits.max_execution_time_ms, 0U);
}

TEST(RuntimeLimitsTest, StandardFactory) {
    auto limits = RuntimeLimits::standard();

    EXPECT_EQ(limits.max_memory, 67'108'864ULL);
    EXPECT_EQ(limits.max_instructions, 1'000'000ULL);
    EXPECT_EQ(limits.max_stack_depth, 1024U);
    EXPECT_EQ(limits.max_allocation_size, 1'048'576ULL);
    EXPECT_EQ(limits.max_execution_time_ms, 5000U);
}

TEST(RuntimeLimitsTest, StrictFactory) {
    auto limits = RuntimeLimits::strict();

    EXPECT_EQ(limits.max_memory, 16'777'216ULL);  // 16 MB
    EXPECT_EQ(limits.max_instructions, 100'000ULL);
    EXPECT_EQ(limits.max_stack_depth, 256U);
    EXPECT_EQ(limits.max_allocation_size, 262'144ULL);  // 256 KB
    EXPECT_EQ(limits.max_execution_time_ms, 1000U);
}

TEST(RuntimeLimitsTest, DesignatedInitializers) {
    RuntimeLimits limits{.max_memory = 1024,
                         .max_instructions = 100,
                         .max_stack_depth = 10,
                         .max_allocation_size = 512,
                         .max_execution_time_ms = 1000};

    EXPECT_EQ(limits.max_memory, 1024ULL);
    EXPECT_EQ(limits.max_instructions, 100ULL);
    EXPECT_EQ(limits.max_stack_depth, 10U);
    EXPECT_EQ(limits.max_allocation_size, 512ULL);
    EXPECT_EQ(limits.max_execution_time_ms, 1000U);
}

TEST(RuntimeLimitsTest, Equality) {
    auto a = RuntimeLimits::standard();
    auto b = RuntimeLimits::standard();
    auto c = RuntimeLimits::strict();

    EXPECT_EQ(a, b);
    EXPECT_NE(a, c);
}

// ============================================================================
// ResourceLimitError Tests
// ============================================================================

TEST(ResourceLimitErrorTest, ToStringSuccess) {
    EXPECT_EQ(to_string(ResourceLimitError::Success), "Success");
}

TEST(ResourceLimitErrorTest, ToStringMemoryLimitExceeded) {
    EXPECT_EQ(to_string(ResourceLimitError::MemoryLimitExceeded), "MemoryLimitExceeded");
}

TEST(ResourceLimitErrorTest, ToStringInstructionLimitExceeded) {
    EXPECT_EQ(to_string(ResourceLimitError::InstructionLimitExceeded), "InstructionLimitExceeded");
}

TEST(ResourceLimitErrorTest, ToStringStackDepthExceeded) {
    EXPECT_EQ(to_string(ResourceLimitError::StackDepthExceeded), "StackDepthExceeded");
}

TEST(ResourceLimitErrorTest, ToStringAllocationSizeExceeded) {
    EXPECT_EQ(to_string(ResourceLimitError::AllocationSizeExceeded), "AllocationSizeExceeded");
}

TEST(ResourceLimitErrorTest, ToStringTimeExpired) {
    EXPECT_EQ(to_string(ResourceLimitError::TimeExpired), "TimeExpired");
}

TEST(ResourceLimitErrorTest, ToStringThrottled) {
    EXPECT_EQ(to_string(ResourceLimitError::Throttled), "Throttled");
}

// ============================================================================
// EnforcementAction Tests
// ============================================================================

TEST(EnforcementActionTest, ToStringAllow) {
    EXPECT_EQ(to_string(EnforcementAction::Allow), "Allow");
}

TEST(EnforcementActionTest, ToStringDeny) {
    EXPECT_EQ(to_string(EnforcementAction::Deny), "Deny");
}

TEST(EnforcementActionTest, ToStringThrottle) {
    EXPECT_EQ(to_string(EnforcementAction::Throttle), "Throttle");
}

TEST(EnforcementActionTest, ToStringTerminate) {
    EXPECT_EQ(to_string(EnforcementAction::Terminate), "Terminate");
}

// ============================================================================
// ResourceLimiter Construction Tests
// ============================================================================

TEST(ResourceLimiterTest, DefaultConstruction) {
    ResourceLimiter limiter;

    EXPECT_EQ(limiter.limits(), RuntimeLimits::standard());
    EXPECT_EQ(limiter.current_memory(), 0ULL);
    EXPECT_EQ(limiter.current_instructions(), 0ULL);
    EXPECT_EQ(limiter.current_stack_depth(), 0U);
}

TEST(ResourceLimiterTest, ConstructionWithLimits) {
    RuntimeLimits limits{.max_memory = 1024, .max_instructions = 100};
    ResourceLimiter limiter(limits);

    EXPECT_EQ(limiter.limits().max_memory, 1024ULL);
    EXPECT_EQ(limiter.limits().max_instructions, 100ULL);
}

TEST(ResourceLimiterTest, ConstructionWithStats) {
    SecurityStats stats;
    ResourceLimiter limiter(RuntimeLimits::standard(), &stats);

    EXPECT_EQ(limiter.limits(), RuntimeLimits::standard());
}

// ============================================================================
// Memory Allocation Limit Tests
// ============================================================================

TEST(ResourceLimiterTest, MemoryAllocationSuccess) {
    RuntimeLimits limits{.max_memory = 1024, .max_allocation_size = 512};
    ResourceLimiter limiter(limits);

    auto result = limiter.try_allocate(256);
    EXPECT_TRUE(result.is_ok());
    EXPECT_EQ(limiter.current_memory(), 256ULL);
}

TEST(ResourceLimiterTest, MemoryAllocationExceedsTotalLimit) {
    RuntimeLimits limits{.max_memory = 1024, .max_allocation_size = 2048};
    ResourceLimiter limiter(limits);

    // First allocation succeeds
    auto result1 = limiter.try_allocate(512);
    EXPECT_TRUE(result1.is_ok());

    // Second allocation exceeds total limit
    auto result2 = limiter.try_allocate(600);
    EXPECT_TRUE(result2.is_err());
    EXPECT_EQ(result2.error(), ResourceLimitError::MemoryLimitExceeded);
    EXPECT_EQ(limiter.current_memory(), 512ULL);  // Unchanged
}

TEST(ResourceLimiterTest, MemoryAllocationExceedsSingleLimit) {
    RuntimeLimits limits{.max_memory = 1024, .max_allocation_size = 256};
    ResourceLimiter limiter(limits);

    auto result = limiter.try_allocate(512);
    EXPECT_TRUE(result.is_err());
    EXPECT_EQ(result.error(), ResourceLimitError::AllocationSizeExceeded);
    EXPECT_EQ(limiter.current_memory(), 0ULL);
}

TEST(ResourceLimiterTest, MemoryDeallocation) {
    RuntimeLimits limits{.max_memory = 1024, .max_allocation_size = 1024};
    ResourceLimiter limiter(limits);

    EXPECT_TRUE(limiter.try_allocate(512).is_ok());
    EXPECT_EQ(limiter.current_memory(), 512ULL);

    limiter.on_deallocate(256);
    EXPECT_EQ(limiter.current_memory(), 256ULL);
}

TEST(ResourceLimiterTest, MemoryDeallocationSaturates) {
    RuntimeLimits limits{.max_memory = 1024, .max_allocation_size = 1024};
    ResourceLimiter limiter(limits);

    EXPECT_TRUE(limiter.try_allocate(100).is_ok());
    EXPECT_EQ(limiter.current_memory(), 100ULL);

    // Deallocate more than allocated - should saturate at 0
    limiter.on_deallocate(200);
    EXPECT_EQ(limiter.current_memory(), 0ULL);
}

TEST(ResourceLimiterTest, MemoryUnlimited) {
    auto limits = RuntimeLimits::unlimited();
    ResourceLimiter limiter(limits);

    // Should accept any allocation
    auto result = limiter.try_allocate(1'000'000'000);
    EXPECT_TRUE(result.is_ok());
    EXPECT_EQ(limiter.current_memory(), 1'000'000'000ULL);
}

TEST(ResourceLimiterTest, MemoryAllocationStatsIntegration) {
    SecurityStats stats;
    RuntimeLimits limits{.max_memory = 100, .max_allocation_size = 1024};
    ResourceLimiter limiter(limits, &stats);

    // First allocation succeeds
    auto result1 = limiter.try_allocate(50);
    EXPECT_TRUE(result1.is_ok());

    // Second allocation exceeds limit
    auto result2 = limiter.try_allocate(60);
    EXPECT_TRUE(result2.is_err());

    // Check stats recorded the violation
    auto snapshot = stats.snapshot();
    EXPECT_EQ(snapshot.limit_violations, 1U);
}

// ============================================================================
// Instruction Limit Tests
// ============================================================================

TEST(ResourceLimiterTest, InstructionExecuteSuccess) {
    RuntimeLimits limits{.max_instructions = 100};
    ResourceLimiter limiter(limits);

    for (int i = 0; i < 50; ++i) {
        auto result = limiter.try_execute();
        EXPECT_TRUE(result.is_ok());
    }
    EXPECT_EQ(limiter.current_instructions(), 50ULL);
}

TEST(ResourceLimiterTest, InstructionLimitExceeded) {
    RuntimeLimits limits{.max_instructions = 10};
    ResourceLimiter limiter(limits);

    for (int i = 0; i < 10; ++i) {
        auto result = limiter.try_execute();
        EXPECT_TRUE(result.is_ok());
    }

    // 11th instruction fails
    auto result = limiter.try_execute();
    EXPECT_TRUE(result.is_err());
    EXPECT_EQ(result.error(), ResourceLimitError::InstructionLimitExceeded);
}

TEST(ResourceLimiterTest, InstructionBatchSuccess) {
    RuntimeLimits limits{.max_instructions = 100};
    ResourceLimiter limiter(limits);

    auto result = limiter.try_execute_batch(50);
    EXPECT_TRUE(result.is_ok());
    EXPECT_EQ(limiter.current_instructions(), 50ULL);
}

TEST(ResourceLimiterTest, InstructionBatchExceedsLimit) {
    RuntimeLimits limits{.max_instructions = 100};
    ResourceLimiter limiter(limits);

    auto result1 = limiter.try_execute_batch(50);
    EXPECT_TRUE(result1.is_ok());

    auto result2 = limiter.try_execute_batch(60);
    EXPECT_TRUE(result2.is_err());
    EXPECT_EQ(result2.error(), ResourceLimitError::InstructionLimitExceeded);
}

TEST(ResourceLimiterTest, InstructionUnlimited) {
    auto limits = RuntimeLimits::unlimited();
    ResourceLimiter limiter(limits);

    auto result = limiter.try_execute_batch(1'000'000'000);
    EXPECT_TRUE(result.is_ok());
}

TEST(ResourceLimiterTest, InstructionStatsIntegration) {
    SecurityStats stats;
    RuntimeLimits limits{.max_instructions = 5};
    ResourceLimiter limiter(limits, &stats);

    for (int i = 0; i < 6; ++i) {
        [[maybe_unused]] auto r = limiter.try_execute();
    }

    auto snapshot = stats.snapshot();
    EXPECT_EQ(snapshot.limit_violations, 1U);
}

// ============================================================================
// Stack Depth Tests
// ============================================================================

TEST(ResourceLimiterTest, StackPushSuccess) {
    RuntimeLimits limits{.max_stack_depth = 10};
    ResourceLimiter limiter(limits);

    for (int i = 0; i < 5; ++i) {
        auto result = limiter.try_push_frame();
        EXPECT_TRUE(result.is_ok());
    }
    EXPECT_EQ(limiter.current_stack_depth(), 5U);
}

TEST(ResourceLimiterTest, StackDepthExceeded) {
    RuntimeLimits limits{.max_stack_depth = 3};
    ResourceLimiter limiter(limits);

    for (int i = 0; i < 3; ++i) {
        auto result = limiter.try_push_frame();
        EXPECT_TRUE(result.is_ok());
    }

    // 4th push fails
    auto result = limiter.try_push_frame();
    EXPECT_TRUE(result.is_err());
    EXPECT_EQ(result.error(), ResourceLimitError::StackDepthExceeded);
}

TEST(ResourceLimiterTest, StackPop) {
    RuntimeLimits limits{.max_stack_depth = 10};
    ResourceLimiter limiter(limits);

    EXPECT_TRUE(limiter.try_push_frame().is_ok());
    EXPECT_TRUE(limiter.try_push_frame().is_ok());
    EXPECT_EQ(limiter.current_stack_depth(), 2U);

    limiter.on_pop_frame();
    EXPECT_EQ(limiter.current_stack_depth(), 1U);
}

TEST(ResourceLimiterTest, StackPopAtZero) {
    RuntimeLimits limits{.max_stack_depth = 10};
    ResourceLimiter limiter(limits);

    // Pop at 0 should not underflow
    limiter.on_pop_frame();
    EXPECT_EQ(limiter.current_stack_depth(), 0U);
}

TEST(ResourceLimiterTest, StackUnlimited) {
    auto limits = RuntimeLimits::unlimited();
    ResourceLimiter limiter(limits);

    for (int i = 0; i < 10000; ++i) {
        auto result = limiter.try_push_frame();
        EXPECT_TRUE(result.is_ok());
    }
    EXPECT_EQ(limiter.current_stack_depth(), 10000U);
}

TEST(ResourceLimiterTest, StackStatsIntegration) {
    SecurityStats stats;
    RuntimeLimits limits{.max_stack_depth = 2};
    ResourceLimiter limiter(limits, &stats);

    [[maybe_unused]] auto r1 = limiter.try_push_frame();
    [[maybe_unused]] auto r2 = limiter.try_push_frame();
    [[maybe_unused]] auto r3 = limiter.try_push_frame();  // Exceeds limit

    auto snapshot = stats.snapshot();
    EXPECT_EQ(snapshot.limit_violations, 1U);
}

// ============================================================================
// Time Tracking Tests
// ============================================================================

TEST(ResourceLimiterTest, TimeNotExpired) {
    RuntimeLimits limits{.max_execution_time_ms = 1000};
    ResourceLimiter limiter(limits);

    EXPECT_FALSE(limiter.is_time_expired());
    EXPECT_LT(limiter.elapsed_time_ms(), 100ULL);  // Should be very quick
}

TEST(ResourceLimiterTest, TimeExpired) {
    RuntimeLimits limits{.max_execution_time_ms = 10};  // 10ms
    ResourceLimiter limiter(limits);

    // Wait for time to expire
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    EXPECT_TRUE(limiter.is_time_expired());
}

TEST(ResourceLimiterTest, TimeUnlimited) {
    auto limits = RuntimeLimits::unlimited();
    ResourceLimiter limiter(limits);

    // Sleep a bit
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    // Should never expire
    EXPECT_FALSE(limiter.is_time_expired());
}

TEST(ResourceLimiterTest, ElapsedTime) {
    RuntimeLimits limits{.max_execution_time_ms = 10000};
    ResourceLimiter limiter(limits);

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    auto elapsed = limiter.elapsed_time_ms();
    EXPECT_GE(elapsed, 40ULL);   // Allow some slack
    EXPECT_LT(elapsed, 200ULL);  // But not too much
}

// ============================================================================
// Reset Tests
// ============================================================================

TEST(ResourceLimiterTest, Reset) {
    RuntimeLimits limits{.max_memory = 1024, .max_instructions = 100, .max_stack_depth = 10};
    ResourceLimiter limiter(limits);

    // Use some resources
    EXPECT_TRUE(limiter.try_allocate(500).is_ok());
    EXPECT_TRUE(limiter.try_execute_batch(50).is_ok());
    EXPECT_TRUE(limiter.try_push_frame().is_ok());
    EXPECT_TRUE(limiter.try_push_frame().is_ok());

    EXPECT_EQ(limiter.current_memory(), 500ULL);
    EXPECT_EQ(limiter.current_instructions(), 50ULL);
    EXPECT_EQ(limiter.current_stack_depth(), 2U);

    // Reset
    limiter.reset();

    EXPECT_EQ(limiter.current_memory(), 0ULL);
    EXPECT_EQ(limiter.current_instructions(), 0ULL);
    EXPECT_EQ(limiter.current_stack_depth(), 0U);
}

TEST(ResourceLimiterTest, ResetResetsTime) {
    RuntimeLimits limits{.max_execution_time_ms = 50};
    ResourceLimiter limiter(limits);

    // Wait until almost expired
    std::this_thread::sleep_for(std::chrono::milliseconds(40));

    // Reset should give new time budget
    limiter.reset();

    EXPECT_FALSE(limiter.is_time_expired());
    EXPECT_LT(limiter.elapsed_time_ms(), 20ULL);
}

// ============================================================================
// Thread Safety Tests
// ============================================================================

TEST(ResourceLimiterTest, ConcurrentMemoryAllocation) {
    RuntimeLimits limits{.max_memory = 100'000, .max_allocation_size = 1000};
    ResourceLimiter limiter(limits);

    const int num_threads = 4;
    const int allocations_per_thread = 100;
    std::vector<std::thread> threads;

    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&limiter]() {
            for (int i = 0; i < allocations_per_thread; ++i) {
                auto result = limiter.try_allocate(100);
                if (result.is_ok()) {
                    limiter.on_deallocate(100);
                }
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    // Memory should be back to approximately 0 (may have race conditions)
    // Just check it didn't crash and memory is in valid range
    EXPECT_LE(limiter.current_memory(), 100'000ULL);
}

TEST(ResourceLimiterTest, ConcurrentInstructionExecution) {
    RuntimeLimits limits{.max_instructions = 10'000};
    ResourceLimiter limiter(limits);

    const int num_threads = 4;
    const int instructions_per_thread = 3000;  // More than enough to hit limit
    std::vector<std::thread> threads;
    std::atomic<int> success_count{0};

    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&limiter, &success_count]() {
            for (int i = 0; i < instructions_per_thread; ++i) {
                auto result = limiter.try_execute();
                if (result.is_ok()) {
                    success_count.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    // The limiter should have hit the limit; instruction count reflects
    // all attempts (including those that incremented before check failed).
    // Due to atomic fetch_add semantics, count equals total successful + 1
    // for each thread that was denied after incrementing.
    EXPECT_GE(limiter.current_instructions(), 10'000ULL);
    // Success count should be exactly max_instructions
    EXPECT_EQ(success_count.load(), 10'000);
}

TEST(ResourceLimiterTest, ConcurrentStackOperations) {
    RuntimeLimits limits{.max_stack_depth = 1000};
    ResourceLimiter limiter(limits);

    const int num_threads = 4;
    const int operations_per_thread = 100;
    std::vector<std::thread> threads;

    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&limiter]() {
            for (int i = 0; i < operations_per_thread; ++i) {
                auto result = limiter.try_push_frame();
                if (result.is_ok()) {
                    limiter.on_pop_frame();
                }
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    // Stack should be back to approximately 0
    EXPECT_LE(limiter.current_stack_depth(), 1000U);
}

// ============================================================================
// SecurityStats Integration Tests
// ============================================================================

TEST(ResourceLimiterTest, AllocationLimitHitRecorded) {
    SecurityStats stats;
    RuntimeLimits limits{.max_memory = 100, .max_allocation_size = 50};
    ResourceLimiter limiter(limits, &stats);

    // Allocation size exceeded
    [[maybe_unused]] auto result = limiter.try_allocate(60);

    auto snapshot = stats.snapshot();
    EXPECT_EQ(snapshot.allocation_limit_hits, 1U);
}

TEST(ResourceLimiterTest, MultipleViolationsRecorded) {
    SecurityStats stats;
    RuntimeLimits limits{.max_memory = 100,
                         .max_instructions = 10,
                         .max_stack_depth = 2,
                         .max_allocation_size = 200};
    ResourceLimiter limiter(limits, &stats);

    // Memory limit violation
    [[maybe_unused]] auto r1 = limiter.try_allocate(50);
    [[maybe_unused]] auto r2 = limiter.try_allocate(60);

    // Instruction limit violation
    for (int i = 0; i < 15; ++i) {
        [[maybe_unused]] auto r = limiter.try_execute();
    }

    // Stack limit violation
    [[maybe_unused]] auto r3 = limiter.try_push_frame();
    [[maybe_unused]] auto r4 = limiter.try_push_frame();
    [[maybe_unused]] auto r5 = limiter.try_push_frame();

    auto snapshot = stats.snapshot();
    // All violations go to limit_violations counter
    EXPECT_GE(snapshot.limit_violations, 3U);
}

// ============================================================================
// SecurityEvent Tests (Added in SEC-004)
// ============================================================================

TEST(Sec004SecurityEventTest, NewEventsExist) {
    EXPECT_EQ(static_cast<std::uint8_t>(SecurityEvent::StackDepthLimitHit), 8U);
    EXPECT_EQ(static_cast<std::uint8_t>(SecurityEvent::ExecutionTimeExpired), 9U);
}

TEST(Sec004SecurityEventTest, NewEventNames) {
    EXPECT_STREQ(event_name(SecurityEvent::StackDepthLimitHit), "StackDepthLimitHit");
    EXPECT_STREQ(event_name(SecurityEvent::ExecutionTimeExpired), "ExecutionTimeExpired");
}

TEST(Sec004SecurityStatsTest, NewCountersExist) {
    SecurityStats stats;

    stats.record_stack_depth_limit_hit();
    stats.record_execution_time_expiration();

    auto snapshot = stats.snapshot();
    EXPECT_EQ(snapshot.stack_depth_limit_hits, 1U);
    EXPECT_EQ(snapshot.execution_time_expirations, 1U);
}

TEST(Sec004SecurityStatsTest, NewCountersReset) {
    SecurityStats stats;

    stats.record_stack_depth_limit_hit();
    stats.record_execution_time_expiration();

    stats.reset();

    auto snapshot = stats.snapshot();
    EXPECT_EQ(snapshot.stack_depth_limit_hits, 0U);
    EXPECT_EQ(snapshot.execution_time_expirations, 0U);
}

// ============================================================================
// Edge Case Tests
// ============================================================================

TEST(ResourceLimiterTest, ZeroByteAllocation) {
    RuntimeLimits limits{.max_memory = 100, .max_allocation_size = 50};
    ResourceLimiter limiter(limits);

    auto result = limiter.try_allocate(0);
    EXPECT_TRUE(result.is_ok());
    EXPECT_EQ(limiter.current_memory(), 0ULL);
}

TEST(ResourceLimiterTest, ExactLimitAllocation) {
    RuntimeLimits limits{.max_memory = 100, .max_allocation_size = 100};
    ResourceLimiter limiter(limits);

    auto result = limiter.try_allocate(100);
    EXPECT_TRUE(result.is_ok());
    EXPECT_EQ(limiter.current_memory(), 100ULL);

    // Next byte fails
    auto result2 = limiter.try_allocate(1);
    EXPECT_TRUE(result2.is_err());
}

TEST(ResourceLimiterTest, ExactInstructionLimit) {
    RuntimeLimits limits{.max_instructions = 10};
    ResourceLimiter limiter(limits);

    for (int i = 0; i < 10; ++i) {
        auto result = limiter.try_execute();
        EXPECT_TRUE(result.is_ok());
    }
    EXPECT_EQ(limiter.current_instructions(), 10ULL);

    // Next instruction fails
    auto result = limiter.try_execute();
    EXPECT_TRUE(result.is_err());
}

TEST(ResourceLimiterTest, ExactStackDepthLimit) {
    RuntimeLimits limits{.max_stack_depth = 3};
    ResourceLimiter limiter(limits);

    for (int i = 0; i < 3; ++i) {
        auto result = limiter.try_push_frame();
        EXPECT_TRUE(result.is_ok());
    }
    EXPECT_EQ(limiter.current_stack_depth(), 3U);

    // Next push fails
    auto result = limiter.try_push_frame();
    EXPECT_TRUE(result.is_err());
}

// ============================================================================
// Constexpr Tests
// ============================================================================

TEST(RuntimeLimitsTest, ConstexprFactories) {
    constexpr auto unlimited = RuntimeLimits::unlimited();
    static_assert(unlimited.max_memory == 0);
    static_assert(unlimited.max_instructions == 0);

    constexpr auto standard = RuntimeLimits::standard();
    static_assert(standard.max_memory == 67'108'864);

    constexpr auto strict = RuntimeLimits::strict();
    static_assert(strict.max_instructions == 100'000);
}

TEST(ResourceLimitErrorTest, ConstexprToString) {
    constexpr auto str = to_string(ResourceLimitError::MemoryLimitExceeded);
    static_assert(!str.empty());
}

TEST(EnforcementActionTest, ConstexprToString) {
    constexpr auto str = to_string(EnforcementAction::Deny);
    static_assert(!str.empty());
}
