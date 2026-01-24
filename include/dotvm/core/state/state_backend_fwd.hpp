#pragma once

/// @file state_backend_fwd.hpp
/// @brief Forward declarations for STATE-001 StateBackend
///
/// Use this header when you only need types for pointers/references
/// without pulling in the full interface.

#include <cstdint>
#include <memory>

namespace dotvm::core::state {

// Error types
enum class StateBackendError : std::uint8_t;
enum class TransactionIsolationLevel : std::uint8_t;
enum class BatchOpType : std::uint8_t;

// Config and ID types
struct StateBackendConfig;
struct TxId;
struct BatchOp;

// Handle types
class TxHandle;

// Main interface
class StateBackend;

// Factory function
[[nodiscard]] std::unique_ptr<StateBackend> create_state_backend(const StateBackendConfig& config);

}  // namespace dotvm::core::state
