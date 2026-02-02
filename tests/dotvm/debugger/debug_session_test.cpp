/// @file debug_session_test.cpp
/// @brief Unit tests for DebugSession - TOOL-011 Debug Client

#include <gtest/gtest.h>

#include "dotvm/debugger/debug_session.hpp"

namespace dotvm::debugger {
namespace {

TEST(DebugSessionTest, InitialState) {
    DebugSession session;
    EXPECT_EQ(session.state(), SessionState::NotStarted);
    EXPECT_EQ(session.pause_reason(), PauseReason::None);
    EXPECT_EQ(session.pc(), 0);
    EXPECT_FALSE(session.hit_breakpoint_id().has_value());
}

TEST(DebugSessionTest, Start) {
    DebugSession session;
    session.start();
    EXPECT_EQ(session.state(), SessionState::Running);
}

TEST(DebugSessionTest, Pause) {
    DebugSession session;
    session.start();
    session.pause(0x10, PauseReason::Breakpoint);
    EXPECT_EQ(session.state(), SessionState::Paused);
    EXPECT_EQ(session.pc(), 0x10);
    EXPECT_EQ(session.pause_reason(), PauseReason::Breakpoint);
}

TEST(DebugSessionTest, Resume) {
    DebugSession session;
    session.start();
    session.pause(0x10, PauseReason::Breakpoint);
    session.resume();
    EXPECT_EQ(session.state(), SessionState::Running);
    EXPECT_EQ(session.pause_reason(), PauseReason::None);
}

TEST(DebugSessionTest, Halt) {
    DebugSession session;
    session.start();
    session.halt();
    EXPECT_EQ(session.state(), SessionState::Halted);
}

TEST(DebugSessionTest, Reset) {
    DebugSession session;
    session.start();
    session.pause(0x10, PauseReason::Breakpoint);
    session.set_hit_breakpoint(1);
    session.reset();

    EXPECT_EQ(session.state(), SessionState::NotStarted);
    EXPECT_EQ(session.pc(), 0);
    EXPECT_FALSE(session.hit_breakpoint_id().has_value());
}

TEST(DebugSessionTest, CanRun) {
    DebugSession session;
    EXPECT_TRUE(session.can_run());  // NotStarted

    session.start();
    session.pause(0, PauseReason::Step);
    EXPECT_TRUE(session.can_run());  // Paused

    session.halt();
    EXPECT_FALSE(session.can_run());  // Halted
}

TEST(DebugSessionTest, CanStep) {
    DebugSession session;
    EXPECT_FALSE(session.can_step());  // NotStarted

    session.start();
    EXPECT_FALSE(session.can_step());  // Running

    session.pause(0, PauseReason::Step);
    EXPECT_TRUE(session.can_step());  // Paused
}

TEST(DebugSessionTest, StackFrameSelection) {
    DebugSession session;

    std::vector<StackFrame> frames = {{0, 0x10, 0x04, "main", "test.dot", 10},
                                      {1, 0x04, 0, "entry", "test.dot", 1}};

    session.update_call_stack(frames);
    EXPECT_EQ(session.call_stack().size(), 2);
    EXPECT_EQ(session.selected_frame(), 0);

    EXPECT_TRUE(session.select_frame(1));
    EXPECT_EQ(session.selected_frame(), 1);

    EXPECT_FALSE(session.select_frame(5));   // Out of bounds
    EXPECT_EQ(session.selected_frame(), 1);  // Unchanged
}

TEST(DebugSessionTest, HitBreakpointTracking) {
    DebugSession session;
    session.start();
    session.pause(0x10, PauseReason::Breakpoint);
    session.set_hit_breakpoint(42);

    EXPECT_TRUE(session.hit_breakpoint_id().has_value());
    EXPECT_EQ(session.hit_breakpoint_id().value(), 42);

    session.resume();
    EXPECT_FALSE(session.hit_breakpoint_id().has_value());
}

TEST(DebugSessionTest, TriggeredWatchpointTracking) {
    DebugSession session;
    session.start();
    session.pause(0x10, PauseReason::Watch);
    session.set_triggered_watchpoint(7);

    EXPECT_TRUE(session.triggered_watchpoint_id().has_value());
    EXPECT_EQ(session.triggered_watchpoint_id().value(), 7);
}

TEST(DebugSessionTest, StateToString) {
    EXPECT_STREQ(to_string(SessionState::NotStarted), "NotStarted");
    EXPECT_STREQ(to_string(SessionState::Running), "Running");
    EXPECT_STREQ(to_string(SessionState::Paused), "Paused");
    EXPECT_STREQ(to_string(SessionState::Halted), "Halted");
}

TEST(DebugSessionTest, PauseReasonToString) {
    EXPECT_STREQ(to_string(PauseReason::None), "None");
    EXPECT_STREQ(to_string(PauseReason::Breakpoint), "Breakpoint");
    EXPECT_STREQ(to_string(PauseReason::Step), "Step");
    EXPECT_STREQ(to_string(PauseReason::Watch), "Watch");
    EXPECT_STREQ(to_string(PauseReason::Exception), "Exception");
}

}  // namespace
}  // namespace dotvm::debugger
