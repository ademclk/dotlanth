/// @file watcher_test.cpp
/// @brief DSL-003 File watcher unit tests

#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "dotvm/cli/watcher.hpp"

using namespace dotvm::cli;
using namespace std::chrono_literals;

// ============================================================================
// Watcher Construction Tests
// ============================================================================

TEST(WatcherTest, Construction) {
    auto temp_dir = std::filesystem::temp_directory_path() / "watcher_test";
    std::filesystem::create_directories(temp_dir);

    auto callback = [](const std::filesystem::path&, WatchEvent) {};
    Watcher watcher(temp_dir, callback);

    EXPECT_EQ(watcher.watch_path(), temp_dir);
    EXPECT_FALSE(watcher.is_running());

    std::filesystem::remove_all(temp_dir);
}

TEST(WatcherTest, ConstructionWithExtensions) {
    auto temp_dir = std::filesystem::temp_directory_path() / "watcher_test_ext";
    std::filesystem::create_directories(temp_dir);

    auto callback = [](const std::filesystem::path&, WatchEvent) {};
    Watcher watcher(temp_dir, callback, {".dsl", ".txt"});

    EXPECT_TRUE(watcher.has_watched_extension(std::filesystem::path("test.dsl")));
    EXPECT_TRUE(watcher.has_watched_extension(std::filesystem::path("test.txt")));
    EXPECT_FALSE(watcher.has_watched_extension(std::filesystem::path("test.cpp")));

    std::filesystem::remove_all(temp_dir);
}

TEST(WatcherTest, ConstructionEmptyExtensions) {
    auto temp_dir = std::filesystem::temp_directory_path() / "watcher_test_empty";
    std::filesystem::create_directories(temp_dir);

    auto callback = [](const std::filesystem::path&, WatchEvent) {};
    Watcher watcher(temp_dir, callback, {});

    // Empty extensions means watch all files
    EXPECT_TRUE(watcher.has_watched_extension(std::filesystem::path("test.dsl")));
    EXPECT_TRUE(watcher.has_watched_extension(std::filesystem::path("test.cpp")));

    std::filesystem::remove_all(temp_dir);
}

// ============================================================================
// Watcher Configuration Tests
// ============================================================================

TEST(WatcherTest, DefaultIntervals) {
    auto temp_dir = std::filesystem::temp_directory_path() / "watcher_test_intervals";
    std::filesystem::create_directories(temp_dir);

    auto callback = [](const std::filesystem::path&, WatchEvent) {};
    Watcher watcher(temp_dir, callback);

    EXPECT_EQ(watcher.poll_interval(), Watcher::kDefaultPollInterval);
    EXPECT_EQ(watcher.debounce_interval(), Watcher::kDefaultDebounceInterval);

    std::filesystem::remove_all(temp_dir);
}

TEST(WatcherTest, SetPollInterval) {
    auto temp_dir = std::filesystem::temp_directory_path() / "watcher_test_poll";
    std::filesystem::create_directories(temp_dir);

    auto callback = [](const std::filesystem::path&, WatchEvent) {};
    Watcher watcher(temp_dir, callback);

    watcher.set_poll_interval(200ms);
    EXPECT_EQ(watcher.poll_interval(), 200ms);

    std::filesystem::remove_all(temp_dir);
}

TEST(WatcherTest, SetDebounceInterval) {
    auto temp_dir = std::filesystem::temp_directory_path() / "watcher_test_debounce";
    std::filesystem::create_directories(temp_dir);

    auto callback = [](const std::filesystem::path&, WatchEvent) {};
    Watcher watcher(temp_dir, callback);

    watcher.set_debounce_interval(50ms);
    EXPECT_EQ(watcher.debounce_interval(), 50ms);

    std::filesystem::remove_all(temp_dir);
}

// ============================================================================
// Watcher Start/Stop Tests
// ============================================================================

TEST(WatcherTest, StartStop) {
    auto temp_dir = std::filesystem::temp_directory_path() / "watcher_test_startstop";
    std::filesystem::create_directories(temp_dir);

    auto callback = [](const std::filesystem::path&, WatchEvent) {};
    Watcher watcher(temp_dir, callback);

    EXPECT_FALSE(watcher.is_running());

    watcher.start();
    EXPECT_TRUE(watcher.is_running());

    watcher.stop();
    EXPECT_FALSE(watcher.is_running());

    std::filesystem::remove_all(temp_dir);
}

TEST(WatcherTest, DoubleStartIgnored) {
    auto temp_dir = std::filesystem::temp_directory_path() / "watcher_test_doublestart";
    std::filesystem::create_directories(temp_dir);

    auto callback = [](const std::filesystem::path&, WatchEvent) {};
    Watcher watcher(temp_dir, callback);

    watcher.start();
    EXPECT_TRUE(watcher.is_running());

    // Second start should be ignored
    watcher.start();
    EXPECT_TRUE(watcher.is_running());

    watcher.stop();
    EXPECT_FALSE(watcher.is_running());

    std::filesystem::remove_all(temp_dir);
}

