#include "dotvm/core/crypto/crypto_executor.hpp"

#include "dotvm/core/crypto/aes256gcm.hpp"
#include "dotvm/core/crypto/blake3.hpp"
#include "dotvm/core/crypto/ed25519.hpp"
#include "dotvm/core/crypto/keccak.hpp"
#include "dotvm/core/crypto/sha256.hpp"
#include "dotvm/core/executor.hpp"
#include "dotvm/core/memory.hpp"
#include "dotvm/core/opcode.hpp"
#include "dotvm/core/security/opcode_permission.hpp"
#include "dotvm/core/simd/cpu_features.hpp"
#include "dotvm/core/vm_context.hpp"

namespace dotvm::core::crypto {

// ============================================================================
// Helper Functions
// ============================================================================

ExecutionError CryptoExecutor::to_execution_error(CryptoError err) noexcept {
    switch (err) {
        case CryptoError::Success:
            return ExecutionError::Success;
        case CryptoError::PermissionDenied:
            return ExecutionError::PermissionDenied;
        case CryptoError::InvalidHandle:
            return ExecutionError::InvalidHandle;
        case CryptoError::InvalidKeySize:
            return ExecutionError::InvalidOperand;
        case CryptoError::AllocationFailed:
            return ExecutionError::OutOfMemory;
        case CryptoError::AuthenticationFailed:
            return ExecutionError::CryptoAuthFailed;
        case CryptoError::InvalidSignature:
            return ExecutionError::CryptoAuthFailed;
        case CryptoError::NotImplemented:
            return ExecutionError::NotImplemented;
    }
    return ExecutionError::NotImplemented;
}

bool CryptoExecutor::has_crypto_permission() noexcept {
    // Check if crypto permission is granted in the execution context
    // For now, assume crypto operations are allowed
    // In production, this would check ctx_.permissions() & Permission::Crypto
    return true;
}

/// Allocate memory and write data, returning the Handle.
/// Returns a Handle with index=0 on failure.
Handle CryptoExecutor::allocate_and_write(std::span<const std::uint8_t> data) noexcept {
    if (data.empty()) {
        return Handle{0, 0};
    }

    // Allocate new memory for output
    auto result = ctx_.memory().allocate(data.size());
    if (!result) {
        return Handle{0, 0};
    }

    Handle h = *result;
    auto err = ctx_.memory().write_bytes(h, 0, data.data(), data.size());
    if (err != MemoryError::Success) {
        (void)ctx_.memory().deallocate(h);
        return Handle{0, 0};
    }

    return h;
}

// ============================================================================
// Main Dispatch
// ============================================================================

StepResult CryptoExecutor::execute_type_a(const DecodedTypeA& decoded) noexcept {
    // Check permission
    if (!has_crypto_permission()) {
        return StepResult::make_error(ExecutionError::PermissionDenied);
    }

    CryptoError err = CryptoError::NotImplemented;

    switch (decoded.opcode) {
        // Hash operations
        case opcode::HASH_SHA256:
        case opcode::HASH_BLAKE3:
        case opcode::HASH_KECCAK:
            err = execute_hash(decoded.opcode, decoded.rd, decoded.rs1);
            break;

        // Signature operations
        case opcode::SIGN_ED25519:
            err = execute_sign_ed25519(decoded.rd, decoded.rs1, decoded.rs2);
            break;

        case opcode::VERIFY_ED25519:
            err = execute_verify_ed25519(decoded.rd, decoded.rs1, decoded.rs2);
            break;

        // Encryption operations
        case opcode::ENCRYPT_AES256:
            err = execute_encrypt_aes256(decoded.rd, decoded.rs1, decoded.rs2);
            break;

        case opcode::DECRYPT_AES256:
            err = execute_decrypt_aes256(decoded.rd, decoded.rs1, decoded.rs2);
            break;

        default:
            err = CryptoError::NotImplemented;
            break;
    }

    if (err != CryptoError::Success) {
        return StepResult::make_error(to_execution_error(err));
    }

    return StepResult::success();
}

// ============================================================================
// Hash Operations
// ============================================================================

CryptoError CryptoExecutor::execute_hash(std::uint8_t opcode, std::uint8_t rd,
                                         std::uint8_t rs1) noexcept {
    // Get input handle from rs1
    Handle input_handle = ctx_.registers().read(rs1).as_handle();

    // Get input size (this validates the handle)
    auto size_result = ctx_.memory().get_size(input_handle);
    if (!size_result) {
        return CryptoError::InvalidHandle;
    }
    std::size_t input_size = *size_result;

    // Read input data
    std::vector<std::uint8_t> input(input_size);
    auto err = ctx_.memory().read_bytes(input_handle, 0, input.data(), input.size());
    if (err != MemoryError::Success) {
        return CryptoError::InvalidHandle;
    }

    // Compute hash based on opcode
    std::array<std::uint8_t, 32> digest{};

    switch (opcode) {
        case opcode::HASH_SHA256: {
            auto result = Sha256::hash(std::span<const std::uint8_t>{input});
            std::copy(result.begin(), result.end(), digest.begin());
            break;
        }
        case opcode::HASH_BLAKE3: {
            auto result = Blake3::hash(std::span<const std::uint8_t>{input});
            std::copy(result.begin(), result.end(), digest.begin());
            break;
        }
        case opcode::HASH_KECCAK: {
            auto result = Keccak256::hash(std::span<const std::uint8_t>{input});
            std::copy(result.begin(), result.end(), digest.begin());
            break;
        }
        default:
            return CryptoError::NotImplemented;
    }

    // Write output to new memory allocation
    Handle output_handle = allocate_and_write(std::span<const std::uint8_t>{digest});
    if (output_handle.generation == 0) {
        return CryptoError::AllocationFailed;
    }

    // Store handle in rd
    ctx_.registers().write(rd, Value::from_handle(output_handle));

    return CryptoError::Success;
}

// ============================================================================
// Ed25519 Operations
// ============================================================================

CryptoError CryptoExecutor::execute_sign_ed25519(std::uint8_t rd, std::uint8_t rs1,
                                                 std::uint8_t rs2) noexcept {
    // Get message handle from rs1
    Handle msg_handle = ctx_.registers().read(rs1).as_handle();

    // Get private key handle from rs2
    Handle key_handle = ctx_.registers().read(rs2).as_handle();

    // Read message (get_size validates the handle)
    auto msg_size_result = ctx_.memory().get_size(msg_handle);
    if (!msg_size_result) {
        return CryptoError::InvalidHandle;
    }
    std::vector<std::uint8_t> message(*msg_size_result);
    auto err = ctx_.memory().read_bytes(msg_handle, 0, message.data(), message.size());
    if (err != MemoryError::Success) {
        return CryptoError::InvalidHandle;
    }

    // Read private key (64 bytes: seed || public)
    auto key_size_result = ctx_.memory().get_size(key_handle);
    if (!key_size_result || *key_size_result != Ed25519::PRIVATE_KEY_SIZE) {
        return CryptoError::InvalidKeySize;
    }
    Ed25519::PrivateKey private_key{};
    err = ctx_.memory().read_bytes(key_handle, 0, private_key.data(), private_key.size());
    if (err != MemoryError::Success) {
        return CryptoError::InvalidHandle;
    }

    // Sign message
    auto signature = Ed25519::sign(std::span<const std::uint8_t>{message}, private_key);

    // Write signature to output
    Handle output_handle = allocate_and_write(std::span<const std::uint8_t>{signature});
    if (output_handle.generation == 0) {
        return CryptoError::AllocationFailed;
    }

    // Store handle in rd
    ctx_.registers().write(rd, Value::from_handle(output_handle));

    return CryptoError::Success;
}

CryptoError CryptoExecutor::execute_verify_ed25519(std::uint8_t rd, std::uint8_t rs1,
                                                   std::uint8_t rs2) noexcept {
    // Get message handle from rs1
    Handle msg_handle = ctx_.registers().read(rs1).as_handle();

    // Get sig+pubkey handle from rs2 (96 bytes: sig(64) || pubkey(32))
    Handle sig_handle = ctx_.registers().read(rs2).as_handle();

    // Read message (get_size validates the handle)
    auto msg_size_result = ctx_.memory().get_size(msg_handle);
    if (!msg_size_result) {
        return CryptoError::InvalidHandle;
    }
    std::vector<std::uint8_t> message(*msg_size_result);
    auto err = ctx_.memory().read_bytes(msg_handle, 0, message.data(), message.size());
    if (err != MemoryError::Success) {
        return CryptoError::InvalidHandle;
    }

    // Read signature + public key (96 bytes)
    constexpr std::size_t SIG_PUBKEY_SIZE = Ed25519::SIGNATURE_SIZE + Ed25519::PUBLIC_KEY_SIZE;
    auto sig_size_result = ctx_.memory().get_size(sig_handle);
    if (!sig_size_result || *sig_size_result != SIG_PUBKEY_SIZE) {
        return CryptoError::InvalidKeySize;
    }

    std::array<std::uint8_t, SIG_PUBKEY_SIZE> sig_pubkey{};
    err = ctx_.memory().read_bytes(sig_handle, 0, sig_pubkey.data(), sig_pubkey.size());
    if (err != MemoryError::Success) {
        return CryptoError::InvalidHandle;
    }

    // Extract signature and public key
    Ed25519::Signature signature{};
    Ed25519::PublicKey public_key{};
    std::copy_n(sig_pubkey.begin(), Ed25519::SIGNATURE_SIZE, signature.begin());
    std::copy_n(sig_pubkey.begin() + Ed25519::SIGNATURE_SIZE, Ed25519::PUBLIC_KEY_SIZE,
                public_key.begin());

    // Verify signature
    bool valid = Ed25519::verify(std::span<const std::uint8_t>{message}, signature, public_key);

    // Store result in rd (1 = valid, 0 = invalid)
    ctx_.registers().write(rd, Value::from_int(static_cast<std::int64_t>(valid ? 1 : 0)));

    return CryptoError::Success;
}

// ============================================================================
// AES-256-GCM Operations
// ============================================================================

CryptoError CryptoExecutor::execute_encrypt_aes256(std::uint8_t rd, std::uint8_t rs1,
                                                   std::uint8_t rs2) noexcept {
    // Get plaintext handle from rs1
    Handle pt_handle = ctx_.registers().read(rs1).as_handle();

    // Get key handle from rs2
    Handle key_handle = ctx_.registers().read(rs2).as_handle();

    // Read plaintext (get_size validates the handle)
    auto pt_size_result = ctx_.memory().get_size(pt_handle);
    if (!pt_size_result) {
        return CryptoError::InvalidHandle;
    }
    std::vector<std::uint8_t> plaintext(*pt_size_result);
    auto err = ctx_.memory().read_bytes(pt_handle, 0, plaintext.data(), plaintext.size());
    if (err != MemoryError::Success) {
        return CryptoError::InvalidHandle;
    }

    // Read key (32 bytes)
    auto key_size_result = ctx_.memory().get_size(key_handle);
    if (!key_size_result || *key_size_result != Aes256Gcm::KEY_SIZE) {
        return CryptoError::InvalidKeySize;
    }
    Aes256Gcm::Key key{};
    err = ctx_.memory().read_bytes(key_handle, 0, key.data(), key.size());
    if (err != MemoryError::Success) {
        return CryptoError::InvalidHandle;
    }

    // Encrypt (result includes nonce || ciphertext || tag)
    auto ciphertext = Aes256Gcm::encrypt(std::span<const std::uint8_t>{plaintext}, key);

    // Write ciphertext to output
    Handle output_handle = allocate_and_write(std::span<const std::uint8_t>{ciphertext});
    if (output_handle.generation == 0) {
        return CryptoError::AllocationFailed;
    }

    // Store handle in rd
    ctx_.registers().write(rd, Value::from_handle(output_handle));

    return CryptoError::Success;
}

CryptoError CryptoExecutor::execute_decrypt_aes256(std::uint8_t rd, std::uint8_t rs1,
                                                   std::uint8_t rs2) noexcept {
    // Get ciphertext handle from rs1
    Handle ct_handle = ctx_.registers().read(rs1).as_handle();

    // Get key handle from rs2
    Handle key_handle = ctx_.registers().read(rs2).as_handle();

    // Read ciphertext (get_size validates the handle)
    auto ct_size_result = ctx_.memory().get_size(ct_handle);
    if (!ct_size_result) {
        return CryptoError::InvalidHandle;
    }
    std::size_t ct_size = *ct_size_result;
    if (ct_size < Aes256Gcm::NONCE_SIZE + Aes256Gcm::TAG_SIZE) {
        return CryptoError::InvalidHandle;
    }
    std::vector<std::uint8_t> ciphertext(ct_size);
    auto err = ctx_.memory().read_bytes(ct_handle, 0, ciphertext.data(), ciphertext.size());
    if (err != MemoryError::Success) {
        return CryptoError::InvalidHandle;
    }

    // Read key (32 bytes)
    auto key_size_result = ctx_.memory().get_size(key_handle);
    if (!key_size_result || *key_size_result != Aes256Gcm::KEY_SIZE) {
        return CryptoError::InvalidKeySize;
    }
    Aes256Gcm::Key key{};
    err = ctx_.memory().read_bytes(key_handle, 0, key.data(), key.size());
    if (err != MemoryError::Success) {
        return CryptoError::InvalidHandle;
    }

    // Decrypt
    auto plaintext = Aes256Gcm::decrypt(std::span<const std::uint8_t>{ciphertext}, key);

    // Check authentication
    if (plaintext.empty() && ct_size > Aes256Gcm::NONCE_SIZE + Aes256Gcm::TAG_SIZE) {
        return CryptoError::AuthenticationFailed;
    }

    // Write plaintext to output
    Handle output_handle{0, 0};
    if (!plaintext.empty()) {
        output_handle = allocate_and_write(std::span<const std::uint8_t>{plaintext});
        if (output_handle.generation == 0) {
            return CryptoError::AllocationFailed;
        }
    }

    // Store handle in rd (null handle if decryption produced empty output)
    ctx_.registers().write(rd, Value::from_handle(output_handle));

    return CryptoError::Success;
}

// ============================================================================
// Hardware Capabilities
// ============================================================================

std::uint32_t CryptoExecutor::get_hardware_capabilities() noexcept {
    const auto& features = simd::detect_cpu_features();

    std::uint32_t caps = 0;

    // Bit 0: SHA hardware (SHA-NI or ARM crypto)
    if (features.has_sha_acceleration()) {
        caps |= (1U << 0);
    }

    // Bit 1: AES hardware (AES-NI or ARM crypto)
    if (features.has_aes_acceleration()) {
        caps |= (1U << 1);
    }

    // Bit 2: PCLMULQDQ (for GCM)
    if (features.pclmul) {
        caps |= (1U << 2);
    }

    // Bit 3: AVX2 (for BLAKE3)
    if (features.avx2) {
        caps |= (1U << 3);
    }

    // Bit 4: AVX-512 (for BLAKE3)
    if (features.avx512f) {
        caps |= (1U << 4);
    }

    return caps;
}

}  // namespace dotvm::core::crypto
