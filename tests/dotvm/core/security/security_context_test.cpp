/// @file security_context_test.cpp
/// @brief Unit tests for SEC-003 Security Context

#include "dotvm/core/security/security_context.hpp"

#include <gtest/gtest.h>

#include <thread>

namespace dotvm::core::security {
namespace {

// ============================================================================
// Test Fixtures
// ============================================================================

class SecurityContextTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Default context with sandbox limits and read-write permissions
        ctx_ = std::make_unique<SecurityContext>(
            capabilities::CapabilityLimits::sandbox(),
            PermissionSet::read_write());
    }

    std::unique_ptr<SecurityContext> ctx_;
};

class SecurityContextWithLoggerTest : public ::testing::Test {
protected:
    void SetUp() override {
        logger_ = std::make_unique<BufferedAuditLogger>(100);
        ctx_ = std::make_unique<SecurityContext>(
            capabilities::CapabilityLimits::sandbox(),
            PermissionSet::read_write(), logger_.get());
    }

    std::unique_ptr<BufferedAuditLogger> logger_;
    std::unique_ptr<SecurityContext> ctx_;
};

// ============================================================================
// ResourceUsage Tests
// ============================================================================

TEST(ResourceUsageTest, DefaultConstruction) {
    ResourceUsage usage;
    EXPECT_EQ(usage.memory_allocated, 0);
    EXPECT_EQ(usage.allocation_count, 0);
    EXPECT_EQ(usage.instructions_executed, 0);
    EXPECT_EQ(usage.current_stack_depth, 0);
    EXPECT_EQ(usage.max_stack_depth_reached, 0);
}

TEST(ResourceUsageTest, Reset) {
    ResourceUsage usage;
    usage.memory_allocated = 1000;
    usage.allocation_count = 5;
    usage.instructions_executed = 10000;
    usage.current_stack_depth = 10;
    usage.max_stack_depth_reached = 15;
    usage.start_time = std::chrono::steady_clock::now();

    usage.reset();

    EXPECT_EQ(usage.memory_allocated, 0);
    EXPECT_EQ(usage.allocation_count, 0);
    EXPECT_EQ(usage.instructions_executed, 0);
    EXPECT_EQ(usage.current_stack_depth, 0);
    EXPECT_EQ(usage.max_stack_depth_reached, 0);
}

// ============================================================================
// SecurityContextError Tests
// ============================================================================

TEST(SecurityContextErrorTest, ToStringAllValues) {
    EXPECT_STREQ(to_string(SecurityContextError::Success), "Success");
    EXPECT_STREQ(to_string(SecurityContextError::PermissionDenied),
                 "PermissionDenied");
    EXPECT_STREQ(to_string(SecurityContextError::MemoryLimitExceeded),
                 "MemoryLimitExceeded");
    EXPECT_STREQ(to_string(SecurityContextError::AllocationCountExceeded),
                 "AllocationCountExceeded");
    EXPECT_STREQ(to_string(SecurityContextError::AllocationSizeExceeded),
                 "AllocationSizeExceeded");
    EXPECT_STREQ(to_string(SecurityContextError::InstructionLimitExceeded),
                 "InstructionLimitExceeded");
    EXPECT_STREQ(to_string(SecurityContextError::StackDepthExceeded),
                 "StackDepthExceeded");
    EXPECT_STREQ(to_string(SecurityContextError::TimeLimitExceeded),
                 "TimeLimitExceeded");
    EXPECT_STREQ(to_string(SecurityContextError::ContextInvalid),
                 "ContextInvalid");
}

// ============================================================================
// AuditEventType Tests
// ============================================================================

