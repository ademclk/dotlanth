#pragma once

/// @file ed25519.hpp
/// @brief Ed25519 digital signature implementation
///
/// Provides Ed25519 signing and verification:
/// - RFC 8032 compliant implementation
/// - Constant-time field operations for security
/// - Self-contained (includes minimal SHA-512)
///
/// Part of SEC-008: Cryptographic Operations for the dotlanth VM

#include <array>
#include <cstdint>
#include <span>

namespace dotvm::core::crypto {

/// Ed25519 digital signature implementation
///
/// Implements RFC 8032 Ed25519 signatures with pure C++ for security-critical
/// applications. All operations are constant-time to prevent timing attacks.
///
/// @example
/// ```cpp
/// // Generate key pair from seed
/// Ed25519::Seed seed = {...};  // 32 random bytes
/// auto [public_key, private_key] = Ed25519::generate_keypair(seed);
///
/// // Sign a message
/// auto signature = Ed25519::sign(message, private_key);
///
/// // Verify signature
/// bool valid = Ed25519::verify(message, signature, public_key);
/// ```
class Ed25519 {
public:
    static constexpr std::size_t SEED_SIZE = 32;
    static constexpr std::size_t PUBLIC_KEY_SIZE = 32;
    static constexpr std::size_t PRIVATE_KEY_SIZE = 64;  // seed || public_key
    static constexpr std::size_t SIGNATURE_SIZE = 64;

    using Seed = std::array<std::uint8_t, SEED_SIZE>;
    using PublicKey = std::array<std::uint8_t, PUBLIC_KEY_SIZE>;
    using PrivateKey = std::array<std::uint8_t, PRIVATE_KEY_SIZE>;
    using Signature = std::array<std::uint8_t, SIGNATURE_SIZE>;

    /// Generate key pair from a 32-byte seed
    /// @param seed 32 random bytes (should be from secure RNG)
    /// @return Pair of (public_key, private_key)
    [[nodiscard]] static std::pair<PublicKey, PrivateKey>
    generate_keypair(const Seed& seed) noexcept;

    /// Sign a message using Ed25519
    /// @param message Message to sign
    /// @param private_key 64-byte private key (seed || public)
    /// @return 64-byte signature
    [[nodiscard]] static Signature sign(std::span<const std::uint8_t> message,
                                        const PrivateKey& private_key) noexcept;

    /// Verify an Ed25519 signature
    /// @param message Original message
    /// @param signature 64-byte signature
    /// @param public_key 32-byte public key
    /// @return true if signature is valid
    [[nodiscard]] static bool verify(std::span<const std::uint8_t> message,
                                     const Signature& signature,
                                     const PublicKey& public_key) noexcept;

    /// Extract public key from private key
    /// @param private_key 64-byte private key
    /// @return 32-byte public key (last 32 bytes of private key)
    [[nodiscard]] static PublicKey get_public_key(const PrivateKey& private_key) noexcept;

    /// Check if hardware acceleration is available
    /// @return Always false (no hardware Ed25519 support)
    [[nodiscard]] static bool has_hardware_acceleration() noexcept { return false; }

private:
    // Field element: 256-bit integer represented as 10 limbs
    // Using radix 2^25.5 representation for efficient multiplication
    using FieldElement = std::array<std::int64_t, 10>;

    // Extended coordinates point (X, Y, Z, T) where x = X/Z, y = Y/Z, xy = T/Z
    struct ExtendedPoint {
        FieldElement X, Y, Z, T;
    };

    // Precomputed point for fixed-base multiplication
    struct PrecomputedPoint {
        FieldElement yplusx, yminusx, xy2d;
    };

    // SHA-512 for internal use (Ed25519 requires SHA-512)
    struct Sha512 {
        static constexpr std::size_t DIGEST_SIZE = 64;
        using Digest = std::array<std::uint8_t, DIGEST_SIZE>;

        std::array<std::uint64_t, 8> state;
        std::array<std::uint8_t, 128> buffer;
        std::size_t buffer_len = 0;
        std::uint64_t total_len = 0;

        Sha512() noexcept;
        void update(std::span<const std::uint8_t> data) noexcept;
        Digest finalize() noexcept;

        static Digest hash(std::span<const std::uint8_t> data) noexcept;
    };

    // Field operations
    static void fe_0(FieldElement& h) noexcept;
    static void fe_1(FieldElement& h) noexcept;
    static void fe_copy(FieldElement& h, const FieldElement& f) noexcept;
    static void fe_add(FieldElement& h, const FieldElement& f, const FieldElement& g) noexcept;
    static void fe_sub(FieldElement& h, const FieldElement& f, const FieldElement& g) noexcept;
    static void fe_neg(FieldElement& h, const FieldElement& f) noexcept;
    static void fe_mul(FieldElement& h, const FieldElement& f, const FieldElement& g) noexcept;
    static void fe_sq(FieldElement& h, const FieldElement& f) noexcept;
    static void fe_invert(FieldElement& out, const FieldElement& z) noexcept;
    static void fe_pow22523(FieldElement& out, const FieldElement& z) noexcept;
    static bool fe_isnonzero(const FieldElement& f) noexcept;
    static bool fe_isnegative(const FieldElement& f) noexcept;
    static void fe_reduce(FieldElement& h) noexcept;
    static void fe_frombytes(FieldElement& h, const std::uint8_t* s) noexcept;
    static void fe_tobytes(std::uint8_t* s, const FieldElement& h) noexcept;
    static void fe_cmov(FieldElement& f, const FieldElement& g, std::uint32_t b) noexcept;

    // Group operations
    static void ge_p3_0(ExtendedPoint& h) noexcept;
    static void ge_p3_tobytes(std::uint8_t* s, const ExtendedPoint& h) noexcept;
    static bool ge_frombytes_vartime(ExtendedPoint& h, const std::uint8_t* s) noexcept;
    static void ge_double_scalarmult_vartime(ExtendedPoint& r, const std::uint8_t* a,
                                             const ExtendedPoint& A,
                                             const std::uint8_t* b) noexcept;
    static void ge_scalarmult_base(ExtendedPoint& h, const std::uint8_t* a) noexcept;
    static void ge_add(ExtendedPoint& r, const ExtendedPoint& p, const ExtendedPoint& q) noexcept;
    static void ge_sub(ExtendedPoint& r, const ExtendedPoint& p, const ExtendedPoint& q) noexcept;
    static void ge_madd(ExtendedPoint& r, const ExtendedPoint& p,
                        const PrecomputedPoint& q) noexcept;
    static void ge_msub(ExtendedPoint& r, const ExtendedPoint& p,
                        const PrecomputedPoint& q) noexcept;
    static void ge_p1p1_to_p3(ExtendedPoint& r, const ExtendedPoint& p) noexcept;
    static void ge_p3_dbl(ExtendedPoint& r, const ExtendedPoint& p) noexcept;

    // Scalar operations (mod L where L is the group order)
    static void sc_reduce(std::uint8_t* s) noexcept;
    static void sc_muladd(std::uint8_t* s, const std::uint8_t* a, const std::uint8_t* b,
                          const std::uint8_t* c) noexcept;

    // Base point table for fast fixed-base multiplication
    static const PrecomputedPoint BASE_TABLE[32][8];

    // Curve constant d = -121665/121666
    static const FieldElement D;
    static const FieldElement D2;      // 2*d
    static const FieldElement SQRTM1;  // sqrt(-1) mod p
};

}  // namespace dotvm::core::crypto
