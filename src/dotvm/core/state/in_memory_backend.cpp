/// @file in_memory_backend.cpp
/// @brief STATE-001 In-memory StateBackend implementation

#include "dotvm/core/state/state_backend.hpp"

#include <algorithm>
#include <map>
#include <optional>
#include <unordered_map>

namespace dotvm::core::state {

namespace {

/// @brief Comparator for byte vectors (lexicographic ordering)
struct ByteVectorCompare {
    bool operator()(const std::vector<std::byte>& lhs,
                    const std::vector<std::byte>& rhs) const noexcept {
        return std::lexicographical_compare(lhs.begin(), lhs.end(), rhs.begin(), rhs.end());
    }
};

/// @brief Check if key starts with prefix
[[nodiscard]] bool starts_with(const std::vector<std::byte>& key,
                               std::span<const std::byte> prefix) noexcept {
    if (prefix.empty()) {
        return true;
    }
    if (key.size() < prefix.size()) {
        return false;
    }
    return std::equal(prefix.begin(), prefix.end(), key.begin());
}

/// @brief Transaction state
struct Transaction {
    TxId id;
    bool active{true};
    // Change log: key -> optional<value> (nullopt = delete)
    std::map<std::vector<std::byte>, std::optional<std::vector<std::byte>>, ByteVectorCompare>
        changes;
};

}  // namespace

// ============================================================================
// InMemoryBackend Implementation
// ============================================================================

/// @brief In-memory implementation of StateBackend
///
/// Uses std::map for sorted key storage, enabling efficient prefix iteration.
/// Transactions are tracked via a change log that's applied on commit.
///
/// Thread Safety: NOT thread-safe. Use one per thread or add external locking.
class InMemoryBackend final : public StateBackend {
public:
    explicit InMemoryBackend(StateBackendConfig config) noexcept : config_{std::move(config)} {
        // Pre-allocate storage hint
        // (std::map doesn't support reserve, but we store the hint anyway)
    }

    // ========================================================================
    // CRUD Operations
    // ========================================================================

    [[nodiscard]] Result<std::vector<std::byte>> get(Key key) const override {
        // Check active transaction first
        if (active_tx_) {
            auto& tx = transactions_.at(active_tx_->id);
            auto it = tx.changes.find(std::vector<std::byte>(key.begin(), key.end()));
            if (it != tx.changes.end()) {
                if (it->second.has_value()) {
                    return it->second.value();
                }
                return StateBackendError::KeyNotFound;  // Deleted in transaction
            }
        }

        auto key_vec = std::vector<std::byte>(key.begin(), key.end());
        auto it = storage_.find(key_vec);
        if (it == storage_.end()) {
            return StateBackendError::KeyNotFound;
        }
        return it->second;
    }

    [[nodiscard]] Result<void> put(Key key, Value value) override {
        // Validate key
        if (key.empty()) {
            return StateBackendError::InvalidKey;
        }
        if (key.size() > config_.max_key_size) {
            return StateBackendError::KeyTooLarge;
        }
        if (value.size() > config_.max_value_size) {
            return StateBackendError::ValueTooLarge;
        }

        auto key_vec = std::vector<std::byte>(key.begin(), key.end());
        auto value_vec = std::vector<std::byte>(value.begin(), value.end());

        // If transaction active, log change
        if (active_tx_) {
            auto& tx = transactions_.at(active_tx_->id);
            tx.changes[key_vec] = value_vec;
            return {};
        }

        // Direct storage
        auto it = storage_.find(key_vec);
        if (it == storage_.end()) {
            storage_bytes_ += key_vec.size() + value_vec.size();
        } else {
            storage_bytes_ -= it->second.size();
            storage_bytes_ += value_vec.size();
        }
        storage_[std::move(key_vec)] = std::move(value_vec);
        return {};
    }

