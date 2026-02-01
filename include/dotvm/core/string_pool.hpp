/// @file string_pool.hpp
/// @brief String pool with Small String Optimization (SSO) for the DotVM
///
/// Provides efficient string storage for the VM with:
/// - SSO for strings ≤23 bytes (inline storage in Value)
/// - Generation-counted handles for use-after-free protection
/// - Optional interning for string deduplication
///
/// @code
/// StringPool pool;
/// auto handle = pool.create("hello");
/// auto view = pool.get(handle);
/// std::cout << view.value(); // "hello"
/// @endcode

#pragma once

#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "memory_config.hpp"
#include "value.hpp"

namespace dotvm::core {

// ============================================================================
// String Handle
// ============================================================================

/// @brief Handle to a string in the StringPool
///
/// Similar to Handle but with a type tag to distinguish from memory handles.
/// Small strings (≤ SSO_THRESHOLD bytes) are stored directly in the handle.
struct StringHandle {
    /// Maximum bytes storable inline with SSO
    /// 23 bytes = 8 bytes (index) + 8 bytes (generation+flags) + 7 bytes data - overhead
    static constexpr std::size_t SSO_THRESHOLD = 23;

    std::uint32_t index;       ///< Index into pool (or SSO marker)
    std::uint32_t generation;  ///< Generation for use-after-free detection
    std::uint8_t flags;        ///< Bit 0: is_sso, Bit 1: is_interned

    /// Check if this handle uses SSO
    [[nodiscard]] constexpr bool is_sso() const noexcept { return (flags & 0x01) != 0; }

    /// Check if this handle is interned
    [[nodiscard]] constexpr bool is_interned() const noexcept { return (flags & 0x02) != 0; }

    /// Check if this is a null/invalid handle
    [[nodiscard]] constexpr bool is_null() const noexcept {
        return index == mem_config::INVALID_INDEX && !is_sso();
    }

    constexpr bool operator==(const StringHandle&) const noexcept = default;

    /// Create a null handle
    [[nodiscard]] static constexpr StringHandle null() noexcept {
        return StringHandle{.index = mem_config::INVALID_INDEX, .generation = 0, .flags = 0};
    }

    /// Create an SSO handle marker
    [[nodiscard]] static constexpr StringHandle sso_marker(std::uint8_t length) noexcept {
        return StringHandle{
            .index = static_cast<std::uint32_t>(length), .generation = 0, .flags = 0x01};
    }
};

// ============================================================================
// String Pool Error
// ============================================================================

/// @brief Error codes for StringPool operations
enum class StringError : std::uint8_t {
    Success = 0,       ///< Operation succeeded
    InvalidHandle,     ///< Handle not found or generation mismatch
    StringTooLong,     ///< String exceeds maximum allowed length
    AllocationFailed,  ///< Memory allocation failed
    PoolFull,          ///< Cannot allocate more string slots
    EncodingError,     ///< Invalid UTF-8 encoding
};

// ============================================================================
// String Entry
// ============================================================================

/// @brief Entry in the string pool
struct StringEntry {
    std::string data;          ///< String content
    std::uint32_t generation;  ///< Generation counter
    std::uint32_t ref_count;   ///< Reference count (for interned strings)
    bool is_active;            ///< Whether this entry is in use
    bool is_interned;          ///< Whether this string is interned

    /// Create an active entry
    [[nodiscard]] static StringEntry create(std::string str, std::uint32_t gen,
                                            bool interned = false) noexcept {
        return StringEntry{.data = std::move(str),
                           .generation = gen,
                           .ref_count = interned ? 1U : 0U,
                           .is_active = true,
                           .is_interned = interned};
    }

    /// Create an inactive entry
    [[nodiscard]] static constexpr StringEntry inactive(std::uint32_t gen) noexcept {
        return StringEntry{.data = {},
                           .generation = gen,
                           .ref_count = 0,
                           .is_active = false,
                           .is_interned = false};
    }
};

// ============================================================================
// SSO String
// ============================================================================

/// @brief Small String Optimization container
///
/// Stores strings up to SSO_MAX_LEN bytes inline without heap allocation.
class SsoString {
public:
    /// Maximum length for SSO
    static constexpr std::size_t SSO_MAX_LEN = StringHandle::SSO_THRESHOLD;

