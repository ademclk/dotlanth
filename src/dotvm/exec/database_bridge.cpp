/// @file database_bridge.cpp
/// @brief STATE-005 DatabaseBridge Implementation

#include "dotvm/exec/database_bridge.hpp"

#include <bit>
#include <cstring>

#include "dotvm/core/vm_context.hpp"
#include "dotvm/exec/state_execution_context.hpp"

namespace dotvm::exec {

DatabaseBridge::DatabaseBridge(core::VmContext& ctx, std::uint64_t namespace_id) noexcept
    : ctx_(ctx), namespace_id_(namespace_id), stats_{} {}

// ============================================================================
// Serialization
// ============================================================================

std::vector<std::byte> DatabaseBridge::serialize_value(core::Value value) noexcept {
    std::vector<std::byte> result;

    if (value.is_nil()) {
        result.resize(1);
        result[0] = std::byte{value_tag::NIL};
        return result;
    }

    if (value.is_integer()) {
        result.resize(9);
        result[0] = std::byte{value_tag::INTEGER};
        std::int64_t val = value.as_integer();
        std::memcpy(&result[1], &val, 8);
        return result;
    }

    if (value.is_float()) {
        result.resize(9);
        result[0] = std::byte{value_tag::FLOAT};
        double val = value.as_float();
        std::memcpy(&result[1], &val, 8);
        return result;
    }

    if (value.is_bool()) {
        result.resize(2);
        result[0] = std::byte{value_tag::BOOL};
        result[1] = std::byte{static_cast<std::uint8_t>(value.as_bool() ? 1 : 0)};
        return result;
    }

    if (value.is_handle()) {
        result.resize(9);
        result[0] = std::byte{value_tag::HANDLE};
        auto h = value.as_handle();
        std::memcpy(&result[1], &h.index, 4);
        std::memcpy(&result[5], &h.generation, 4);
        return result;
    }

    if (value.is_pointer()) {
        result.resize(9);
        result[0] = std::byte{value_tag::POINTER};
        auto ptr = value.as_pointer();
        std::uint64_t addr = reinterpret_cast<std::uint64_t>(ptr);
        std::memcpy(&result[1], &addr, 8);
        return result;
    }

    // Unknown type - return empty (should not happen)
    return result;
}

DatabaseBridge::Result<core::Value>
DatabaseBridge::deserialize_value(std::span<const std::byte> data) noexcept {
    if (data.empty()) {
        return DatabaseBridgeError::SerializationError;
    }

    auto tag = static_cast<std::uint8_t>(data[0]);

    switch (tag) {
        case value_tag::NIL: {
            return core::Value::nil();
        }

        case value_tag::INTEGER: {
            if (data.size() < 9) {
                return DatabaseBridgeError::SerializationError;
            }
            std::int64_t val = 0;
            std::memcpy(&val, &data[1], 8);
            return core::Value::from_int(val);
        }

        case value_tag::FLOAT: {
            if (data.size() < 9) {
                return DatabaseBridgeError::SerializationError;
            }
            double val = 0.0;
            std::memcpy(&val, &data[1], 8);
            return core::Value::from_float(val);
        }

        case value_tag::BOOL: {
            if (data.size() < 2) {
                return DatabaseBridgeError::SerializationError;
            }
            bool val = static_cast<std::uint8_t>(data[1]) != 0;
            return core::Value::from_bool(val);
        }

        case value_tag::HANDLE: {
            if (data.size() < 9) {
                return DatabaseBridgeError::SerializationError;
            }
            std::uint32_t index = 0, gen = 0;
            std::memcpy(&index, &data[1], 4);
            std::memcpy(&gen, &data[5], 4);
            return core::Value::from_handle({.index = index, .generation = gen});
        }

        case value_tag::POINTER: {
            if (data.size() < 9) {
                return DatabaseBridgeError::SerializationError;
            }
            std::uint64_t addr = 0;
            std::memcpy(&addr, &data[1], 8);
            // NOLINTNEXTLINE(performance-no-int-to-ptr)
            return core::Value{reinterpret_cast<void*>(addr)};
        }

        default:
            return DatabaseBridgeError::SerializationError;
    }
}

// ============================================================================
// Private Helpers
// ============================================================================

std::vector<std::byte>
DatabaseBridge::build_namespaced_key(std::span<const std::byte> raw_key) const noexcept {
    // Format: [namespace_id: 8 bytes LE][0x3A (':')][raw_key bytes]
    std::vector<std::byte> result;
    result.resize(8 + 1 + raw_key.size());

    // Write namespace_id in little-endian
    std::memcpy(&result[0], &namespace_id_, 8);

    // Write separator ':'
    result[8] = std::byte{0x3A};

    // Copy raw key bytes
    if (!raw_key.empty()) {
        std::memcpy(&result[9], raw_key.data(), raw_key.size());
    }

    return result;
}

DatabaseBridge::Result<std::vector<std::byte>>
DatabaseBridge::read_key_bytes(core::Handle key_handle) const noexcept {
    // Validate handle
    if (!ctx_.memory().is_valid(key_handle)) {
        return DatabaseBridgeError::InvalidKeyHandle;
    }

    // Get the logical size of the allocation
    auto size_result = ctx_.memory().get_size(key_handle);
    if (!size_result) {
        return DatabaseBridgeError::InvalidKeyHandle;
    }

    std::size_t size = size_result.value();
    std::vector<std::byte> key_bytes(size);

    if (size > 0) {
        auto err = ctx_.memory().read_bytes(key_handle, 0, key_bytes.data(), size);
        if (err != core::MemoryError::Success) {
            return DatabaseBridgeError::InvalidKeyHandle;
        }
    }

    return key_bytes;
}

// ============================================================================
// Error Conversion Helper
// ============================================================================

namespace {

DatabaseBridgeError convert_state_error(StateExecError err) noexcept {
    switch (err) {
        case StateExecError::Success:
            return DatabaseBridgeError::Success;
        case StateExecError::NotEnabled:
            return DatabaseBridgeError::StateNotEnabled;
        case StateExecError::InvalidHandle:
            return DatabaseBridgeError::TransactionError;
        case StateExecError::KeyNotFound:
            return DatabaseBridgeError::KeyNotFound;
        case StateExecError::TransactionConflict:
            return DatabaseBridgeError::TransactionError;
        case StateExecError::BackendError:
            return DatabaseBridgeError::TransactionError;
    }
    return DatabaseBridgeError::TransactionError;
}

}  // namespace

// ============================================================================
// Core Operations
// ============================================================================

DatabaseBridge::Result<core::Value> DatabaseBridge::read_key(std::uint64_t tx_handle,
                                                             core::Handle key_handle) noexcept {
    // Check if state is enabled
    if (!ctx_.state_enabled()) {
        return DatabaseBridgeError::StateNotEnabled;
    }

    // Read key bytes from memory
    auto key_bytes_result = read_key_bytes(key_handle);
    if (!key_bytes_result) {
        return key_bytes_result.error();
    }
    auto raw_key = std::move(key_bytes_result.value());

    // Build namespaced key
    auto namespaced_key = build_namespaced_key(raw_key);

    // Read from state backend
    std::vector<std::byte> value_bytes;
    auto err = ctx_.state_context().get(tx_handle, namespaced_key, value_bytes);
    if (err != StateExecError::Success) {
        return convert_state_error(err);
    }

    // Update stats
    ++stats_.reads;
    stats_.bytes_read += value_bytes.size();

    // Deserialize value
    return deserialize_value(value_bytes);
}

DatabaseBridge::Result<void> DatabaseBridge::write_key(std::uint64_t tx_handle,
                                                       core::Handle key_handle,
                                                       core::Value value) noexcept {
    // Check if state is enabled
    if (!ctx_.state_enabled()) {
        return DatabaseBridgeError::StateNotEnabled;
    }

    // Read key bytes from memory
    auto key_bytes_result = read_key_bytes(key_handle);
    if (!key_bytes_result) {
        return key_bytes_result.error();
    }
    auto raw_key = std::move(key_bytes_result.value());

    // Build namespaced key
    auto namespaced_key = build_namespaced_key(raw_key);

    // Serialize value
    auto value_bytes = serialize_value(value);

    // Write to state backend
    auto err = ctx_.state_context().put(tx_handle, namespaced_key, value_bytes);
    if (err != StateExecError::Success) {
        return convert_state_error(err);
    }

    // Update stats
    ++stats_.writes;
    stats_.bytes_written += value_bytes.size();

    return core::Result<void, DatabaseBridgeError>{};
}

DatabaseBridge::Result<void> DatabaseBridge::delete_key(std::uint64_t tx_handle,
                                                        core::Handle key_handle) noexcept {
    // Check if state is enabled
    if (!ctx_.state_enabled()) {
        return DatabaseBridgeError::StateNotEnabled;
    }

    // Read key bytes from memory
    auto key_bytes_result = read_key_bytes(key_handle);
    if (!key_bytes_result) {
        return key_bytes_result.error();
    }
    auto raw_key = std::move(key_bytes_result.value());

    // Build namespaced key
    auto namespaced_key = build_namespaced_key(raw_key);

    // Remove from state backend
    auto err = ctx_.state_context().remove(tx_handle, namespaced_key);
    if (err != StateExecError::Success) {
        return convert_state_error(err);
    }

    // Update stats
    ++stats_.deletes;

    return core::Result<void, DatabaseBridgeError>{};
}

DatabaseBridge::Result<bool> DatabaseBridge::key_exists(std::uint64_t tx_handle,
                                                        core::Handle key_handle) noexcept {
    // Check if state is enabled
    if (!ctx_.state_enabled()) {
        return DatabaseBridgeError::StateNotEnabled;
    }

    // Read key bytes from memory
    auto key_bytes_result = read_key_bytes(key_handle);
    if (!key_bytes_result) {
        return key_bytes_result.error();
    }
    auto raw_key = std::move(key_bytes_result.value());

    // Build namespaced key
    auto namespaced_key = build_namespaced_key(raw_key);

    // Check existence
    bool exists = false;
    auto err = ctx_.state_context().exists(tx_handle, namespaced_key, exists);
    if (err != StateExecError::Success) {
        return convert_state_error(err);
    }

    // Update stats
    ++stats_.exists_checks;

    return exists;
}

}  // namespace dotvm::exec
