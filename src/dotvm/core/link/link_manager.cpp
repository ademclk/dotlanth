#include "dotvm/core/link/link_manager.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <span>
#include <string>
#include <unordered_set>
#include <vector>

#include "dotvm/core/link/link_key_encoder.hpp"
#include "dotvm/core/schema/object_type.hpp"
#include "dotvm/core/schema/schema_registry.hpp"
#include "dotvm/core/state/state_backend.hpp"

namespace dotvm::core::link {
namespace {

constexpr std::size_t kCountSize = sizeof(std::uint64_t);
constexpr std::size_t kObjectIdSize = 16;

constexpr std::uint64_t fnv1a_64(std::string_view input) noexcept {
    std::uint64_t hash = 14695981039346656037ULL;  // FNV-1a offset basis
    for (char c : input) {
        const auto ch = static_cast<std::uint64_t>(static_cast<unsigned char>(c));
        hash ^= ch;
        hash *= 1099511628211ULL;  // FNV-1a prime
    }
    return hash;
}

[[nodiscard]] constexpr std::uint64_t type_hash_for(std::string_view type_name) noexcept {
    auto hash = fnv1a_64(type_name);
    if (hash == 0) {
        hash = 1;
    }
    return hash;
}

[[nodiscard]] std::array<std::byte, kCountSize> encode_u64_le(std::uint64_t value) noexcept {
    std::array<std::byte, kCountSize> out{};
    for (std::size_t i = 0; i < kCountSize; ++i) {
        out[i] = static_cast<std::byte>((value >> (i * 8)) & 0xFFU);
    }
    return out;
}

[[nodiscard]] std::uint64_t decode_u64_le(std::span<const std::byte, kCountSize> bytes) noexcept {
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < kCountSize; ++i) {
        value |= (static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(bytes[i])) << (i * 8));
    }
    return value;
}

[[nodiscard]] std::span<const std::byte> as_span(const std::vector<std::byte>& bytes) {
    return {bytes.data(), bytes.size()};
}

struct ObjectIdHash {
    std::size_t operator()(const ObjectId& id) const noexcept {
        const auto h1 = std::hash<std::uint64_t>{}(id.type_hash);
        const auto h2 = std::hash<std::uint64_t>{}(id.instance_id);
        return h1 ^ (h2 + 0x9e3779b97f4a7c15ULL + (h1 << 6) + (h1 >> 2));
    }
};

}  // namespace

LinkManager::LinkManager(std::shared_ptr<schema::SchemaRegistry> schema,
                         std::shared_ptr<state::StateBackend> backend, LinkManagerConfig config)
    : schema_(std::move(schema)), backend_(std::move(backend)), config_(config) {}

LinkManager::Result<void> LinkManager::create_link(ObjectId src, std::string_view source_type,
                                                   std::string_view link_name, ObjectId tgt) {
    const std::scoped_lock lock(mutex_);
    return create_link_internal(src, source_type, link_name, tgt, true);
}

LinkManager::Result<void> LinkManager::remove_link(ObjectId src, std::string_view source_type,
                                                   std::string_view link_name, ObjectId tgt,
                                                   CascadePolicy cascade) {
    const std::scoped_lock lock(mutex_);
    return remove_link_internal(src, source_type, link_name, tgt, cascade, true);
}

LinkManager::Result<void> LinkManager::remove_all_links_from(ObjectId src,
                                                             std::string_view source_type,
                                                             CascadePolicy cascade) {
    const std::scoped_lock lock(mutex_);

    if (backend_ == nullptr || schema_ == nullptr) {
        return LinkError::StorageError;
    }

    if (!src.is_valid()) {
        return LinkError::InvalidObjectId;
    }

    if (src.type_hash != type_hash_for(source_type)) {
        return LinkError::InvalidObjectId;
    }

    auto type_result = schema_->get_type(std::string(source_type));
    if (type_result.is_err()) {
        return LinkError::LinkDefinitionNotFound;
    }

    LinkError first_error = LinkError::StorageError;
    bool has_error = false;

    const auto& source_type_def = type_result.value();
    for (const auto& link_name : source_type_def->link_names()) {
        auto targets_result = get_links(src, link_name);
        if (targets_result.is_err()) {
            if (!has_error) {
                first_error = targets_result.error();
                has_error = true;
            }
            continue;
        }

        for (const auto& tgt : targets_result.value()) {
            auto remove_result =
                remove_link_internal(src, source_type, link_name, tgt, cascade, true);
            if (remove_result.is_err()) {
                if (!has_error) {
                    first_error = remove_result.error();
                    has_error = true;
                }
            }
        }
    }

    if (has_error) {
        return first_error;
    }

    return {};
}

