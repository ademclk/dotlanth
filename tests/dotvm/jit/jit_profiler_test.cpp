/// @file jit_profiler_test.cpp
/// @brief Unit tests for JIT profiler

#include <gtest/gtest.h>

#include "dotvm/jit/jit_profiler.hpp"

namespace dotvm::jit {
namespace {

// ============================================================================
// JitConfig Tests
// ============================================================================

class JitConfigTest : public ::testing::Test {};

TEST_F(JitConfigTest, Defaults_HasExpectedValues) {
    auto config = JitConfig::defaults();
    EXPECT_EQ(config.call_threshold, thresholds::CALL_THRESHOLD);
    EXPECT_EQ(config.loop_threshold, thresholds::LOOP_THRESHOLD);
    EXPECT_TRUE(config.enabled);
    EXPECT_TRUE(config.osr_enabled);
}

TEST_F(JitConfigTest, Aggressive_HasLowerThresholds) {
    auto config = JitConfig::aggressive();
    EXPECT_LT(config.call_threshold, thresholds::CALL_THRESHOLD);
    EXPECT_LT(config.loop_threshold, thresholds::LOOP_THRESHOLD);
}

TEST_F(JitConfigTest, Disabled_HasJitDisabled) {
    auto config = JitConfig::disabled();
    EXPECT_FALSE(config.enabled);
    EXPECT_FALSE(config.osr_enabled);
    EXPECT_FALSE(config.allows_jit());
    EXPECT_FALSE(config.allows_osr());
}

TEST_F(JitConfigTest, MethodOnly_HasOsrDisabled) {
    auto config = JitConfig::method_only();
    EXPECT_TRUE(config.enabled);
    EXPECT_FALSE(config.osr_enabled);
    EXPECT_TRUE(config.allows_jit());
    EXPECT_FALSE(config.allows_osr());
}

TEST_F(JitConfigTest, Debug_HasDebugSettings) {
    auto config = JitConfig::debug();
    EXPECT_TRUE(config.debug_info);
    EXPECT_TRUE(config.bounds_checking);
    EXPECT_LT(config.call_threshold, thresholds::CALL_THRESHOLD);
}

TEST_F(JitConfigTest, AllowsJit_WhenEnabledAndHasCache) {
    JitConfig config;
    config.enabled = true;
    config.max_code_cache = 1024;
    EXPECT_TRUE(config.allows_jit());

    config.max_code_cache = 0;
    EXPECT_FALSE(config.allows_jit());

    config.max_code_cache = 1024;
    config.enabled = false;
    EXPECT_FALSE(config.allows_jit());
}

// ============================================================================
// FunctionProfile Tests
// ============================================================================

class FunctionProfileTest : public ::testing::Test {};

TEST_F(FunctionProfileTest, InitializesWithZeroCount) {
    FunctionProfile profile;
    EXPECT_EQ(profile.call_count.load(), 0u);
    EXPECT_FALSE(profile.compiled.load());
}

TEST_F(FunctionProfileTest, Increment_ReturnsNewCount) {
    FunctionProfile profile;
    EXPECT_EQ(profile.increment(), 1u);
    EXPECT_EQ(profile.increment(), 2u);
    EXPECT_EQ(profile.increment(), 3u);
}

TEST_F(FunctionProfileTest, ReachedThreshold_ReturnsTrueWhenMet) {
    FunctionProfile profile;
    profile.call_count.store(99);
    EXPECT_FALSE(profile.reached_threshold(100));

    profile.call_count.store(100);
    EXPECT_TRUE(profile.reached_threshold(100));

    profile.call_count.store(101);
    EXPECT_TRUE(profile.reached_threshold(100));
}

TEST_F(FunctionProfileTest, Reset_ClearsCountAndCompiled) {
    FunctionProfile profile;
    profile.call_count.store(1000);
    profile.compiled.store(true);

    profile.reset();
    EXPECT_EQ(profile.call_count.load(), 0u);
    EXPECT_FALSE(profile.compiled.load());
}

// ============================================================================
// LoopProfile Tests
// ============================================================================

class LoopProfileTest : public ::testing::Test {};

TEST_F(LoopProfileTest, InitializesWithZeroCount) {
    LoopProfile profile;
    EXPECT_EQ(profile.iteration_count.load(), 0u);
    EXPECT_FALSE(profile.osr_triggered.load());
}

TEST_F(LoopProfileTest, Increment_ReturnsNewCount) {
    LoopProfile profile;
    EXPECT_EQ(profile.increment(), 1u);
    EXPECT_EQ(profile.increment(), 2u);
    EXPECT_EQ(profile.increment(), 3u);
}

TEST_F(LoopProfileTest, ReachedThreshold_ReturnsTrueWhenMet) {
    LoopProfile profile;
    profile.iteration_count.store(999);
    EXPECT_FALSE(profile.reached_threshold(1000));

    profile.iteration_count.store(1000);
    EXPECT_TRUE(profile.reached_threshold(1000));
}

// ============================================================================
// Loop ID Tests
// ============================================================================

class LoopIdTest : public ::testing::Test {};

TEST_F(LoopIdTest, MakeLoopId_EncodesCorrectly) {
    auto id = make_loop_id(42, 7);
    EXPECT_EQ(loop_id_function(id), 42u);
    EXPECT_EQ(loop_id_index(id), 7u);
}

TEST_F(LoopIdTest, MakeLoopId_HandlesMaxValues) {
    auto id = make_loop_id(0xFFFFFFFF, 0xFFFFFFFF);
    EXPECT_EQ(loop_id_function(id), 0xFFFFFFFFu);
    EXPECT_EQ(loop_id_index(id), 0xFFFFFFFFu);
}

TEST_F(LoopIdTest, MakeLoopId_HandlesZero) {
    auto id = make_loop_id(0, 0);
    EXPECT_EQ(loop_id_function(id), 0u);
    EXPECT_EQ(loop_id_index(id), 0u);
}

// ============================================================================
// JitProfiler Function Registration Tests
// ============================================================================

class JitProfilerFunctionTest : public ::testing::Test {
protected:
    JitProfiler profiler_;
};

TEST_F(JitProfilerFunctionTest, RegisterFunction_ReturnsIncrementingIds) {
    auto id1 = profiler_.register_function(0x100, 0x200);
    auto id2 = profiler_.register_function(0x200, 0x300);
    auto id3 = profiler_.register_function(0x300, 0x400);

    EXPECT_EQ(id1, 0u);
    EXPECT_EQ(id2, 1u);
    EXPECT_EQ(id3, 2u);
}

TEST_F(JitProfilerFunctionTest, RegisterFunction_StoresProfile) {
    auto id = profiler_.register_function(0x100, 0x200);
    auto* profile = profiler_.get_function(id);

    ASSERT_NE(profile, nullptr);
    EXPECT_EQ(profile->entry_pc, 0x100u);
    EXPECT_EQ(profile->end_pc, 0x200u);
}

TEST_F(JitProfilerFunctionTest, GetFunction_InvalidId_ReturnsNull) {
    EXPECT_EQ(profiler_.get_function(999), nullptr);
}

TEST_F(JitProfilerFunctionTest, FindFunctionByPc_FindsRegistered) {
    auto id = profiler_.register_function(0x100, 0x200);
    auto found = profiler_.find_function_by_pc(0x100);

    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(*found, id);
}

TEST_F(JitProfilerFunctionTest, FindFunctionByPc_ReturnsNulloptForUnknown) {
    (void)profiler_.register_function(0x100, 0x200);
    EXPECT_FALSE(profiler_.find_function_by_pc(0x999).has_value());
}

// ============================================================================
// JitProfiler Call Recording Tests
// ============================================================================

class JitProfilerCallTest : public ::testing::Test {
protected:
    void SetUp() override {
        profiler_.set_call_threshold(100);
        func_id_ = profiler_.register_function(0x100, 0x200);
    }

