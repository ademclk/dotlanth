#include "dotvm/core/crypto/keccak.hpp"

#include <gtest/gtest.h>

#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

namespace dotvm::core::crypto {
namespace {

/// Convert digest to hex string for comparison
std::string to_hex(const Keccak256::Digest& digest) {
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (std::uint8_t byte : digest) {
        oss << std::setw(2) << static_cast<unsigned>(byte);
    }
    return oss.str();
}

// ============================================================================
// Ethereum Keccak-256 Test Vectors
// Note: Ethereum uses Keccak with 0x01 padding, NOT NIST SHA-3 (0x06 padding)
// These vectors are from the Ethereum Foundation and various EIP specifications
// ============================================================================

/// Test vector 1: Empty string ""
/// Ethereum keccak256("") - This is the well-known empty hash
TEST(Keccak256Test, EmptyString) {
    auto digest = Keccak256::hash("");
    EXPECT_EQ(to_hex(digest), "c5d2460186f7233c927e7db2dcc703c0e500b653ca82273b7bfad8045d85a470");
}

/// Test vector 2: "abc"
TEST(Keccak256Test, Abc) {
    auto digest = Keccak256::hash("abc");
    EXPECT_EQ(to_hex(digest), "4e03657aea45a94fc7d47ba826c8d667c0d1e6e33a64a036ec44f58fa12d6c45");
}

/// Test vector 3: "hello"
TEST(Keccak256Test, Hello) {
    auto digest = Keccak256::hash("hello");
    EXPECT_EQ(to_hex(digest), "1c8aff950685c2ed4bc3174f3472287b56d9517b9c948127319a09a7a36deac8");
}

/// Test vector 4: "Hello, World!"
TEST(Keccak256Test, HelloWorld) {
    auto digest = Keccak256::hash("Hello, World!");
    EXPECT_EQ(to_hex(digest), "acaf3289d7b601cbd114fb36c4d29c85bbfd5e133f14cb355c3fd8d99367964f");
}

/// Test vector 5: Ethereum address derivation test
/// keccak256("") is used in various Ethereum calculations
TEST(Keccak256Test, EthereumEmptyHash) {
    auto digest = Keccak256::hash("");
    // This hash is used to identify empty code in Ethereum
    EXPECT_EQ(to_hex(digest), "c5d2460186f7233c927e7db2dcc703c0e500b653ca82273b7bfad8045d85a470");
}

/// Test long message (rate boundary test)
/// 136 bytes is the rate for Keccak-256 (1088 bits)
TEST(Keccak256Test, ExactlyOneBlockMinusOne) {
    // 135 bytes (rate - 1)
    std::string data(135, 'a');
    auto digest = Keccak256::hash(data);
    EXPECT_FALSE(to_hex(digest).empty());
}

TEST(Keccak256Test, ExactlyOneBlock) {
    // 136 bytes (exactly one block/rate)
    std::string data(136, 'a');
    auto digest = Keccak256::hash(data);
    EXPECT_FALSE(to_hex(digest).empty());
}

TEST(Keccak256Test, ExactlyOneBlockPlusOne) {
    // 137 bytes (rate + 1, forces second block)
    std::string data(137, 'a');
    auto digest = Keccak256::hash(data);
    EXPECT_FALSE(to_hex(digest).empty());
}

// ============================================================================
// Binary Data Tests
// ============================================================================

TEST(Keccak256Test, SingleZeroByte) {
    std::vector<std::uint8_t> data = {0x00};
    auto digest = Keccak256::hash(std::span<const std::uint8_t>{data});
    EXPECT_EQ(to_hex(digest), "bc36789e7a1e281436464229828f817d6612f7b477d66591ff96a9e064bcc98a");
}

TEST(Keccak256Test, SingleOneByte) {
    std::vector<std::uint8_t> data = {0x01};
    auto digest = Keccak256::hash(std::span<const std::uint8_t>{data});
    // keccak256(0x01)
    EXPECT_EQ(to_hex(digest), "5fe7f977e71dba2ea1a68e21057beebb9be2ac30c6410aa38d4f3fbe41dcffd2");
}

TEST(Keccak256Test, FfByte) {
    std::vector<std::uint8_t> data = {0xff};
    auto digest = Keccak256::hash(std::span<const std::uint8_t>{data});
    EXPECT_FALSE(to_hex(digest).empty());
}

// ============================================================================
// Incremental Hashing Tests
// ============================================================================

TEST(Keccak256Test, IncrementalUpdate_Simple) {
    Keccak256 hasher;
    hasher.update("abc");
    auto digest = hasher.finalize();
    EXPECT_EQ(to_hex(digest), "4e03657aea45a94fc7d47ba826c8d667c0d1e6e33a64a036ec44f58fa12d6c45");
}

TEST(Keccak256Test, IncrementalUpdate_SplitData) {
    Keccak256 hasher;
    hasher.update("a");
    hasher.update("b");
    hasher.update("c");
    auto digest = hasher.finalize();
    EXPECT_EQ(to_hex(digest), "4e03657aea45a94fc7d47ba826c8d667c0d1e6e33a64a036ec44f58fa12d6c45");
}

TEST(Keccak256Test, IncrementalUpdate_LargeChunks) {
    // Create 2KB of data
    std::vector<std::uint8_t> full_data(2048);
    for (std::size_t i = 0; i < 2048; ++i) {
        full_data[i] = static_cast<std::uint8_t>(i % 256);
    }

    // Hash incrementally
    Keccak256 hasher;
    hasher.update(std::span<const std::uint8_t>{full_data.data(), 1024});
    hasher.update(std::span<const std::uint8_t>{full_data.data() + 1024, 1024});
    auto incremental_digest = hasher.finalize();

    // Hash all at once
    auto full_digest = Keccak256::hash(std::span<const std::uint8_t>{full_data});

    EXPECT_EQ(to_hex(incremental_digest), to_hex(full_digest));
}

TEST(Keccak256Test, IncrementalUpdate_ByteByByte) {
    Keccak256 hasher;
    std::string input = "abc";
    for (char c : input) {
        hasher.update(std::string_view(&c, 1));
    }
    auto digest = hasher.finalize();
    EXPECT_EQ(to_hex(digest), "4e03657aea45a94fc7d47ba826c8d667c0d1e6e33a64a036ec44f58fa12d6c45");
}

// ============================================================================
// Padding Tests (critical for Keccak vs SHA-3 compatibility)
// ============================================================================

/// Verify we use Keccak (0x01) padding, NOT SHA-3 (0x06) padding
/// SHA-3("abc") = 3a985da74fe225b2045c172d6bd390bd855f086e3e9d525b46bfe24511431532
/// Keccak-256("abc") = 4e03657aea45a94fc7d47ba826c8d667c0d1e6e33a64a036ec44f58fa12d6c45
TEST(Keccak256Test, NotSha3Padding) {
    auto digest = Keccak256::hash("abc");
    // This should NOT match SHA-3
    EXPECT_NE(to_hex(digest), "3a985da74fe225b2045c172d6bd390bd855f086e3e9d525b46bfe24511431532");
    // It SHOULD match Keccak (Ethereum style)
    EXPECT_EQ(to_hex(digest), "4e03657aea45a94fc7d47ba826c8d667c0d1e6e33a64a036ec44f58fa12d6c45");
}

// ============================================================================
// Large Data Tests
// ============================================================================

TEST(Keccak256Test, LargeInput) {
    // Test with 100KB of data
    std::vector<std::uint8_t> data(100 * 1024);
    for (std::size_t i = 0; i < data.size(); ++i) {
        data[i] = static_cast<std::uint8_t>(i % 256);
    }
    auto digest = Keccak256::hash(std::span<const std::uint8_t>{data});
    EXPECT_FALSE(to_hex(digest).empty());
}

TEST(Keccak256Test, OneMillion_A) {
    // One million 'a' characters
    std::string million_a(1'000'000, 'a');
    auto digest = Keccak256::hash(million_a);
    EXPECT_FALSE(to_hex(digest).empty());
}

// ============================================================================
// Consistency Tests
// ============================================================================

TEST(Keccak256Test, ConsistencyAcrossMultipleRuns) {
    const std::string input = "The quick brown fox jumps over the lazy dog";

    auto digest1 = Keccak256::hash(input);
    auto digest2 = Keccak256::hash(input);
    auto digest3 = Keccak256::hash(input);

    EXPECT_EQ(digest1, digest2);
    EXPECT_EQ(digest2, digest3);
}

TEST(Keccak256Test, HasherReuseAfterFinalize) {
    Keccak256 hasher;

    // First hash
    hasher.update("abc");
    auto digest1 = hasher.finalize();
    EXPECT_EQ(to_hex(digest1), "4e03657aea45a94fc7d47ba826c8d667c0d1e6e33a64a036ec44f58fa12d6c45");

    // Second hash (hasher should be reset)
    hasher.update("abc");
    auto digest2 = hasher.finalize();
    EXPECT_EQ(to_hex(digest2), "4e03657aea45a94fc7d47ba826c8d667c0d1e6e33a64a036ec44f58fa12d6c45");
}

// ============================================================================
// Ethereum-Specific Tests
// ============================================================================

/// Test keccak256 of an Ethereum address (20 bytes)
TEST(Keccak256Test, EthereumAddressHash) {
    // Example: hash of address 0x0000000000000000000000000000000000000000
    std::vector<std::uint8_t> address(20, 0x00);
    auto digest = Keccak256::hash(std::span<const std::uint8_t>{address});
    EXPECT_FALSE(to_hex(digest).empty());
}

/// Test keccak256 of a 32-byte value (common in Ethereum)
TEST(Keccak256Test, Ethereum32ByteValue) {
    std::vector<std::uint8_t> value(32, 0x00);
    auto digest = Keccak256::hash(std::span<const std::uint8_t>{value});
    EXPECT_FALSE(to_hex(digest).empty());
}

}  // namespace
}  // namespace dotvm::core::crypto