LinkManager::Result<void> LinkManager::remove_all_links_to(ObjectId tgt,
                                                           std::string_view target_type) {
    const std::scoped_lock lock(mutex_);

    if (backend_ == nullptr || schema_ == nullptr) {
        return LinkError::StorageError;
    }

    if (!tgt.is_valid()) {
        return LinkError::InvalidObjectId;
    }

    if (tgt.type_hash != type_hash_for(target_type)) {
        return LinkError::InvalidObjectId;
    }

    LinkError first_error = LinkError::StorageError;
    bool has_error = false;

    const auto type_names = schema_->type_names();
    for (const auto& type_name : type_names) {
        auto type_result = schema_->get_type(type_name);
        if (type_result.is_err()) {
            if (!has_error) {
                first_error = LinkError::LinkDefinitionNotFound;
                has_error = true;
            }
            continue;
        }

        const auto& source_type_def = type_result.value();
        const auto expected_hash = type_hash_for(source_type_def->name());
        for (const auto& link_name : source_type_def->link_names()) {
            const auto* link_def = source_type_def->get_link(link_name);
            if (link_def == nullptr) {
                if (!has_error) {
                    first_error = LinkError::LinkDefinitionNotFound;
                    has_error = true;
                }
                continue;
            }

            if (link_def->target_type != target_type) {
                continue;
            }

            auto sources_result = get_inverse(tgt, link_name);
            if (sources_result.is_err()) {
                if (!has_error) {
                    first_error = sources_result.error();
                    has_error = true;
                }
                continue;
            }

            for (const auto& src : sources_result.value()) {
                if (src.type_hash != expected_hash) {
                    continue;
                }
                auto remove_result = remove_link_internal(src, source_type_def->name(), link_name,
                                                          tgt, CascadePolicy::None, true);
                if (remove_result.is_err()) {
                    if (!has_error) {
                        first_error = remove_result.error();
                        has_error = true;
                    }
                }
            }
        }
    }

    if (has_error) {
        return first_error;
    }

    return {};
}

bool LinkManager::has_link(ObjectId src, std::string_view link_name, ObjectId tgt) const {
    if (backend_ == nullptr) {
        return false;
    }

    const auto forward_key = LinkKeyEncoder::encode_forward_key(src, link_name, tgt);
    return backend_->exists(as_span(forward_key));
}

LinkManager::Result<std::vector<ObjectId>>
LinkManager::get_links(ObjectId src, std::string_view link_name) const {
    if (backend_ == nullptr) {
        return LinkError::StorageError;
    }

    std::vector<ObjectId> targets;
    bool corrupted = false;

    const auto prefix = LinkKeyEncoder::encode_forward_prefix(src, link_name);
    const std::size_t expected_size = prefix.size() + kObjectIdSize;
    auto result = backend_->iterate(
        as_span(prefix), [&](state::StateBackend::Key key, state::StateBackend::Value value) {
            (void)value;
            if (key.size() != expected_size) {
                corrupted = true;
                return false;
            }
            const auto target = LinkKeyEncoder::decode_target_from_forward(key);
            if (!target.is_valid()) {
                corrupted = true;
                return false;
            }
            targets.push_back(target);
            return true;
        });

    if (result.is_err()) {
        return LinkError::StorageError;
    }

    if (corrupted) {
        return LinkError::IndexCorruption;
    }

    return targets;
}

LinkManager::Result<std::vector<ObjectId>>
LinkManager::traverse(ObjectId start, std::string_view start_type,
                      std::span<const std::string_view> path) const {
    if (path.empty()) {
        return LinkError::TraversalPathEmpty;
    }

    if (path.size() > config_.max_traversal_depth) {
        return LinkError::TraversalDepthExceeded;
    }

    if (schema_ == nullptr || backend_ == nullptr) {
        return LinkError::StorageError;
    }

    if (!start.is_valid()) {
        return LinkError::InvalidObjectId;
    }

    if (start.type_hash != type_hash_for(start_type)) {
        return LinkError::InvalidObjectId;
    }

    std::vector<ObjectId> current = {start};
    std::string_view current_type = start_type;

    for (const auto& link_name : path) {
        auto type_result = schema_->get_type(std::string(current_type));
        if (type_result.is_err()) {
            return LinkError::TraversalPathInvalid;
        }

        const auto* link_def = type_result.value()->get_link(link_name);
        if (link_def == nullptr) {
            return LinkError::TraversalPathInvalid;
        }

        std::vector<ObjectId> next;
        std::unordered_set<ObjectId, ObjectIdHash> seen;
        for (const auto& obj : current) {
            auto targets = get_links(obj, link_name);
            if (targets.is_err()) {
                return targets.error();
            }
            for (const auto& tgt : targets.value()) {
                if (seen.insert(tgt).second) {
                    next.push_back(tgt);
                }
            }
        }

        current = std::move(next);
        current_type = link_def->target_type;
    }

    return current;
}