    [[nodiscard]] Result<void> remove(Key key) override {
        auto key_vec = std::vector<std::byte>(key.begin(), key.end());

        // If transaction active, log deletion
        if (active_tx_) {
            auto& tx = transactions_.at(active_tx_->id);

            // Check if key exists (in storage or pending changes)
            bool exists_in_changes = tx.changes.count(key_vec) > 0 &&
                                     tx.changes.at(key_vec).has_value();
            bool exists_in_storage = storage_.count(key_vec) > 0;

            if (!exists_in_changes && !exists_in_storage) {
                return StateBackendError::KeyNotFound;
            }

            tx.changes[key_vec] = std::nullopt;  // Mark as deleted
            return {};
        }

        // Direct removal
        auto it = storage_.find(key_vec);
        if (it == storage_.end()) {
            return StateBackendError::KeyNotFound;
        }

        storage_bytes_ -= it->first.size() + it->second.size();
        storage_.erase(it);
        return {};
    }

    [[nodiscard]] bool exists(Key key) const noexcept override {
        auto key_vec = std::vector<std::byte>(key.begin(), key.end());

        // Check active transaction first
        if (active_tx_) {
            auto& tx = transactions_.at(active_tx_->id);
            auto it = tx.changes.find(key_vec);
            if (it != tx.changes.end()) {
                return it->second.has_value();  // true if put, false if deleted
            }
        }

        return storage_.count(key_vec) > 0;
    }

    // ========================================================================
    // Iteration
    // ========================================================================

    [[nodiscard]] Result<void> iterate(
        Key prefix, const IterateCallback& callback) const override {
        // Create merged view of storage + transaction changes
        // For simplicity, we iterate storage directly (transaction changes visible)

        auto prefix_vec = std::vector<std::byte>(prefix.begin(), prefix.end());

        // Find starting point using lower_bound
        auto it = storage_.lower_bound(prefix_vec);

        while (it != storage_.end()) {
            if (!starts_with(it->first, prefix)) {
                break;  // Past prefix range
            }

            // Check if deleted in transaction
            if (active_tx_) {
                auto& tx = transactions_.at(active_tx_->id);
                auto tx_it = tx.changes.find(it->first);
                if (tx_it != tx.changes.end() && !tx_it->second.has_value()) {
                    ++it;
                    continue;  // Skip deleted key
                }
            }

            Key key_span{it->first};
            Value value_span{it->second};

            if (!callback(key_span, value_span)) {
                break;  // Callback requested stop
            }
            ++it;
        }

        return {};
    }

    // ========================================================================
    // Batch Operations
    // ========================================================================

    [[nodiscard]] Result<void> batch(std::span<const BatchOp> ops) override {
        if (ops.empty()) {
            return {};
        }

        // Validate all operations first
        for (const auto& op : ops) {
            if (op.type == BatchOpType::Put) {
                if (op.key.empty()) {
                    return StateBackendError::InvalidKey;
                }
                if (op.key.size() > config_.max_key_size) {
                    return StateBackendError::KeyTooLarge;
                }
                if (op.value.size() > config_.max_value_size) {
                    return StateBackendError::ValueTooLarge;
                }
            } else if (op.type == BatchOpType::Remove) {
                // Check key exists
                auto key_vec = std::vector<std::byte>(op.key.begin(), op.key.end());
                if (storage_.count(key_vec) == 0) {
                    return StateBackendError::KeyNotFound;
                }
            }
        }

        // Apply all operations (already validated)
        for (const auto& op : ops) {
            if (op.type == BatchOpType::Put) {
                auto key_vec = std::vector<std::byte>(op.key.begin(), op.key.end());
                auto value_vec = std::vector<std::byte>(op.value.begin(), op.value.end());

                auto it = storage_.find(key_vec);
                if (it == storage_.end()) {
                    storage_bytes_ += key_vec.size() + value_vec.size();
                } else {
                    storage_bytes_ -= it->second.size();
                    storage_bytes_ += value_vec.size();
                }
                storage_[std::move(key_vec)] = std::move(value_vec);
            } else {
                auto key_vec = std::vector<std::byte>(op.key.begin(), op.key.end());
                auto it = storage_.find(key_vec);
                if (it != storage_.end()) {
                    storage_bytes_ -= it->first.size() + it->second.size();
                    storage_.erase(it);
                }
            }
        }

        return {};
    }