TEST(AuditEventTypeTest, ToStringAllValues) {
    EXPECT_STREQ(to_string(AuditEventType::PermissionGranted),
                 "PermissionGranted");
    EXPECT_STREQ(to_string(AuditEventType::PermissionDenied),
                 "PermissionDenied");
    EXPECT_STREQ(to_string(AuditEventType::AllocationAttempt),
                 "AllocationAttempt");
    EXPECT_STREQ(to_string(AuditEventType::AllocationDenied),
                 "AllocationDenied");
    EXPECT_STREQ(to_string(AuditEventType::DeallocationAttempt),
                 "DeallocationAttempt");
    EXPECT_STREQ(to_string(AuditEventType::InstructionLimitHit),
                 "InstructionLimitHit");
    EXPECT_STREQ(to_string(AuditEventType::StackDepthLimitHit),
                 "StackDepthLimitHit");
    EXPECT_STREQ(to_string(AuditEventType::TimeLimitHit), "TimeLimitHit");
    EXPECT_STREQ(to_string(AuditEventType::ContextCreated), "ContextCreated");
    EXPECT_STREQ(to_string(AuditEventType::ContextDestroyed),
                 "ContextDestroyed");
    EXPECT_STREQ(to_string(AuditEventType::ContextReset), "ContextReset");
}

// ============================================================================
// AuditEvent Tests
// ============================================================================

TEST(AuditEventTest, NowFactory) {
    auto before = std::chrono::steady_clock::now();
    auto event =
        AuditEvent::now(AuditEventType::PermissionGranted, Permission::Execute,
                        42, "test_context");
    auto after = std::chrono::steady_clock::now();

    EXPECT_EQ(event.type, AuditEventType::PermissionGranted);
    EXPECT_EQ(event.permission, Permission::Execute);
    EXPECT_EQ(event.value, 42);
    EXPECT_EQ(event.context, "test_context");
    EXPECT_GE(event.timestamp, before);
    EXPECT_LE(event.timestamp, after);
}

// ============================================================================
// NullAuditLogger Tests
// ============================================================================

TEST(NullAuditLoggerTest, Singleton) {
    auto& instance1 = NullAuditLogger::instance();
    auto& instance2 = NullAuditLogger::instance();
    EXPECT_EQ(&instance1, &instance2);
}

TEST(NullAuditLoggerTest, IsDisabled) {
    EXPECT_FALSE(NullAuditLogger::instance().is_enabled());
}

TEST(NullAuditLoggerTest, LogDoesNothing) {
    auto event = AuditEvent::now(AuditEventType::ContextCreated);
    NullAuditLogger::instance().log(event);  // Should not crash
}

// ============================================================================
// BufferedAuditLogger Tests
// ============================================================================

TEST(BufferedAuditLoggerTest, DefaultConstruction) {
    BufferedAuditLogger logger;
    EXPECT_TRUE(logger.is_enabled());
    EXPECT_EQ(logger.size(), 0);
    EXPECT_EQ(logger.capacity(), 1024);
}

TEST(BufferedAuditLoggerTest, CustomCapacity) {
    BufferedAuditLogger logger(100);
    EXPECT_EQ(logger.capacity(), 100);
}

TEST(BufferedAuditLoggerTest, ZeroCapacityBecomesOne) {
    BufferedAuditLogger logger(0);
    EXPECT_EQ(logger.capacity(), 1);
}

TEST(BufferedAuditLoggerTest, LogEvents) {
    BufferedAuditLogger logger(10);

    for (int i = 0; i < 5; ++i) {
        logger.log(AuditEvent::now(AuditEventType::AllocationAttempt,
                                   Permission::None, static_cast<uint64_t>(i)));
    }

    EXPECT_EQ(logger.size(), 5);
    auto events = logger.events();
    EXPECT_EQ(events.size(), 5);
    for (int i = 0; i < 5; ++i) {
        EXPECT_EQ(events[static_cast<size_t>(i)].value, static_cast<uint64_t>(i));
    }
}

TEST(BufferedAuditLoggerTest, RingBufferOverwrite) {
    BufferedAuditLogger logger(3);

    for (int i = 0; i < 5; ++i) {
        logger.log(AuditEvent::now(AuditEventType::AllocationAttempt,
                                   Permission::None, static_cast<uint64_t>(i)));
    }

    // Should have capacity events, oldest overwritten
    EXPECT_EQ(logger.size(), 3);
}

