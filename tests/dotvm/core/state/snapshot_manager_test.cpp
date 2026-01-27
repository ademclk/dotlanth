/// @file snapshot_manager_test.cpp
/// @brief STATE-009 SnapshotManager unit tests (TDD)
///
/// Tests for SnapshotManager providing point-in-time consistent reads:
/// - Snapshot creation and release
/// - Point-in-time state capture via COW semantics
/// - Isolation from concurrent writes
/// - GC coordination with active snapshots
/// - Concurrency and thread safety

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <future>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "dotvm/core/state/snapshot_manager.hpp"
#include "dotvm/core/state/state_backend.hpp"

namespace dotvm::core::state {
namespace {

// ============================================================================
// Helper Functions
// ============================================================================

/// @brief Create a byte span from a string literal
[[nodiscard]] std::span<const std::byte> to_bytes(std::string_view str) {
    return {reinterpret_cast<const std::byte*>(str.data()), str.size()};
}

/// @brief Create a byte vector from a string (unused but kept for consistency with other tests)
[[maybe_unused]] [[nodiscard]] std::vector<std::byte> make_bytes(std::string_view str) {
    std::vector<std::byte> result(str.size());
    std::memcpy(result.data(), str.data(), str.size());
    return result;
}

/// @brief Convert byte vector to string for comparison
[[nodiscard]] std::string to_string(std::span<const std::byte> bytes) {
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

// ============================================================================
// Test Fixture
// ============================================================================

class SnapshotManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        StateBackendConfig backend_config;
        backend_config.enable_transactions = true;

        SnapshotManagerConfig sm_config;
        sm_config.max_concurrent_snapshots = 10;
        sm_config.enable_periodic_snapshots = false;

        auto backend = create_state_backend(backend_config);
        sm_ = std::make_unique<SnapshotManager>(std::move(backend), sm_config);
    }