LinkManager::Result<std::vector<ObjectId>>
LinkManager::get_inverse(ObjectId tgt, std::string_view link_name) const {
    if (backend_ == nullptr) {
        return LinkError::StorageError;
    }

    std::vector<ObjectId> sources;
    bool corrupted = false;

    const auto prefix = LinkKeyEncoder::encode_inverse_prefix(tgt, link_name);
    const std::size_t expected_size = prefix.size() + kObjectIdSize;
    auto result = backend_->iterate(
        as_span(prefix), [&](state::StateBackend::Key key, state::StateBackend::Value value) {
            (void)value;
            if (key.size() != expected_size) {
                corrupted = true;
                return false;
            }
            const auto source = LinkKeyEncoder::decode_source_from_inverse(key);
            if (!source.is_valid()) {
                corrupted = true;
                return false;
            }
            sources.push_back(source);
            return true;
        });

    if (result.is_err()) {
        return LinkError::StorageError;
    }

    if (corrupted) {
        return LinkError::IndexCorruption;
    }

    return sources;
}

LinkManager::Result<void> LinkManager::create_link_internal(ObjectId src,
                                                            std::string_view source_type,
                                                            std::string_view link_name,
                                                            ObjectId tgt, bool manage_inverse) {
    if (backend_ == nullptr || schema_ == nullptr) {
        return LinkError::StorageError;
    }

    if (!src.is_valid() || !tgt.is_valid()) {
        return LinkError::InvalidObjectId;
    }

    if (src.type_hash != type_hash_for(source_type)) {
        return LinkError::InvalidObjectId;
    }

    auto type_result = schema_->get_type(std::string(source_type));
    if (type_result.is_err()) {
        return LinkError::LinkDefinitionNotFound;
    }

    const auto& source_type_def = type_result.value();
    const auto* link_def = source_type_def->get_link(link_name);
    if (link_def == nullptr) {
        return LinkError::LinkDefinitionNotFound;
    }

    if (tgt.type_hash != type_hash_for(link_def->target_type)) {
        return LinkError::InvalidObjectId;
    }

    const auto forward_key = LinkKeyEncoder::encode_forward_key(src, link_def->name, tgt);
    if (backend_->exists(as_span(forward_key))) {
        return LinkError::LinkInstanceAlreadyExists;
    }

    const bool should_manage_inverse =
        manage_inverse && config_.auto_manage_inverse && !link_def->inverse_link.empty();
    const schema::LinkDef* inverse_def = nullptr;
    bool inverse_already_exists = false;
    if (should_manage_inverse) {
        auto target_type_result = schema_->get_type(link_def->target_type);
        if (target_type_result.is_err()) {
            return LinkError::InverseLinkMismatch;
        }

        inverse_def = target_type_result.value()->get_link(link_def->inverse_link);
        if (inverse_def == nullptr) {
            return LinkError::InverseLinkMismatch;
        }

        if (inverse_def->target_type != link_def->source_type) {
            return LinkError::InverseLinkMismatch;
        }
    }

    auto count_result = get_link_count(src, link_def->name);
    if (count_result.is_err()) {
        return count_result.error();
    }
    const std::uint64_t current_count = count_result.value();

    if (current_count == std::numeric_limits<std::uint64_t>::max()) {
        return LinkError::CardinalityViolation;
    }

    if (link_def->cardinality == schema::Cardinality::OneToOne && current_count > 0) {
        return LinkError::CardinalityViolation;
    }

    if (config_.max_links_per_source > 0 &&
        current_count >= static_cast<std::uint64_t>(config_.max_links_per_source)) {
        return LinkError::CardinalityViolation;
    }

    std::uint64_t inverse_count = 0;
    if (inverse_def != nullptr) {
        const auto inverse_forward_key =
            LinkKeyEncoder::encode_forward_key(tgt, inverse_def->name, src);
        if (backend_->exists(as_span(inverse_forward_key))) {
            inverse_already_exists = true;
        } else {
            auto inverse_count_result = get_link_count(tgt, inverse_def->name);
            if (inverse_count_result.is_err()) {
                return inverse_count_result.error();
            }

            inverse_count = inverse_count_result.value();
            if (inverse_count == std::numeric_limits<std::uint64_t>::max()) {
                return LinkError::CardinalityViolation;
            }

            if (inverse_def->cardinality == schema::Cardinality::OneToOne && inverse_count > 0) {
                return LinkError::CardinalityViolation;
            }

            if (config_.max_links_per_source > 0 &&
                inverse_count >= static_cast<std::uint64_t>(config_.max_links_per_source)) {
                return LinkError::CardinalityViolation;
            }
        }
    }

    constexpr std::span<const std::byte> empty_value{};
    const auto inverse_key = LinkKeyEncoder::encode_inverse_key(tgt, link_def->name, src);
    const auto count_key = LinkKeyEncoder::encode_count_key(src, link_def->name);
    const auto new_count = current_count + 1;
    const auto encoded_count = encode_u64_le(new_count);
    std::array<state::BatchOp, 3> ops{
        state::BatchOp{state::BatchOpType::Put, as_span(forward_key), empty_value},
        state::BatchOp{state::BatchOpType::Put, as_span(inverse_key), empty_value},
        state::BatchOp{state::BatchOpType::Put, as_span(count_key),
                       std::span<const std::byte>(encoded_count.data(), encoded_count.size())}};

    auto batch_result = backend_->batch(ops);
    if (batch_result.is_err()) {
        return LinkError::StorageError;
    }

    if (inverse_def != nullptr && !inverse_already_exists) {
        auto inverse_link_result =
            create_link_internal(tgt, link_def->target_type, inverse_def->name, src, false);
        if (inverse_link_result.is_err() &&
            inverse_link_result.error() != LinkError::LinkInstanceAlreadyExists) {
            const bool remove_count_key = current_count == 0;
            const auto rollback_count = encode_u64_le(current_count);
            std::array<state::BatchOp, 3> rollback_ops{};
            std::size_t rollback_size = 0;
            rollback_ops[rollback_size++] =
                state::BatchOp{state::BatchOpType::Remove, as_span(forward_key), empty_value};
            rollback_ops[rollback_size++] =
                state::BatchOp{state::BatchOpType::Remove, as_span(inverse_key), empty_value};
            if (remove_count_key) {
                rollback_ops[rollback_size++] =
                    state::BatchOp{state::BatchOpType::Remove, as_span(count_key), empty_value};
            } else {
                rollback_ops[rollback_size++] = state::BatchOp{
                    state::BatchOpType::Put, as_span(count_key),
                    std::span<const std::byte>(rollback_count.data(), rollback_count.size())};
            }

            auto rollback_result = backend_->batch(
                std::span<const state::BatchOp>(rollback_ops.data(), rollback_size));
            if (rollback_result.is_err()) {
                return LinkError::StorageError;
            }

            return inverse_link_result.error();
        }
    }

    return {};
}