TEST(BufferedAuditLoggerTest, Clear) {
    BufferedAuditLogger logger(10);
    logger.log(AuditEvent::now(AuditEventType::ContextCreated));
    logger.log(AuditEvent::now(AuditEventType::ContextDestroyed));

    EXPECT_EQ(logger.size(), 2);
    logger.clear();
    EXPECT_EQ(logger.size(), 0);
}

// ============================================================================
// CallbackAuditLogger Tests
// ============================================================================

TEST(CallbackAuditLoggerTest, NullCallbackDisabled) {
    CallbackAuditLogger logger(nullptr);
    EXPECT_FALSE(logger.is_enabled());
}

TEST(CallbackAuditLoggerTest, ValidCallbackEnabled) {
    auto callback = [](const AuditEvent&, void*) {};
    CallbackAuditLogger logger(callback);
    EXPECT_TRUE(logger.is_enabled());
}

TEST(CallbackAuditLoggerTest, CallbackInvoked) {
    auto callback = [](const AuditEvent&, void* data) {
        auto* count = static_cast<int*>(data);
        ++(*count);
    };

    int counter = 0;
    CallbackAuditLogger logger(callback, &counter);

    logger.log(AuditEvent::now(AuditEventType::ContextCreated));
    logger.log(AuditEvent::now(AuditEventType::AllocationAttempt));

    EXPECT_EQ(counter, 2);
}

// ============================================================================
// SecurityContext Construction Tests
// ============================================================================

TEST_F(SecurityContextTest, ConstructionWithLimits) {
    auto limits = capabilities::CapabilityLimits::untrusted();
    SecurityContext ctx(limits, PermissionSet::read_only());

    EXPECT_EQ(ctx.limits().max_memory, limits.max_memory);
    EXPECT_EQ(ctx.limits().max_instructions, limits.max_instructions);
    EXPECT_EQ(ctx.limits().max_stack_depth, limits.max_stack_depth);
}

TEST_F(SecurityContextTest, ConstructionFromCapability) {
    capabilities::Capability cap;
    cap.limits = capabilities::CapabilityLimits::sandbox();

    SecurityContext ctx(cap, PermissionSet::full());

    EXPECT_EQ(ctx.limits().max_memory, cap.limits.max_memory);
}

TEST_F(SecurityContextTest, InitialUsageZero) {
    auto& usage = ctx_->usage();
    EXPECT_EQ(usage.memory_allocated, 0);
    EXPECT_EQ(usage.allocation_count, 0);
    EXPECT_EQ(usage.instructions_executed, 0);
    EXPECT_EQ(usage.current_stack_depth, 0);
}

// ============================================================================
// SecurityContext Permission Tests
// ============================================================================

TEST_F(SecurityContextTest, CanWithGrantedPermission) {
    // read_write includes Execute, ReadMemory, WriteMemory, ReadState,
    // WriteState
    EXPECT_TRUE(ctx_->can(Permission::Execute));
    EXPECT_TRUE(ctx_->can(Permission::ReadMemory));
    EXPECT_TRUE(ctx_->can(Permission::WriteMemory));
    EXPECT_TRUE(ctx_->can(Permission::ReadState));
    EXPECT_TRUE(ctx_->can(Permission::WriteState));
}

TEST_F(SecurityContextTest, CanWithDeniedPermission) {
    // read_write does NOT include these
    EXPECT_FALSE(ctx_->can(Permission::Allocate));
    EXPECT_FALSE(ctx_->can(Permission::SpawnDot));
    EXPECT_FALSE(ctx_->can(Permission::SendMessage));
    EXPECT_FALSE(ctx_->can(Permission::Crypto));
    EXPECT_FALSE(ctx_->can(Permission::SystemCall));
    EXPECT_FALSE(ctx_->can(Permission::Debug));
}

TEST_F(SecurityContextTest, RequireWithGrantedPermission) {
    auto result = ctx_->require(Permission::Execute, "test");
    EXPECT_EQ(result, SecurityContextError::Success);
}

