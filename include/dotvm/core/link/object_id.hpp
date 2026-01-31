#pragma once

/// @file object_id.hpp
/// @brief DEP-004 ObjectId and generator for relationship model
///
/// ObjectId is a 128-bit persistent identifier composed of:
/// - type_hash (64-bit)
/// - instance_id (64-bit)
///
/// Byte serialization uses little-endian ordering for each 64-bit field.

#include <array>
#include <atomic>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <span>
#include <string_view>

namespace dotvm::core::link {

// ============================================================================
// ObjectId
// ============================================================================

/// @brief 128-bit persistent identifier for objects
struct ObjectId {
    std::uint64_t type_hash{0};
    std::uint64_t instance_id{0};

    /// @brief Check if this ObjectId is valid (both parts non-zero)
    [[nodiscard]] constexpr bool is_valid() const noexcept {
        return type_hash != 0 && instance_id != 0;
    }

    /// @brief Create an invalid ObjectId (all zeros)
    [[nodiscard]] static constexpr ObjectId invalid() noexcept { return ObjectId{}; }

    /// @brief Serialize to bytes (little-endian fields)
    [[nodiscard]] constexpr std::array<std::byte, 16> to_bytes() const noexcept {
        std::array<std::byte, 16> out{};

        auto write_u64 = [](std::uint64_t value, std::byte* dest) constexpr {
            for (std::size_t i = 0; i < 8; ++i) {
                dest[i] = static_cast<std::byte>((value >> (i * 8)) & 0xFFU);
            }
        };

        write_u64(type_hash, out.data());
        write_u64(instance_id, out.data() + 8);

        return out;
    }

    /// @brief Deserialize from bytes (little-endian fields)
    [[nodiscard]] static constexpr ObjectId
    from_bytes(std::span<const std::byte, 16> bytes) noexcept {
        auto read_u64 = [](const std::byte* src) constexpr {
            std::uint64_t value = 0;
            for (std::size_t i = 0; i < 8; ++i) {
                const auto byte_value =
                    static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(src[i]));
                value |= (byte_value << (i * 8));
            }
            return value;
        };

        return ObjectId{read_u64(bytes.data()), read_u64(bytes.data() + 8)};
    }

    auto operator<=>(const ObjectId&) const = default;
};

// ============================================================================
// ObjectIdGenerator
// ============================================================================

/// @brief Thread-safe generator for ObjectId values
class ObjectIdGenerator {
public:
    /// @brief Generate a new ObjectId for a given type name
    /// @note Empty type names return an invalid ObjectId
    [[nodiscard]] ObjectId generate(std::string_view type_name) noexcept;

private:
    std::atomic<std::uint64_t> counter_{1};
};

}  // namespace dotvm::core::link

// ============================================================================
// std::formatter specialization for ObjectId
// ============================================================================

template <>
struct std::formatter<dotvm::core::link::ObjectId> : std::formatter<std::string> {
    auto format(const dotvm::core::link::ObjectId& id, std::format_context& ctx) const {
        return std::format_to(ctx.out(), "ObjectId{{type_hash={}, instance_id={}}}", id.type_hash,
                              id.instance_id);
    }
};