    JitProfiler profiler_;
    FunctionId func_id_{0};
};

TEST_F(JitProfilerCallTest, RecordCall_IncrementsCount) {
    EXPECT_EQ(profiler_.record_call(func_id_), 1u);
    EXPECT_EQ(profiler_.record_call(func_id_), 2u);
    EXPECT_EQ(profiler_.record_call(func_id_), 3u);
}

TEST_F(JitProfilerCallTest, RecordCall_InvalidId_ReturnsZero) {
    EXPECT_EQ(profiler_.record_call(999), 0u);
}

TEST_F(JitProfilerCallTest, ShouldCompile_BelowThreshold_ReturnsFalse) {
    for (int i = 0; i < 99; ++i) {
        (void)profiler_.record_call(func_id_);
    }
    EXPECT_FALSE(profiler_.should_compile(func_id_));
}

TEST_F(JitProfilerCallTest, ShouldCompile_AtThreshold_ReturnsTrue) {
    for (int i = 0; i < 100; ++i) {
        (void)profiler_.record_call(func_id_);
    }
    EXPECT_TRUE(profiler_.should_compile(func_id_));
}

TEST_F(JitProfilerCallTest, ShouldCompile_AlreadyCompiled_ReturnsFalse) {
    for (int i = 0; i < 100; ++i) {
        (void)profiler_.record_call(func_id_);
    }
    profiler_.mark_compiled(func_id_);
    EXPECT_FALSE(profiler_.should_compile(func_id_));
}

TEST_F(JitProfilerCallTest, MarkCompiled_SetsFlag) {
    EXPECT_FALSE(profiler_.is_compiled(func_id_));
    profiler_.mark_compiled(func_id_);
    EXPECT_TRUE(profiler_.is_compiled(func_id_));
}

// ============================================================================
// JitProfiler Loop Registration Tests
// ============================================================================

class JitProfilerLoopTest : public ::testing::Test {
protected:
    void SetUp() override {
        profiler_.set_loop_threshold(1000);
        func_id_ = profiler_.register_function(0x100, 0x200);
        loop_id_ = profiler_.register_loop(func_id_, 0x120, 0x180);
    }