TEST(WatcherTest, DoubleStopSafe) {
    auto temp_dir = std::filesystem::temp_directory_path() / "watcher_test_doublestop";
    std::filesystem::create_directories(temp_dir);

    auto callback = [](const std::filesystem::path&, WatchEvent) {};
    Watcher watcher(temp_dir, callback);

    watcher.start();
    watcher.stop();
    EXPECT_FALSE(watcher.is_running());

    // Second stop should be safe
    watcher.stop();
    EXPECT_FALSE(watcher.is_running());

    std::filesystem::remove_all(temp_dir);
}

// ============================================================================
// Watcher Move Tests
// ============================================================================

TEST(WatcherTest, MoveConstruction) {
    auto temp_dir = std::filesystem::temp_directory_path() / "watcher_test_move";
    std::filesystem::create_directories(temp_dir);

    auto callback = [](const std::filesystem::path&, WatchEvent) {};
    Watcher watcher1(temp_dir, callback);
    watcher1.set_poll_interval(200ms);

    Watcher watcher2(std::move(watcher1));

    EXPECT_EQ(watcher2.watch_path(), temp_dir);
    EXPECT_EQ(watcher2.poll_interval(), 200ms);
    EXPECT_FALSE(watcher2.is_running());

    std::filesystem::remove_all(temp_dir);
}

TEST(WatcherTest, MoveAssignment) {
    auto temp_dir1 = std::filesystem::temp_directory_path() / "watcher_test_move1";
    auto temp_dir2 = std::filesystem::temp_directory_path() / "watcher_test_move2";
    std::filesystem::create_directories(temp_dir1);
    std::filesystem::create_directories(temp_dir2);

    auto callback = [](const std::filesystem::path&, WatchEvent) {};
    Watcher watcher1(temp_dir1, callback);
    watcher1.set_poll_interval(200ms);

    Watcher watcher2(temp_dir2, callback);

    watcher2 = std::move(watcher1);

    EXPECT_EQ(watcher2.watch_path(), temp_dir1);
    EXPECT_EQ(watcher2.poll_interval(), 200ms);

    std::filesystem::remove_all(temp_dir1);
    std::filesystem::remove_all(temp_dir2);
}

// ============================================================================
// Watcher Destruction Tests
// ============================================================================

TEST(WatcherTest, DestructorStopsWatcher) {
    auto temp_dir = std::filesystem::temp_directory_path() / "watcher_test_destructor";
    std::filesystem::create_directories(temp_dir);

    {
        auto callback = [](const std::filesystem::path&, WatchEvent) {};
        Watcher watcher(temp_dir, callback);
        watcher.start();
        EXPECT_TRUE(watcher.is_running());
        // Destructor should stop the watcher
    }

    // If we get here without hanging, the destructor worked
    SUCCEED();

    std::filesystem::remove_all(temp_dir);
}

// ============================================================================
// File Change Detection Tests
// ============================================================================

TEST(WatcherTest, DetectsFileCreation) {
    auto temp_dir = std::filesystem::temp_directory_path() / "watcher_test_create";
    std::filesystem::create_directories(temp_dir);

    std::vector<std::pair<std::filesystem::path, WatchEvent>> events;
    std::mutex events_mutex;

    auto callback = [&](const std::filesystem::path& path, WatchEvent event) {
        std::lock_guard lock(events_mutex);
        events.emplace_back(path, event);
    };

    Watcher watcher(temp_dir, callback, {".dsl"});
    watcher.set_poll_interval(50ms);
    watcher.set_debounce_interval(10ms);
    watcher.start();

    // Wait for initial scan
    std::this_thread::sleep_for(100ms);

    // Create a new file
    auto test_file = temp_dir / "test.dsl";
    {
        std::ofstream ofs(test_file);
        ofs << "fn test() { }" << std::endl;
    }

    // Wait for detection
    std::this_thread::sleep_for(200ms);

    watcher.stop();

    {
        std::lock_guard lock(events_mutex);
        ASSERT_GE(events.size(), 1u);
        EXPECT_EQ(events[0].second, WatchEvent::Created);
        EXPECT_EQ(events[0].first.filename(), "test.dsl");
    }

    std::filesystem::remove_all(temp_dir);
}