LinkManager::Result<void> LinkManager::remove_link_internal(ObjectId src,
                                                            std::string_view source_type,
                                                            std::string_view link_name,
                                                            ObjectId tgt, CascadePolicy cascade,
                                                            bool manage_inverse) {
    (void)cascade;

    if (backend_ == nullptr || schema_ == nullptr) {
        return LinkError::StorageError;
    }

    if (!src.is_valid() || !tgt.is_valid()) {
        return LinkError::InvalidObjectId;
    }

    if (src.type_hash != type_hash_for(source_type)) {
        return LinkError::InvalidObjectId;
    }

    auto type_result = schema_->get_type(std::string(source_type));
    if (type_result.is_err()) {
        return LinkError::LinkDefinitionNotFound;
    }

    const auto& source_type_def = type_result.value();
    const auto* link_def = source_type_def->get_link(link_name);
    if (link_def == nullptr) {
        return LinkError::LinkDefinitionNotFound;
    }

    if (tgt.type_hash != type_hash_for(link_def->target_type)) {
        return LinkError::InvalidObjectId;
    }

    const auto forward_key = LinkKeyEncoder::encode_forward_key(src, link_def->name, tgt);
    if (!backend_->exists(as_span(forward_key))) {
        return LinkError::LinkInstanceNotFound;
    }

    const bool should_manage_inverse =
        manage_inverse && config_.auto_manage_inverse && !link_def->inverse_link.empty();
    const schema::LinkDef* inverse_def = nullptr;
    if (should_manage_inverse) {
        auto target_type_result = schema_->get_type(link_def->target_type);
        if (target_type_result.is_err()) {
            return LinkError::InverseLinkMismatch;
        }

        inverse_def = target_type_result.value()->get_link(link_def->inverse_link);
        if (inverse_def == nullptr) {
            return LinkError::InverseLinkMismatch;
        }

        if (inverse_def->target_type != link_def->source_type) {
            return LinkError::InverseLinkMismatch;
        }
    }

    auto count_result = get_link_count(src, link_def->name);
    if (count_result.is_err()) {
        return count_result.error();
    }
    const std::uint64_t current_count = count_result.value();
    if (current_count == 0) {
        return LinkError::IndexCorruption;
    }

    constexpr std::span<const std::byte> empty_value{};
    const auto inverse_key = LinkKeyEncoder::encode_inverse_key(tgt, link_def->name, src);
    const auto count_key = LinkKeyEncoder::encode_count_key(src, link_def->name);
    const auto new_count = current_count - 1;

    std::array<state::BatchOp, 3> ops{};
    std::size_t op_count = 0;
    ops[op_count++] = state::BatchOp{state::BatchOpType::Remove, as_span(forward_key), empty_value};
    ops[op_count++] = state::BatchOp{state::BatchOpType::Remove, as_span(inverse_key), empty_value};
    if (new_count == 0) {
        ops[op_count++] =
            state::BatchOp{state::BatchOpType::Remove, as_span(count_key), empty_value};
    } else {
        const auto encoded_count = encode_u64_le(new_count);
        ops[op_count++] =
            state::BatchOp{state::BatchOpType::Put, as_span(count_key),
                           std::span<const std::byte>(encoded_count.data(), encoded_count.size())};
    }

    auto batch_result = backend_->batch(std::span<const state::BatchOp>(ops.data(), op_count));
    if (batch_result.is_err()) {
        return LinkError::StorageError;
    }

    if (inverse_def != nullptr) {
        auto inverse_result = remove_link_internal(tgt, link_def->target_type, inverse_def->name,
                                                   src, cascade, false);
        if (inverse_result.is_err() && inverse_result.error() != LinkError::LinkInstanceNotFound) {
            const auto rollback_count = encode_u64_le(current_count);
            std::array<state::BatchOp, 3> rollback_ops{
                state::BatchOp{state::BatchOpType::Put, as_span(forward_key), empty_value},
                state::BatchOp{state::BatchOpType::Put, as_span(inverse_key), empty_value},
                state::BatchOp{
                    state::BatchOpType::Put, as_span(count_key),
                    std::span<const std::byte>(rollback_count.data(), rollback_count.size())}};
            auto rollback_result = backend_->batch(rollback_ops);
            if (rollback_result.is_err()) {
                return LinkError::StorageError;
            }
            return inverse_result.error();
        }
    }

    return {};
}

