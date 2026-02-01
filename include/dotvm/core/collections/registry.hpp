/// @file registry.hpp
/// @brief Collection registry for handle management in the DotVM
///
/// Provides unified handle management for all collection types (List, Map, Set).

#pragma once

#include <cstdint>
#include <expected>
#include <memory>
#include <variant>
#include <vector>

#include "dotvm/core/memory_config.hpp"
#include "dotvm/core/value.hpp"
#include "list.hpp"
#include "map.hpp"
#include "set.hpp"

namespace dotvm::core::collections {

// ============================================================================
// Collection Handle
// ============================================================================

/// @brief Type tag for collections
enum class CollectionType : std::uint8_t {
    None = 0,  ///< Invalid/uninitialized
    List = 1,  ///< DotList
    Map = 2,   ///< DotMap
    Set = 3,   ///< DotSet
};

/// @brief Handle to a collection in the registry
struct CollectionHandle {
    std::uint32_t index;       ///< Index into registry
    std::uint32_t generation;  ///< Generation counter
    CollectionType type;       ///< Collection type

    /// Check if this is a null handle
    [[nodiscard]] constexpr bool is_null() const noexcept {
        return index == mem_config::INVALID_INDEX && type == CollectionType::None;
    }

    constexpr bool operator==(const CollectionHandle&) const noexcept = default;

    /// Create a null handle
    [[nodiscard]] static constexpr CollectionHandle null() noexcept {
        return CollectionHandle{
            .index = mem_config::INVALID_INDEX, .generation = 0, .type = CollectionType::None};
    }
};

// ============================================================================
// Collection Error
// ============================================================================

/// @brief Error codes for collection operations
enum class CollectionError : std::uint8_t {
    Success = 0,      ///< Operation succeeded
    InvalidHandle,    ///< Handle not valid
    TypeMismatch,     ///< Wrong collection type
    RegistryFull,     ///< Cannot allocate more collections
    OutOfBounds,      ///< Index out of bounds
    KeyNotFound,      ///< Map key not found
    AllocationFailed  ///< Memory allocation failed
};

// ============================================================================
// Collection Entry
// ============================================================================

/// @brief Storage for any collection type
using CollectionVariant = std::variant<std::monostate, DotList, DotMap, DotSet>;

/// @brief Entry in the collection registry
struct CollectionEntry {
    CollectionVariant data;    ///< The collection
    std::uint32_t generation;  ///< Generation counter
    bool is_active;            ///< Whether entry is in use

    /// Get the collection type
    [[nodiscard]] CollectionType type() const noexcept {
        if (std::holds_alternative<DotList>(data))
            return CollectionType::List;
        if (std::holds_alternative<DotMap>(data))
            return CollectionType::Map;
        if (std::holds_alternative<DotSet>(data))
            return CollectionType::Set;
        return CollectionType::None;
    }

    /// Create an inactive entry
    [[nodiscard]] static CollectionEntry inactive(std::uint32_t gen) noexcept {
        return CollectionEntry{.data = std::monostate{}, .generation = gen, .is_active = false};
    }
};

// ============================================================================
// Collection Registry
// ============================================================================

/// @brief Registry managing collection handles and storage
///
/// Provides handle-based access to collections with generation counting
/// for use-after-free protection.
class CollectionRegistry {
public:
    /// Result type for operations that can fail
    template <typename T>
    using Result = std::expected<T, CollectionError>;

    /// @brief Configuration
    struct Config {
        std::size_t initial_capacity = 256;
        std::size_t max_collections = 100'000;
    };

    /// Construct with default configuration
    CollectionRegistry() noexcept : CollectionRegistry(Config{}) {}

    /// Construct with custom configuration
    explicit CollectionRegistry(Config config) noexcept : config_(config) {
        entries_.reserve(config_.initial_capacity);
        free_list_.reserve(config_.initial_capacity);
    }

    // Non-copyable, movable
    CollectionRegistry(const CollectionRegistry&) = delete;
    CollectionRegistry& operator=(const CollectionRegistry&) = delete;
    CollectionRegistry(CollectionRegistry&&) noexcept = default;
    CollectionRegistry& operator=(CollectionRegistry&&) noexcept = default;

    ~CollectionRegistry() = default;

    // =========================================================================
    // Creation
    // =========================================================================

    /// Create a new list
    [[nodiscard]] Result<CollectionHandle> create_list() noexcept {
        return create_collection(DotList{});
    }

    /// Create a new map
    [[nodiscard]] Result<CollectionHandle> create_map() noexcept {
        return create_collection(DotMap{});
    }

    /// Create a new set
    [[nodiscard]] Result<CollectionHandle> create_set() noexcept {
        return create_collection(DotSet{});
    }

