// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2025 DotLanth Project

#include <gtest/gtest.h>
#include "dotvm/jit/profiling_context.hpp"

namespace dotvm::jit {
namespace {

class ProfilingContextTest : public ::testing::Test {
protected:
    void SetUp() override {
        ctx_.set_enabled(true);
    }

    ProfilingContext ctx_;
};

// ============================================================================
// Basic State Tests
// ============================================================================

TEST_F(ProfilingContextTest, DefaultDisabled) {
    ProfilingContext fresh_ctx;
    EXPECT_FALSE(fresh_ctx.is_enabled());
}

TEST_F(ProfilingContextTest, EnableDisable) {
    ProfilingContext ctx;
    EXPECT_FALSE(ctx.is_enabled());

    ctx.set_enabled(true);
    EXPECT_TRUE(ctx.is_enabled());

    ctx.set_enabled(false);
    EXPECT_FALSE(ctx.is_enabled());
}

// ============================================================================
// Function Profiling Tests
// ============================================================================

TEST_F(ProfilingContextTest, RecordCallBasic) {
    constexpr std::size_t FUNC_PC = 0x1000;

    // First call should not return hot (threshold not reached)
    bool hot = ctx_.record_call(FUNC_PC);
    EXPECT_FALSE(hot);

    auto profile = ctx_.get_function_profile(FUNC_PC);
    ASSERT_TRUE(profile.has_value());
    EXPECT_EQ(profile->entry_pc, FUNC_PC);
    EXPECT_EQ(profile->call_count, 1u);
    EXPECT_FALSE(profile->is_compiled);
}

TEST_F(ProfilingContextTest, RecordCallDisabled) {
    ctx_.set_enabled(false);

    bool hot = ctx_.record_call(0x1000);
    EXPECT_FALSE(hot);

    // No profile should be recorded when disabled
    auto profile = ctx_.get_function_profile(0x1000);
    EXPECT_FALSE(profile.has_value());
}

TEST_F(ProfilingContextTest, FunctionBecomesHot) {
    constexpr std::size_t FUNC_PC = 0x2000;

    // Call just below threshold
    for (std::uint64_t i = 0; i < threshold::FUNCTION_CALLS - 1; ++i) {
        bool hot = ctx_.record_call(FUNC_PC);
        EXPECT_FALSE(hot) << "Should not be hot at call " << i;
    }

    // The threshold-reaching call should return true
    bool hot = ctx_.record_call(FUNC_PC);
    EXPECT_TRUE(hot);

    // Function should now be in pending list
    EXPECT_EQ(ctx_.pending_functions().size(), 1u);
    EXPECT_EQ(ctx_.pending_functions().front(), FUNC_PC);

    // Subsequent calls should not return hot (already triggered)
    hot = ctx_.record_call(FUNC_PC);
    EXPECT_FALSE(hot);
}

TEST_F(ProfilingContextTest, MarkFunctionCompiled) {
    constexpr std::size_t FUNC_PC = 0x3000;

    // Record some calls
    for (int i = 0; i < 10; ++i) {
        (void)ctx_.record_call(FUNC_PC);
    }

    auto profile = ctx_.get_function_profile(FUNC_PC);
    ASSERT_TRUE(profile.has_value());
    EXPECT_FALSE(profile->is_compiled);

    ctx_.mark_function_compiled(FUNC_PC);

    profile = ctx_.get_function_profile(FUNC_PC);
    ASSERT_TRUE(profile.has_value());
    EXPECT_TRUE(profile->is_compiled);
}

TEST_F(ProfilingContextTest, FunctionCount) {
    EXPECT_EQ(ctx_.function_count(), 0u);

    (void)ctx_.record_call(0x1000);
    EXPECT_EQ(ctx_.function_count(), 1u);

    (void)ctx_.record_call(0x2000);
    EXPECT_EQ(ctx_.function_count(), 2u);

    // Same function doesn't increase count
    (void)ctx_.record_call(0x1000);
    EXPECT_EQ(ctx_.function_count(), 2u);
}

// ============================================================================
// Loop Profiling Tests
// ============================================================================

TEST_F(ProfilingContextTest, RecordBackwardBranchBasic) {
    constexpr std::size_t BRANCH_PC = 0x1010;
    constexpr std::size_t HEADER_PC = 0x1000;

    bool hot = ctx_.record_backward_branch(BRANCH_PC, HEADER_PC);
    EXPECT_FALSE(hot);

    auto profile = ctx_.get_loop_profile(HEADER_PC);
    ASSERT_TRUE(profile.has_value());
    EXPECT_EQ(profile->header_pc, HEADER_PC);
    EXPECT_EQ(profile->backedge_pc, BRANCH_PC);
    EXPECT_EQ(profile->iteration_count, 1u);
    EXPECT_FALSE(profile->is_compiled);
}

TEST_F(ProfilingContextTest, RecordBackwardBranchDisabled) {
    ctx_.set_enabled(false);

    bool hot = ctx_.record_backward_branch(0x1010, 0x1000);
    EXPECT_FALSE(hot);

    auto profile = ctx_.get_loop_profile(0x1000);
    EXPECT_FALSE(profile.has_value());
}

TEST_F(ProfilingContextTest, LoopBecomesHot) {
    constexpr std::size_t BRANCH_PC = 0x1010;
    constexpr std::size_t HEADER_PC = 0x1000;

    // Iterate just below threshold
    for (std::uint64_t i = 0; i < threshold::LOOP_ITERATIONS - 1; ++i) {
        bool hot = ctx_.record_backward_branch(BRANCH_PC, HEADER_PC);
        EXPECT_FALSE(hot) << "Should not be hot at iteration " << i;
    }

    // The threshold-reaching iteration should return true
    bool hot = ctx_.record_backward_branch(BRANCH_PC, HEADER_PC);
    EXPECT_TRUE(hot);

    // Loop should now be in pending list
    EXPECT_EQ(ctx_.pending_loops().size(), 1u);
    EXPECT_EQ(ctx_.pending_loops().front(), HEADER_PC);
}

TEST_F(ProfilingContextTest, MarkLoopCompiled) {
    constexpr std::size_t HEADER_PC = 0x1000;

    for (int i = 0; i < 10; ++i) {
        (void)ctx_.record_backward_branch(0x1010, HEADER_PC);
    }

    auto profile = ctx_.get_loop_profile(HEADER_PC);
    ASSERT_TRUE(profile.has_value());
    EXPECT_FALSE(profile->is_compiled);

    ctx_.mark_loop_compiled(HEADER_PC);

    profile = ctx_.get_loop_profile(HEADER_PC);
    ASSERT_TRUE(profile.has_value());
    EXPECT_TRUE(profile->is_compiled);
}

TEST_F(ProfilingContextTest, LoopCount) {
    EXPECT_EQ(ctx_.loop_count(), 0u);

    (void)ctx_.record_backward_branch(0x1010, 0x1000);
    EXPECT_EQ(ctx_.loop_count(), 1u);

    (void)ctx_.record_backward_branch(0x2010, 0x2000);
    EXPECT_EQ(ctx_.loop_count(), 2u);

    // Same loop (same header) doesn't increase count
    (void)ctx_.record_backward_branch(0x1010, 0x1000);
    EXPECT_EQ(ctx_.loop_count(), 2u);
}

// ============================================================================
// Compilation Queue Tests
// ============================================================================

TEST_F(ProfilingContextTest, HasPendingCompilations) {
    EXPECT_FALSE(ctx_.has_pending_compilations());

    // Make a function hot
    for (std::uint64_t i = 0; i < threshold::FUNCTION_CALLS; ++i) {
        (void)ctx_.record_call(0x1000);
    }

    EXPECT_TRUE(ctx_.has_pending_compilations());
}

TEST_F(ProfilingContextTest, PopCompilationRequestFunction) {
    constexpr std::size_t FUNC_PC = 0x1000;

    // Make function hot
    for (std::uint64_t i = 0; i < threshold::FUNCTION_CALLS; ++i) {
        (void)ctx_.record_call(FUNC_PC);
    }

    auto request = ctx_.pop_compilation_request();
    ASSERT_TRUE(request.has_value());
    EXPECT_EQ(request->type, CompilationRequest::Type::Function);
    EXPECT_EQ(request->pc, FUNC_PC);
    EXPECT_EQ(request->priority, threshold::FUNCTION_CALLS);

    // Queue should now be empty
    EXPECT_FALSE(ctx_.has_pending_compilations());
}

TEST_F(ProfilingContextTest, PopCompilationRequestLoop) {
    constexpr std::size_t HEADER_PC = 0x1000;

    // Make loop hot
    for (std::uint64_t i = 0; i < threshold::LOOP_ITERATIONS; ++i) {
        (void)ctx_.record_backward_branch(0x1010, HEADER_PC);
    }

    auto request = ctx_.pop_compilation_request();
    ASSERT_TRUE(request.has_value());
    EXPECT_EQ(request->type, CompilationRequest::Type::Loop);
    EXPECT_EQ(request->pc, HEADER_PC);
    EXPECT_EQ(request->priority, threshold::LOOP_ITERATIONS);
}

TEST_F(ProfilingContextTest, FunctionPrioritizedOverLoop) {
    // Make both a function and loop hot
    for (std::uint64_t i = 0; i < threshold::FUNCTION_CALLS; ++i) {
        (void)ctx_.record_call(0x1000);
    }
    for (std::uint64_t i = 0; i < threshold::LOOP_ITERATIONS; ++i) {
        (void)ctx_.record_backward_branch(0x2010, 0x2000);
    }

    // Functions should be popped first
    auto request = ctx_.pop_compilation_request();
    ASSERT_TRUE(request.has_value());
    EXPECT_EQ(request->type, CompilationRequest::Type::Function);

    // Then loops
    request = ctx_.pop_compilation_request();
    ASSERT_TRUE(request.has_value());
    EXPECT_EQ(request->type, CompilationRequest::Type::Loop);
}

TEST_F(ProfilingContextTest, PopEmptyQueue) {
    auto request = ctx_.pop_compilation_request();
    EXPECT_FALSE(request.has_value());
}

TEST_F(ProfilingContextTest, ClearPendingFunctions) {
    for (std::uint64_t i = 0; i < threshold::FUNCTION_CALLS; ++i) {
        (void)ctx_.record_call(0x1000);
    }

    EXPECT_FALSE(ctx_.pending_functions().empty());
    ctx_.clear_pending_functions();
    EXPECT_TRUE(ctx_.pending_functions().empty());
}

TEST_F(ProfilingContextTest, ClearPendingLoops) {
    for (std::uint64_t i = 0; i < threshold::LOOP_ITERATIONS; ++i) {
        (void)ctx_.record_backward_branch(0x1010, 0x1000);
    }

    EXPECT_FALSE(ctx_.pending_loops().empty());
    ctx_.clear_pending_loops();
    EXPECT_TRUE(ctx_.pending_loops().empty());
}

// ============================================================================
// Statistics Tests
// ============================================================================

TEST_F(ProfilingContextTest, TotalFunctionCalls) {
    (void)ctx_.record_call(0x1000);  // 1 call
    (void)ctx_.record_call(0x1000);  // 2 calls to same func
    (void)ctx_.record_call(0x2000);  // 1 call to different func

    EXPECT_EQ(ctx_.total_function_calls(), 3u);
}

TEST_F(ProfilingContextTest, TotalLoopIterations) {
    (void)ctx_.record_backward_branch(0x1010, 0x1000);
    (void)ctx_.record_backward_branch(0x1010, 0x1000);
    (void)ctx_.record_backward_branch(0x2010, 0x2000);

    EXPECT_EQ(ctx_.total_loop_iterations(), 3u);
}

TEST_F(ProfilingContextTest, HotFunctionCount) {
    EXPECT_EQ(ctx_.hot_function_count(), 0u);

    // Make one function hot
    for (std::uint64_t i = 0; i < threshold::FUNCTION_CALLS; ++i) {
        (void)ctx_.record_call(0x1000);
    }

    EXPECT_EQ(ctx_.hot_function_count(), 1u);

    // Record another function (not hot)
    (void)ctx_.record_call(0x2000);
    EXPECT_EQ(ctx_.hot_function_count(), 1u);
}

TEST_F(ProfilingContextTest, HotLoopCount) {
    EXPECT_EQ(ctx_.hot_loop_count(), 0u);

    // Make one loop hot
    for (std::uint64_t i = 0; i < threshold::LOOP_ITERATIONS; ++i) {
        (void)ctx_.record_backward_branch(0x1010, 0x1000);
    }

    EXPECT_EQ(ctx_.hot_loop_count(), 1u);

    // Record another loop (not hot)
    (void)ctx_.record_backward_branch(0x2010, 0x2000);
    EXPECT_EQ(ctx_.hot_loop_count(), 1u);
}

// ============================================================================
// Reset Tests
// ============================================================================

TEST_F(ProfilingContextTest, Reset) {
    // Record some data
    (void)ctx_.record_call(0x1000);
    (void)ctx_.record_backward_branch(0x2010, 0x2000);

    // Make one function hot
    for (std::uint64_t i = 0; i < threshold::FUNCTION_CALLS; ++i) {
        (void)ctx_.record_call(0x3000);
    }

    EXPECT_GT(ctx_.function_count(), 0u);
    EXPECT_GT(ctx_.loop_count(), 0u);
    EXPECT_TRUE(ctx_.has_pending_compilations());

    ctx_.reset();

    EXPECT_EQ(ctx_.function_count(), 0u);
    EXPECT_EQ(ctx_.loop_count(), 0u);
    EXPECT_FALSE(ctx_.has_pending_compilations());
}

// ============================================================================
// FunctionProfile Tests
// ============================================================================

TEST(FunctionProfileTest, IsHot) {
    FunctionProfile profile;
    profile.call_count = threshold::FUNCTION_CALLS - 1;
    EXPECT_FALSE(profile.is_hot());

    profile.call_count = threshold::FUNCTION_CALLS;
    EXPECT_TRUE(profile.is_hot());

    profile.call_count = threshold::FUNCTION_CALLS + 1;
    EXPECT_TRUE(profile.is_hot());
}

// ============================================================================
// LoopProfile Tests
// ============================================================================

TEST(LoopProfileTest, IsHot) {
    LoopProfile profile;
    profile.iteration_count = threshold::LOOP_ITERATIONS - 1;
    EXPECT_FALSE(profile.is_hot());

    profile.iteration_count = threshold::LOOP_ITERATIONS;
    EXPECT_TRUE(profile.is_hot());

    profile.iteration_count = threshold::LOOP_ITERATIONS + 1;
    EXPECT_TRUE(profile.is_hot());
}

// ============================================================================
// Threshold Tests
// ============================================================================

TEST(ThresholdTest, DefaultValues) {
    // Verify threshold values match spec
    EXPECT_EQ(threshold::FUNCTION_CALLS, 10'000u);
    EXPECT_EQ(threshold::LOOP_ITERATIONS, 100'000u);
}

}  // namespace
}  // namespace dotvm::jit
