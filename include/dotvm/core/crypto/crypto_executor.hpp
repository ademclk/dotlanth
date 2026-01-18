#pragma once

/// @file crypto_executor.hpp
/// @brief Cryptographic opcode executor for DotVM
///
/// This header provides the CryptoExecutor class which handles execution
/// of cryptographic opcodes (0xB0-0xBF) for SEC-008.
///
/// Supported operations:
/// - Hash: SHA-256, BLAKE3, Keccak-256
/// - Signature: Ed25519 sign/verify
/// - Encryption: AES-256-GCM encrypt/decrypt

#include <cstdint>
#include <span>

#include "../execution_error.hpp"
#include "../instruction.hpp"
#include "../opcode.hpp"

namespace dotvm::core {

// Forward declarations
class VmContext;
struct StepResult;
struct Handle;

}  // namespace dotvm::core

namespace dotvm::core::crypto {

/// Result of a crypto operation
enum class CryptoError : std::uint8_t {
    Success = 0,
    PermissionDenied,
    InvalidHandle,
    InvalidKeySize,
    AllocationFailed,
    AuthenticationFailed,
    InvalidSignature,
    NotImplemented
};

/// Executes cryptographic opcodes (0xB0-0xBF)
///
/// This class handles SEC-008 cryptographic operations including:
/// - Hash algorithms: SHA-256, BLAKE3, Keccak-256
/// - Digital signatures: Ed25519 sign/verify
/// - Authenticated encryption: AES-256-GCM
///
/// All operations require Permission::Crypto to be granted.
///
/// @example
/// ```cpp
/// CryptoExecutor crypto{ctx};
/// auto result = crypto.execute_type_a(decoded);
/// ```
class CryptoExecutor {
public:
    /// Construct with reference to VM context
    explicit CryptoExecutor(VmContext& ctx) noexcept : ctx_{ctx} {}

    /// Execute a Type A crypto instruction
    ///
    /// @param decoded Decoded Type A instruction
    /// @return StepResult indicating success or error
    [[nodiscard]] StepResult execute_type_a(const DecodedTypeA& decoded) noexcept;

    /// Execute hash operation (SHA-256, BLAKE3, Keccak-256)
    /// @param opcode Hash opcode (0xB0, 0xB1, or 0xB2)
    /// @param rd Destination register (output handle)
    /// @param rs1 Source register (input handle)
    /// @return CryptoError code
    [[nodiscard]] CryptoError execute_hash(std::uint8_t opcode, std::uint8_t rd,
                                           std::uint8_t rs1) noexcept;

    /// Execute Ed25519 sign operation
    /// @param rd Destination register (signature handle)
    /// @param rs1 Message handle
    /// @param rs2 Private key handle
    /// @return CryptoError code
    [[nodiscard]] CryptoError execute_sign_ed25519(std::uint8_t rd, std::uint8_t rs1,
                                                   std::uint8_t rs2) noexcept;

    /// Execute Ed25519 verify operation
    /// @param rd Destination register (1 if valid, 0 if invalid)
    /// @param rs1 Message handle
    /// @param rs2 Signature+public key handle
    /// @return CryptoError code
    [[nodiscard]] CryptoError execute_verify_ed25519(std::uint8_t rd, std::uint8_t rs1,
                                                     std::uint8_t rs2) noexcept;

    /// Execute AES-256-GCM encrypt operation
    /// @param rd Destination register (ciphertext handle)
    /// @param rs1 Plaintext handle
    /// @param rs2 Key handle
    /// @return CryptoError code
    [[nodiscard]] CryptoError execute_encrypt_aes256(std::uint8_t rd, std::uint8_t rs1,
                                                     std::uint8_t rs2) noexcept;

    /// Execute AES-256-GCM decrypt operation
    /// @param rd Destination register (plaintext handle)
    /// @param rs1 Ciphertext handle
    /// @param rs2 Key handle
    /// @return CryptoError code
    [[nodiscard]] CryptoError execute_decrypt_aes256(std::uint8_t rd, std::uint8_t rs1,
                                                     std::uint8_t rs2) noexcept;

    /// Check if CPU has crypto hardware acceleration
    /// @return Bitmask of supported hardware features
    [[nodiscard]] static std::uint32_t get_hardware_capabilities() noexcept;

private:
    VmContext& ctx_;

    /// Allocate memory and write data
    /// @param data Data to write
    /// @return Handle to allocated memory, or Handle{0,0} on failure
    [[nodiscard]] Handle allocate_and_write(std::span<const std::uint8_t> data) noexcept;

    /// Check if crypto permission is granted
    [[nodiscard]] bool has_crypto_permission() noexcept;

    /// Convert CryptoError to ExecutionError
    [[nodiscard]] static ExecutionError to_execution_error(CryptoError err) noexcept;
};

}  // namespace dotvm::core::crypto