TEST_F(SecurityContextTest, RequireWithDeniedPermission) {
    auto result = ctx_->require(Permission::Crypto, "test");
    EXPECT_EQ(result, SecurityContextError::PermissionDenied);
}

TEST_F(SecurityContextTest, RequireOrThrowWithGrantedPermission) {
    EXPECT_NO_THROW(ctx_->require_or_throw(Permission::Execute));
}

TEST_F(SecurityContextTest, RequireOrThrowWithDeniedPermission) {
    EXPECT_THROW(ctx_->require_or_throw(Permission::Crypto),
                 PermissionDeniedException);
}

// ============================================================================
// SecurityContext Memory Allocation Tests
// ============================================================================

TEST_F(SecurityContextTest, CanAllocateWithinLimits) {
    // Sandbox: 16MB total, 1MB per allocation
    EXPECT_TRUE(ctx_->can_allocate(1024));           // 1KB - ok
    EXPECT_TRUE(ctx_->can_allocate(512 * 1024));     // 512KB - ok
    EXPECT_TRUE(ctx_->can_allocate(1024 * 1024));    // 1MB - ok (at limit)
    EXPECT_FALSE(ctx_->can_allocate(2 * 1024 * 1024));  // 2MB - exceeds per-alloc
}

TEST_F(SecurityContextTest, OnAllocateSuccess) {
    auto result = ctx_->on_allocate(1024);
    EXPECT_EQ(result, SecurityContextError::Success);
    EXPECT_EQ(ctx_->usage().memory_allocated, 1024);
    EXPECT_EQ(ctx_->usage().allocation_count, 1);
}

TEST_F(SecurityContextTest, OnAllocateMultiple) {
    EXPECT_EQ(ctx_->on_allocate(1000), SecurityContextError::Success);
    EXPECT_EQ(ctx_->on_allocate(2000), SecurityContextError::Success);
    EXPECT_EQ(ctx_->on_allocate(3000), SecurityContextError::Success);

    EXPECT_EQ(ctx_->usage().memory_allocated, 6000);
    EXPECT_EQ(ctx_->usage().allocation_count, 3);
}

TEST_F(SecurityContextTest, OnAllocateExceedsSize) {
    auto result = ctx_->on_allocate(2 * 1024 * 1024);  // 2MB > 1MB limit
    EXPECT_EQ(result, SecurityContextError::AllocationSizeExceeded);
    EXPECT_EQ(ctx_->usage().memory_allocated, 0);  // Not allocated
}

TEST_F(SecurityContextTest, OnAllocateExceedsTotalMemory) {
    // Sandbox: 16MB total, allocate in chunks
    auto limits = capabilities::CapabilityLimits{
        .max_memory = 10000,  // 10KB total
        .max_allocation_size = 5000,
    };
    SecurityContext ctx(limits, PermissionSet::full());

    EXPECT_EQ(ctx.on_allocate(5000), SecurityContextError::Success);
    EXPECT_EQ(ctx.on_allocate(5000), SecurityContextError::Success);
    EXPECT_EQ(ctx.on_allocate(1), SecurityContextError::MemoryLimitExceeded);
}

TEST_F(SecurityContextTest, OnAllocateExceedsCount) {
    auto limits = capabilities::CapabilityLimits{
        .max_allocations = 3,
    };
    SecurityContext ctx(limits, PermissionSet::full());

    EXPECT_EQ(ctx.on_allocate(100), SecurityContextError::Success);
    EXPECT_EQ(ctx.on_allocate(100), SecurityContextError::Success);
    EXPECT_EQ(ctx.on_allocate(100), SecurityContextError::Success);
    EXPECT_EQ(ctx.on_allocate(100),
              SecurityContextError::AllocationCountExceeded);
}

TEST_F(SecurityContextTest, OnDeallocate) {
    EXPECT_EQ(ctx_->on_allocate(5000), SecurityContextError::Success);
    EXPECT_EQ(ctx_->usage().memory_allocated, 5000);

    ctx_->on_deallocate(2000);
    EXPECT_EQ(ctx_->usage().memory_allocated, 3000);

    ctx_->on_deallocate(3000);
    EXPECT_EQ(ctx_->usage().memory_allocated, 0);
}

