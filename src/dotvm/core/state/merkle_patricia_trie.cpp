/// @file merkle_patricia_trie.cpp
/// @brief STATE-008 Merkle Patricia Trie implementation
///
/// Implements the core trie operations using a modified Merkle Patricia Trie.
/// Uses incremental hashing for performance.

#include "dotvm/core/state/merkle_patricia_trie.hpp"

#include <unordered_map>

#include "dotvm/core/state/mpt_node.hpp"
#include "dotvm/core/state/mpt_proof.hpp"

namespace dotvm::core::state {

// ============================================================================
// Implementation Details
// ============================================================================

/// @brief Internal trie node representation with children
///
/// Unlike the serializable MptNode types, this node holds pointers
/// to children for efficient traversal during modification.
struct TrieNode {
    enum class Type { Empty, Leaf, Extension, Branch };

    Type type = Type::Empty;
    Nibbles path;                                 // Path for leaf/extension
    std::vector<std::byte> value;                 // Value for leaf/branch
    std::array<std::unique_ptr<TrieNode>, 16> children;  // For branch nodes
    std::unique_ptr<TrieNode> child;              // For extension nodes
    bool dirty = true;                            // Needs rehashing
    Hash256 cached_hash;                          // Cached hash

    TrieNode() = default;

    static std::unique_ptr<TrieNode> make_leaf(Nibbles path, std::vector<std::byte> value) {
        auto node = std::make_unique<TrieNode>();
        node->type = Type::Leaf;
        node->path = std::move(path);
        node->value = std::move(value);
        return node;
    }

    static std::unique_ptr<TrieNode> make_extension(Nibbles path, std::unique_ptr<TrieNode> child) {
        auto node = std::make_unique<TrieNode>();
        node->type = Type::Extension;
        node->path = std::move(path);
        node->child = std::move(child);
        return node;
    }

    static std::unique_ptr<TrieNode> make_branch() {
        auto node = std::make_unique<TrieNode>();
        node->type = Type::Branch;
        return node;
    }
};

/// @brief Deep clone a trie node and its children
[[nodiscard]] std::unique_ptr<TrieNode> clone_node(const TrieNode* node) {
    if (!node) return nullptr;

    auto cloned = std::make_unique<TrieNode>();
    cloned->type = node->type;
    cloned->path = node->path;
    cloned->value = node->value;
    cloned->dirty = node->dirty;
    cloned->cached_hash = node->cached_hash;

    if (node->child) {
        cloned->child = clone_node(node->child.get());
    }

    for (std::size_t i = 0; i < 16; ++i) {
        if (node->children[i]) {
            cloned->children[i] = clone_node(node->children[i].get());
        }
    }

    return cloned;
}

class MerklePatriciaTrie::Impl {
public:
    std::unique_ptr<TrieNode> root_;
    std::size_t size_ = 0;
    InMemoryNodeStore store_;

    // -------------------------------------------------------------------------
    // Insert Operation
    // -------------------------------------------------------------------------

    Result<void> insert(const Nibbles& key, std::span<const std::byte> value) {
        std::vector<std::byte> value_copy(value.begin(), value.end());

        if (!root_) {
            // Empty trie: create leaf directly
            root_ = TrieNode::make_leaf(key, std::move(value_copy));
            ++size_;
            return {};
        }

        bool key_existed = false;
        root_ = insert_recursive(std::move(root_), key, 0, std::move(value_copy), key_existed);

        if (!key_existed) {
            ++size_;
        }

        return {};
    }

    std::unique_ptr<TrieNode> insert_recursive(std::unique_ptr<TrieNode> node, const Nibbles& key,
                                               std::size_t key_pos,
                                               std::vector<std::byte> value, bool& key_existed) {
        if (!node || node->type == TrieNode::Type::Empty) {
            // Create leaf with remaining key
            return TrieNode::make_leaf(key.slice(key_pos, key.size() - key_pos), std::move(value));
        }

        node->dirty = true;

        switch (node->type) {
            case TrieNode::Type::Leaf:
                return insert_at_leaf(std::move(node), key, key_pos, std::move(value), key_existed);

            case TrieNode::Type::Extension:
                return insert_at_extension(std::move(node), key, key_pos, std::move(value),
                                           key_existed);

            case TrieNode::Type::Branch:
                return insert_at_branch(std::move(node), key, key_pos, std::move(value),
                                        key_existed);

            default:
                return node;
        }
    }

