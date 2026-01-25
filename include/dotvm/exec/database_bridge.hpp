/// @file database_bridge.hpp
/// @brief STATE-005 DatabaseBridge - High-level abstraction for VM state operations
///
/// DatabaseBridge provides:
/// - Namespace-prefixed keys for VM isolation
/// - Type-safe Value serialization/deserialization
/// - Handle-based memory operations for keys
/// - Statistics tracking (reads, writes, bytes)

#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "dotvm/core/memory.hpp"
#include "dotvm/core/result.hpp"
#include "dotvm/core/value.hpp"

namespace dotvm::core {
class VmContext;
}  // namespace dotvm::core

namespace dotvm::exec {

/// @brief Error codes for DatabaseBridge operations
enum class DatabaseBridgeError : std::uint8_t {
    Success = 0,           ///< Operation completed successfully
    StateNotEnabled = 1,   ///< State context not enabled
    InvalidKeyHandle = 2,  ///< Invalid key handle
    InvalidValueHandle = 3,  ///< Invalid value handle (reserved for future use)
    KeyNotFound = 4,       ///< Key not found in state store
    SerializationError = 5,  ///< Value serialization/deserialization failed
    AllocationFailed = 6,  ///< Memory allocation failed
    TransactionError = 7,  ///< Transaction operation failed
    NamespaceError = 8,    ///< Namespace construction failed
};

/// @brief Statistics for DatabaseBridge operations
struct DatabaseBridgeStats {
    std::uint64_t reads{0};         ///< Number of read operations
    std::uint64_t writes{0};        ///< Number of write operations
    std::uint64_t deletes{0};       ///< Number of delete operations
    std::uint64_t exists_checks{0}; ///< Number of existence checks
    std::uint64_t bytes_read{0};    ///< Total bytes read
    std::uint64_t bytes_written{0}; ///< Total bytes written

    constexpr bool operator==(const DatabaseBridgeStats&) const noexcept = default;
};

/// @brief Value serialization type tags (little-endian tagged binary)
namespace value_tag {
inline constexpr std::uint8_t NIL = 0x00;
inline constexpr std::uint8_t INTEGER = 0x01;
inline constexpr std::uint8_t FLOAT = 0x02;
inline constexpr std::uint8_t BOOL = 0x03;
inline constexpr std::uint8_t HANDLE = 0x04;
inline constexpr std::uint8_t POINTER = 0x05;
}  // namespace value_tag

/// @brief High-level database abstraction for VM state operations
///
/// DatabaseBridge wraps StateExecutionContext to provide:
/// - Namespace isolation: Keys are prefixed with 8-byte namespace_id
/// - Type-safe serialization: Values are serialized to tagged binary
/// - Handle-based keys: Keys are read from MemoryManager via Handle
/// - Statistics tracking: Counts reads/writes and bytes transferred
///
/// Key format: [namespace_id: 8 bytes LE][0x3A (':')][raw_key bytes]
///
/// Value serialization format (little-endian):
/// | Type Tag (1 byte) | Payload (0-8 bytes) |
///
/// @note This class requires VmContext to be enabled with a TransactionManager
class DatabaseBridge {
public:
    /// Result type for operations that can fail
    template <typename T>
    using Result = core::Result<T, DatabaseBridgeError>;

    /// @brief Construct a DatabaseBridge for a VmContext
    /// @param ctx The VM context (provides memory and state access)
    /// @param namespace_id Namespace for key isolation (default: 0)
    explicit DatabaseBridge(core::VmContext& ctx, std::uint64_t namespace_id = 0) noexcept;

    // Non-copyable, non-movable (references VmContext)
    DatabaseBridge(const DatabaseBridge&) = delete;
    DatabaseBridge& operator=(const DatabaseBridge&) = delete;
    DatabaseBridge(DatabaseBridge&&) = delete;
    DatabaseBridge& operator=(DatabaseBridge&&) = delete;

    // ========================================================================
    // Core Operations
    // ========================================================================

    /// @brief Read a value from state
    /// @param tx_handle Transaction handle (0 = no transaction)
    /// @param key_handle Handle to memory containing the key bytes
    /// @return The deserialized value, or error
    [[nodiscard]] Result<core::Value> read_key(std::uint64_t tx_handle,
                                                core::Handle key_handle) noexcept;

    /// @brief Write a value to state
    /// @param tx_handle Transaction handle (0 = no transaction)
    /// @param key_handle Handle to memory containing the key bytes
    /// @param value The value to write
    /// @return Success or error
    [[nodiscard]] Result<void> write_key(std::uint64_t tx_handle, core::Handle key_handle,
                                          core::Value value) noexcept;

    /// @brief Delete a key from state
    /// @param tx_handle Transaction handle (0 = no transaction)
    /// @param key_handle Handle to memory containing the key bytes
    /// @return Success or error
    [[nodiscard]] Result<void> delete_key(std::uint64_t tx_handle,
                                           core::Handle key_handle) noexcept;

    /// @brief Check if a key exists in state
    /// @param tx_handle Transaction handle (0 = no transaction)
    /// @param key_handle Handle to memory containing the key bytes
    /// @return true if exists, false if not, or error
    [[nodiscard]] Result<bool> key_exists(std::uint64_t tx_handle,
                                           core::Handle key_handle) noexcept;

    // ========================================================================
    // Statistics
    // ========================================================================

    /// @brief Get current statistics
    /// @return Const reference to statistics
    [[nodiscard]] const DatabaseBridgeStats& stats() const noexcept { return stats_; }

    /// @brief Reset all statistics to zero
    void reset_stats() noexcept { stats_ = DatabaseBridgeStats{}; }

    // ========================================================================
    // Namespace
    // ========================================================================

    /// @brief Get the namespace ID
    [[nodiscard]] std::uint64_t namespace_id() const noexcept { return namespace_id_; }

    /// @brief Set the namespace ID
    void set_namespace_id(std::uint64_t id) noexcept { namespace_id_ = id; }

    // ========================================================================
    // Serialization (public for testing)
    // ========================================================================

    /// @brief Serialize a Value to binary format
    /// @param value The value to serialize
    /// @return Binary representation (tag + payload)
    [[nodiscard]] static std::vector<std::byte> serialize_value(core::Value value) noexcept;

    /// @brief Deserialize a Value from binary format
    /// @param data The binary data
    /// @return The deserialized value, or error
    [[nodiscard]] static Result<core::Value> deserialize_value(
        std::span<const std::byte> data) noexcept;

private:
    /// @brief Build a namespaced key from raw key bytes
    /// @param raw_key The raw key bytes
    /// @return Namespaced key bytes
    [[nodiscard]] std::vector<std::byte> build_namespaced_key(
        std::span<const std::byte> raw_key) const noexcept;

    /// @brief Read key bytes from a memory handle
    /// @param key_handle The handle to read from
    /// @return Key bytes, or error
    [[nodiscard]] Result<std::vector<std::byte>> read_key_bytes(
        core::Handle key_handle) const noexcept;

    core::VmContext& ctx_;
    std::uint64_t namespace_id_;
    DatabaseBridgeStats stats_;
};

}  // namespace dotvm::exec