    JitProfiler profiler_;
    FunctionId func_id_{0};
    LoopId loop_id_{0};
};

TEST_F(JitProfilerLoopTest, RegisterLoop_StoresProfile) {
    auto* profile = profiler_.get_loop(loop_id_);
    ASSERT_NE(profile, nullptr);
    EXPECT_EQ(profile->header_pc, 0x120u);
    EXPECT_EQ(profile->backedge_pc, 0x180u);
}

TEST_F(JitProfilerLoopTest, RegisterLoop_AssociatesWithFunction) {
    EXPECT_EQ(loop_id_function(loop_id_), func_id_);
}

TEST_F(JitProfilerLoopTest, RegisterLoop_MultipleLoops_UniqueIds) {
    auto loop2 = profiler_.register_loop(func_id_, 0x140, 0x160);
    EXPECT_NE(loop_id_, loop2);
    EXPECT_EQ(loop_id_function(loop2), func_id_);
    EXPECT_EQ(loop_id_index(loop2), 1u);
}

TEST_F(JitProfilerLoopTest, FindLoopByBackedge_FindsRegistered) {
    auto found = profiler_.find_loop_by_backedge(0x180);
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(*found, loop_id_);
}

TEST_F(JitProfilerLoopTest, RecordIteration_IncrementsCount) {
    EXPECT_EQ(profiler_.record_iteration(loop_id_), 1u);
    EXPECT_EQ(profiler_.record_iteration(loop_id_), 2u);
    EXPECT_EQ(profiler_.record_iteration(loop_id_), 3u);
}

TEST_F(JitProfilerLoopTest, ShouldOsr_BelowThreshold_ReturnsFalse) {
    for (int i = 0; i < 999; ++i) {
        (void)profiler_.record_iteration(loop_id_);
    }
    EXPECT_FALSE(profiler_.should_osr(loop_id_));
}

TEST_F(JitProfilerLoopTest, ShouldOsr_AtThreshold_ReturnsTrue) {
    for (int i = 0; i < 1000; ++i) {
        (void)profiler_.record_iteration(loop_id_);
    }
    EXPECT_TRUE(profiler_.should_osr(loop_id_));
}

TEST_F(JitProfilerLoopTest, ShouldOsr_AlreadyTriggered_ReturnsFalse) {
    for (int i = 0; i < 1000; ++i) {
        (void)profiler_.record_iteration(loop_id_);
    }
    profiler_.mark_osr_triggered(loop_id_);
    EXPECT_FALSE(profiler_.should_osr(loop_id_));
}

// ============================================================================
// JitProfiler Stats Tests
// ============================================================================

class JitProfilerStatsTest : public ::testing::Test {
protected:
    JitProfiler profiler_;
};

TEST_F(JitProfilerStatsTest, GetStats_EmptyProfiler_ReturnsZeros) {
    auto stats = profiler_.get_stats();
    EXPECT_EQ(stats.total_functions, 0u);
    EXPECT_EQ(stats.compiled_functions, 0u);
    EXPECT_EQ(stats.total_loops, 0u);
    EXPECT_EQ(stats.osr_triggered_loops, 0u);
    EXPECT_EQ(stats.total_calls, 0u);
    EXPECT_EQ(stats.total_iterations, 0u);
}

TEST_F(JitProfilerStatsTest, GetStats_CountsFunctions) {
    (void)profiler_.register_function(0x100, 0x200);
    (void)profiler_.register_function(0x200, 0x300);

    auto stats = profiler_.get_stats();
    EXPECT_EQ(stats.total_functions, 2u);
}

TEST_F(JitProfilerStatsTest, GetStats_CountsCompiledFunctions) {
    auto id1 = profiler_.register_function(0x100, 0x200);
    (void)profiler_.register_function(0x200, 0x300);  // id2 not used
    profiler_.mark_compiled(id1);

    auto stats = profiler_.get_stats();
    EXPECT_EQ(stats.compiled_functions, 1u);
}

TEST_F(JitProfilerStatsTest, GetStats_CountsTotalCalls) {
    auto id1 = profiler_.register_function(0x100, 0x200);
    auto id2 = profiler_.register_function(0x200, 0x300);

    for (int i = 0; i < 10; ++i) {
        (void)profiler_.record_call(id1);
    }
    for (int i = 0; i < 20; ++i) {
        (void)profiler_.record_call(id2);
    }

    auto stats = profiler_.get_stats();
    EXPECT_EQ(stats.total_calls, 30u);
}

TEST_F(JitProfilerStatsTest, GetStats_CountsLoopsAndIterations) {
    auto func_id = profiler_.register_function(0x100, 0x200);
    auto loop_id = profiler_.register_loop(func_id, 0x120, 0x180);

    for (int i = 0; i < 50; ++i) {
        (void)profiler_.record_iteration(loop_id);
    }

    auto stats = profiler_.get_stats();
    EXPECT_EQ(stats.total_loops, 1u);
    EXPECT_EQ(stats.total_iterations, 50u);
}

// ============================================================================
// JitProfiler Reset Tests
// ============================================================================

class JitProfilerResetTest : public ::testing::Test {
protected:
    void SetUp() override {
        func_id_ = profiler_.register_function(0x100, 0x200);
        loop_id_ = profiler_.register_loop(func_id_, 0x120, 0x180);

        // Record some activity
        for (int i = 0; i < 50; ++i) {
            (void)profiler_.record_call(func_id_);
            (void)profiler_.record_iteration(loop_id_);
        }
        profiler_.mark_compiled(func_id_);
        profiler_.mark_osr_triggered(loop_id_);
    }

