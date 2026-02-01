/// @file breakpoint_manager_test.cpp
/// @brief Unit tests for BreakpointManager - TOOL-011 Debug Client

#include <gtest/gtest.h>

#include "dotvm/debugger/breakpoint_manager.hpp"

namespace dotvm::debugger {
namespace {

TEST(BreakpointManagerTest, SetBreakpoint) {
    BreakpointManager mgr;
    std::uint32_t id = mgr.set(0x10);

    EXPECT_EQ(id, 1);
    EXPECT_EQ(mgr.count(), 1);

    const Breakpoint* bp = mgr.get(id);
    ASSERT_NE(bp, nullptr);
    EXPECT_EQ(bp->address, 0x10);
    EXPECT_TRUE(bp->enabled);
    EXPECT_TRUE(bp->condition.empty());
}

TEST(BreakpointManagerTest, SetConditionalBreakpoint) {
    BreakpointManager mgr;
    std::uint32_t id = mgr.set_conditional(0x20, "r1 > 10");

    const Breakpoint* bp = mgr.get(id);
    ASSERT_NE(bp, nullptr);
    EXPECT_EQ(bp->condition, "r1 > 10");
}

TEST(BreakpointManagerTest, RemoveBreakpoint) {
    BreakpointManager mgr;
    std::uint32_t id = mgr.set(0x10);

    EXPECT_TRUE(mgr.remove(id));
    EXPECT_EQ(mgr.count(), 0);
    EXPECT_EQ(mgr.get(id), nullptr);
}

TEST(BreakpointManagerTest, RemoveNonexistent) {
    BreakpointManager mgr;
    EXPECT_FALSE(mgr.remove(999));
}

TEST(BreakpointManagerTest, EnableDisable) {
    BreakpointManager mgr;
    std::uint32_t id = mgr.set(0x10);

    EXPECT_TRUE(mgr.disable(id));
    EXPECT_FALSE(mgr.get(id)->enabled);

    EXPECT_TRUE(mgr.enable(id));
    EXPECT_TRUE(mgr.get(id)->enabled);
}

TEST(BreakpointManagerTest, EnableNonexistent) {
    BreakpointManager mgr;
    EXPECT_FALSE(mgr.enable(999));
    EXPECT_FALSE(mgr.disable(999));
}

TEST(BreakpointManagerTest, SetCondition) {
    BreakpointManager mgr;
    std::uint32_t id = mgr.set(0x10);

    EXPECT_TRUE(mgr.set_condition(id, "r2 == 0"));
    EXPECT_EQ(mgr.get(id)->condition, "r2 == 0");
}

TEST(BreakpointManagerTest, SetIgnoreCount) {
    BreakpointManager mgr;
    std::uint32_t id = mgr.set(0x10);

    EXPECT_TRUE(mgr.set_ignore_count(id, 5));
    EXPECT_EQ(mgr.get(id)->ignore_count, 5);
}

TEST(BreakpointManagerTest, ListBreakpoints) {
    BreakpointManager mgr;
    mgr.set(0x10);
    mgr.set(0x20);
    mgr.set(0x30);

    auto list = mgr.list();
    EXPECT_EQ(list.size(), 3);

    // Should be sorted by ID
    EXPECT_EQ(list[0]->id, 1);
    EXPECT_EQ(list[1]->id, 2);
    EXPECT_EQ(list[2]->id, 3);
}

TEST(BreakpointManagerTest, AtAddress) {
    BreakpointManager mgr;
    mgr.set(0x10);
    mgr.set(0x10);  // Second breakpoint at same address
    mgr.set(0x20);

    auto at_10 = mgr.at_address(0x10);
    EXPECT_EQ(at_10.size(), 2);

    auto at_20 = mgr.at_address(0x20);
    EXPECT_EQ(at_20.size(), 1);

    auto at_30 = mgr.at_address(0x30);
    EXPECT_TRUE(at_30.empty());
}

TEST(BreakpointManagerTest, RemoveAtAddress) {
    BreakpointManager mgr;
    mgr.set(0x10);
    mgr.set(0x10);
    mgr.set(0x20);

    std::size_t removed = mgr.remove_at_address(0x10);
    EXPECT_EQ(removed, 2);
    EXPECT_EQ(mgr.count(), 1);
}

TEST(BreakpointManagerTest, CheckSimple) {
    BreakpointManager mgr;
    std::uint32_t id = mgr.set(0x10);

    auto result = mgr.check_simple(0x10);
    EXPECT_TRUE(result.should_break);
    EXPECT_TRUE(result.id.has_value());
    EXPECT_EQ(result.id.value(), id);

    // Hit count should increment
    EXPECT_EQ(mgr.get(id)->hit_count, 1);
}

TEST(BreakpointManagerTest, CheckNoBreakpoint) {
    BreakpointManager mgr;
    mgr.set(0x10);

    auto result = mgr.check_simple(0x20);  // Different address
    EXPECT_FALSE(result.should_break);
    EXPECT_FALSE(result.id.has_value());
}

TEST(BreakpointManagerTest, CheckDisabledBreakpoint) {
    BreakpointManager mgr;
    std::uint32_t id = mgr.set(0x10);
    mgr.disable(id);

    auto result = mgr.check_simple(0x10);
    EXPECT_FALSE(result.should_break);
}

TEST(BreakpointManagerTest, CheckWithIgnoreCount) {
    BreakpointManager mgr;
    std::uint32_t id = mgr.set(0x10);
    mgr.set_ignore_count(id, 2);

    // First two checks should not break
    EXPECT_FALSE(mgr.check_simple(0x10).should_break);
    EXPECT_FALSE(mgr.check_simple(0x10).should_break);

    // Third check should break
    EXPECT_TRUE(mgr.check_simple(0x10).should_break);
}

TEST(BreakpointManagerTest, CheckWithCondition) {
    BreakpointManager mgr;
    (void)mgr.set_conditional(0x10, "r1 > 10");

    // Condition evaluates to false
    auto result1 = mgr.check(0x10, [](const std::string&) { return false; });
    EXPECT_FALSE(result1.should_break);

    // Condition evaluates to true
    auto result2 = mgr.check(0x10, [](const std::string&) { return true; });
    EXPECT_TRUE(result2.should_break);
}

TEST(BreakpointManagerTest, HasBreakpointAt) {
    BreakpointManager mgr;
    std::uint32_t id = mgr.set(0x10);

    EXPECT_TRUE(mgr.has_breakpoint_at(0x10));
    EXPECT_FALSE(mgr.has_breakpoint_at(0x20));

    mgr.disable(id);
    EXPECT_FALSE(mgr.has_breakpoint_at(0x10));  // Disabled doesn't count
}

TEST(BreakpointManagerTest, GetActiveAddresses) {
    BreakpointManager mgr;
    mgr.set(0x10);
    auto id2 = mgr.set(0x20);
    mgr.set(0x30);

    mgr.disable(id2);

    auto addrs = mgr.get_active_addresses();
    EXPECT_EQ(addrs.size(), 2);

    // Should contain 0x10 and 0x30, not 0x20
    EXPECT_TRUE(std::find(addrs.begin(), addrs.end(), 0x10) != addrs.end());
    EXPECT_TRUE(std::find(addrs.begin(), addrs.end(), 0x30) != addrs.end());
    EXPECT_TRUE(std::find(addrs.begin(), addrs.end(), 0x20) == addrs.end());
}

TEST(BreakpointManagerTest, Clear) {
    BreakpointManager mgr;
    mgr.set(0x10);
    mgr.set(0x20);

    mgr.clear();
    EXPECT_EQ(mgr.count(), 0);
    EXPECT_TRUE(mgr.list().empty());
}

TEST(BreakpointManagerTest, UniqueIds) {
    BreakpointManager mgr;
    std::uint32_t id1 = mgr.set(0x10);
    std::uint32_t id2 = mgr.set(0x20);
    std::uint32_t id3 = mgr.set(0x30);

    EXPECT_NE(id1, id2);
    EXPECT_NE(id2, id3);
    EXPECT_NE(id1, id3);
}

}  // namespace
}  // namespace dotvm::debugger