LinkManager::Result<std::uint64_t> LinkManager::get_link_count(ObjectId src,
                                                               std::string_view link_name) const {
    if (backend_ == nullptr) {
        return LinkError::StorageError;
    }

    const auto count_key = LinkKeyEncoder::encode_count_key(src, link_name);
    auto result = backend_->get(as_span(count_key));
    if (result.is_err()) {
        if (result.error() == state::StateBackendError::KeyNotFound) {
            return std::uint64_t{0};
        }
        return LinkError::StorageError;
    }

    const auto& bytes = result.value();
    if (bytes.size() != kCountSize) {
        return LinkError::IndexCorruption;
    }

    std::span<const std::byte, kCountSize> view{bytes.data(), kCountSize};
    const auto decoded = decode_u64_le(view);
    return decoded;
}

LinkManager::Result<void> LinkManager::set_link_count(ObjectId src, std::string_view link_name,
                                                      std::uint64_t count) const {
    if (backend_ == nullptr) {
        return LinkError::StorageError;
    }

    const auto count_key = LinkKeyEncoder::encode_count_key(src, link_name);
    const auto encoded = encode_u64_le(count);
    auto result = backend_->put(as_span(count_key),
                                std::span<const std::byte>(encoded.data(), encoded.size()));
    if (result.is_err()) {
        return LinkError::StorageError;
    }

    return {};
}

}  // namespace dotvm::core::link