    // =========================================================================
    // Access
    // =========================================================================

    /// Get a list by handle
    [[nodiscard]] Result<DotList*> get_list(CollectionHandle handle) noexcept {
        auto* entry = get_entry(handle);
        if (entry == nullptr) {
            return std::unexpected{CollectionError::InvalidHandle};
        }
        if (auto* list = std::get_if<DotList>(&entry->data)) {
            return list;
        }
        return std::unexpected{CollectionError::TypeMismatch};
    }

    /// Get a const list by handle
    [[nodiscard]] Result<const DotList*> get_list(CollectionHandle handle) const noexcept {
        auto* entry = get_entry(handle);
        if (entry == nullptr) {
            return std::unexpected{CollectionError::InvalidHandle};
        }
        if (auto* list = std::get_if<DotList>(&entry->data)) {
            return list;
        }
        return std::unexpected{CollectionError::TypeMismatch};
    }

    /// Get a map by handle
    [[nodiscard]] Result<DotMap*> get_map(CollectionHandle handle) noexcept {
        auto* entry = get_entry(handle);
        if (entry == nullptr) {
            return std::unexpected{CollectionError::InvalidHandle};
        }
        if (auto* map = std::get_if<DotMap>(&entry->data)) {
            return map;
        }
        return std::unexpected{CollectionError::TypeMismatch};
    }

    /// Get a const map by handle
    [[nodiscard]] Result<const DotMap*> get_map(CollectionHandle handle) const noexcept {
        auto* entry = get_entry(handle);
        if (entry == nullptr) {
            return std::unexpected{CollectionError::InvalidHandle};
        }
        if (auto* map = std::get_if<DotMap>(&entry->data)) {
            return map;
        }
        return std::unexpected{CollectionError::TypeMismatch};
    }

    /// Get a set by handle
    [[nodiscard]] Result<DotSet*> get_set(CollectionHandle handle) noexcept {
        auto* entry = get_entry(handle);
        if (entry == nullptr) {
            return std::unexpected{CollectionError::InvalidHandle};
        }
        if (auto* set = std::get_if<DotSet>(&entry->data)) {
            return set;
        }
        return std::unexpected{CollectionError::TypeMismatch};
    }

    /// Get a const set by handle
    [[nodiscard]] Result<const DotSet*> get_set(CollectionHandle handle) const noexcept {
        auto* entry = get_entry(handle);
        if (entry == nullptr) {
            return std::unexpected{CollectionError::InvalidHandle};
        }
        if (auto* set = std::get_if<DotSet>(&entry->data)) {
            return set;
        }
        return std::unexpected{CollectionError::TypeMismatch};
    }

    /// Check if handle is valid
    [[nodiscard]] bool is_valid(CollectionHandle handle) const noexcept {
        return get_entry(handle) != nullptr;
    }

    /// Get the type of a collection
    [[nodiscard]] CollectionType get_type(CollectionHandle handle) const noexcept {
        auto* entry = get_entry(handle);
        if (entry == nullptr) {
            return CollectionType::None;
        }
        return entry->type();
    }

    // =========================================================================
    // Lifecycle
    // =========================================================================

    /// Release a collection handle
    [[nodiscard]] CollectionError release(CollectionHandle handle) noexcept {
        if (handle.is_null()) {
            return CollectionError::Success;
        }
        if (handle.index >= entries_.size()) {
            return CollectionError::InvalidHandle;
        }

        auto& entry = entries_[handle.index];
        if (!entry.is_active || entry.generation != handle.generation) {
            return CollectionError::InvalidHandle;
        }

        entry.data = std::monostate{};
        entry.is_active = false;

        // Increment generation
        if (entry.generation < mem_config::MAX_GENERATION) {
            entry.generation++;
        } else {
            entry.generation = mem_config::INITIAL_GENERATION;
        }

        free_list_.push_back(handle.index);
        return CollectionError::Success;
    }

    /// Clear all collections
    void clear() noexcept {
        entries_.clear();
        free_list_.clear();
    }

    // =========================================================================
    // Statistics
    // =========================================================================

    /// Get number of active collections
    [[nodiscard]] std::size_t active_count() const noexcept {
        std::size_t count = 0;
        for (const auto& e : entries_) {
            if (e.is_active)
                count++;
        }
        return count;
    }

    /// Get total capacity
    [[nodiscard]] std::size_t capacity() const noexcept { return entries_.capacity(); }

private:
    /// Get entry by handle, returns nullptr if invalid
    [[nodiscard]] CollectionEntry* get_entry(CollectionHandle handle) noexcept {
        if (handle.is_null() || handle.index >= entries_.size()) {
            return nullptr;
        }
        auto& entry = entries_[handle.index];
        if (!entry.is_active || entry.generation != handle.generation) {
            return nullptr;
        }
        return &entry;
    }