    std::unique_ptr<TrieNode> insert_at_leaf(std::unique_ptr<TrieNode> leaf, const Nibbles& key,
                                             std::size_t key_pos, std::vector<std::byte> value,
                                             bool& key_existed) {
        const auto remaining_key = key.slice(key_pos, key.size() - key_pos);
        const auto common_len = leaf->path.common_prefix_length(remaining_key);

        // Exact match: update value
        if (common_len == leaf->path.size() && common_len == remaining_key.size()) {
            leaf->value = std::move(value);
            key_existed = true;
            return leaf;
        }

        // Create branch at divergence point
        auto branch = TrieNode::make_branch();

        // Handle existing leaf path
        if (common_len < leaf->path.size()) {
            const auto leaf_nibble = leaf->path[common_len];
            leaf->path = leaf->path.slice(common_len + 1, leaf->path.size() - common_len - 1);
            leaf->dirty = true;
            branch->children[leaf_nibble] = std::move(leaf);
        } else {
            // Existing leaf terminates at branch
            branch->value = std::move(leaf->value);
        }

        // Handle new key
        if (common_len < remaining_key.size()) {
            const auto new_nibble = remaining_key[common_len];
            branch->children[new_nibble] = TrieNode::make_leaf(
                remaining_key.slice(common_len + 1, remaining_key.size() - common_len - 1),
                std::move(value));
        } else {
            // New key terminates at branch
            branch->value = std::move(value);
        }

        // Add extension if there's a common prefix
        if (common_len > 0) {
            return TrieNode::make_extension(remaining_key.slice(0, common_len), std::move(branch));
        }

        return branch;
    }

    std::unique_ptr<TrieNode> insert_at_extension(std::unique_ptr<TrieNode> ext, const Nibbles& key,
                                                  std::size_t key_pos,
                                                  std::vector<std::byte> value,
                                                  bool& key_existed) {
        const auto remaining_key = key.slice(key_pos, key.size() - key_pos);
        const auto common_len = ext->path.common_prefix_length(remaining_key);

        // Full extension path matches: recurse into child
        if (common_len == ext->path.size()) {
            ext->child = insert_recursive(std::move(ext->child), key, key_pos + common_len,
                                          std::move(value), key_existed);
            return ext;
        }

        // Partial match: split extension
        auto branch = TrieNode::make_branch();

        // Remaining extension goes into branch
        if (common_len + 1 < ext->path.size()) {
            const auto ext_nibble = ext->path[common_len];
            auto new_ext = TrieNode::make_extension(
                ext->path.slice(common_len + 1, ext->path.size() - common_len - 1),
                std::move(ext->child));
            branch->children[ext_nibble] = std::move(new_ext);
        } else {
            // Extension ends at this nibble
            const auto ext_nibble = ext->path[common_len];
            branch->children[ext_nibble] = std::move(ext->child);
        }

        // Insert new key into branch
        if (common_len < remaining_key.size()) {
            const auto new_nibble = remaining_key[common_len];
            branch->children[new_nibble] = TrieNode::make_leaf(
                remaining_key.slice(common_len + 1, remaining_key.size() - common_len - 1),
                std::move(value));
        } else {
            branch->value = std::move(value);
        }

        // Wrap with extension if there's common prefix
        if (common_len > 0) {
            return TrieNode::make_extension(ext->path.slice(0, common_len), std::move(branch));
        }

        return branch;
    }

