#pragma once

/// @file syscall_whitelist.hpp
/// @brief SEC-007 Syscall Whitelist for DotVM isolation enforcement
///
/// This header defines the SyscallId enum representing future VM syscalls
/// and the SyscallWhitelist class for efficient O(1) syscall validation.
/// Syscalls are organized into categories for logical grouping.
///
/// Note: Syscalls are designed for future opcodes (0xF0+). This API defines
/// the whitelist structure now for use with IsolationManager.

#include <bitset>
#include <cstdint>

namespace dotvm::core::security {

// ============================================================================
// SyscallId Enum
// ============================================================================

/// @brief Syscall identifiers for DotVM operations
///
/// Syscalls are organized into categories with reserved ranges:
/// - 0x00-0x0F: Memory operations
/// - 0x10-0x1F: I/O operations
/// - 0x20-0x2F: Network operations (blocked in Strict mode)
/// - 0x30-0x3F: Filesystem operations (blocked in Strict mode)
/// - 0x40-0x4F: Dot lifecycle operations
/// - 0x50-0x5F: Time operations
/// - 0x60-0x6F: Cryptographic operations
/// - 0x70-0xFF: Reserved for future use
///
/// @note These syscall IDs are designed for future opcode implementation.
/// The whitelist API is implemented now for integration with IsolationManager.
enum class SyscallId : std::uint8_t {
    // ===== Memory Operations (0x00-0x0F) =====

    /// Allocate memory region
    MemAllocate = 0x00,

    /// Deallocate memory region
    MemDeallocate = 0x01,

    /// Query memory region properties
    MemQuery = 0x02,

    /// Copy memory between regions
    MemCopy = 0x03,

    /// Zero memory region
    MemZero = 0x04,

    // ===== I/O Operations (0x10-0x1F) =====

    /// Read from I/O channel
    IoRead = 0x10,

    /// Write to I/O channel
    IoWrite = 0x11,

    /// Flush I/O channel
    IoFlush = 0x12,

    /// Close I/O channel
    IoClose = 0x13,

    // ===== Network Operations (0x20-0x2F) - Blocked in Strict =====

    /// Create network socket
    NetSocket = 0x20,

    /// Connect to remote endpoint
    NetConnect = 0x21,

    /// Bind to local address
    NetBind = 0x22,

    /// Listen for connections
    NetListen = 0x23,

    /// Accept incoming connection
    NetAccept = 0x24,

    /// Send data
    NetSend = 0x25,

    /// Receive data
    NetRecv = 0x26,

    // ===== Filesystem Operations (0x30-0x3F) - Blocked in Strict =====

    /// Open file
    FsOpen = 0x30,

    /// Read from file
    FsRead = 0x31,

    /// Write to file
    FsWrite = 0x32,

    /// Seek in file
    FsSeek = 0x33,

    /// Get file status
    FsStat = 0x34,

    /// Close file
    FsClose = 0x35,

    /// Create directory
    FsMkdir = 0x36,

    /// Remove file/directory
    FsRemove = 0x37,

    // ===== Dot Lifecycle Operations (0x40-0x4F) =====

    /// Spawn child Dot
    DotSpawn = 0x40,

    /// Yield execution to scheduler
    DotYield = 0x41,

    /// Get current Dot ID
    DotGetId = 0x42,

    /// Send message to another Dot
    DotSend = 0x43,

    /// Receive message from another Dot
    DotRecv = 0x44,

    /// Terminate current Dot
    DotExit = 0x45,

    // ===== Time Operations (0x50-0x5F) =====

    /// Get current time
    TimeNow = 0x50,

    /// Sleep for duration
    TimeSleep = 0x51,

    /// Get monotonic clock
    TimeMonotonic = 0x52,

    // ===== Cryptographic Operations (0x60-0x6F) =====

    /// Generate random bytes
    CryptoRandom = 0x60,

    /// Compute hash
    CryptoHash = 0x61,

