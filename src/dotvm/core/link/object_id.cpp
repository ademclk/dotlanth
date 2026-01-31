#include "dotvm/core/link/object_id.hpp"

namespace dotvm::core::link {

namespace {

constexpr std::uint64_t fnv1a_64(std::string_view input) noexcept {
    std::uint64_t hash = 14695981039346656037ULL;  // FNV-1a offset basis
    for (char c : input) {
        const auto ch = static_cast<std::uint64_t>(static_cast<unsigned char>(c));
        hash ^= ch;
        hash *= 1099511628211ULL;  // FNV-1a prime
    }
    return hash;
}

}  // namespace

ObjectId ObjectIdGenerator::generate(std::string_view type_name) noexcept {
    if (type_name.empty()) {
        return ObjectId::invalid();
    }

    auto type_hash = fnv1a_64(type_name);
    if (type_hash == 0) {
        type_hash = 1;
    }

    auto instance_id = counter_.fetch_add(1, std::memory_order_relaxed);
    if (instance_id == 0) {
        instance_id = counter_.fetch_add(1, std::memory_order_relaxed);
    }

    return ObjectId{type_hash, instance_id};
}

}  // namespace dotvm::core::link