    std::unique_ptr<TrieNode> insert_at_branch(std::unique_ptr<TrieNode> branch, const Nibbles& key,
                                               std::size_t key_pos, std::vector<std::byte> value,
                                               bool& key_existed) {
        const auto remaining_key = key.slice(key_pos, key.size() - key_pos);

        if (remaining_key.empty()) {
            // Key terminates at this branch
            if (!branch->value.empty()) {
                key_existed = true;
            }
            branch->value = std::move(value);
            return branch;
        }

        // Recurse into appropriate child
        const auto nibble = remaining_key[0];
        branch->children[nibble] =
            insert_recursive(std::move(branch->children[nibble]), key, key_pos + 1,
                             std::move(value), key_existed);
        return branch;
    }

    // -------------------------------------------------------------------------
    // Get Operation
    // -------------------------------------------------------------------------

    Result<std::vector<std::byte>> get(const Nibbles& key) const {
        if (!root_) {
            return MptError::KeyNotFound;
        }

        return get_recursive(root_.get(), key, 0);
    }

    Result<std::vector<std::byte>> get_recursive(const TrieNode* node, const Nibbles& key,
                                                 std::size_t key_pos) const {
        if (!node || node->type == TrieNode::Type::Empty) {
            return MptError::KeyNotFound;
        }

        const auto remaining_key = key.slice(key_pos, key.size() - key_pos);

        switch (node->type) {
            case TrieNode::Type::Leaf: {
                if (node->path == remaining_key) {
                    return node->value;
                }
                return MptError::KeyNotFound;
            }

            case TrieNode::Type::Extension: {
                const auto common_len = node->path.common_prefix_length(remaining_key);
                if (common_len != node->path.size()) {
                    return MptError::KeyNotFound;
                }
                return get_recursive(node->child.get(), key, key_pos + common_len);
            }

            case TrieNode::Type::Branch: {
                if (remaining_key.empty()) {
                    if (node->value.empty()) {
                        return MptError::KeyNotFound;
                    }
                    return node->value;
                }
                const auto nibble = remaining_key[0];
                return get_recursive(node->children[nibble].get(), key, key_pos + 1);
            }

            default:
                return MptError::KeyNotFound;
        }
    }

    // -------------------------------------------------------------------------
    // Remove Operation
    // -------------------------------------------------------------------------

    Result<void> remove(const Nibbles& key) {
        if (!root_) {
            return MptError::KeyNotFound;
        }

        bool found = false;
        root_ = remove_recursive(std::move(root_), key, 0, found);

        if (!found) {
            return MptError::KeyNotFound;
        }

        --size_;
        return {};
    }

    std::unique_ptr<TrieNode> remove_recursive(std::unique_ptr<TrieNode> node, const Nibbles& key,
                                               std::size_t key_pos, bool& found) {
        if (!node || node->type == TrieNode::Type::Empty) {
            return node;
        }

        node->dirty = true;
        const auto remaining_key = key.slice(key_pos, key.size() - key_pos);

        switch (node->type) {
            case TrieNode::Type::Leaf: {
                if (node->path == remaining_key) {
                    found = true;
                    return nullptr;  // Remove leaf
                }
                return node;
            }

            case TrieNode::Type::Extension: {
                const auto common_len = node->path.common_prefix_length(remaining_key);
                if (common_len != node->path.size()) {
                    return node;  // Key doesn't match
                }

                node->child = remove_recursive(std::move(node->child), key, key_pos + common_len,
                                               found);

                if (!found) {
                    return node;
                }

                // Simplify after removal
                return simplify_extension(std::move(node));
            }

            case TrieNode::Type::Branch: {
                if (remaining_key.empty()) {
                    if (node->value.empty()) {
                        return node;  // No value to remove
                    }
                    found = true;
                    node->value.clear();
                } else {
                    const auto nibble = remaining_key[0];
                    node->children[nibble] =
                        remove_recursive(std::move(node->children[nibble]), key, key_pos + 1,
                                         found);
                }

                if (!found) {
                    return node;
                }

                return simplify_branch(std::move(node));
            }

            default:
                return node;
        }
    }