    /// Verify signature
    CryptoVerify = 0x62,
};

/// @brief Convert SyscallId to human-readable string
///
/// @param id The syscall ID to convert
/// @return String representation of the syscall
[[nodiscard]] constexpr const char* to_string(SyscallId id) noexcept {
    switch (id) {
        // Memory
        case SyscallId::MemAllocate:
            return "MemAllocate";
        case SyscallId::MemDeallocate:
            return "MemDeallocate";
        case SyscallId::MemQuery:
            return "MemQuery";
        case SyscallId::MemCopy:
            return "MemCopy";
        case SyscallId::MemZero:
            return "MemZero";
        // I/O
        case SyscallId::IoRead:
            return "IoRead";
        case SyscallId::IoWrite:
            return "IoWrite";
        case SyscallId::IoFlush:
            return "IoFlush";
        case SyscallId::IoClose:
            return "IoClose";
        // Network
        case SyscallId::NetSocket:
            return "NetSocket";
        case SyscallId::NetConnect:
            return "NetConnect";
        case SyscallId::NetBind:
            return "NetBind";
        case SyscallId::NetListen:
            return "NetListen";
        case SyscallId::NetAccept:
            return "NetAccept";
        case SyscallId::NetSend:
            return "NetSend";
        case SyscallId::NetRecv:
            return "NetRecv";
        // Filesystem
        case SyscallId::FsOpen:
            return "FsOpen";
        case SyscallId::FsRead:
            return "FsRead";
        case SyscallId::FsWrite:
            return "FsWrite";
        case SyscallId::FsSeek:
            return "FsSeek";
        case SyscallId::FsStat:
            return "FsStat";
        case SyscallId::FsClose:
            return "FsClose";
        case SyscallId::FsMkdir:
            return "FsMkdir";
        case SyscallId::FsRemove:
            return "FsRemove";
        // Dot lifecycle
        case SyscallId::DotSpawn:
            return "DotSpawn";
        case SyscallId::DotYield:
            return "DotYield";
        case SyscallId::DotGetId:
            return "DotGetId";
        case SyscallId::DotSend:
            return "DotSend";
        case SyscallId::DotRecv:
            return "DotRecv";
        case SyscallId::DotExit:
            return "DotExit";
        // Time
        case SyscallId::TimeNow:
            return "TimeNow";
        case SyscallId::TimeSleep:
            return "TimeSleep";
        case SyscallId::TimeMonotonic:
            return "TimeMonotonic";
        // Crypto
        case SyscallId::CryptoRandom:
            return "CryptoRandom";
        case SyscallId::CryptoHash:
            return "CryptoHash";
        case SyscallId::CryptoVerify:
            return "CryptoVerify";
    }
    return "Unknown";
}

// ============================================================================
// SyscallWhitelist Class
// ============================================================================

/// @brief Efficient syscall whitelist using bitset for O(1) lookup
///
/// Provides a compact representation of allowed syscalls with constant-time
/// validation. Use factory methods `strict_default()` and `allow_all()` for
/// common configurations.
///
/// Thread Safety: NOT thread-safe. Use one instance per IsolationManager.
///
/// @par Usage Example
/// @code
/// // Create strict whitelist (safe syscalls only)
/// auto whitelist = SyscallWhitelist::strict_default();
///
/// // Check if syscall is allowed
/// if (whitelist.is_allowed(SyscallId::MemAllocate)) {
///     // Proceed with syscall
/// }
///
/// // Customize by adding specific syscalls
/// whitelist.allow(SyscallId::IoRead);
/// @endcode
class SyscallWhitelist {
public:
    /// @brief Default constructor creates an empty whitelist (all blocked)
    constexpr SyscallWhitelist() noexcept = default;

    /// @brief Add a syscall to the whitelist
    ///
    /// @param id The syscall to allow
    /// @return Reference to this whitelist for chaining
    constexpr SyscallWhitelist& allow(SyscallId id) noexcept {
        allowed_.set(static_cast<std::size_t>(id));
        return *this;
    }