    /// Default constructor - empty string
    constexpr SsoString() noexcept : len_(0), data_{} {}

    /// Construct from string_view (must be <= SSO_MAX_LEN)
    explicit SsoString(std::string_view sv) noexcept : len_(0), data_{} {
        if (sv.size() <= SSO_MAX_LEN) {
            len_ = static_cast<std::uint8_t>(sv.size());
            std::copy_n(sv.data(), sv.size(), data_.data());
        }
    }

    /// Get the string view
    [[nodiscard]] constexpr std::string_view view() const noexcept {
        return std::string_view(data_.data(), len_);
    }

    /// Get length
    [[nodiscard]] constexpr std::size_t size() const noexcept { return len_; }

    /// Check if empty
    [[nodiscard]] constexpr bool empty() const noexcept { return len_ == 0; }

    /// Check if this is a valid SSO string
    [[nodiscard]] constexpr bool valid() const noexcept { return len_ <= SSO_MAX_LEN; }

    /// Get raw data pointer
    [[nodiscard]] constexpr const char* data() const noexcept { return data_.data(); }

    /// Check if a string_view can be stored as SSO
    [[nodiscard]] static constexpr bool fits(std::string_view sv) noexcept {
        return sv.size() <= SSO_MAX_LEN;
    }

    /// Serialize to bytes (for Value storage)
    [[nodiscard]] std::array<std::uint8_t, SSO_MAX_LEN + 1> to_bytes() const noexcept {
        std::array<std::uint8_t, SSO_MAX_LEN + 1> result{};
        result[0] = len_;
        std::copy_n(data_.data(), len_, reinterpret_cast<char*>(result.data() + 1));
        return result;
    }

    /// Deserialize from bytes
    [[nodiscard]] static SsoString from_bytes(std::span<const std::uint8_t> bytes) noexcept {
        SsoString s;
        if (!bytes.empty() && bytes[0] <= SSO_MAX_LEN && bytes.size() > bytes[0]) {
            s.len_ = bytes[0];
            std::copy_n(reinterpret_cast<const char*>(bytes.data() + 1), s.len_, s.data_.data());
        }
        return s;
    }

private:
    std::uint8_t len_;
    std::array<char, SSO_MAX_LEN> data_;
};

static_assert(sizeof(SsoString) <= 32, "SsoString should fit in cache line");

// ============================================================================
// String Pool
// ============================================================================

/// @brief Pool for managing VM strings with SSO and optional interning
///
/// Features:
/// - SSO: Strings ≤23 bytes stored inline (no heap allocation)
/// - Interning: Optional deduplication of strings
/// - Generation counting: Protects against use-after-free
/// - Thread safety: NOT thread-safe; use one pool per thread
class StringPool {
public:
    /// Result type for operations that can fail
    template <typename T>
    using Result = std::expected<T, StringError>;

    /// @brief Configuration for StringPool
    struct Config {
        std::size_t initial_capacity = 256;       ///< Initial slot capacity
        std::size_t max_strings = 1'000'000;      ///< Maximum number of strings
        std::size_t max_string_length = 1 << 20;  ///< Maximum string length (1MB)
        bool enable_interning = false;            ///< Enable string interning
    };

    /// Construct with default configuration
    StringPool() noexcept : StringPool(Config{}) {}

    /// Construct with custom configuration
    explicit StringPool(Config config) noexcept;

    // Non-copyable, movable
    StringPool(const StringPool&) = delete;
    StringPool& operator=(const StringPool&) = delete;
    StringPool(StringPool&&) noexcept = default;
    StringPool& operator=(StringPool&&) noexcept = default;

    ~StringPool() = default;

    // =========================================================================
    // Creation
    // =========================================================================

    /// @brief Create a string from a string_view
    ///
    /// @param sv The string content
    /// @return Handle to the string, or error
    [[nodiscard]] Result<StringHandle> create(std::string_view sv) noexcept;

    /// @brief Create or reuse an interned string
    ///
    /// If interning is enabled and the string already exists, returns
    /// a handle to the existing string. Otherwise creates a new string.
    ///
    /// @param sv The string content
    /// @return Handle to the string, or error
    [[nodiscard]] Result<StringHandle> intern(std::string_view sv) noexcept;