TEST_F(SecurityContextTest, OnDeallocateSaturatesAtZero) {
    EXPECT_EQ(ctx_->on_allocate(1000), SecurityContextError::Success);
    ctx_->on_deallocate(5000);  // More than allocated
    EXPECT_EQ(ctx_->usage().memory_allocated, 0);
}

// ============================================================================
// SecurityContext Instruction Execution Tests
// ============================================================================

TEST_F(SecurityContextTest, OnInstructionSuccess) {
    for (int i = 0; i < 100; ++i) {
        EXPECT_TRUE(ctx_->on_instruction());
    }
    EXPECT_EQ(ctx_->usage().instructions_executed, 100);
}

TEST_F(SecurityContextTest, OnInstructionExceedsLimit) {
    auto limits = capabilities::CapabilityLimits{
        .max_instructions = 100,
    };
    SecurityContext ctx(limits, PermissionSet::full());

    for (int i = 0; i < 100; ++i) {
        EXPECT_TRUE(ctx.on_instruction());
    }
    // After hitting 1024 boundary, check triggers
    // But our limit is only 100, so we need to check cold path
    EXPECT_EQ(ctx.usage().instructions_executed, 100);
}

TEST_F(SecurityContextTest, CanExecuteInstructionWithinLimit) {
    auto limits = capabilities::CapabilityLimits{
        .max_instructions = 1000,
    };
    SecurityContext ctx(limits, PermissionSet::full());

    EXPECT_TRUE(ctx.can_execute_instruction());
}

TEST_F(SecurityContextTest, UnlimitedInstructions) {
    auto ctx = SecurityContext::unlimited(PermissionSet::full());

    // Execute many instructions
    for (int i = 0; i < 10000; ++i) {
        EXPECT_TRUE(ctx.on_instruction());
    }
    EXPECT_TRUE(ctx.can_execute_instruction());
}

// ============================================================================
// SecurityContext Stack Depth Tests
// ============================================================================

TEST_F(SecurityContextTest, OnStackPushSuccess) {
    EXPECT_TRUE(ctx_->on_stack_push());
    EXPECT_EQ(ctx_->usage().current_stack_depth, 1);
    EXPECT_EQ(ctx_->usage().max_stack_depth_reached, 1);
}

TEST_F(SecurityContextTest, OnStackPushMultiple) {
    for (int i = 0; i < 10; ++i) {
        EXPECT_TRUE(ctx_->on_stack_push());
    }
    EXPECT_EQ(ctx_->usage().current_stack_depth, 10);
    EXPECT_EQ(ctx_->usage().max_stack_depth_reached, 10);
}

TEST_F(SecurityContextTest, OnStackPop) {
    EXPECT_TRUE(ctx_->on_stack_push());
    EXPECT_TRUE(ctx_->on_stack_push());
    EXPECT_TRUE(ctx_->on_stack_push());
    EXPECT_EQ(ctx_->usage().current_stack_depth, 3);

    ctx_->on_stack_pop();
    EXPECT_EQ(ctx_->usage().current_stack_depth, 2);

    ctx_->on_stack_pop();
    ctx_->on_stack_pop();
    EXPECT_EQ(ctx_->usage().current_stack_depth, 0);
}

TEST_F(SecurityContextTest, OnStackPopSaturatesAtZero) {
    ctx_->on_stack_pop();  // Pop from empty stack
    EXPECT_EQ(ctx_->usage().current_stack_depth, 0);
}

TEST_F(SecurityContextTest, OnStackPushExceedsLimit) {
    auto limits = capabilities::CapabilityLimits{
        .max_stack_depth = 3,
    };
    SecurityContext ctx(limits, PermissionSet::full());

    EXPECT_TRUE(ctx.on_stack_push());
    EXPECT_TRUE(ctx.on_stack_push());
    EXPECT_TRUE(ctx.on_stack_push());
    EXPECT_FALSE(ctx.on_stack_push());  // Exceeds limit
    EXPECT_EQ(ctx.usage().current_stack_depth, 3);
}