TEST(WatcherTest, DetectsFileModification) {
    auto temp_dir = std::filesystem::temp_directory_path() / "watcher_test_modify";
    std::filesystem::create_directories(temp_dir);

    // Create the file first
    auto test_file = temp_dir / "test.dsl";
    {
        std::ofstream ofs(test_file);
        ofs << "fn test() { }" << std::endl;
    }

    std::vector<std::pair<std::filesystem::path, WatchEvent>> events;
    std::mutex events_mutex;

    auto callback = [&](const std::filesystem::path& path, WatchEvent event) {
        std::lock_guard lock(events_mutex);
        events.emplace_back(path, event);
    };

    Watcher watcher(temp_dir, callback, {".dsl"});
    watcher.set_poll_interval(50ms);
    watcher.set_debounce_interval(10ms);
    watcher.start();

    // Wait for initial scan
    std::this_thread::sleep_for(100ms);

    // Modify the file
    {
        std::ofstream ofs(test_file, std::ios::app);
        ofs << "fn test2() { }" << std::endl;
    }

    // Wait for detection
    std::this_thread::sleep_for(200ms);

    watcher.stop();

    {
        std::lock_guard lock(events_mutex);
        ASSERT_GE(events.size(), 1u);
        EXPECT_EQ(events.back().second, WatchEvent::Modified);
        EXPECT_EQ(events.back().first.filename(), "test.dsl");
    }

    std::filesystem::remove_all(temp_dir);
}

TEST(WatcherTest, IgnoresNonMatchingExtensions) {
    auto temp_dir = std::filesystem::temp_directory_path() / "watcher_test_ignore";
    std::filesystem::create_directories(temp_dir);

    std::vector<std::pair<std::filesystem::path, WatchEvent>> events;
    std::mutex events_mutex;

    auto callback = [&](const std::filesystem::path& path, WatchEvent event) {
        std::lock_guard lock(events_mutex);
        events.emplace_back(path, event);
    };

    Watcher watcher(temp_dir, callback, {".dsl"});
    watcher.set_poll_interval(50ms);
    watcher.set_debounce_interval(10ms);
    watcher.start();

    // Wait for initial scan
    std::this_thread::sleep_for(100ms);

    // Create a file with non-matching extension
    auto test_file = temp_dir / "test.txt";
    {
        std::ofstream ofs(test_file);
        ofs << "hello" << std::endl;
    }

    // Wait for potential detection
    std::this_thread::sleep_for(200ms);

    watcher.stop();

    {
        std::lock_guard lock(events_mutex);
        // Should not have detected the .txt file
        EXPECT_TRUE(events.empty());
    }

    std::filesystem::remove_all(temp_dir);
}

// ============================================================================
// Single File Watch Tests
// ============================================================================

TEST(WatcherTest, WatchSingleFile) {
    auto temp_dir = std::filesystem::temp_directory_path() / "watcher_test_single";
    std::filesystem::create_directories(temp_dir);

    // Create the file first
    auto test_file = temp_dir / "single.dsl";
    {
        std::ofstream ofs(test_file);
        ofs << "fn test() { }" << std::endl;
    }

    std::vector<std::pair<std::filesystem::path, WatchEvent>> events;
    std::mutex events_mutex;

    auto callback = [&](const std::filesystem::path& path, WatchEvent event) {
        std::lock_guard lock(events_mutex);
        events.emplace_back(path, event);
    };

    // Watch the single file, not the directory
    Watcher watcher(test_file, callback, {".dsl"});
    watcher.set_poll_interval(50ms);
    watcher.set_debounce_interval(10ms);
    watcher.start();

    // Wait for initial scan
    std::this_thread::sleep_for(100ms);

    // Modify the file
    {
        std::ofstream ofs(test_file, std::ios::app);
        ofs << "fn test2() { }" << std::endl;
    }

    // Wait for detection
    std::this_thread::sleep_for(200ms);

    watcher.stop();

    {
        std::lock_guard lock(events_mutex);
        ASSERT_GE(events.size(), 1u);
        EXPECT_EQ(events.back().second, WatchEvent::Modified);
    }

    std::filesystem::remove_all(temp_dir);
}

// ============================================================================
// Debouncing Tests
// ============================================================================

TEST(WatcherTest, DebouncesRapidChanges) {
    auto temp_dir = std::filesystem::temp_directory_path() / "watcher_test_debounce_rapid";
    std::filesystem::create_directories(temp_dir);

    // Create the file first
    auto test_file = temp_dir / "debounce.dsl";
    {
        std::ofstream ofs(test_file);
        ofs << "fn test() { }" << std::endl;
    }

    std::atomic<int> event_count{0};

    auto callback = [&](const std::filesystem::path&, WatchEvent) { event_count.fetch_add(1); };

    Watcher watcher(temp_dir, callback, {".dsl"});
    watcher.set_poll_interval(20ms);
    watcher.set_debounce_interval(150ms);  // 150ms debounce
    watcher.start();

    // Wait for initial scan
    std::this_thread::sleep_for(100ms);

    // Make multiple rapid changes (within debounce window)
    for (int i = 0; i < 5; ++i) {
        {
            std::ofstream ofs(test_file);
            ofs << "fn test" << i << "() { }" << std::endl;
        }
        std::this_thread::sleep_for(20ms);  // Less than debounce interval
    }

    // Wait for debounce window to close
    std::this_thread::sleep_for(300ms);

    watcher.stop();

    // Should have received far fewer events than modifications due to debouncing
    // The exact number depends on timing, but should be less than 5
    EXPECT_LT(event_count.load(), 5);

    std::filesystem::remove_all(temp_dir);
}