    /// Get const entry by handle
    [[nodiscard]] const CollectionEntry* get_entry(CollectionHandle handle) const noexcept {
        if (handle.is_null() || handle.index >= entries_.size()) {
            return nullptr;
        }
        const auto& entry = entries_[handle.index];
        if (!entry.is_active || entry.generation != handle.generation) {
            return nullptr;
        }
        return &entry;
    }

    /// Create a collection of any type
    template <typename T>
    [[nodiscard]] Result<CollectionHandle> create_collection(T&& coll) noexcept {
        // Allocate slot
        std::uint32_t index;
        if (!free_list_.empty()) {
            index = free_list_.back();
            free_list_.pop_back();
        } else {
            if (entries_.size() >= config_.max_collections) {
                return std::unexpected{CollectionError::RegistryFull};
            }
            index = static_cast<std::uint32_t>(entries_.size());
            entries_.push_back(CollectionEntry::inactive(mem_config::INITIAL_GENERATION));
        }

        auto& entry = entries_[index];
        entry.data = std::forward<T>(coll);
        entry.is_active = true;

        return CollectionHandle{
            .index = index, .generation = entry.generation, .type = entry.type()};
    }

    Config config_;
    std::vector<CollectionEntry> entries_;
    std::vector<std::uint32_t> free_list_;
};

// ============================================================================
// Value Integration
// ============================================================================

/// @brief Collection tag for Value encoding
/// Uses bit 31 (high bit of index) as collection marker, type in bits 28-30
namespace collection_tag {
/// Bit 31 of the 32-bit index field marks this as a collection handle
inline constexpr std::uint32_t COLLECTION_BIT = 0x8000'0000U;
/// Bits 28-30 encode the collection type (0-7)
inline constexpr std::uint32_t TYPE_MASK = 0x7000'0000U;
inline constexpr std::uint32_t TYPE_SHIFT = 28;
/// Bits 0-27 hold the actual index (up to 256M collections)
inline constexpr std::uint32_t INDEX_MASK = 0x0FFF'FFFFU;
}  // namespace collection_tag

/// @brief Encode a CollectionHandle into a Value
/// Layout in the 48-bit payload area:
///   bits 0-27:  collection index (up to 256M)
///   bits 28-30: collection type (List=1, Map=2, Set=3)
///   bit 31:     collection marker (always 1)
///   bits 32-47: generation (16 bits)
[[nodiscard]] inline Value encode_collection_value(CollectionHandle handle) noexcept {
    std::uint64_t bits = nan_box::HANDLE_PREFIX;
    // Encode index with collection marker and type
    std::uint32_t tagged_index = collection_tag::COLLECTION_BIT;
    tagged_index |= (static_cast<std::uint32_t>(handle.type) << collection_tag::TYPE_SHIFT);
    tagged_index |= (handle.index & collection_tag::INDEX_MASK);
    bits |= static_cast<std::uint64_t>(tagged_index);
    // Generation in upper 16 bits of payload
    bits |= (static_cast<std::uint64_t>(handle.generation & 0xFFFF) << 32);
    return Value::from_raw(bits);
}

/// @brief Decode a CollectionHandle from a Value
[[nodiscard]] inline CollectionHandle decode_collection_handle(Value v) noexcept {
    if (!v.is_handle()) {
        return CollectionHandle::null();
    }

    std::uint64_t bits = v.raw_bits();
    auto tagged_index = static_cast<std::uint32_t>(bits & 0xFFFF'FFFF);

    // Check collection marker bit
    if ((tagged_index & collection_tag::COLLECTION_BIT) == 0) {
        return CollectionHandle::null();
    }

    auto index = tagged_index & collection_tag::INDEX_MASK;
    auto type = static_cast<CollectionType>((tagged_index & collection_tag::TYPE_MASK) >>
                                            collection_tag::TYPE_SHIFT);
    auto generation = static_cast<std::uint32_t>((bits >> 32) & 0xFFFF);

    return CollectionHandle{.index = index, .generation = generation, .type = type};
}

/// @brief Check if a Value contains a collection handle
[[nodiscard]] inline bool is_collection_value(Value v) noexcept {
    if (!v.is_handle()) {
        return false;
    }
    std::uint64_t bits = v.raw_bits();
    auto tagged_index = static_cast<std::uint32_t>(bits & 0xFFFF'FFFF);
    return (tagged_index & collection_tag::COLLECTION_BIT) != 0;
}

}  // namespace dotvm::core::collections
