/// @file watch_manager_test.cpp
/// @brief Unit tests for WatchManager - TOOL-011 Debug Client

#include <gtest/gtest.h>

#include "dotvm/debugger/watch_manager.hpp"

namespace dotvm::debugger {
namespace {

TEST(WatchManagerTest, SetWatchpoint) {
    WatchManager mgr;
    core::Handle handle{1, 0};
    std::uint32_t id = mgr.set(handle, 0, 4);

    EXPECT_EQ(id, 1);
    EXPECT_EQ(mgr.count(), 1);

    const Watchpoint* wp = mgr.get(id);
    ASSERT_NE(wp, nullptr);
    EXPECT_EQ(wp->handle.index, 1);
    EXPECT_EQ(wp->offset, 0);
    EXPECT_EQ(wp->size, 4);
    EXPECT_EQ(wp->type, WatchType::Write);
    EXPECT_TRUE(wp->enabled);
}

TEST(WatchManagerTest, SetWatchpointWithType) {
    WatchManager mgr;
    core::Handle handle{1, 0};
    std::uint32_t id = mgr.set(handle, 0, 4, WatchType::ReadWrite);

    const Watchpoint* wp = mgr.get(id);
    EXPECT_EQ(wp->type, WatchType::ReadWrite);
}

TEST(WatchManagerTest, RemoveWatchpoint) {
    WatchManager mgr;
    core::Handle handle{1, 0};
    std::uint32_t id = mgr.set(handle, 0, 4);

    EXPECT_TRUE(mgr.remove(id));
    EXPECT_EQ(mgr.count(), 0);
    EXPECT_EQ(mgr.get(id), nullptr);
}

TEST(WatchManagerTest, RemoveNonexistent) {
    WatchManager mgr;
    EXPECT_FALSE(mgr.remove(999));
}

TEST(WatchManagerTest, EnableDisable) {
    WatchManager mgr;
    core::Handle handle{1, 0};
    std::uint32_t id = mgr.set(handle, 0, 4);

    EXPECT_TRUE(mgr.disable(id));
    EXPECT_FALSE(mgr.get(id)->enabled);

    EXPECT_TRUE(mgr.enable(id));
    EXPECT_TRUE(mgr.get(id)->enabled);
}

TEST(WatchManagerTest, EnableNonexistent) {
    WatchManager mgr;
    EXPECT_FALSE(mgr.enable(999));
    EXPECT_FALSE(mgr.disable(999));
}

TEST(WatchManagerTest, ListWatchpoints) {
    WatchManager mgr;
    core::Handle h1{1, 0}, h2{2, 0};
    mgr.set(h1, 0, 4);
    mgr.set(h2, 0, 8);

    auto list = mgr.list();
    EXPECT_EQ(list.size(), 2);

    // Should be sorted by ID
    EXPECT_EQ(list[0]->id, 1);
    EXPECT_EQ(list[1]->id, 2);
}

TEST(WatchManagerTest, CheckTriggered) {
    WatchManager mgr;
    core::Handle handle{1, 0};
    std::uint32_t id = mgr.set(handle, 0, 4);

    // First, update cached values
    mgr.update_cached_values([](core::Handle, std::size_t, std::size_t) {
        return std::vector<std::uint8_t>{0, 0, 0, 0};
    });

    // Check with same value - no trigger
    auto result1 = mgr.check([](core::Handle, std::size_t, std::size_t) {
        return std::vector<std::uint8_t>{0, 0, 0, 0};
    });
    EXPECT_FALSE(result1.triggered);

    // Check with different value - should trigger
    auto result2 = mgr.check([](core::Handle, std::size_t, std::size_t) {
        return std::vector<std::uint8_t>{1, 0, 0, 0};
    });
    EXPECT_TRUE(result2.triggered);
    EXPECT_TRUE(result2.id.has_value());
    EXPECT_EQ(result2.id.value(), id);
}

TEST(WatchManagerTest, CheckDisabledWatchpoint) {
    WatchManager mgr;
    core::Handle handle{1, 0};
    std::uint32_t id = mgr.set(handle, 0, 4);
    mgr.disable(id);

    // Update cached values
    mgr.update_cached_values([](core::Handle, std::size_t, std::size_t) {
        return std::vector<std::uint8_t>{0, 0, 0, 0};
    });

    // Even with change, disabled watchpoint should not trigger
    auto result = mgr.check([](core::Handle, std::size_t, std::size_t) {
        return std::vector<std::uint8_t>{1, 0, 0, 0};
    });
    EXPECT_FALSE(result.triggered);
}

TEST(WatchManagerTest, CheckWithoutCachedValue) {
    WatchManager mgr;
    core::Handle handle{1, 0};
    mgr.set(handle, 0, 4);

    // Without update_cached_values, check should not trigger
    auto result = mgr.check([](core::Handle, std::size_t, std::size_t) {
        return std::vector<std::uint8_t>{1, 0, 0, 0};
    });
    EXPECT_FALSE(result.triggered);
}

TEST(WatchManagerTest, HitCount) {
    WatchManager mgr;
    core::Handle handle{1, 0};
    std::uint32_t id = mgr.set(handle, 0, 4);

    // Update cached values
    int call_count = 0;
    mgr.update_cached_values([&](core::Handle, std::size_t, std::size_t) {
        return std::vector<std::uint8_t>{static_cast<std::uint8_t>(call_count++), 0, 0, 0};
    });

    // Trigger twice
    (void)mgr.check([&](core::Handle, std::size_t, std::size_t) {
        return std::vector<std::uint8_t>{static_cast<std::uint8_t>(call_count++), 0, 0, 0};
    });
    (void)mgr.check([&](core::Handle, std::size_t, std::size_t) {
        return std::vector<std::uint8_t>{static_cast<std::uint8_t>(call_count++), 0, 0, 0};
    });

    EXPECT_EQ(mgr.get(id)->hit_count, 2);
}

TEST(WatchManagerTest, Clear) {
    WatchManager mgr;
    core::Handle h1{1, 0}, h2{2, 0};
    mgr.set(h1, 0, 4);
    mgr.set(h2, 0, 8);

    mgr.clear();
    EXPECT_EQ(mgr.count(), 0);
    EXPECT_TRUE(mgr.list().empty());
}

TEST(WatchManagerTest, WatchTypeToString) {
    EXPECT_STREQ(to_string(WatchType::Write), "write");
    EXPECT_STREQ(to_string(WatchType::Read), "read");
    EXPECT_STREQ(to_string(WatchType::ReadWrite), "read/write");
}

TEST(WatchManagerTest, UniqueIds) {
    WatchManager mgr;
    core::Handle handle{1, 0};
    std::uint32_t id1 = mgr.set(handle, 0, 4);
    std::uint32_t id2 = mgr.set(handle, 4, 4);
    std::uint32_t id3 = mgr.set(handle, 8, 4);

    EXPECT_NE(id1, id2);
    EXPECT_NE(id2, id3);
    EXPECT_NE(id1, id3);
}

}  // namespace
}  // namespace dotvm::debugger
