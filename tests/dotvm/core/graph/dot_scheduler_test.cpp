/// @file dot_scheduler_test.cpp
/// @brief Unit tests for DotScheduler

#include <algorithm>
#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "dotvm/core/graph/dot_scheduler.hpp"

namespace dotvm::core::graph {
namespace {

// Helper to create simple bytecode
std::vector<std::uint8_t> make_bytecode(std::size_t size = 4) {
    std::vector<std::uint8_t> bytecode(size);
    for (std::size_t i = 0; i < size; ++i) {
        bytecode[i] = static_cast<std::uint8_t>(i & 0xFF);
    }
    return bytecode;
}

// ============================================================================
// Phase 1: Core Types Tests
// ============================================================================

TEST(DotHandleTest, DefaultConstruction) {
    DotHandle handle;
    EXPECT_EQ(handle.index, 0U);
    EXPECT_EQ(handle.generation, 0U);
    EXPECT_FALSE(handle.is_valid());
}

TEST(DotHandleTest, InvalidHandle) {
    DotHandle invalid = DotHandle::invalid();
    EXPECT_EQ(invalid.index, 0U);
    EXPECT_EQ(invalid.generation, 0U);
    EXPECT_FALSE(invalid.is_valid());
}

TEST(DotHandleTest, ValidHandle) {
    DotHandle handle{1, 1};
    EXPECT_TRUE(handle.is_valid());
}

TEST(DotHandleTest, Equality) {
    DotHandle h1{1, 2};
    DotHandle h2{1, 2};
    DotHandle h3{1, 3};
    DotHandle h4{2, 2};

    EXPECT_EQ(h1, h2);
    EXPECT_NE(h1, h3);
    EXPECT_NE(h1, h4);
}

TEST(DotStateTest, EnumValues) {
    EXPECT_EQ(static_cast<std::uint8_t>(DotState::Pending), 0);
    EXPECT_EQ(static_cast<std::uint8_t>(DotState::Ready), 1);
    EXPECT_EQ(static_cast<std::uint8_t>(DotState::Running), 2);
    EXPECT_EQ(static_cast<std::uint8_t>(DotState::Done), 3);
    EXPECT_EQ(static_cast<std::uint8_t>(DotState::Failed), 4);
    EXPECT_EQ(static_cast<std::uint8_t>(DotState::Cancelled), 5);
}

TEST(DotStateTest, ToString) {
    EXPECT_EQ(to_string(DotState::Pending), "Pending");
    EXPECT_EQ(to_string(DotState::Ready), "Ready");
    EXPECT_EQ(to_string(DotState::Running), "Running");
    EXPECT_EQ(to_string(DotState::Done), "Done");
    EXPECT_EQ(to_string(DotState::Failed), "Failed");
    EXPECT_EQ(to_string(DotState::Cancelled), "Cancelled");
}

TEST(SchedulerErrorTest, EnumValues) {
    EXPECT_EQ(static_cast<std::uint8_t>(SchedulerError::HandleNotFound), 160);
    EXPECT_EQ(static_cast<std::uint8_t>(SchedulerError::MaxPendingExceeded), 161);
    EXPECT_EQ(static_cast<std::uint8_t>(SchedulerError::DotAlreadyCancelled), 162);
    EXPECT_EQ(static_cast<std::uint8_t>(SchedulerError::DotAlreadyCompleted), 163);
    EXPECT_EQ(static_cast<std::uint8_t>(SchedulerError::DependencyNotFound), 164);
    EXPECT_EQ(static_cast<std::uint8_t>(SchedulerError::InvalidPriority), 165);
    EXPECT_EQ(static_cast<std::uint8_t>(SchedulerError::InvalidBytecode), 166);
    EXPECT_EQ(static_cast<std::uint8_t>(SchedulerError::ShuttingDown), 167);
    EXPECT_EQ(static_cast<std::uint8_t>(SchedulerError::WaitTimeout), 168);
    EXPECT_EQ(static_cast<std::uint8_t>(SchedulerError::InternalGraphError), 169);
    EXPECT_EQ(static_cast<std::uint8_t>(SchedulerError::ExecutionFailed), 170);
    EXPECT_EQ(static_cast<std::uint8_t>(SchedulerError::StaleHandle), 171);
    EXPECT_EQ(static_cast<std::uint8_t>(SchedulerError::DotNotRunning), 172);
    EXPECT_EQ(static_cast<std::uint8_t>(SchedulerError::CycleDetected), 173);
}

TEST(SchedulerErrorTest, ToString) {
    EXPECT_EQ(to_string(SchedulerError::HandleNotFound), "HandleNotFound");
    EXPECT_EQ(to_string(SchedulerError::MaxPendingExceeded), "MaxPendingExceeded");
    EXPECT_EQ(to_string(SchedulerError::DotAlreadyCancelled), "DotAlreadyCancelled");
    EXPECT_EQ(to_string(SchedulerError::DotAlreadyCompleted), "DotAlreadyCompleted");
    EXPECT_EQ(to_string(SchedulerError::DependencyNotFound), "DependencyNotFound");
    EXPECT_EQ(to_string(SchedulerError::InvalidPriority), "InvalidPriority");
    EXPECT_EQ(to_string(SchedulerError::InvalidBytecode), "InvalidBytecode");
    EXPECT_EQ(to_string(SchedulerError::ShuttingDown), "ShuttingDown");
    EXPECT_EQ(to_string(SchedulerError::WaitTimeout), "WaitTimeout");
    EXPECT_EQ(to_string(SchedulerError::InternalGraphError), "InternalGraphError");
    EXPECT_EQ(to_string(SchedulerError::ExecutionFailed), "ExecutionFailed");
    EXPECT_EQ(to_string(SchedulerError::StaleHandle), "StaleHandle");
    EXPECT_EQ(to_string(SchedulerError::DotNotRunning), "DotNotRunning");
    EXPECT_EQ(to_string(SchedulerError::CycleDetected), "CycleDetected");
}

TEST(SchedulerConfigTest, Defaults) {
    auto config = SchedulerConfig::defaults();
    EXPECT_EQ(config.max_pending, 10000U);
    EXPECT_EQ(config.min_priority, -1000);
    EXPECT_EQ(config.max_priority, 1000);
}

// ============================================================================
// Phase 2: Basic Submit/State Tests
// ============================================================================

TEST(DotSchedulerTest, SubmitSingleDotReturnsValidHandle) {
    DotScheduler scheduler;
    auto bytecode = make_bytecode();

    auto result = scheduler.submit(bytecode);
    ASSERT_TRUE(result.is_ok());

    DotHandle handle = result.value();
    EXPECT_TRUE(handle.is_valid());
    EXPECT_EQ(handle.generation, 1U);  // First generation
}

TEST(DotSchedulerTest, GetStateReturnsPendingOrReadyForNewDot) {
    DotScheduler scheduler;
    auto bytecode = make_bytecode();

    auto submit_result = scheduler.submit(bytecode);
    ASSERT_TRUE(submit_result.is_ok());
    DotHandle handle = submit_result.value();

    auto state_result = scheduler.get_state(handle);
    ASSERT_TRUE(state_result.is_ok());
    // Without dependencies, should be Ready immediately
    EXPECT_EQ(state_result.value(), DotState::Ready);
}

TEST(DotSchedulerTest, SubmitEmptyBytecodeFailsWithInvalidBytecode) {
    DotScheduler scheduler;
    std::vector<std::uint8_t> empty_bytecode;

    auto result = scheduler.submit(empty_bytecode);
    ASSERT_TRUE(result.is_err());
    EXPECT_EQ(result.error(), SchedulerError::InvalidBytecode);
}

TEST(DotSchedulerTest, SubmitAfterMaxPendingFailsWithMaxPendingExceeded) {
    SchedulerConfig config;
    config.max_pending = 2;
    DotScheduler scheduler(config);

    auto bytecode = make_bytecode();

    auto result1 = scheduler.submit(bytecode);
    ASSERT_TRUE(result1.is_ok());

    auto result2 = scheduler.submit(bytecode);
    ASSERT_TRUE(result2.is_ok());

    auto result3 = scheduler.submit(bytecode);
    ASSERT_TRUE(result3.is_err());
    EXPECT_EQ(result3.error(), SchedulerError::MaxPendingExceeded);
}

TEST(DotSchedulerTest, SubmitWithInvalidPriorityFails) {
    DotScheduler scheduler;
    auto bytecode = make_bytecode();

    auto result_low = scheduler.submit(bytecode, {}, -2000);
    ASSERT_TRUE(result_low.is_err());
    EXPECT_EQ(result_low.error(), SchedulerError::InvalidPriority);

    auto result_high = scheduler.submit(bytecode, {}, 2000);
    ASSERT_TRUE(result_high.is_err());
    EXPECT_EQ(result_high.error(), SchedulerError::InvalidPriority);
}

// ============================================================================
// Phase 3: Ready Queue Tests
// ============================================================================

TEST(DotSchedulerTest, DotWithoutDepsIsImmediatelyReady) {
    DotScheduler scheduler;
    auto bytecode = make_bytecode();

    auto submit_result = scheduler.submit(bytecode);
    ASSERT_TRUE(submit_result.is_ok());
    DotHandle handle = submit_result.value();

    auto state_result = scheduler.get_state(handle);
    ASSERT_TRUE(state_result.is_ok());
    EXPECT_EQ(state_result.value(), DotState::Ready);
}

TEST(DotSchedulerTest, TryPopReadyReturnsHandleWhenReady) {
    DotScheduler scheduler;
    auto bytecode = make_bytecode();

    auto submit_result = scheduler.submit(bytecode);
    ASSERT_TRUE(submit_result.is_ok());
    DotHandle submitted = submit_result.value();

    auto popped = scheduler.try_pop_ready();
    ASSERT_TRUE(popped.has_value());
    EXPECT_EQ(popped.value(), submitted);

    // Should now be Running
    auto state_result = scheduler.get_state(submitted);
    ASSERT_TRUE(state_result.is_ok());
    EXPECT_EQ(state_result.value(), DotState::Running);
}

TEST(DotSchedulerTest, TryPopReadyReturnsNulloptWhenEmpty) {
    DotScheduler scheduler;

    auto popped = scheduler.try_pop_ready();
    EXPECT_FALSE(popped.has_value());
}

TEST(DotSchedulerTest, HigherPriorityPopsFirst) {
    DotScheduler scheduler;
    auto bytecode = make_bytecode();

    auto low_result = scheduler.submit(bytecode, {}, 0);
    ASSERT_TRUE(low_result.is_ok());
    DotHandle low_handle = low_result.value();

    auto high_result = scheduler.submit(bytecode, {}, 100);
    ASSERT_TRUE(high_result.is_ok());
    DotHandle high_handle = high_result.value();

    // Higher priority should pop first
    auto first = scheduler.try_pop_ready();
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(first.value(), high_handle);

    auto second = scheduler.try_pop_ready();
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(second.value(), low_handle);
}

TEST(DotSchedulerTest, SamePriorityUsesFIFO) {
    DotScheduler scheduler;
    auto bytecode = make_bytecode();

    auto first_result = scheduler.submit(bytecode, {}, 50);
    ASSERT_TRUE(first_result.is_ok());
    DotHandle first_handle = first_result.value();

    auto second_result = scheduler.submit(bytecode, {}, 50);
    ASSERT_TRUE(second_result.is_ok());
    DotHandle second_handle = second_result.value();

    // FIFO order at same priority
    auto popped_first = scheduler.try_pop_ready();
    ASSERT_TRUE(popped_first.has_value());
    EXPECT_EQ(popped_first.value(), first_handle);

    auto popped_second = scheduler.try_pop_ready();
    ASSERT_TRUE(popped_second.has_value());
    EXPECT_EQ(popped_second.value(), second_handle);
}

// ============================================================================
// Phase 4: Dependencies Tests
// ============================================================================

TEST(DotSchedulerTest, DotWithDepsStartsAsPending) {
    DotScheduler scheduler;
    auto bytecode = make_bytecode();

    // Submit dependency first
    auto dep_result = scheduler.submit(bytecode);
    ASSERT_TRUE(dep_result.is_ok());
    DotHandle dep_handle = dep_result.value();

    // Submit dependent
    std::vector<DotHandle> deps = {dep_handle};
    auto dependent_result = scheduler.submit(bytecode, deps);
    ASSERT_TRUE(dependent_result.is_ok());
    DotHandle dependent_handle = dependent_result.value();

    // Dependent should be Pending (not Ready yet)
    auto state_result = scheduler.get_state(dependent_handle);
    ASSERT_TRUE(state_result.is_ok());
    EXPECT_EQ(state_result.value(), DotState::Pending);
}

TEST(DotSchedulerTest, NotifyCompletePromotesDependents) {
    DotScheduler scheduler;
    auto bytecode = make_bytecode();

    // Submit dependency
    auto dep_result = scheduler.submit(bytecode);
    ASSERT_TRUE(dep_result.is_ok());
    DotHandle dep_handle = dep_result.value();

    // Submit dependent
    std::vector<DotHandle> deps = {dep_handle};
    auto dependent_result = scheduler.submit(bytecode, deps);
    ASSERT_TRUE(dependent_result.is_ok());
    DotHandle dependent_handle = dependent_result.value();

    // Pop and complete dependency
    auto popped = scheduler.try_pop_ready();
    ASSERT_TRUE(popped.has_value());
    EXPECT_EQ(popped.value(), dep_handle);

    auto complete_result = scheduler.notify_complete(dep_handle);
    ASSERT_TRUE(complete_result.is_ok());

    // Dependent should now be Ready
    auto state_result = scheduler.get_state(dependent_handle);
    ASSERT_TRUE(state_result.is_ok());
    EXPECT_EQ(state_result.value(), DotState::Ready);
}

TEST(DotSchedulerTest, InvalidDependencyFails) {
    DotScheduler scheduler;
    auto bytecode = make_bytecode();

    DotHandle invalid_dep{999, 1};
    std::vector<DotHandle> deps = {invalid_dep};

    auto result = scheduler.submit(bytecode, deps);
    ASSERT_TRUE(result.is_err());
    EXPECT_EQ(result.error(), SchedulerError::DependencyNotFound);
}

TEST(DotSchedulerTest, CancelledDependencyCannotBeDepended) {
    DotScheduler scheduler;
    auto bytecode = make_bytecode();

    // Submit and cancel a Dot
    auto dep_result = scheduler.submit(bytecode);
    ASSERT_TRUE(dep_result.is_ok());
    DotHandle dep_handle = dep_result.value();

    auto cancel_result = scheduler.cancel(dep_handle);
    ASSERT_TRUE(cancel_result.is_ok());

    // Try to depend on cancelled Dot
    std::vector<DotHandle> deps = {dep_handle};
    auto dependent_result = scheduler.submit(bytecode, deps);
    ASSERT_TRUE(dependent_result.is_err());
    EXPECT_EQ(dependent_result.error(), SchedulerError::DependencyNotFound);
}

// ============================================================================
// Phase 5: Worker Interface Tests
// ============================================================================

TEST(DotSchedulerTest, GetBytecodeReturnsSpanForRunningDot) {
    DotScheduler scheduler;
    auto bytecode = make_bytecode(8);

    auto submit_result = scheduler.submit(bytecode);
    ASSERT_TRUE(submit_result.is_ok());
    DotHandle handle = submit_result.value();

    // Pop to Running
    auto popped = scheduler.try_pop_ready();
    ASSERT_TRUE(popped.has_value());

    // Get bytecode
    auto bc_result = scheduler.get_bytecode(handle);
    ASSERT_TRUE(bc_result.is_ok());
    auto span = bc_result.value();
    EXPECT_EQ(span.size(), 8U);
    for (std::size_t i = 0; i < span.size(); ++i) {
        EXPECT_EQ(span[i], bytecode[i]);
    }
}

TEST(DotSchedulerTest, NotifyCompleteTransitionsToDone) {
    DotScheduler scheduler;
    auto bytecode = make_bytecode();

    auto submit_result = scheduler.submit(bytecode);
    ASSERT_TRUE(submit_result.is_ok());
    DotHandle handle = submit_result.value();

    auto popped = scheduler.try_pop_ready();
    ASSERT_TRUE(popped.has_value());

    auto complete_result = scheduler.notify_complete(handle);
    ASSERT_TRUE(complete_result.is_ok());

    auto state_result = scheduler.get_state(handle);
    ASSERT_TRUE(state_result.is_ok());
    EXPECT_EQ(state_result.value(), DotState::Done);
}

TEST(DotSchedulerTest, NotifyFailedTransitionsToFailed) {
    DotScheduler scheduler;
    auto bytecode = make_bytecode();

    auto submit_result = scheduler.submit(bytecode);
    ASSERT_TRUE(submit_result.is_ok());
    DotHandle handle = submit_result.value();

    auto popped = scheduler.try_pop_ready();
    ASSERT_TRUE(popped.has_value());

    auto failed_result = scheduler.notify_failed(handle);
    ASSERT_TRUE(failed_result.is_ok());

    auto state_result = scheduler.get_state(handle);
    ASSERT_TRUE(state_result.is_ok());
    EXPECT_EQ(state_result.value(), DotState::Failed);
}

TEST(DotSchedulerTest, DoubleNotifyCompleteFails) {
    DotScheduler scheduler;
    auto bytecode = make_bytecode();

    auto submit_result = scheduler.submit(bytecode);
    ASSERT_TRUE(submit_result.is_ok());
    DotHandle handle = submit_result.value();

    auto popped = scheduler.try_pop_ready();
    ASSERT_TRUE(popped.has_value());

    auto complete_result = scheduler.notify_complete(handle);
    ASSERT_TRUE(complete_result.is_ok());

    auto double_complete = scheduler.notify_complete(handle);
    ASSERT_TRUE(double_complete.is_err());
    EXPECT_EQ(double_complete.error(), SchedulerError::DotAlreadyCompleted);
}

TEST(DotSchedulerTest, NotifyCompleteOnNonRunningFails) {
    DotScheduler scheduler;
    auto bytecode = make_bytecode();

    auto submit_result = scheduler.submit(bytecode);
    ASSERT_TRUE(submit_result.is_ok());
    DotHandle handle = submit_result.value();

    // Don't pop - still Ready
    auto complete_result = scheduler.notify_complete(handle);
    ASSERT_TRUE(complete_result.is_err());
    EXPECT_EQ(complete_result.error(), SchedulerError::DotNotRunning);
}

// ============================================================================
// Phase 6: Wait Operations Tests
// ============================================================================

TEST(DotSchedulerTest, WaitOnDoneReturnsImmediately) {
    DotScheduler scheduler;
    auto bytecode = make_bytecode();

    auto submit_result = scheduler.submit(bytecode);
    ASSERT_TRUE(submit_result.is_ok());
    DotHandle handle = submit_result.value();

    // Complete the Dot
    auto popped = scheduler.try_pop_ready();
    ASSERT_TRUE(popped.has_value());
    auto complete_result = scheduler.notify_complete(handle);
    ASSERT_TRUE(complete_result.is_ok());

    // Wait should return immediately
    auto wait_result = scheduler.wait(handle);
    ASSERT_TRUE(wait_result.is_ok());
}

TEST(DotSchedulerTest, WaitBlocksUntilComplete) {
    DotScheduler scheduler;
    auto bytecode = make_bytecode();

    auto submit_result = scheduler.submit(bytecode);
    ASSERT_TRUE(submit_result.is_ok());
    DotHandle handle = submit_result.value();

    std::atomic<bool> wait_started{false};
    std::atomic<bool> wait_completed{false};

    // Start waiting in another thread
    std::thread waiter([&]() {
        wait_started.store(true);
        auto result = scheduler.wait(handle);
        EXPECT_TRUE(result.is_ok());
        wait_completed.store(true);
    });

    // Wait for waiter to start
    while (!wait_started.load()) {
        std::this_thread::yield();
    }

    // Small delay to ensure waiter is actually waiting
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    EXPECT_FALSE(wait_completed.load());

    // Complete the Dot
    auto popped = scheduler.try_pop_ready();
    ASSERT_TRUE(popped.has_value());
    auto complete_result = scheduler.notify_complete(handle);
    ASSERT_TRUE(complete_result.is_ok());

    waiter.join();
    EXPECT_TRUE(wait_completed.load());
}

TEST(DotSchedulerTest, WaitForWithTimeoutReturnsWaitTimeout) {
    DotScheduler scheduler;
    auto bytecode = make_bytecode();

    // Submit with a dependency so it stays Pending
    auto dep_result = scheduler.submit(bytecode);
    ASSERT_TRUE(dep_result.is_ok());
    DotHandle dep_handle = dep_result.value();

    std::vector<DotHandle> deps = {dep_handle};
    auto submit_result = scheduler.submit(bytecode, deps);
    ASSERT_TRUE(submit_result.is_ok());
    DotHandle handle = submit_result.value();

    // Wait with timeout (dependency not completed)
    auto wait_result = scheduler.wait_for(handle, std::chrono::milliseconds(10));
    ASSERT_TRUE(wait_result.is_err());
    EXPECT_EQ(wait_result.error(), SchedulerError::WaitTimeout);
}

TEST(DotSchedulerTest, WaitOnFailedReturnsExecutionFailed) {
    DotScheduler scheduler;
    auto bytecode = make_bytecode();

    auto submit_result = scheduler.submit(bytecode);
    ASSERT_TRUE(submit_result.is_ok());
    DotHandle handle = submit_result.value();

    auto popped = scheduler.try_pop_ready();
    ASSERT_TRUE(popped.has_value());

    auto failed_result = scheduler.notify_failed(handle);
    ASSERT_TRUE(failed_result.is_ok());

    auto wait_result = scheduler.wait(handle);
    ASSERT_TRUE(wait_result.is_err());
    EXPECT_EQ(wait_result.error(), SchedulerError::ExecutionFailed);
}

// ============================================================================
// Phase 7: Cancellation Tests
// ============================================================================

TEST(DotSchedulerTest, CancelPendingDotSucceeds) {
    DotScheduler scheduler;
    auto bytecode = make_bytecode();

    // Create dependency to keep second dot pending
    auto dep_result = scheduler.submit(bytecode);
    ASSERT_TRUE(dep_result.is_ok());
    DotHandle dep_handle = dep_result.value();

    std::vector<DotHandle> deps = {dep_handle};
    auto submit_result = scheduler.submit(bytecode, deps);
    ASSERT_TRUE(submit_result.is_ok());
    DotHandle handle = submit_result.value();

    auto cancel_result = scheduler.cancel(handle);
    ASSERT_TRUE(cancel_result.is_ok());

    auto state_result = scheduler.get_state(handle);
    ASSERT_TRUE(state_result.is_ok());
    EXPECT_EQ(state_result.value(), DotState::Cancelled);
}

TEST(DotSchedulerTest, CancelReadyDotSucceeds) {
    DotScheduler scheduler;
    auto bytecode = make_bytecode();

    auto submit_result = scheduler.submit(bytecode);
    ASSERT_TRUE(submit_result.is_ok());
    DotHandle handle = submit_result.value();

    auto cancel_result = scheduler.cancel(handle);
    ASSERT_TRUE(cancel_result.is_ok());

    auto state_result = scheduler.get_state(handle);
    ASSERT_TRUE(state_result.is_ok());
    EXPECT_EQ(state_result.value(), DotState::Cancelled);
}

TEST(DotSchedulerTest, CancelRunningDotSucceeds) {
    DotScheduler scheduler;
    auto bytecode = make_bytecode();

    auto submit_result = scheduler.submit(bytecode);
    ASSERT_TRUE(submit_result.is_ok());
    DotHandle handle = submit_result.value();

    auto popped = scheduler.try_pop_ready();
    ASSERT_TRUE(popped.has_value());

    auto cancel_result = scheduler.cancel(handle);
    ASSERT_TRUE(cancel_result.is_ok());

    auto state_result = scheduler.get_state(handle);
    ASSERT_TRUE(state_result.is_ok());
    EXPECT_EQ(state_result.value(), DotState::Cancelled);
}

TEST(DotSchedulerTest, CancelDoneDotFails) {
    DotScheduler scheduler;
    auto bytecode = make_bytecode();

    auto submit_result = scheduler.submit(bytecode);
    ASSERT_TRUE(submit_result.is_ok());
    DotHandle handle = submit_result.value();

    auto popped = scheduler.try_pop_ready();
    ASSERT_TRUE(popped.has_value());

    auto complete_result = scheduler.notify_complete(handle);
    ASSERT_TRUE(complete_result.is_ok());

    auto cancel_result = scheduler.cancel(handle);
    ASSERT_TRUE(cancel_result.is_err());
    EXPECT_EQ(cancel_result.error(), SchedulerError::DotAlreadyCompleted);
}

TEST(DotSchedulerTest, CancelUpdatesStats) {
    DotScheduler scheduler;
    auto bytecode = make_bytecode();

    auto submit_result = scheduler.submit(bytecode);
    ASSERT_TRUE(submit_result.is_ok());
    DotHandle handle = submit_result.value();

    auto stats_before = scheduler.stats();
    EXPECT_EQ(stats_before.ready_count, 1U);
    EXPECT_EQ(stats_before.cancelled_count, 0U);

    auto cancel_result = scheduler.cancel(handle);
    ASSERT_TRUE(cancel_result.is_ok());

    auto stats_after = scheduler.stats();
    EXPECT_EQ(stats_after.ready_count, 0U);
    EXPECT_EQ(stats_after.cancelled_count, 1U);
}

// ============================================================================
// Phase 8: Stats & Shutdown Tests
// ============================================================================

TEST(DotSchedulerTest, StatsReturnsCorrectCounts) {
    DotScheduler scheduler;
    auto bytecode = make_bytecode();

    // Submit 3 Dots
    auto r1 = scheduler.submit(bytecode);
    ASSERT_TRUE(r1.is_ok());
    (void)r1.value();  // Just validate handle is returned

    auto r2 = scheduler.submit(bytecode);
    ASSERT_TRUE(r2.is_ok());
    (void)r2.value();

    auto r3 = scheduler.submit(bytecode);
    ASSERT_TRUE(r3.is_ok());

    auto stats1 = scheduler.stats();
    EXPECT_EQ(stats1.ready_count, 3U);
    EXPECT_EQ(stats1.total_submitted, 3U);

    // Pop and complete one
    auto popped1 = scheduler.try_pop_ready();
    ASSERT_TRUE(popped1.has_value());
    EXPECT_TRUE(scheduler.notify_complete(popped1.value()).is_ok());

    auto stats2 = scheduler.stats();
    EXPECT_EQ(stats2.ready_count, 2U);
    EXPECT_EQ(stats2.running_count, 0U);
    EXPECT_EQ(stats2.done_count, 1U);

    // Pop and fail one
    auto popped2 = scheduler.try_pop_ready();
    ASSERT_TRUE(popped2.has_value());
    EXPECT_TRUE(scheduler.notify_failed(popped2.value()).is_ok());

    auto stats3 = scheduler.stats();
    EXPECT_EQ(stats3.ready_count, 1U);
    EXPECT_EQ(stats3.done_count, 1U);
    EXPECT_EQ(stats3.failed_count, 1U);
}

TEST(DotSchedulerTest, ShutdownRejectsNewSubmissions) {
    DotScheduler scheduler;
    auto bytecode = make_bytecode();

    scheduler.shutdown();

    auto result = scheduler.submit(bytecode);
    ASSERT_TRUE(result.is_err());
    EXPECT_EQ(result.error(), SchedulerError::ShuttingDown);
}

TEST(DotSchedulerTest, ShutdownCancelsPending) {
    DotScheduler scheduler;
    auto bytecode = make_bytecode();

    // Create pending dots with dependencies
    auto dep_result = scheduler.submit(bytecode);
    ASSERT_TRUE(dep_result.is_ok());
    DotHandle dep_handle = dep_result.value();

    std::vector<DotHandle> deps = {dep_handle};
    auto submit_result = scheduler.submit(bytecode, deps);
    ASSERT_TRUE(submit_result.is_ok());
    DotHandle pending_handle = submit_result.value();

    auto state_before = scheduler.get_state(pending_handle);
    ASSERT_TRUE(state_before.is_ok());
    EXPECT_EQ(state_before.value(), DotState::Pending);

    scheduler.shutdown();

    // Both should be cancelled
    auto state_dep = scheduler.get_state(dep_handle);
    ASSERT_TRUE(state_dep.is_ok());
    EXPECT_EQ(state_dep.value(), DotState::Cancelled);

    auto state_after = scheduler.get_state(pending_handle);
    ASSERT_TRUE(state_after.is_ok());
    EXPECT_EQ(state_after.value(), DotState::Cancelled);
}

TEST(DotSchedulerTest, IsShuttingDownReflectsState) {
    DotScheduler scheduler;

    EXPECT_FALSE(scheduler.is_shutting_down());

    scheduler.shutdown();

    EXPECT_TRUE(scheduler.is_shutting_down());
}

// ============================================================================
// Phase 9: Thread Safety Tests
// ============================================================================

TEST(DotSchedulerTest, ConcurrentSubmitFromMultipleThreads) {
    DotScheduler scheduler;
    auto bytecode = make_bytecode();

    const int thread_count = 4;
    const int submits_per_thread = 50;
    std::vector<std::thread> threads;
    std::atomic<int> success_count{0};
    std::atomic<int> error_count{0};

    for (int t = 0; t < thread_count; ++t) {
        threads.emplace_back([&]() {
            for (int i = 0; i < submits_per_thread; ++i) {
                auto result = scheduler.submit(bytecode);
                if (result.is_ok()) {
                    success_count.fetch_add(1, std::memory_order_relaxed);
                } else {
                    error_count.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    EXPECT_EQ(success_count.load(), thread_count * submits_per_thread);
    EXPECT_EQ(error_count.load(), 0);

    auto stats = scheduler.stats();
    EXPECT_EQ(stats.total_submitted, static_cast<std::size_t>(thread_count * submits_per_thread));
}

TEST(DotSchedulerTest, ConcurrentPopFromMultipleWorkers) {
    DotScheduler scheduler;
    auto bytecode = make_bytecode();

    const int dot_count = 100;
    for (int i = 0; i < dot_count; ++i) {
        auto result = scheduler.submit(bytecode);
        ASSERT_TRUE(result.is_ok());
    }

    const int worker_count = 4;
    std::vector<std::thread> workers;
    std::atomic<int> pop_count{0};
    std::atomic<int> complete_count{0};

    for (int w = 0; w < worker_count; ++w) {
        workers.emplace_back([&]() {
            while (true) {
                auto popped = scheduler.try_pop_ready();
                if (!popped.has_value()) {
                    break;
                }
                pop_count.fetch_add(1, std::memory_order_relaxed);

                auto result = scheduler.notify_complete(popped.value());
                if (result.is_ok()) {
                    complete_count.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }

    for (auto& worker : workers) {
        worker.join();
    }

    EXPECT_EQ(pop_count.load(), dot_count);
    EXPECT_EQ(complete_count.load(), dot_count);

    auto stats = scheduler.stats();
    EXPECT_EQ(stats.done_count, static_cast<std::size_t>(dot_count));
    EXPECT_EQ(stats.ready_count, 0U);
    EXPECT_EQ(stats.running_count, 0U);
}

TEST(DotSchedulerTest, ReaderWriterContention) {
    DotScheduler scheduler;
    auto bytecode = make_bytecode();

    std::atomic<bool> stop{false};
    std::atomic<int> read_count{0};
    std::atomic<int> submit_count{0};

    // Reader thread - continuously calls stats()
    std::thread reader([&]() {
        while (!stop.load(std::memory_order_acquire)) {
            (void)scheduler.stats();
            read_count.fetch_add(1, std::memory_order_relaxed);
            std::this_thread::yield();
        }
    });

    // Writer thread - submits and completes
    std::thread writer([&]() {
        for (int i = 0; i < 100; ++i) {
            auto result = scheduler.submit(bytecode);
            if (result.is_ok()) {
                submit_count.fetch_add(1, std::memory_order_relaxed);
                auto popped = scheduler.try_pop_ready();
                if (popped.has_value()) {
                    (void)scheduler.notify_complete(popped.value());
                }
            }
        }
    });

    writer.join();
    stop.store(true, std::memory_order_release);
    reader.join();

    EXPECT_EQ(submit_count.load(), 100);
    EXPECT_GT(read_count.load(), 0);

    auto stats = scheduler.stats();
    EXPECT_EQ(stats.done_count, 100U);
}

// ============================================================================
// Edge Cases and Error Handling
// ============================================================================

TEST(DotSchedulerTest, InvalidHandleReturnsError) {
    DotScheduler scheduler;

    DotHandle invalid{999, 1};

    auto state_result = scheduler.get_state(invalid);
    EXPECT_TRUE(state_result.is_err());
    EXPECT_EQ(state_result.error(), SchedulerError::HandleNotFound);

    auto cancel_result = scheduler.cancel(invalid);
    EXPECT_TRUE(cancel_result.is_err());
    EXPECT_EQ(cancel_result.error(), SchedulerError::HandleNotFound);

    auto wait_result = scheduler.wait(invalid);
    EXPECT_TRUE(wait_result.is_err());
    EXPECT_EQ(wait_result.error(), SchedulerError::HandleNotFound);
}

TEST(DotSchedulerTest, CancelledDotNotPoppedFromReadyQueue) {
    DotScheduler scheduler;
    auto bytecode = make_bytecode();

    auto r1 = scheduler.submit(bytecode);
    ASSERT_TRUE(r1.is_ok());
    DotHandle h1 = r1.value();

    auto r2 = scheduler.submit(bytecode);
    ASSERT_TRUE(r2.is_ok());
    DotHandle h2 = r2.value();

    // Cancel first one
    auto cancel_result = scheduler.cancel(h1);
    ASSERT_TRUE(cancel_result.is_ok());

    // Pop should skip cancelled and return h2
    auto popped = scheduler.try_pop_ready();
    ASSERT_TRUE(popped.has_value());
    EXPECT_EQ(popped.value(), h2);

    // No more ready
    auto popped2 = scheduler.try_pop_ready();
    EXPECT_FALSE(popped2.has_value());
}

TEST(DotSchedulerTest, MultipleDependencies) {
    DotScheduler scheduler;
    auto bytecode = make_bytecode();

    // Create two dependencies
    auto dep1_result = scheduler.submit(bytecode);
    ASSERT_TRUE(dep1_result.is_ok());
    DotHandle dep1 = dep1_result.value();

    auto dep2_result = scheduler.submit(bytecode);
    ASSERT_TRUE(dep2_result.is_ok());
    DotHandle dep2 = dep2_result.value();

    // Submit with both dependencies
    std::vector<DotHandle> deps = {dep1, dep2};
    auto dependent_result = scheduler.submit(bytecode, deps);
    ASSERT_TRUE(dependent_result.is_ok());
    DotHandle dependent = dependent_result.value();

    // Dependent should be Pending
    auto state1 = scheduler.get_state(dependent);
    ASSERT_TRUE(state1.is_ok());
    EXPECT_EQ(state1.value(), DotState::Pending);

    // Complete first dependency
    auto popped1 = scheduler.try_pop_ready();
    ASSERT_TRUE(popped1.has_value());
    EXPECT_TRUE(scheduler.notify_complete(popped1.value()).is_ok());

    // Still Pending (one dep remaining)
    auto state2 = scheduler.get_state(dependent);
    ASSERT_TRUE(state2.is_ok());
    EXPECT_EQ(state2.value(), DotState::Pending);

    // Complete second dependency
    auto popped2 = scheduler.try_pop_ready();
    ASSERT_TRUE(popped2.has_value());
    EXPECT_TRUE(scheduler.notify_complete(popped2.value()).is_ok());

    // Now should be Ready
    auto state3 = scheduler.get_state(dependent);
    ASSERT_TRUE(state3.is_ok());
    EXPECT_EQ(state3.value(), DotState::Ready);
}

TEST(DotSchedulerTest, WaitOnCancelledReturnsOk) {
    DotScheduler scheduler;
    auto bytecode = make_bytecode();

    auto submit_result = scheduler.submit(bytecode);
    ASSERT_TRUE(submit_result.is_ok());
    DotHandle handle = submit_result.value();

    auto cancel_result = scheduler.cancel(handle);
    ASSERT_TRUE(cancel_result.is_ok());

    // Wait on cancelled should return Ok (terminal state)
    auto wait_result = scheduler.wait(handle);
    ASSERT_TRUE(wait_result.is_ok());
}

TEST(DotSchedulerTest, FailurePropagatestoToDependents) {
    DotScheduler scheduler;
    auto bytecode = make_bytecode();

    // Create dependency chain: dep -> dependent
    auto dep_result = scheduler.submit(bytecode);
    ASSERT_TRUE(dep_result.is_ok());
    DotHandle dep_handle = dep_result.value();

    std::vector<DotHandle> deps = {dep_handle};
    auto dependent_result = scheduler.submit(bytecode, deps);
    ASSERT_TRUE(dependent_result.is_ok());
    DotHandle dependent_handle = dependent_result.value();

    // Dependent should be Pending
    auto state1 = scheduler.get_state(dependent_handle);
    ASSERT_TRUE(state1.is_ok());
    EXPECT_EQ(state1.value(), DotState::Pending);

    // Pop and fail dependency
    auto popped = scheduler.try_pop_ready();
    ASSERT_TRUE(popped.has_value());
    EXPECT_EQ(popped.value(), dep_handle);

    auto failed_result = scheduler.notify_failed(dep_handle);
    ASSERT_TRUE(failed_result.is_ok());

    // Dependent should be cancelled due to failure propagation
    auto state2 = scheduler.get_state(dependent_handle);
    ASSERT_TRUE(state2.is_ok());
    EXPECT_EQ(state2.value(), DotState::Cancelled);
}

TEST(DotSchedulerTest, SlotReuseAfterCompletion) {
    SchedulerConfig config;
    config.max_pending = 3;
    DotScheduler scheduler(config);
    auto bytecode = make_bytecode();

    // Fill up to max
    std::vector<DotHandle> handles;
    for (int i = 0; i < 3; ++i) {
        auto result = scheduler.submit(bytecode);
        ASSERT_TRUE(result.is_ok());
        handles.push_back(result.value());
    }

    // Should fail (at max)
    auto over_result = scheduler.submit(bytecode);
    ASSERT_TRUE(over_result.is_err());
    EXPECT_EQ(over_result.error(), SchedulerError::MaxPendingExceeded);

    // Complete all three
    for (std::size_t i = 0; i < handles.size(); ++i) {
        auto popped = scheduler.try_pop_ready();
        ASSERT_TRUE(popped.has_value());
        auto complete = scheduler.notify_complete(popped.value());
        ASSERT_TRUE(complete.is_ok());
    }

    // Now we should be able to submit more
    auto new_result = scheduler.submit(bytecode);
    ASSERT_TRUE(new_result.is_ok());
}

TEST(DotSchedulerTest, CapacityStressTest) {
    // Test that many submit/complete cycles don't exhaust capacity
    SchedulerConfig config;
    config.max_pending = 10;
    DotScheduler scheduler(config);
    auto bytecode = make_bytecode();

    // Do many cycles of submit and complete
    for (int cycle = 0; cycle < 50; ++cycle) {
        // Submit up to max
        for (int i = 0; i < 10; ++i) {
            auto result = scheduler.submit(bytecode);
            ASSERT_TRUE(result.is_ok()) << "Failed at cycle " << cycle << ", submit " << i;
        }

        // Complete all
        for (int i = 0; i < 10; ++i) {
            auto popped = scheduler.try_pop_ready();
            ASSERT_TRUE(popped.has_value()) << "Failed to pop at cycle " << cycle;
            auto complete = scheduler.notify_complete(popped.value());
            ASSERT_TRUE(complete.is_ok()) << "Failed to complete at cycle " << cycle;
        }
    }

    auto stats = scheduler.stats();
    EXPECT_EQ(stats.done_count, 500U);
    EXPECT_EQ(stats.total_submitted, 500U);
}

TEST(DotSchedulerTest, CapacityStressWithDependencies) {
    // Test capacity with dependency chains
    SchedulerConfig config;
    config.max_pending = 10;
    DotScheduler scheduler(config);
    auto bytecode = make_bytecode();

    // Do many cycles with dependency chains
    for (int cycle = 0; cycle < 20; ++cycle) {
        // Create a chain: root -> child1 -> child2
        auto root_result = scheduler.submit(bytecode);
        ASSERT_TRUE(root_result.is_ok()) << "Failed root at cycle " << cycle;
        DotHandle root = root_result.value();

        std::vector<DotHandle> deps1 = {root};
        auto child1_result = scheduler.submit(bytecode, deps1);
        ASSERT_TRUE(child1_result.is_ok()) << "Failed child1 at cycle " << cycle;
        DotHandle child1 = child1_result.value();

        std::vector<DotHandle> deps2 = {child1};
        auto child2_result = scheduler.submit(bytecode, deps2);
        ASSERT_TRUE(child2_result.is_ok()) << "Failed child2 at cycle " << cycle;

        // Complete root -> promotes child1
        auto popped_root = scheduler.try_pop_ready();
        ASSERT_TRUE(popped_root.has_value());
        EXPECT_TRUE(scheduler.notify_complete(popped_root.value()).is_ok());

        // Complete child1 -> promotes child2
        auto popped_child1 = scheduler.try_pop_ready();
        ASSERT_TRUE(popped_child1.has_value());
        EXPECT_TRUE(scheduler.notify_complete(popped_child1.value()).is_ok());

        // Complete child2
        auto popped_child2 = scheduler.try_pop_ready();
        ASSERT_TRUE(popped_child2.has_value());
        EXPECT_TRUE(scheduler.notify_complete(popped_child2.value()).is_ok());
    }

    auto stats = scheduler.stats();
    EXPECT_EQ(stats.done_count, 60U);  // 20 cycles * 3 dots
    EXPECT_EQ(stats.total_submitted, 60U);
}

}  // namespace
}  // namespace dotvm::core::graph