    std::unique_ptr<TrieNode> simplify_extension(std::unique_ptr<TrieNode> ext) {
        if (!ext->child) {
            return nullptr;
        }

        // If child is a leaf or extension, merge paths
        if (ext->child->type == TrieNode::Type::Leaf) {
            // Merge extension path with leaf path
            std::vector<std::uint8_t> merged;
            for (std::size_t i = 0; i < ext->path.size(); ++i) {
                merged.push_back(ext->path[i]);
            }
            for (std::size_t i = 0; i < ext->child->path.size(); ++i) {
                merged.push_back(ext->child->path[i]);
            }

            ext->child->path = nibbles_from_raw(merged);
            return std::move(ext->child);
        }

        if (ext->child->type == TrieNode::Type::Extension) {
            // Merge two extensions
            std::vector<std::uint8_t> merged;
            for (std::size_t i = 0; i < ext->path.size(); ++i) {
                merged.push_back(ext->path[i]);
            }
            for (std::size_t i = 0; i < ext->child->path.size(); ++i) {
                merged.push_back(ext->child->path[i]);
            }

            ext->path = nibbles_from_raw(merged);
            ext->child = std::move(ext->child->child);
            return ext;
        }

        return ext;
    }

    std::unique_ptr<TrieNode> simplify_branch(std::unique_ptr<TrieNode> branch) {
        // Count non-null children
        std::size_t child_count = 0;
        std::size_t last_child_idx = 0;
        bool has_child = false;
        for (std::size_t i = 0; i < 16; ++i) {
            if (branch->children[i]) {
                ++child_count;
                last_child_idx = i;
                has_child = true;
            }
        }

        const bool has_value = !branch->value.empty();

        // Multiple children: stay as branch
        if (child_count > 1 || (child_count == 1 && has_value)) {
            return branch;
        }

        // No children, no value: remove entirely
        if (!has_child && !has_value) {
            return nullptr;
        }

        // Only value: convert to leaf with empty path
        if (!has_child && has_value) {
            return TrieNode::make_leaf(Nibbles(), std::move(branch->value));
        }

        // Single child, no value: convert to extension
        std::vector<std::uint8_t> path = {static_cast<std::uint8_t>(last_child_idx)};
        auto child = std::move(branch->children[last_child_idx]);

        // Merge with child if it's also an extension or leaf
        if (child->type == TrieNode::Type::Leaf) {
            for (std::size_t i = 0; i < child->path.size(); ++i) {
                path.push_back(child->path[i]);
            }
            child->path = nibbles_from_raw(path);
            return child;
        }

        if (child->type == TrieNode::Type::Extension) {
            for (std::size_t i = 0; i < child->path.size(); ++i) {
                path.push_back(child->path[i]);
            }
            child->path = nibbles_from_raw(path);
            return child;
        }

        return TrieNode::make_extension(nibbles_from_raw(path), std::move(child));
    }

    // Helper to create Nibbles from raw vector
    static Nibbles nibbles_from_raw(const std::vector<std::uint8_t>& raw) {
        std::vector<std::byte> bytes;
        for (std::size_t i = 0; i + 1 < raw.size(); i += 2) {
            bytes.push_back(static_cast<std::byte>((raw[i] << 4) | raw[i + 1]));
        }
        if (raw.size() % 2 != 0) {
            bytes.push_back(static_cast<std::byte>(raw.back() << 4));
        }
        Nibbles result(bytes);
        return result.slice(0, raw.size());
    }

    // -------------------------------------------------------------------------
    // Hashing
    // -------------------------------------------------------------------------

    Hash256 compute_root_hash() const {
        if (!root_) {
            return Hash256::zero();
        }

        return hash_node_recursive(root_.get());
    }

    Hash256 hash_node_recursive(TrieNode* node) const {
        if (!node) {
            return Hash256::zero();
        }

        // Use cached hash if not dirty
        if (!node->dirty) {
            return node->cached_hash;
        }

        Hash256 result;

        switch (node->type) {
            case TrieNode::Type::Empty:
                result = Hash256::zero();
                break;

            case TrieNode::Type::Leaf: {
                LeafNode leaf{node->path, node->value};
                result = hash_node(leaf);
                break;
            }

            case TrieNode::Type::Extension: {
                const auto child_hash = hash_node_recursive(node->child.get());
                ExtensionNode ext{node->path, child_hash};
                result = hash_node(ext);
                break;
            }

            case TrieNode::Type::Branch: {
                BranchNode branch;
                for (std::size_t i = 0; i < 16; ++i) {
                    if (node->children[i]) {
                        branch.children[i] = hash_node_recursive(node->children[i].get());
                    }
                }
                branch.value = node->value;
                result = hash_node(branch);
                break;
            }
        }

        node->dirty = false;
        node->cached_hash = result;
        return result;
    }