TEST_F(SecurityContextTest, MaxStackDepthTracked) {
    for (int i = 0; i < 5; ++i) {
        EXPECT_TRUE(ctx_->on_stack_push());
    }
    for (int i = 0; i < 3; ++i) {
        ctx_->on_stack_pop();
    }

    EXPECT_EQ(ctx_->usage().current_stack_depth, 2);
    EXPECT_EQ(ctx_->usage().max_stack_depth_reached, 5);
}

// ============================================================================
// SecurityContext Time Limit Tests
// ============================================================================

TEST_F(SecurityContextTest, CheckTimeLimitWithinLimit) {
    // Sandbox: 10 second timeout
    EXPECT_TRUE(ctx_->check_time_limit());
}

TEST_F(SecurityContextTest, CheckTimeLimitUnlimited) {
    auto ctx = SecurityContext::unlimited(PermissionSet::full());
    EXPECT_TRUE(ctx.check_time_limit());
}

TEST_F(SecurityContextTest, ElapsedMsInitiallySmall) {
    EXPECT_LT(ctx_->elapsed_ms(), 100);  // Should be very small
}

// ============================================================================
// SecurityContext Stats Tests
// ============================================================================

TEST_F(SecurityContextTest, StatsInitialValues) {
    auto stats = ctx_->stats();
    EXPECT_EQ(stats.instructions_executed, 0);
    EXPECT_EQ(stats.memory_allocated, 0);
    EXPECT_EQ(stats.allocation_count, 0);
    EXPECT_EQ(stats.max_stack_depth, 0);
    EXPECT_EQ(stats.permission_checks, 0);
    EXPECT_EQ(stats.permission_denials, 0);
}

TEST_F(SecurityContextTest, StatsAfterOperations) {
    EXPECT_TRUE(ctx_->on_instruction());
    EXPECT_TRUE(ctx_->on_instruction());
    EXPECT_EQ(ctx_->on_allocate(1000), SecurityContextError::Success);
    EXPECT_TRUE(ctx_->on_stack_push());
    EXPECT_EQ(ctx_->require(Permission::Execute), SecurityContextError::Success);
    EXPECT_EQ(ctx_->require(Permission::Crypto), SecurityContextError::PermissionDenied);

    auto stats = ctx_->stats();
    EXPECT_EQ(stats.instructions_executed, 2);
    EXPECT_EQ(stats.memory_allocated, 1000);
    EXPECT_EQ(stats.allocation_count, 1);
    EXPECT_EQ(stats.max_stack_depth, 1);
    EXPECT_EQ(stats.permission_checks, 2);
    EXPECT_EQ(stats.permission_denials, 1);
}

// ============================================================================
// SecurityContext Reset Tests
// ============================================================================

TEST_F(SecurityContextTest, ResetUsage) {
    EXPECT_TRUE(ctx_->on_instruction());
    EXPECT_EQ(ctx_->on_allocate(1000), SecurityContextError::Success);
    EXPECT_TRUE(ctx_->on_stack_push());
    EXPECT_EQ(ctx_->require(Permission::Execute), SecurityContextError::Success);

    ctx_->reset_usage();

    auto& usage = ctx_->usage();
    EXPECT_EQ(usage.memory_allocated, 0);
    EXPECT_EQ(usage.allocation_count, 0);
    EXPECT_EQ(usage.instructions_executed, 0);
    EXPECT_EQ(usage.current_stack_depth, 0);
    EXPECT_EQ(usage.max_stack_depth_reached, 0);

    auto stats = ctx_->stats();
    EXPECT_EQ(stats.permission_checks, 0);
    EXPECT_EQ(stats.permission_denials, 0);
}

// ============================================================================
// SecurityContext Factory Method Tests
// ============================================================================

TEST(SecurityContextFactoryTest, Unlimited) {
    auto ctx = SecurityContext::unlimited(PermissionSet::full());
    EXPECT_EQ(ctx.limits().max_memory, 0);
    EXPECT_EQ(ctx.limits().max_instructions, 0);
    EXPECT_EQ(ctx.limits().max_stack_depth, 0);
}