    /// @brief Remove a syscall from the whitelist
    ///
    /// @param id The syscall to block
    /// @return Reference to this whitelist for chaining
    constexpr SyscallWhitelist& block(SyscallId id) noexcept {
        allowed_.reset(static_cast<std::size_t>(id));
        return *this;
    }

    /// @brief Check if a syscall is allowed
    ///
    /// @param id The syscall to check
    /// @return true if the syscall is in the whitelist
    [[nodiscard]] constexpr bool is_allowed(SyscallId id) const noexcept {
        return allowed_.test(static_cast<std::size_t>(id));
    }

    /// @brief Get the number of allowed syscalls
    ///
    /// @return Count of syscalls in the whitelist
    [[nodiscard]] constexpr std::size_t count() const noexcept { return allowed_.count(); }

    /// @brief Check if any syscalls are allowed
    ///
    /// @return true if at least one syscall is whitelisted
    [[nodiscard]] constexpr bool any() const noexcept { return allowed_.any(); }

    /// @brief Check if no syscalls are allowed
    ///
    /// @return true if whitelist is empty (all blocked)
    [[nodiscard]] constexpr bool none() const noexcept { return allowed_.none(); }

    /// @brief Reset whitelist to empty (all blocked)
    constexpr void clear() noexcept { allowed_.reset(); }

    // ========== Factory Methods ==========

    /// @brief Create a strict whitelist with only safe syscalls
    ///
    /// Allows:
    /// - Memory operations (MemAllocate, MemDeallocate, MemQuery, MemCopy, MemZero)
    /// - Basic I/O (IoRead, IoWrite, IoFlush, IoClose)
    /// - Dot lifecycle (DotYield, DotGetId, DotExit)
    /// - Time (TimeNow, TimeMonotonic)
    /// - Crypto (CryptoRandom, CryptoHash, CryptoVerify)
    ///
    /// Blocks:
    /// - Network operations (all)
    /// - Filesystem operations (all)
    /// - Dot spawning (DotSpawn) - requires explicit grant
    /// - TimeSleep - can be used for timing attacks
    ///
    /// @return Whitelist configured for strict isolation
    [[nodiscard]] static SyscallWhitelist strict_default() noexcept {
        SyscallWhitelist whitelist;

        // Memory operations (safe)
        whitelist.allow(SyscallId::MemAllocate);
        whitelist.allow(SyscallId::MemDeallocate);
        whitelist.allow(SyscallId::MemQuery);
        whitelist.allow(SyscallId::MemCopy);
        whitelist.allow(SyscallId::MemZero);

        // Basic I/O (safe - sandboxed channels)
        whitelist.allow(SyscallId::IoRead);
        whitelist.allow(SyscallId::IoWrite);
        whitelist.allow(SyscallId::IoFlush);
        whitelist.allow(SyscallId::IoClose);

        // Dot lifecycle (safe subset)
        whitelist.allow(SyscallId::DotYield);
        whitelist.allow(SyscallId::DotGetId);
        whitelist.allow(SyscallId::DotExit);

        // Time (safe subset - TimeNow and Monotonic only)
        whitelist.allow(SyscallId::TimeNow);
        whitelist.allow(SyscallId::TimeMonotonic);

        // Crypto (all safe)
        whitelist.allow(SyscallId::CryptoRandom);
        whitelist.allow(SyscallId::CryptoHash);
        whitelist.allow(SyscallId::CryptoVerify);

        return whitelist;
    }

    /// @brief Create a whitelist that allows all syscalls
    ///
    /// Use for trusted code or when isolation is disabled.
    ///
    /// @return Whitelist with all syscalls allowed
    [[nodiscard]] static SyscallWhitelist allow_all() noexcept {
        SyscallWhitelist whitelist;
        whitelist.allowed_.set();  // Set all bits
        return whitelist;
    }

    /// @brief Equality comparison
    [[nodiscard]] constexpr bool operator==(const SyscallWhitelist& other) const noexcept {
        return allowed_ == other.allowed_;
    }

private:
    std::bitset<256> allowed_;
};

}  // namespace dotvm::core::security