    std::unique_ptr<SnapshotManager> sm_;
};

// ============================================================================
// Snapshot Creation Tests
// ============================================================================

/// @test create_snapshot() returns a valid SnapshotId
TEST_F(SnapshotManagerTest, CreateSnapshotReturnsValidId) {
    auto result = sm_->create_snapshot();
    ASSERT_TRUE(result.is_ok());

    SnapshotId id = result.value();
    EXPECT_GT(id.id, 0u);
    EXPECT_GT(id.generation, 0u);
}

/// @test Multiple snapshots have unique IDs
TEST_F(SnapshotManagerTest, MultipleSnapshotsHaveUniqueIds) {
    auto result1 = sm_->create_snapshot();
    auto result2 = sm_->create_snapshot();
    ASSERT_TRUE(result1.is_ok());
    ASSERT_TRUE(result2.is_ok());

    EXPECT_NE(result1.value().id, result2.value().id);

    // Clean up
    EXPECT_TRUE(sm_->release_snapshot(result1.value()).is_ok());
    EXPECT_TRUE(sm_->release_snapshot(result2.value()).is_ok());
}

/// @test release_snapshot() succeeds for valid snapshot
TEST_F(SnapshotManagerTest, ReleaseSnapshotSucceeds) {
    auto create_result = sm_->create_snapshot();
    ASSERT_TRUE(create_result.is_ok());

    auto release_result = sm_->release_snapshot(create_result.value());
    EXPECT_TRUE(release_result.is_ok());
}

/// @test release_snapshot() fails for already-released snapshot
TEST_F(SnapshotManagerTest, ReleaseAlreadyReleasedFails) {
    auto create_result = sm_->create_snapshot();
    ASSERT_TRUE(create_result.is_ok());

    SnapshotId id = create_result.value();
    ASSERT_TRUE(sm_->release_snapshot(id).is_ok());

    // Second release should fail
    auto second_release = sm_->release_snapshot(id);
    EXPECT_TRUE(second_release.is_err());
    EXPECT_EQ(second_release.error(), SnapshotError::SnapshotNotFound);
}

/// @test release_snapshot() fails for invalid snapshot ID
TEST_F(SnapshotManagerTest, ReleaseInvalidIdFails) {
    SnapshotId invalid_id{.id = 999, .generation = 999};
    auto result = sm_->release_snapshot(invalid_id);
    EXPECT_TRUE(result.is_err());
    EXPECT_EQ(result.error(), SnapshotError::SnapshotNotFound);
}

// ============================================================================
// Max Snapshots Tests
// ============================================================================

/// @test Max concurrent snapshots is enforced
TEST_F(SnapshotManagerTest, MaxSnapshotsEnforced) {
    // Create config with limit of 3
    StateBackendConfig backend_config;
    SnapshotManagerConfig sm_config;
    sm_config.max_concurrent_snapshots = 3;

    auto backend = create_state_backend(backend_config);
    SnapshotManager sm(std::move(backend), sm_config);

    // Create 3 snapshots (at limit)
    std::vector<SnapshotId> snapshots;
    for (int i = 0; i < 3; ++i) {
        auto result = sm.create_snapshot();
        ASSERT_TRUE(result.is_ok()) << "Failed to create snapshot " << i;
        snapshots.push_back(result.value());
    }

    // 4th snapshot should fail
    auto result = sm.create_snapshot();
    EXPECT_TRUE(result.is_err());
    EXPECT_EQ(result.error(), SnapshotError::TooManySnapshots);

    // Release one snapshot
    ASSERT_TRUE(sm.release_snapshot(snapshots[0]).is_ok());

    // Now we can create a new one
    auto new_result = sm.create_snapshot();
    EXPECT_TRUE(new_result.is_ok());

    // Clean up
    for (std::size_t i = 1; i < snapshots.size(); ++i) {
        EXPECT_TRUE(sm.release_snapshot(snapshots[i]).is_ok());
    }
    EXPECT_TRUE(sm.release_snapshot(new_result.value()).is_ok());
}

// ============================================================================
// Point-in-Time State Tests
// ============================================================================

/// @test Snapshot sees state at creation time
TEST_F(SnapshotManagerTest, SnapshotSeesPointInTimeState) {
    auto key = to_bytes("snapshot_key");
    auto initial_value = to_bytes("initial");
    auto modified_value = to_bytes("modified");

    // Write initial value
    ASSERT_TRUE(sm_->backend().put(key, initial_value).is_ok());

    // Create snapshot
    auto snap_result = sm_->create_snapshot();
    ASSERT_TRUE(snap_result.is_ok());
    SnapshotId snap = snap_result.value();

    // Modify value after snapshot
    ASSERT_TRUE(sm_->backend().put(key, modified_value).is_ok());

    // Snapshot should still see initial value
    auto get_result = sm_->get(snap, key);
    ASSERT_TRUE(get_result.is_ok());
    EXPECT_EQ(to_string(get_result.value()), "initial");

    // Direct backend get should see modified value
    auto current_result = sm_->backend().get(key);
    ASSERT_TRUE(current_result.is_ok());
    EXPECT_EQ(to_string(current_result.value()), "modified");

    EXPECT_TRUE(sm_->release_snapshot(snap).is_ok());
}

/// @test Snapshot is isolated from new writes
TEST_F(SnapshotManagerTest, SnapshotIsolatedFromNewWrites) {
    auto key1 = to_bytes("key1");
    auto key2 = to_bytes("key2");
    auto value1 = to_bytes("value1");
    auto value2 = to_bytes("value2");

    // Write key1
    ASSERT_TRUE(sm_->backend().put(key1, value1).is_ok());

    // Create snapshot
    auto snap_result = sm_->create_snapshot();
    ASSERT_TRUE(snap_result.is_ok());
    SnapshotId snap = snap_result.value();

    // Write key2 after snapshot
    ASSERT_TRUE(sm_->backend().put(key2, value2).is_ok());

    // Snapshot should see key1
    auto get1 = sm_->get(snap, key1);
    ASSERT_TRUE(get1.is_ok());
    EXPECT_EQ(to_string(get1.value()), "value1");

    // Snapshot should NOT see key2
    auto get2 = sm_->get(snap, key2);
    EXPECT_TRUE(get2.is_err());
    EXPECT_EQ(get2.error(), SnapshotError::KeyNotFound);

    // Current state should see both
    EXPECT_TRUE(sm_->backend().exists(key1));
    EXPECT_TRUE(sm_->backend().exists(key2));

    EXPECT_TRUE(sm_->release_snapshot(snap).is_ok());
}

/// @test Snapshot sees deletion at snapshot time
TEST_F(SnapshotManagerTest, SnapshotSeesDeletedState) {
    auto key = to_bytes("delete_key");
    auto value = to_bytes("delete_value");

    // Write then delete
    ASSERT_TRUE(sm_->backend().put(key, value).is_ok());
    ASSERT_TRUE(sm_->backend().remove(key).is_ok());

    // Create snapshot (key is deleted)
    auto snap_result = sm_->create_snapshot();
    ASSERT_TRUE(snap_result.is_ok());
    SnapshotId snap = snap_result.value();

    // Re-add the key
    ASSERT_TRUE(sm_->backend().put(key, to_bytes("new_value")).is_ok());

    // Snapshot should NOT see the key (it was deleted at snapshot time)
    auto get_result = sm_->get(snap, key);
    EXPECT_TRUE(get_result.is_err());
    EXPECT_EQ(get_result.error(), SnapshotError::KeyNotFound);

    EXPECT_TRUE(sm_->release_snapshot(snap).is_ok());
}

// ============================================================================
// Iteration Tests
// ============================================================================

/// @test Snapshot iteration sees point-in-time state
TEST_F(SnapshotManagerTest, IterateAtSnapshotTime) {
    // Write initial data
    for (int i = 0; i < 5; ++i) {
        std::string key = "iter_key_" + std::to_string(i);
        std::string value = "iter_value_" + std::to_string(i);
        ASSERT_TRUE(sm_->backend().put(to_bytes(key), to_bytes(value)).is_ok());
    }

    // Create snapshot
    auto snap_result = sm_->create_snapshot();
    ASSERT_TRUE(snap_result.is_ok());
    SnapshotId snap = snap_result.value();

    // Add more data after snapshot
    for (int i = 5; i < 10; ++i) {
        std::string key = "iter_key_" + std::to_string(i);
        std::string value = "iter_value_" + std::to_string(i);
        ASSERT_TRUE(sm_->backend().put(to_bytes(key), to_bytes(value)).is_ok());
    }

    // Iterate snapshot - should only see first 5
    std::vector<std::string> snapshot_keys;
    auto result = sm_->iterate(snap, to_bytes("iter_key_"),
                               [&snapshot_keys](StateBackend::Key key, StateBackend::Value) {
                                   snapshot_keys.push_back(to_string(key));
                                   return true;
                               });
    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(snapshot_keys.size(), 5u);

    // Iterate current state - should see all 10
    std::vector<std::string> current_keys;
    auto current_result = sm_->backend().iterate(
        to_bytes("iter_key_"), [&current_keys](StateBackend::Key key, StateBackend::Value) {
            current_keys.push_back(to_string(key));
            return true;
        });
    ASSERT_TRUE(current_result.is_ok());
    EXPECT_EQ(current_keys.size(), 10u);

    EXPECT_TRUE(sm_->release_snapshot(snap).is_ok());
}

// ============================================================================
// GC Coordination Tests
// ============================================================================

/// @test GC is blocked by active snapshot
TEST_F(SnapshotManagerTest, GCBlockedByActiveSnapshot) {
    auto key = to_bytes("gc_key");

    // Write multiple versions
    ASSERT_TRUE(sm_->backend().put(key, to_bytes("version1")).is_ok());

    // Create snapshot at version1
    auto snap_result = sm_->create_snapshot();
    ASSERT_TRUE(snap_result.is_ok());
    SnapshotId snap = snap_result.value();

    // Write more versions (would normally be GC'd)
    for (int i = 2; i <= 10; ++i) {
        std::string val = "version" + std::to_string(i);
        ASSERT_TRUE(sm_->backend().put(key, to_bytes(val)).is_ok());
    }

    // Snapshot should still see version1 (GC blocked)
    auto get_result = sm_->get(snap, key);
    ASSERT_TRUE(get_result.is_ok());
    EXPECT_EQ(to_string(get_result.value()), "version1");

    // Release snapshot
    EXPECT_TRUE(sm_->release_snapshot(snap).is_ok());

    // Now GC can proceed (versions before released snapshot can be cleaned)
    // Verify current state is intact
    auto current = sm_->backend().get(key);
    ASSERT_TRUE(current.is_ok());
    EXPECT_EQ(to_string(current.value()), "version10");
}

/// @test get_min_active_snapshot_version() returns correct value
TEST_F(SnapshotManagerTest, MinActiveSnapshotVersionCorrect) {
    // No snapshots
    std::uint64_t min_v1 = sm_->get_min_active_snapshot_version();
    EXPECT_EQ(min_v1, std::numeric_limits<std::uint64_t>::max());

    // Write to advance version
    ASSERT_TRUE(sm_->backend().put(to_bytes("key1"), to_bytes("val1")).is_ok());
    ASSERT_TRUE(sm_->backend().put(to_bytes("key2"), to_bytes("val2")).is_ok());

    // Create snapshot
    auto snap1 = sm_->create_snapshot();
    ASSERT_TRUE(snap1.is_ok());
    std::uint64_t snap1_version = sm_->get_min_active_snapshot_version();

    // Write more
    ASSERT_TRUE(sm_->backend().put(to_bytes("key3"), to_bytes("val3")).is_ok());

    // Create another snapshot
    auto snap2 = sm_->create_snapshot();
    ASSERT_TRUE(snap2.is_ok());

    // Min should still be snap1's version
    EXPECT_EQ(sm_->get_min_active_snapshot_version(), snap1_version);

    // Release snap1
    EXPECT_TRUE(sm_->release_snapshot(snap1.value()).is_ok());

    // Min should now be snap2's version (higher)
    EXPECT_GT(sm_->get_min_active_snapshot_version(), snap1_version);

    // Release snap2
    EXPECT_TRUE(sm_->release_snapshot(snap2.value()).is_ok());

    // No active snapshots again
    EXPECT_EQ(sm_->get_min_active_snapshot_version(), std::numeric_limits<std::uint64_t>::max());
}

// ============================================================================
// Concurrency Tests
// ============================================================================

/// @test Concurrent create and release are safe
TEST_F(SnapshotManagerTest, ConcurrentCreateReleaseSafe) {
    constexpr int num_threads = 4;
    constexpr int ops_per_thread = 25;

    std::atomic<int> success_count{0};
    std::atomic<int> limit_count{0};

    std::vector<std::future<void>> futures;

    for (int t = 0; t < num_threads; ++t) {
        futures.push_back(std::async(std::launch::async, [&]() {
            for (int i = 0; i < ops_per_thread; ++i) {
                auto create_result = sm_->create_snapshot();
                if (create_result.is_ok()) {
                    success_count++;
                    // Small delay to simulate work
                    std::this_thread::sleep_for(std::chrono::microseconds(100));
                    auto release_result = sm_->release_snapshot(create_result.value());
                    EXPECT_TRUE(release_result.is_ok());
                } else {
                    limit_count++;
                }
            }
        }));
    }

    for (auto& f : futures) {
        f.wait();
    }

    // Most should succeed (limit is 10, we're cycling quickly)
    EXPECT_GT(success_count.load(), 0);
    EXPECT_EQ(success_count.load() + limit_count.load(), num_threads * ops_per_thread);
}

/// @test Concurrent reads via different snapshots have no data races
TEST_F(SnapshotManagerTest, ConcurrentSnapshotReadsNoRace) {
    // Setup data
    for (int i = 0; i < 100; ++i) {
        std::string key = "race_key_" + std::to_string(i);
        std::string value = "race_value_" + std::to_string(i);
        ASSERT_TRUE(sm_->backend().put(to_bytes(key), to_bytes(value)).is_ok());
    }

    // Create a shared snapshot
    auto snap_result = sm_->create_snapshot();
    ASSERT_TRUE(snap_result.is_ok());
    SnapshotId snap = snap_result.value();

    std::atomic<int> read_count{0};
    constexpr int num_threads = 4;

    std::vector<std::future<void>> futures;

    for (int t = 0; t < num_threads; ++t) {
        futures.push_back(std::async(std::launch::async, [&]() {
            for (int i = 0; i < 100; ++i) {
                std::string key = "race_key_" + std::to_string(i);
                auto get_result = sm_->get(snap, to_bytes(key));
                if (get_result.is_ok()) {
                    read_count++;
                }
            }
        }));
    }

    for (auto& f : futures) {
        f.wait();
    }

    EXPECT_EQ(read_count.load(), num_threads * 100);

    EXPECT_TRUE(sm_->release_snapshot(snap).is_ok());
}

/// @test Concurrent writes don't affect existing snapshot
TEST_F(SnapshotManagerTest, ConcurrentWritesDontAffectSnapshot) {
    // Initial state
    ASSERT_TRUE(sm_->backend().put(to_bytes("shared_key"), to_bytes("initial")).is_ok());

    // Create snapshot
    auto snap_result = sm_->create_snapshot();
    ASSERT_TRUE(snap_result.is_ok());
    SnapshotId snap = snap_result.value();

    // Concurrent writes
    std::vector<std::future<void>> writers;
    for (int t = 0; t < 4; ++t) {
        writers.push_back(std::async(std::launch::async, [&, t]() {
            for (int i = 0; i < 100; ++i) {
                std::string value = "thread_" + std::to_string(t) + "_" + std::to_string(i);
                sm_->backend().put(to_bytes("shared_key"), to_bytes(value));
            }
        }));
    }

    // Concurrent reads from snapshot
    std::atomic<int> correct_reads{0};
    std::vector<std::future<void>> readers;
    for (int t = 0; t < 4; ++t) {
        readers.push_back(std::async(std::launch::async, [&]() {
            for (int i = 0; i < 100; ++i) {
                auto get_result = sm_->get(snap, to_bytes("shared_key"));
                if (get_result.is_ok() && to_string(get_result.value()) == "initial") {
                    correct_reads++;
                }
            }
        }));
    }

    for (auto& f : writers) {
        f.wait();
    }
    for (auto& f : readers) {
        f.wait();
    }

    // All snapshot reads should see "initial"
    EXPECT_EQ(correct_reads.load(), 4 * 100);

    EXPECT_TRUE(sm_->release_snapshot(snap).is_ok());
}

// ============================================================================
// Edge Cases
// ============================================================================

/// @test Snapshot on empty backend
TEST_F(SnapshotManagerTest, SnapshotEmptyBackend) {
    auto snap_result = sm_->create_snapshot();
    ASSERT_TRUE(snap_result.is_ok());
    SnapshotId snap = snap_result.value();

    // Get should fail
    auto get_result = sm_->get(snap, to_bytes("nonexistent"));
    EXPECT_TRUE(get_result.is_err());
    EXPECT_EQ(get_result.error(), SnapshotError::KeyNotFound);

    // Iterate should succeed with no results
    int count = 0;
    auto iter_result = sm_->iterate(snap, {}, [&count](StateBackend::Key, StateBackend::Value) {
        count++;
        return true;
    });
    EXPECT_TRUE(iter_result.is_ok());
    EXPECT_EQ(count, 0);

    EXPECT_TRUE(sm_->release_snapshot(snap).is_ok());
}

/// @test Using invalid snapshot ID fails gracefully
TEST_F(SnapshotManagerTest, InvalidSnapshotIdFails) {
    SnapshotId invalid{.id = 999, .generation = 999};

    auto get_result = sm_->get(invalid, to_bytes("key"));
    EXPECT_TRUE(get_result.is_err());
    EXPECT_EQ(get_result.error(), SnapshotError::SnapshotNotFound);

    auto iter_result =
        sm_->iterate(invalid, {}, [](StateBackend::Key, StateBackend::Value) { return true; });
    EXPECT_TRUE(iter_result.is_err());
    EXPECT_EQ(iter_result.error(), SnapshotError::SnapshotNotFound);
}

/// @test Generation prevents reuse issues
TEST_F(SnapshotManagerTest, GenerationPreventsReuse) {
    // Create and release snapshot
    auto snap1 = sm_->create_snapshot();
    ASSERT_TRUE(snap1.is_ok());
    SnapshotId id1 = snap1.value();
    EXPECT_TRUE(sm_->release_snapshot(id1).is_ok());

    // Create new snapshot (might reuse ID internally)
    auto snap2 = sm_->create_snapshot();
    ASSERT_TRUE(snap2.is_ok());

    // Old ID should not work (different generation)
    auto get_result = sm_->get(id1, to_bytes("key"));
    EXPECT_TRUE(get_result.is_err());

    EXPECT_TRUE(sm_->release_snapshot(snap2.value()).is_ok());
}

/// @test Snapshot count is accurate
TEST_F(SnapshotManagerTest, ActiveSnapshotCountAccurate) {
    EXPECT_EQ(sm_->active_snapshot_count(), 0u);

    auto s1 = sm_->create_snapshot();
    ASSERT_TRUE(s1.is_ok());
    EXPECT_EQ(sm_->active_snapshot_count(), 1u);

    auto s2 = sm_->create_snapshot();
    ASSERT_TRUE(s2.is_ok());
    EXPECT_EQ(sm_->active_snapshot_count(), 2u);

    EXPECT_TRUE(sm_->release_snapshot(s1.value()).is_ok());
    EXPECT_EQ(sm_->active_snapshot_count(), 1u);

    EXPECT_TRUE(sm_->release_snapshot(s2.value()).is_ok());
    EXPECT_EQ(sm_->active_snapshot_count(), 0u);
}

/// @test Config is accessible
TEST_F(SnapshotManagerTest, ConfigAccessible) {
    const auto& config = sm_->config();
    EXPECT_EQ(config.max_concurrent_snapshots, 10u);
    EXPECT_FALSE(config.enable_periodic_snapshots);
}

/// @test Backend is accessible
TEST_F(SnapshotManagerTest, BackendAccessible) {
    StateBackend& backend = sm_->backend();

    auto key = to_bytes("backend_key");
    auto value = to_bytes("backend_value");
    ASSERT_TRUE(backend.put(key, value).is_ok());

    auto get_result = backend.get(key);
    ASSERT_TRUE(get_result.is_ok());
    EXPECT_EQ(to_string(get_result.value()), "backend_value");
}

}  // namespace
}  // namespace dotvm::core::state
