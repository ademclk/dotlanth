/// @file object_id_test.cpp
/// @brief Unit tests for ObjectId and ObjectIdGenerator

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <thread>
#include <unordered_set>
#include <vector>

#include <gtest/gtest.h>

#include "dotvm/core/link/object_id.hpp"

namespace dotvm::core::link {
namespace {

// ============================================================================
// ObjectId Tests
// ============================================================================

TEST(ObjectIdTest, DefaultIsInvalid) {
    ObjectId id;
    EXPECT_FALSE(id.is_valid());
}

TEST(ObjectIdTest, NonZeroIsValid) {
    ObjectId id{1U, 2U};
    EXPECT_TRUE(id.is_valid());
}

TEST(ObjectIdTest, BytesRoundTrip) {
    ObjectId original{0x1122334455667788ULL, 0x99AABBCCDDEEFF00ULL};
    auto bytes = original.to_bytes();
    auto restored = ObjectId::from_bytes(bytes);

    EXPECT_EQ(restored, original);
}

TEST(ObjectIdTest, BytesAreLittleEndian) {
    ObjectId original{0x0102030405060708ULL, 0x0A0B0C0D0E0F1011ULL};
    auto bytes = original.to_bytes();

    const std::array<std::byte, 16> expected = {
        std::byte{0x08}, std::byte{0x07}, std::byte{0x06}, std::byte{0x05},
        std::byte{0x04}, std::byte{0x03}, std::byte{0x02}, std::byte{0x01},
        std::byte{0x11}, std::byte{0x10}, std::byte{0x0F}, std::byte{0x0E},
        std::byte{0x0D}, std::byte{0x0C}, std::byte{0x0B}, std::byte{0x0A},
    };

    EXPECT_EQ(bytes, expected);
}

// ============================================================================
// ObjectIdGenerator Tests
// ============================================================================

TEST(ObjectIdGeneratorTest, UniqueSequentialIds) {
    ObjectIdGenerator generator;

    auto first = generator.generate("User");
    auto second = generator.generate("User");

    EXPECT_NE(first.instance_id, second.instance_id);
    EXPECT_NE(first, second);
    EXPECT_TRUE(first.is_valid());
    EXPECT_TRUE(second.is_valid());
}

TEST(ObjectIdGeneratorTest, ConsistentTypeHash) {
    ObjectIdGenerator generator;

    auto first = generator.generate("Order");
    auto second = generator.generate("Order");

    EXPECT_EQ(first.type_hash, second.type_hash);
}

TEST(ObjectIdGeneratorTest, EmptyTypeNameIsInvalid) {
    ObjectIdGenerator generator;

    auto id = generator.generate("");

    EXPECT_FALSE(id.is_valid());
    EXPECT_EQ(id, ObjectId::invalid());
}

TEST(ObjectIdGeneratorTest, ThreadSafeGeneration) {
    ObjectIdGenerator generator;

    constexpr std::size_t kThreadCount = 8;
    constexpr std::size_t kIdsPerThread = 500;

    std::vector<ObjectId> ids;
    ids.reserve(kThreadCount * kIdsPerThread);

    std::mutex mutex;
    std::vector<std::thread> threads;
    threads.reserve(kThreadCount);

    for (std::size_t i = 0; i < kThreadCount; ++i) {
        threads.emplace_back([&]() {
            std::vector<ObjectId> local;
            local.reserve(kIdsPerThread);

            for (std::size_t j = 0; j < kIdsPerThread; ++j) {
                local.push_back(generator.generate("Session"));
            }

            const std::scoped_lock lock(mutex);
            ids.insert(ids.end(), local.begin(), local.end());
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    std::unordered_set<std::uint64_t> seen;
    seen.reserve(ids.size());

    for (const auto& id : ids) {
        EXPECT_TRUE(seen.insert(id.instance_id).second);
    }

    EXPECT_EQ(seen.size(), ids.size());
}

}  // namespace
}  // namespace dotvm::core::link