    /// @brief Create a string from a C string
    [[nodiscard]] Result<StringHandle> create(const char* s) noexcept {
        return create(std::string_view(s ? s : ""));
    }

    /// @brief Create a string from std::string
    [[nodiscard]] Result<StringHandle> create(const std::string& s) noexcept {
        return create(std::string_view(s));
    }

    // =========================================================================
    // Access
    // =========================================================================

    /// @brief Get string content by handle
    ///
    /// @param handle The string handle
    /// @return String view if valid, or error
    [[nodiscard]] Result<std::string_view> get(StringHandle handle) const noexcept;

    /// @brief Get string from SSO bytes stored alongside handle
    ///
    /// @param sso The SSO string
    /// @return String view
    [[nodiscard]] static std::string_view get_sso(const SsoString& sso) noexcept {
        return sso.view();
    }

    /// @brief Check if a handle is valid
    [[nodiscard]] bool is_valid(StringHandle handle) const noexcept;

    /// @brief Get length of string
    [[nodiscard]] Result<std::size_t> length(StringHandle handle) const noexcept;

    // =========================================================================
    // Modification
    // =========================================================================

    /// @brief Concatenate two strings
    ///
    /// @param a First string handle
    /// @param b Second string handle
    /// @return Handle to concatenated string
    [[nodiscard]] Result<StringHandle> concat(StringHandle a, StringHandle b) noexcept;

    /// @brief Get substring
    ///
    /// @param handle Source string
    /// @param start Start index
    /// @param len Length (std::string_view::npos for rest of string)
    /// @return Handle to substring
    [[nodiscard]] Result<StringHandle> substr(StringHandle handle, std::size_t start,
                                              std::size_t len = std::string_view::npos) noexcept;

    // =========================================================================
    // Lifecycle
    // =========================================================================

    /// @brief Release a string handle
    ///
    /// For interned strings, decrements ref count. For non-interned strings,
    /// marks the slot as free.
    ///
    /// @param handle The handle to release
    /// @return Error code
    [[nodiscard]] StringError release(StringHandle handle) noexcept;

    /// @brief Clear all strings and reset the pool
    void clear() noexcept;

    // =========================================================================
    // Statistics
    // =========================================================================

    /// @brief Get number of active strings
    [[nodiscard]] std::size_t active_count() const noexcept;

    /// @brief Get total bytes used by strings
    [[nodiscard]] std::size_t total_bytes() const noexcept;

    /// @brief Get number of interned strings
    [[nodiscard]] std::size_t interned_count() const noexcept;

    /// @brief Get configuration
    [[nodiscard]] const Config& config() const noexcept { return config_; }

private:
    /// Allocate a new slot
    [[nodiscard]] std::optional<std::uint32_t> allocate_slot() noexcept;

    /// Release a slot back to free list
    void release_slot(std::uint32_t index) noexcept;

    /// Validate a handle
    [[nodiscard]] StringError validate_handle(StringHandle handle) const noexcept;

    Config config_;
    std::vector<StringEntry> entries_;
    std::vector<std::uint32_t> free_list_;
    std::unordered_map<std::string_view, std::uint32_t> intern_map_;  // For interning
    std::size_t total_bytes_ = 0;
};

// ============================================================================
// Value Integration
// ============================================================================

/// @brief Encode a StringHandle into a Value
///
/// For SSO strings, the string content is encoded directly into the Value.
/// For heap strings, the handle is encoded as a tagged value.
[[nodiscard]] Value encode_string_value(StringHandle handle,
                                        const SsoString* sso = nullptr) noexcept;

/// @brief Decode a StringHandle from a Value
///
/// @param v The value to decode
/// @param sso_out If non-null and the value contains SSO data, populated with the SSO string
/// @return The StringHandle (may have is_sso() = true if SSO)
[[nodiscard]] StringHandle decode_string_handle(Value v, SsoString* sso_out = nullptr) noexcept;

/// @brief Check if a Value contains a string
[[nodiscard]] bool is_string_value(Value v) noexcept;

}  // namespace dotvm::core