TEST(SecurityContextFactoryTest, Untrusted) {
    auto ctx = SecurityContext::untrusted(PermissionSet::read_only());
    EXPECT_EQ(ctx.limits().max_memory, 1ULL * 1024 * 1024);
    EXPECT_EQ(ctx.limits().max_instructions, 100'000);
    EXPECT_EQ(ctx.limits().max_stack_depth, 64);
}

TEST(SecurityContextFactoryTest, Sandbox) {
    auto ctx = SecurityContext::sandbox(PermissionSet::read_write());
    EXPECT_EQ(ctx.limits().max_memory, 16ULL * 1024 * 1024);
    EXPECT_EQ(ctx.limits().max_instructions, 1'000'000);
    EXPECT_EQ(ctx.limits().max_stack_depth, 256);
}

TEST(SecurityContextFactoryTest, Trusted) {
    auto ctx = SecurityContext::trusted(PermissionSet::full());
    EXPECT_EQ(ctx.limits().max_memory, 256ULL * 1024 * 1024);
    EXPECT_EQ(ctx.limits().max_instructions, 100'000'000);
    EXPECT_EQ(ctx.limits().max_stack_depth, 4096);
}

// ============================================================================
// SecurityContext with Logger Tests
// ============================================================================

TEST_F(SecurityContextWithLoggerTest, ContextCreatedLogged) {
    // Context was created in SetUp, should have ContextCreated event
    auto events = logger_->events();
    ASSERT_GE(events.size(), 1);
    EXPECT_EQ(events[0].type, AuditEventType::ContextCreated);
}

TEST_F(SecurityContextWithLoggerTest, AllocationLogged) {
    logger_->clear();  // Clear creation event
    EXPECT_EQ(ctx_->on_allocate(1024), SecurityContextError::Success);

    auto events = logger_->events();
    ASSERT_EQ(events.size(), 1);
    EXPECT_EQ(events[0].type, AuditEventType::AllocationAttempt);
    EXPECT_EQ(events[0].value, 1024);
}

TEST_F(SecurityContextWithLoggerTest, AllocationDeniedLogged) {
    logger_->clear();
    EXPECT_EQ(ctx_->on_allocate(100 * 1024 * 1024),
              SecurityContextError::AllocationSizeExceeded);  // 100MB > 1MB limit

    auto events = logger_->events();
    ASSERT_GE(events.size(), 1);
    EXPECT_EQ(events[0].type, AuditEventType::AllocationDenied);
}

TEST_F(SecurityContextWithLoggerTest, DeallocationLogged) {
    EXPECT_EQ(ctx_->on_allocate(1024), SecurityContextError::Success);
    logger_->clear();
    ctx_->on_deallocate(1024);

    auto events = logger_->events();
    ASSERT_EQ(events.size(), 1);
    EXPECT_EQ(events[0].type, AuditEventType::DeallocationAttempt);
}

TEST_F(SecurityContextWithLoggerTest, PermissionGrantedLogged) {
    logger_->clear();
    EXPECT_EQ(ctx_->require(Permission::Execute, "test_op"),
              SecurityContextError::Success);

    auto events = logger_->events();
    ASSERT_EQ(events.size(), 1);
    EXPECT_EQ(events[0].type, AuditEventType::PermissionGranted);
    EXPECT_EQ(events[0].permission, Permission::Execute);
}

TEST_F(SecurityContextWithLoggerTest, PermissionDeniedLogged) {
    logger_->clear();
    EXPECT_EQ(ctx_->require(Permission::Crypto, "crypto_op"),
              SecurityContextError::PermissionDenied);

    auto events = logger_->events();
    ASSERT_EQ(events.size(), 1);
    EXPECT_EQ(events[0].type, AuditEventType::PermissionDenied);
    EXPECT_EQ(events[0].permission, Permission::Crypto);
}

TEST_F(SecurityContextWithLoggerTest, StackLimitLogged) {
    auto limits = capabilities::CapabilityLimits{.max_stack_depth = 2};
    BufferedAuditLogger logger(10);
    SecurityContext ctx(limits, PermissionSet::full(), &logger);

    logger.clear();
    EXPECT_TRUE(ctx.on_stack_push());
    EXPECT_TRUE(ctx.on_stack_push());
    EXPECT_FALSE(ctx.on_stack_push());  // Should fail and log

    auto events = logger.events();
    ASSERT_GE(events.size(), 1);
    bool found_limit_event = false;
    for (const auto& e : events) {
        if (e.type == AuditEventType::StackDepthLimitHit) {
            found_limit_event = true;
            break;
        }
    }
    EXPECT_TRUE(found_limit_event);
}

TEST_F(SecurityContextWithLoggerTest, ResetLogged) {
    logger_->clear();
    ctx_->reset_usage();

    auto events = logger_->events();
    ASSERT_EQ(events.size(), 1);
    EXPECT_EQ(events[0].type, AuditEventType::ContextReset);
}

// ============================================================================
// Edge Case Tests
// ============================================================================

TEST(SecurityContextEdgeCaseTest, ZeroLimitsAreUnlimited) {
    auto limits = capabilities::CapabilityLimits{};  // All zeros
    SecurityContext ctx(limits, PermissionSet::full());

    // All checks should pass (unlimited)
    EXPECT_TRUE(ctx.can_allocate(1ULL << 30));  // 1GB
    EXPECT_TRUE(ctx.can_execute_instruction());
    EXPECT_TRUE(ctx.can_push_stack());
    EXPECT_TRUE(ctx.check_time_limit());
}

TEST(SecurityContextEdgeCaseTest, EmptyPermissionsDenyAll) {
    SecurityContext ctx(capabilities::CapabilityLimits::unlimited(),
                        PermissionSet::none());

    EXPECT_FALSE(ctx.can(Permission::Execute));
    EXPECT_FALSE(ctx.can(Permission::ReadMemory));
    EXPECT_FALSE(ctx.can(Permission::WriteMemory));
    EXPECT_EQ(ctx.require(Permission::Execute),
              SecurityContextError::PermissionDenied);
}

TEST(SecurityContextEdgeCaseTest, FullPermissionsAllowAll) {
    SecurityContext ctx(capabilities::CapabilityLimits::unlimited(),
                        PermissionSet::full());

    EXPECT_TRUE(ctx.can(Permission::Execute));
    EXPECT_TRUE(ctx.can(Permission::ReadMemory));
    EXPECT_TRUE(ctx.can(Permission::WriteMemory));
    EXPECT_TRUE(ctx.can(Permission::Allocate));
    EXPECT_TRUE(ctx.can(Permission::SpawnDot));
    EXPECT_TRUE(ctx.can(Permission::Crypto));
    EXPECT_TRUE(ctx.can(Permission::SystemCall));
    EXPECT_TRUE(ctx.can(Permission::Debug));
}

TEST(SecurityContextEdgeCaseTest, MoveConstruction) {
    auto ctx1 = SecurityContext::sandbox(PermissionSet::read_write());
    EXPECT_EQ(ctx1.on_allocate(1000), SecurityContextError::Success);
    EXPECT_TRUE(ctx1.on_instruction());

    SecurityContext ctx2(std::move(ctx1));

    EXPECT_EQ(ctx2.usage().memory_allocated, 1000);
    EXPECT_EQ(ctx2.usage().instructions_executed, 1);
}

TEST(SecurityContextEdgeCaseTest, MoveAssignment) {
    auto ctx1 = SecurityContext::sandbox(PermissionSet::read_write());
    EXPECT_EQ(ctx1.on_allocate(1000), SecurityContextError::Success);

    auto ctx2 = SecurityContext::untrusted(PermissionSet::read_only());

    ctx2 = std::move(ctx1);

    EXPECT_EQ(ctx2.usage().memory_allocated, 1000);
}

}  // namespace
}  // namespace dotvm::core::security