    // -------------------------------------------------------------------------
    // Proof Generation
    // -------------------------------------------------------------------------

    Result<MptProof> get_proof(const Nibbles& key) const {
        if (!root_) {
            return MptError::KeyNotFound;
        }

        MptProof proof;
        proof.key = key.to_bytes();

        bool found = get_proof_recursive(root_.get(), key, 0, proof);
        if (!found) {
            return MptError::KeyNotFound;
        }

        return proof;
    }

    bool get_proof_recursive(const TrieNode* node, const Nibbles& key, std::size_t key_pos,
                             MptProof& proof) const {
        if (!node || node->type == TrieNode::Type::Empty) {
            return false;
        }

        const auto remaining_key = key.slice(key_pos, key.size() - key_pos);

        switch (node->type) {
            case TrieNode::Type::Leaf: {
                if (node->path == remaining_key) {
                    // Found the key - serialize this leaf and add to proof
                    LeafNode leaf{node->path, node->value};
                    proof.nodes.push_back(serialize_node(leaf));
                    proof.value = node->value;
                    return true;
                }
                return false;
            }

            case TrieNode::Type::Extension: {
                const auto common_len = node->path.common_prefix_length(remaining_key);
                if (common_len != node->path.size()) {
                    return false;
                }

                // Recurse into child first
                if (!get_proof_recursive(node->child.get(), key, key_pos + common_len, proof)) {
                    return false;
                }

                // Add extension node to proof (with child's hash)
                const auto child_hash = hash_node_recursive(const_cast<TrieNode*>(node->child.get()));
                ExtensionNode ext{node->path, child_hash};
                proof.nodes.push_back(serialize_node(ext));
                return true;
            }

            case TrieNode::Type::Branch: {
                if (remaining_key.empty()) {
                    if (node->value.empty()) {
                        return false;
                    }
                    // Key terminates at branch - serialize branch
                    BranchNode branch;
                    for (std::size_t i = 0; i < 16; ++i) {
                        if (node->children[i]) {
                            branch.children[i] =
                                hash_node_recursive(const_cast<TrieNode*>(node->children[i].get()));
                        }
                    }
                    branch.value = node->value;
                    proof.nodes.push_back(serialize_node(branch));
                    proof.value = node->value;
                    return true;
                }

                const auto nibble = remaining_key[0];
                if (!get_proof_recursive(node->children[nibble].get(), key, key_pos + 1, proof)) {
                    return false;
                }

                // Add branch node to proof
                BranchNode branch;
                for (std::size_t i = 0; i < 16; ++i) {
                    if (node->children[i]) {
                        branch.children[i] =
                            hash_node_recursive(const_cast<TrieNode*>(node->children[i].get()));
                    }
                }
                branch.value = node->value;
                proof.nodes.push_back(serialize_node(branch));
                return true;
            }

            default:
                return false;
        }
    }

    Result<MptProof> get_exclusion_proof(const Nibbles& key) const {
        if (!root_) {
            // Empty trie - exclusion is trivially provable
            return MptProof{key.to_bytes(), {}, {}};
        }

        MptProof proof;
        proof.key = key.to_bytes();

        bool key_exists = false;
        get_exclusion_proof_recursive(root_.get(), key, 0, proof, key_exists);

        if (key_exists) {
            // Key actually exists - can't prove exclusion
            return MptError::InvalidProof;
        }

        return proof;
    }

