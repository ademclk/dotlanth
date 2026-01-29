/// @file query_ast.cpp
/// @brief STATE-010 Query AST implementation

#include "dotvm/core/state/query_ast.hpp"

#include <algorithm>

namespace dotvm::core::state {

// ============================================================================
// Predicate Implementation
// ============================================================================

namespace {

/// @brief Compare two byte spans lexicographically
/// @return negative if a < b, 0 if equal, positive if a > b
[[nodiscard]] int compare_bytes(std::span<const std::byte> a,
                                std::span<const std::byte> b) noexcept {
    const auto min_len = std::min(a.size(), b.size());
    for (std::size_t i = 0; i < min_len; ++i) {
        const auto av = static_cast<unsigned char>(a[i]);
        const auto bv = static_cast<unsigned char>(b[i]);
        if (av < bv)
            return -1;
        if (av > bv)
            return 1;
    }
    if (a.size() < b.size())
        return -1;
    if (a.size() > b.size())
        return 1;
    return 0;
}

/// @brief Check if a starts with prefix b
[[nodiscard]] bool starts_with(std::span<const std::byte> a,
                               std::span<const std::byte> b) noexcept {
    if (b.size() > a.size())
        return false;
    for (std::size_t i = 0; i < b.size(); ++i) {
        if (a[i] != b[i])
            return false;
    }
    return true;
}

}  // namespace

bool Predicate::matches(std::span<const std::byte> key) const noexcept {
    const int cmp = compare_bytes(key, operand);

    switch (op) {
        case PredicateOp::Eq:
            return cmp == 0;
        case PredicateOp::Lt:
            return cmp < 0;
        case PredicateOp::Le:
            return cmp <= 0;
        case PredicateOp::Gt:
            return cmp > 0;
        case PredicateOp::Ge:
            return cmp >= 0;
        case PredicateOp::Prefix:
            return starts_with(key, operand);
    }
    return false;
}

// ============================================================================
// RangeBounds Implementation
// ============================================================================

bool RangeBounds::contains(std::span<const std::byte> key) const noexcept {
    // Check lower bound
    if (lower.has_value()) {
        const int cmp = compare_bytes(key, *lower);
        if (lower_inclusive) {
            if (cmp < 0)
                return false;
        } else {
            if (cmp <= 0)
                return false;
        }
    }

    // Check upper bound
    if (upper.has_value()) {
        const int cmp = compare_bytes(key, *upper);
        if (upper_inclusive) {
            if (cmp > 0)
                return false;
        } else {
            if (cmp >= 0)
                return false;
        }
    }

    return true;
}

RangeBounds RangeBounds::from_predicates(const std::vector<Predicate>& predicates) {
    RangeBounds bounds;

    for (const auto& pred : predicates) {
        switch (pred.op) {
            case PredicateOp::Eq:
                // Equality sets both bounds to the same value
                bounds.lower = pred.operand;
                bounds.upper = pred.operand;
                bounds.lower_inclusive = true;
                bounds.upper_inclusive = true;
                return bounds;  // Equality is most restrictive

            case PredicateOp::Gt:
                // key > operand: lower bound, exclusive
                if (!bounds.lower.has_value() || compare_bytes(pred.operand, *bounds.lower) > 0) {
                    bounds.lower = pred.operand;
                    bounds.lower_inclusive = false;
                }
                break;

            case PredicateOp::Ge:
                // key >= operand: lower bound, inclusive
                if (!bounds.lower.has_value() || compare_bytes(pred.operand, *bounds.lower) > 0) {
                    bounds.lower = pred.operand;
                    bounds.lower_inclusive = true;
                }
                break;

            case PredicateOp::Lt:
                // key < operand: upper bound, exclusive
                if (!bounds.upper.has_value() || compare_bytes(pred.operand, *bounds.upper) < 0) {
                    bounds.upper = pred.operand;
                    bounds.upper_inclusive = false;
                }
                break;

            case PredicateOp::Le:
                // key <= operand: upper bound, inclusive
                if (!bounds.upper.has_value() || compare_bytes(pred.operand, *bounds.upper) < 0) {
                    bounds.upper = pred.operand;
                    bounds.upper_inclusive = true;
                }
                break;

            case PredicateOp::Prefix: {
                // Prefix sets lower to prefix, upper to prefix with last byte incremented
                bounds.lower = pred.operand;
                bounds.lower_inclusive = true;

                // Calculate upper bound: increment last byte of prefix
                if (!pred.operand.empty()) {
                    bounds.upper = pred.operand;
                    // Increment last byte (handle overflow conceptually)
                    auto& last = bounds.upper->back();
                    last = static_cast<std::byte>(static_cast<unsigned char>(last) + 1);
                    bounds.upper_inclusive = false;
                }
                break;
            }
        }
    }

    return bounds;
}

// ============================================================================
// QueryNode Implementation
// ============================================================================

std::unique_ptr<QueryNode> QueryNode::clone() const {
    auto result = std::make_unique<QueryNode>();

    std::visit(
        [&result](const auto& n) {
            using T = std::decay_t<decltype(n)>;

            if constexpr (std::is_same_v<T, ScanNode>) {
                result->node = ScanNode{n.prefix};
            } else if constexpr (std::is_same_v<T, FilterNode>) {
                FilterNode cloned;
                cloned.predicates = n.predicates;
                if (n.input) {
                    cloned.input = n.input->clone();
                }
                result->node = std::move(cloned);
            } else if constexpr (std::is_same_v<T, ProjectNode>) {
                ProjectNode cloned;
                cloned.include_key = n.include_key;
                cloned.include_value = n.include_value;
                if (n.input) {
                    cloned.input = n.input->clone();
                }
                result->node = std::move(cloned);
            } else if constexpr (std::is_same_v<T, AggregateNode>) {
                AggregateNode cloned;
                cloned.func = n.func;
                if (n.input) {
                    cloned.input = n.input->clone();
                }
                result->node = std::move(cloned);
            } else if constexpr (std::is_same_v<T, LimitNode>) {
                LimitNode cloned;
                cloned.count = n.count;
                if (n.input) {
                    cloned.input = n.input->clone();
                }
                result->node = std::move(cloned);
            }
        },
        node);

    return result;
}

QueryNode* QueryNode::input() noexcept {
    return std::visit(
        [](auto& n) -> QueryNode* {
            using T = std::decay_t<decltype(n)>;

            if constexpr (std::is_same_v<T, ScanNode>) {
                return nullptr;
            } else {
                return n.input.get();
            }
        },
        node);
}

const QueryNode* QueryNode::input() const noexcept {
    return std::visit(
        [](const auto& n) -> const QueryNode* {
            using T = std::decay_t<decltype(n)>;

            if constexpr (std::is_same_v<T, ScanNode>) {
                return nullptr;
            } else {
                return n.input.get();
            }
        },
        node);
}

// ============================================================================
// Query Implementation
// ============================================================================

Query Query::clone() const {
    Query result;
    if (root) {
        result.root = root->clone();
    }
    return result;
}

std::vector<Predicate> Query::collect_predicates() const {
    std::vector<Predicate> result;

    // Traverse the AST and collect predicates from filter nodes
    const QueryNode* current = root.get();
    while (current != nullptr) {
        if (const auto* filter = std::get_if<FilterNode>(&current->node)) {
            result.insert(result.end(), filter->predicates.begin(), filter->predicates.end());
            current = filter->input.get();
        } else if (const auto* project = std::get_if<ProjectNode>(&current->node)) {
            current = project->input.get();
        } else if (const auto* agg = std::get_if<AggregateNode>(&current->node)) {
            current = agg->input.get();
        } else if (const auto* lim = std::get_if<LimitNode>(&current->node)) {
            current = lim->input.get();
        } else {
            // ScanNode has no input
            current = nullptr;
        }
    }

    return result;
}

// ============================================================================
// Query::Builder Implementation
// ============================================================================

Query::Builder& Query::Builder::scan(std::vector<std::byte> prefix) {
    auto scan_node = std::make_unique<QueryNode>();
    scan_node->node = ScanNode{std::move(prefix)};
    root_ = std::move(scan_node);
    return *this;
}

Query::Builder& Query::Builder::filter(PredicateOp op, std::vector<std::byte> operand) {
    pending_filters_.push_back(Predicate{op, std::move(operand)});
    return *this;
}

Query::Builder& Query::Builder::project(bool include_key, bool include_value) {
    // First, flush any pending filters
    if (!pending_filters_.empty() && root_) {
        auto filter_node = std::make_unique<QueryNode>();
        FilterNode fn;
        fn.predicates = std::move(pending_filters_);
        fn.input = std::move(root_);
        filter_node->node = std::move(fn);
        root_ = std::move(filter_node);
        pending_filters_.clear();
    }

    auto project_node = std::make_unique<QueryNode>();
    ProjectNode pn;
    pn.include_key = include_key;
    pn.include_value = include_value;
    pn.input = std::move(root_);
    project_node->node = std::move(pn);
    root_ = std::move(project_node);
    return *this;
}

Query::Builder& Query::Builder::aggregate(AggregateFunc func) {
    // First, flush any pending filters
    if (!pending_filters_.empty() && root_) {
        auto filter_node = std::make_unique<QueryNode>();
        FilterNode fn;
        fn.predicates = std::move(pending_filters_);
        fn.input = std::move(root_);
        filter_node->node = std::move(fn);
        root_ = std::move(filter_node);
        pending_filters_.clear();
    }

    auto agg_node = std::make_unique<QueryNode>();
    AggregateNode an;
    an.func = func;
    an.input = std::move(root_);
    agg_node->node = std::move(an);
    root_ = std::move(agg_node);
    return *this;
}

Query::Builder& Query::Builder::limit(std::size_t count) {
    // First, flush any pending filters
    if (!pending_filters_.empty() && root_) {
        auto filter_node = std::make_unique<QueryNode>();
        FilterNode fn;
        fn.predicates = std::move(pending_filters_);
        fn.input = std::move(root_);
        filter_node->node = std::move(fn);
        root_ = std::move(filter_node);
        pending_filters_.clear();
    }

    auto limit_node = std::make_unique<QueryNode>();
    LimitNode ln;
    ln.count = count;
    ln.input = std::move(root_);
    limit_node->node = std::move(ln);
    root_ = std::move(limit_node);
    return *this;
}

Query Query::Builder::build() {
    // Flush any remaining pending filters
    if (!pending_filters_.empty() && root_) {
        auto filter_node = std::make_unique<QueryNode>();
        FilterNode fn;
        fn.predicates = std::move(pending_filters_);
        fn.input = std::move(root_);
        filter_node->node = std::move(fn);
        root_ = std::move(filter_node);
        pending_filters_.clear();
    }

    Query result;
    result.root = std::move(root_);
    return result;
}

}  // namespace dotvm::core::state