    JitProfiler profiler_;
    FunctionId func_id_{0};
    LoopId loop_id_{0};
};

TEST_F(JitProfilerResetTest, Reset_ClearsCallCounts) {
    profiler_.reset();
    auto* profile = profiler_.get_function(func_id_);
    ASSERT_NE(profile, nullptr);
    EXPECT_EQ(profile->call_count.load(), 0u);
}

TEST_F(JitProfilerResetTest, Reset_ClearsCompiledFlag) {
    profiler_.reset();
    EXPECT_FALSE(profiler_.is_compiled(func_id_));
}

TEST_F(JitProfilerResetTest, Reset_ClearsIterationCounts) {
    profiler_.reset();
    auto* profile = profiler_.get_loop(loop_id_);
    ASSERT_NE(profile, nullptr);
    EXPECT_EQ(profile->iteration_count.load(), 0u);
}

TEST_F(JitProfilerResetTest, Reset_ClearsOsrTriggeredFlag) {
    profiler_.reset();
    EXPECT_FALSE(profiler_.is_osr_triggered(loop_id_));
}

TEST_F(JitProfilerResetTest, Reset_PreservesRegistrations) {
    auto count_before = profiler_.function_count();
    profiler_.reset();
    EXPECT_EQ(profiler_.function_count(), count_before);
}

// ============================================================================
// JitStatus/OsrStatus String Tests
// ============================================================================

class StatusStringTest : public ::testing::Test {};

TEST_F(StatusStringTest, JitStatusString_AllValues) {
    EXPECT_STREQ(jit_status_string(JitStatus::Success), "Success");
    EXPECT_STREQ(jit_status_string(JitStatus::Disabled), "Disabled");
    EXPECT_STREQ(jit_status_string(JitStatus::BelowThreshold), "BelowThreshold");
    EXPECT_STREQ(jit_status_string(JitStatus::CacheFull), "CacheFull");
    EXPECT_STREQ(jit_status_string(JitStatus::UnsupportedOpcode), "UnsupportedOpcode");
    EXPECT_STREQ(jit_status_string(JitStatus::AllocationFailed), "AllocationFailed");
    EXPECT_STREQ(jit_status_string(JitStatus::ProtectionFailed), "ProtectionFailed");
    EXPECT_STREQ(jit_status_string(JitStatus::InvalidFunction), "InvalidFunction");
    EXPECT_STREQ(jit_status_string(JitStatus::InternalError), "InternalError");
}

TEST_F(StatusStringTest, OsrStatusString_AllValues) {
    EXPECT_STREQ(osr_status_string(OsrStatus::Success), "Success");
    EXPECT_STREQ(osr_status_string(OsrStatus::Disabled), "Disabled");
    EXPECT_STREQ(osr_status_string(OsrStatus::BelowThreshold), "BelowThreshold");
    EXPECT_STREQ(osr_status_string(OsrStatus::NoEntryPoint), "NoEntryPoint");
    EXPECT_STREQ(osr_status_string(OsrStatus::StateTransferFailed), "StateTransferFailed");
    EXPECT_STREQ(osr_status_string(OsrStatus::InvalidLoop), "InvalidLoop");
}

}  // namespace
}  // namespace dotvm::jit