    void get_exclusion_proof_recursive(const TrieNode* node, const Nibbles& key, std::size_t key_pos,
                                       MptProof& proof, bool& key_exists) const {
        if (!node || node->type == TrieNode::Type::Empty) {
            return;  // Path terminates - key doesn't exist
        }

        const auto remaining_key = key.slice(key_pos, key.size() - key_pos);

        switch (node->type) {
            case TrieNode::Type::Leaf: {
                // Add the leaf to proof - it shows where the path diverges
                LeafNode leaf{node->path, node->value};
                proof.nodes.push_back(serialize_node(leaf));

                if (node->path == remaining_key) {
                    key_exists = true;  // Key actually exists
                }
                return;
            }

            case TrieNode::Type::Extension: {
                const auto common_len = node->path.common_prefix_length(remaining_key);

                // Add extension to proof
                const auto child_hash = hash_node_recursive(const_cast<TrieNode*>(node->child.get()));
                ExtensionNode ext{node->path, child_hash};
                proof.nodes.push_back(serialize_node(ext));

                if (common_len != node->path.size()) {
                    // Path diverges at extension - key doesn't exist
                    return;
                }

                // Continue down the path
                get_exclusion_proof_recursive(node->child.get(), key, key_pos + common_len, proof,
                                              key_exists);
                return;
            }

            case TrieNode::Type::Branch: {
                // Add branch to proof
                BranchNode branch;
                for (std::size_t i = 0; i < 16; ++i) {
                    if (node->children[i]) {
                        branch.children[i] =
                            hash_node_recursive(const_cast<TrieNode*>(node->children[i].get()));
                    }
                }
                branch.value = node->value;
                proof.nodes.push_back(serialize_node(branch));

                if (remaining_key.empty()) {
                    if (!node->value.empty()) {
                        key_exists = true;  // Key exists (value at branch)
                    }
                    return;
                }

                const auto nibble = remaining_key[0];
                if (!node->children[nibble]) {
                    // Child doesn't exist - key doesn't exist
                    return;
                }

                // Continue down the path
                get_exclusion_proof_recursive(node->children[nibble].get(), key, key_pos + 1, proof,
                                              key_exists);
                return;
            }

            default:
                return;
        }
    }
};

// ============================================================================
// Public Interface Implementation
// ============================================================================

MerklePatriciaTrie::MerklePatriciaTrie() : impl_(std::make_unique<Impl>()) {}

MerklePatriciaTrie::~MerklePatriciaTrie() = default;

MerklePatriciaTrie::MerklePatriciaTrie(MerklePatriciaTrie&& other) noexcept = default;

MerklePatriciaTrie& MerklePatriciaTrie::operator=(MerklePatriciaTrie&& other) noexcept = default;

Hash256 MerklePatriciaTrie::root_hash() const noexcept {
    return impl_->compute_root_hash();
}

bool MerklePatriciaTrie::empty() const noexcept {
    return impl_->size_ == 0;
}

std::size_t MerklePatriciaTrie::size() const noexcept {
    return impl_->size_;
}

MerklePatriciaTrie::Result<void> MerklePatriciaTrie::insert(Key key, Value value) {
    const Nibbles nibbles(key);
    return impl_->insert(nibbles, value);
}

MerklePatriciaTrie::Result<std::vector<std::byte>> MerklePatriciaTrie::get(Key key) const {
    const Nibbles nibbles(key);
    return impl_->get(nibbles);
}

MerklePatriciaTrie::Result<void> MerklePatriciaTrie::remove(Key key) {
    const Nibbles nibbles(key);
    return impl_->remove(nibbles);
}

bool MerklePatriciaTrie::contains(Key key) const {
    const Nibbles nibbles(key);
    return impl_->get(nibbles).is_ok();
}

void MerklePatriciaTrie::clear() {
    impl_->root_.reset();
    impl_->size_ = 0;
    impl_->store_.clear();
}

MerklePatriciaTrie::Result<MptProof> MerklePatriciaTrie::get_proof(Key key) const {
    const Nibbles nibbles(key);
    return impl_->get_proof(nibbles);
}

MerklePatriciaTrie::Result<MptProof> MerklePatriciaTrie::get_exclusion_proof(Key key) const {
    const Nibbles nibbles(key);
    return impl_->get_exclusion_proof(nibbles);
}

}  // namespace dotvm::core::state