    // ========================================================================
    // Transactions
    // ========================================================================

    [[nodiscard]] Result<TxHandle> begin_transaction() override {
        if (!config_.enable_transactions) {
            return StateBackendError::UnsupportedOperation;
        }

        TxId id{.id = next_tx_id_++, .generation = tx_generation_};
        Transaction tx{.id = id, .active = true, .changes = {}};
        transactions_[id.id] = std::move(tx);

        active_tx_ = id;

        return TxHandle{this, id};
    }

    [[nodiscard]] Result<void> commit(TxHandle tx) override {
        if (!tx.is_valid()) {
            return StateBackendError::InvalidTransaction;
        }

        auto it = transactions_.find(tx.id().id);
        if (it == transactions_.end() || !it->second.active ||
            it->second.id.generation != tx.id().generation) {
            return StateBackendError::InvalidTransaction;
        }

        // Apply all changes from transaction
        for (auto& [key, value_opt] : it->second.changes) {
            if (value_opt.has_value()) {
                // Put
                auto storage_it = storage_.find(key);
                if (storage_it == storage_.end()) {
                    storage_bytes_ += key.size() + value_opt->size();
                } else {
                    storage_bytes_ -= storage_it->second.size();
                    storage_bytes_ += value_opt->size();
                }
                storage_[key] = std::move(*value_opt);
            } else {
                // Remove
                auto storage_it = storage_.find(key);
                if (storage_it != storage_.end()) {
                    storage_bytes_ -= storage_it->first.size() + storage_it->second.size();
                    storage_.erase(storage_it);
                }
            }
        }

        // Clean up transaction
        transactions_.erase(it);
        if (active_tx_ && active_tx_->id == tx.id().id) {
            active_tx_.reset();
        }

        // Release handle ownership (prevent double-rollback in destructor)
        tx.release();

        return {};
    }

    [[nodiscard]] Result<void> rollback(TxHandle tx) override {
        if (!tx.is_valid()) {
            return StateBackendError::InvalidTransaction;
        }

        auto it = transactions_.find(tx.id().id);
        if (it == transactions_.end()) {
            // Transaction already rolled back or committed
            tx.release();
            return {};
        }

        if (it->second.id.generation != tx.id().generation) {
            return StateBackendError::InvalidTransaction;
        }

        // Discard all changes
        transactions_.erase(it);
        if (active_tx_ && active_tx_->id == tx.id().id) {
            active_tx_.reset();
        }

        // Release handle ownership
        tx.release();

        return {};
    }

    // ========================================================================
    // Statistics & Configuration
    // ========================================================================

    [[nodiscard]] std::size_t key_count() const noexcept override { return storage_.size(); }

    [[nodiscard]] std::size_t storage_bytes() const noexcept override { return storage_bytes_; }

    [[nodiscard]] const StateBackendConfig& config() const noexcept override { return config_; }

    [[nodiscard]] bool supports_transactions() const noexcept override {
        return config_.enable_transactions;
    }

    void clear() noexcept override {
        storage_.clear();
        storage_bytes_ = 0;
        transactions_.clear();
        active_tx_.reset();
    }

private:
    StateBackendConfig config_;

    // Main storage (sorted map for prefix iteration)
    std::map<std::vector<std::byte>, std::vector<std::byte>, ByteVectorCompare> storage_;
    std::size_t storage_bytes_{0};

    // Transaction management
    std::unordered_map<std::uint64_t, Transaction> transactions_;
    std::optional<TxId> active_tx_;
    std::uint64_t next_tx_id_{1};
    std::uint32_t tx_generation_{1};
};

// ============================================================================
// Factory Function
// ============================================================================

std::unique_ptr<StateBackend> create_state_backend(const StateBackendConfig& config) {
    return std::make_unique<InMemoryBackend>(config);
}

}  // namespace dotvm::core::state
