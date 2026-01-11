#include "dotvm/core/crypto/sha256.hpp"

#include <gtest/gtest.h>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

namespace dotvm::core::crypto {
namespace {

/// Convert digest to hex string for comparison
std::string to_hex(const Sha256::Digest& digest) {
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (std::uint8_t byte : digest) {
        oss << std::setw(2) << static_cast<unsigned>(byte);
    }
    return oss.str();
}

// ============================================================================
// NIST SHA-256 Test Vectors (FIPS 180-4)
// ============================================================================

/// Test vector 1: Empty string ""
/// Expected: e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855
TEST(Sha256Test, EmptyString) {
    auto digest = Sha256::hash("");
    EXPECT_EQ(to_hex(digest), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

/// Test vector 2: "abc"
/// Expected: ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad
TEST(Sha256Test, Abc) {
    auto digest = Sha256::hash("abc");
    EXPECT_EQ(to_hex(digest), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

/// Test vector 3: "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq" (448 bits)
/// Expected: 248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1
TEST(Sha256Test, TwoBlockMessage) {
    auto digest = Sha256::hash("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq");
    EXPECT_EQ(to_hex(digest), "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}

/// Test vector 4: One million 'a' characters
/// Expected: cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0
TEST(Sha256Test, OneMillion_A) {
    std::string million_a(1'000'000, 'a');
    auto digest = Sha256::hash(million_a);
    EXPECT_EQ(to_hex(digest), "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

/// Additional test: "abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu"
/// Expected: cf5b16a778af8380036ce59e7b0492370b249b11e8f07a51afac45037afee9d1
TEST(Sha256Test, LongTwoBlockMessage) {
    auto digest = Sha256::hash(
        "abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmno"
        "ijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu"
    );
    EXPECT_EQ(to_hex(digest), "cf5b16a778af8380036ce59e7b0492370b249b11e8f07a51afac45037afee9d1");
}

// ============================================================================
// Incremental Hashing Tests
// ============================================================================

TEST(Sha256Test, IncrementalUpdate_Simple) {
    Sha256 hasher;
    hasher.update("abc");
    auto digest = hasher.finalize();
    EXPECT_EQ(to_hex(digest), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

TEST(Sha256Test, IncrementalUpdate_SplitData) {
    Sha256 hasher;
    hasher.update("ab");
    hasher.update("c");
    auto digest = hasher.finalize();
    EXPECT_EQ(to_hex(digest), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

TEST(Sha256Test, IncrementalUpdate_MultipleCalls) {
    Sha256 hasher;
    hasher.update("abcd");
    hasher.update("bcde");
    hasher.update("cdef");
    hasher.update("defg");
    hasher.update("efgh");
    hasher.update("fghi");
    hasher.update("ghij");
    hasher.update("hijk");
    hasher.update("ijkl");
    hasher.update("jklm");
    hasher.update("klmn");
    hasher.update("lmno");
    hasher.update("mnop");
    hasher.update("nopq");
    auto digest = hasher.finalize();
    EXPECT_EQ(to_hex(digest), "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}

TEST(Sha256Test, IncrementalUpdate_ByteByByte) {
    Sha256 hasher;
    std::string input = "abc";
    for (char c : input) {
        hasher.update(std::string_view(&c, 1));
    }
    auto digest = hasher.finalize();
    EXPECT_EQ(to_hex(digest), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

TEST(Sha256Test, IncrementalUpdate_BinaryData) {
    std::vector<std::uint8_t> data = {0x61, 0x62, 0x63};  // "abc"
    Sha256 hasher;
    hasher.update(std::span<const std::uint8_t>{data});
    auto digest = hasher.finalize();
    EXPECT_EQ(to_hex(digest), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

// ============================================================================
// Block Boundary Tests
// ============================================================================

/// Test exactly one block (64 bytes)
TEST(Sha256Test, ExactlyOneBlock) {
    std::string data(64, 'x');
    auto digest = Sha256::hash(data);
    // Verified with Python: hashlib.sha256(b'x' * 64).hexdigest()
    EXPECT_EQ(to_hex(digest), "7ce100971f64e7001e8fe5a51973ecdfe1ced42befe7ee8d5fd6219506b5393c");
}

/// Test exactly two blocks (128 bytes)
TEST(Sha256Test, ExactlyTwoBlocks) {
    std::string data(128, 'x');
    auto digest = Sha256::hash(data);
    // Verified with Python: hashlib.sha256(b'x' * 128).hexdigest()
    EXPECT_EQ(to_hex(digest), "24da1b81d0b16df6428eee73c69fcb2a93c76bc6df706f0c6670fe6bfe800464");
}

/// Test message requiring padding that spans blocks
TEST(Sha256Test, PaddingSpansBlock) {
    // 55 bytes of data + 1 byte padding + 8 bytes length = 64 bytes (one block)
    std::string data(55, 'a');
    auto digest = Sha256::hash(data);
    // Verified with Python: hashlib.sha256(b'a' * 55).hexdigest()
    EXPECT_EQ(to_hex(digest), "9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318");
}

/// Test message requiring padding in next block
TEST(Sha256Test, PaddingInNextBlock) {
    // 56 bytes of data requires padding in next block
    std::string data(56, 'a');
    auto digest = Sha256::hash(data);
    // Pre-computed expected value
    EXPECT_EQ(to_hex(digest), "b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a");
}

// ============================================================================
// Reuse Tests
// ============================================================================

TEST(Sha256Test, HasherReuseAfterFinalize) {
    Sha256 hasher;

    // First hash
    hasher.update("abc");
    auto digest1 = hasher.finalize();
    EXPECT_EQ(to_hex(digest1), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");

    // Second hash (hasher should be reset)
    hasher.update("abc");
    auto digest2 = hasher.finalize();
    EXPECT_EQ(to_hex(digest2), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");

    // Third hash with different input
    hasher.update("");
    auto digest3 = hasher.finalize();
    EXPECT_EQ(to_hex(digest3), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

// ============================================================================
// Hardware Acceleration Detection
// ============================================================================

TEST(Sha256Test, HardwareAccelerationQuery) {
    // Just verify the function doesn't crash
    bool has_hw = Sha256::has_hardware_acceleration();

    // Output for diagnostic purposes
    if (has_hw) {
        std::cout << "[   INFO   ] SHA256 hardware acceleration: AVAILABLE\n";
    } else {
        std::cout << "[   INFO   ] SHA256 hardware acceleration: NOT AVAILABLE (using scalar)\n";
    }
}

// ============================================================================
// Consistency Test (Scalar vs Hardware)
// ============================================================================

TEST(Sha256Test, ConsistencyAcrossMultipleRuns) {
    // Run the same hash multiple times to ensure deterministic behavior
    const std::string input = "The quick brown fox jumps over the lazy dog";

    auto digest1 = Sha256::hash(input);
    auto digest2 = Sha256::hash(input);
    auto digest3 = Sha256::hash(input);

    EXPECT_EQ(digest1, digest2);
    EXPECT_EQ(digest2, digest3);
    EXPECT_EQ(to_hex(digest1), "d7a8fbb307d7809469ca9abcb0082e4f8d5651e46d3cdb762d02d0bf37c9e592");
}

}  // namespace
}  // namespace dotvm::core::crypto
