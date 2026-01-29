/// @file cost_model.cpp
/// @brief STATE-010 Cost model implementation

#include "dotvm/core/state/cost_model.hpp"

#include <algorithm>
#include <cmath>

namespace dotvm::core::state {

namespace {

/// @brief Compare two byte spans lexicographically
[[nodiscard]] int compare_bytes(std::span<const std::byte> a,
                                 std::span<const std::byte> b) noexcept {
    const auto min_len = std::min(a.size(), b.size());
    for (std::size_t i = 0; i < min_len; ++i) {
        const auto av = static_cast<unsigned char>(a[i]);
        const auto bv = static_cast<unsigned char>(b[i]);
        if (av < bv) return -1;
        if (av > bv) return 1;
    }
    if (a.size() < b.size()) return -1;
    if (a.size() > b.size()) return 1;
    return 0;
}

}  // namespace

// ============================================================================
// CostModel Implementation
// ============================================================================

PlanCost CostModel::estimate(const ExecutionPlan& plan,
                              const ScopeStatistics& stats) const noexcept {
    PlanCost total;
    std::size_t current_rows = stats.key_count;

    for (const auto& op : plan.operators) {
        PlanCost op_cost = estimate_operator(op, stats, current_rows);
        total += op_cost;

        // Update row estimate after operator
        std::visit([&current_rows, &stats, this](const auto& o) {
            using T = std::decay_t<decltype(o)>;

            if constexpr (std::is_same_v<T, PrefixScanOp>) {
                // Prefix scan reduces rows based on selectivity
                Predicate prefix_pred{PredicateOp::Prefix, o.prefix};
                double sel = estimate_selectivity(stats, prefix_pred);
                current_rows = estimate_cardinality(stats, sel);
            } else if constexpr (std::is_same_v<T, FilterOp>) {
                // Filter reduces rows based on combined selectivity
                double combined_sel = 1.0;
                for (const auto& pred : o.predicates) {
                    combined_sel *= estimate_selectivity(stats, pred);
                }
                current_rows = static_cast<std::size_t>(
                    static_cast<double>(current_rows) * combined_sel);
            } else if constexpr (std::is_same_v<T, AggregateOp>) {
                // Aggregate produces single row
                current_rows = 1;
            } else if constexpr (std::is_same_v<T, LimitOp>) {
                // Limit caps rows
                current_rows = std::min(current_rows, o.count);
            }
            // FullScanOp, ProjectOp don't change row count
        }, op);
    }

    return total;
}

double CostModel::estimate_selectivity(const ScopeStatistics& stats,
                                        const Predicate& pred) const noexcept {
    // Use histogram if available
    if (!stats.histogram.empty()) {
        return histogram_selectivity(stats, pred);
    }

    // Fall back to default estimates
    return default_selectivity(pred);
}

std::size_t CostModel::estimate_cardinality(const ScopeStatistics& stats,
                                             double selectivity) const noexcept {
    return static_cast<std::size_t>(
        std::ceil(static_cast<double>(stats.key_count) * selectivity));
}

PlanCost CostModel::estimate_operator(const Operator& op,
                                       const ScopeStatistics& stats,
                                       std::size_t input_rows) const noexcept {
    return std::visit([&](const auto& o) -> PlanCost {
        using T = std::decay_t<decltype(o)>;

        if constexpr (std::is_same_v<T, FullScanOp>) {
            // Full scan: setup + scan all keys
            return {
                .io_cost = config_.full_scan_setup_cost +
                           static_cast<double>(stats.key_count) * config_.cost_per_key_scan,
                .cpu_cost = 0.0,
                .memory_cost = 0.0
            };
        } else if constexpr (std::is_same_v<T, PrefixScanOp>) {
            // Prefix scan: reduced I/O based on selectivity
            Predicate prefix_pred{PredicateOp::Prefix, o.prefix};
            double sel = estimate_selectivity(stats, prefix_pred);
            std::size_t estimated_keys = estimate_cardinality(stats, sel);

            return {
                .io_cost = config_.prefix_scan_setup_cost +
                           static_cast<double>(estimated_keys) * config_.cost_per_key_scan,
                .cpu_cost = 0.0,
                .memory_cost = 0.0
            };
        } else if constexpr (std::is_same_v<T, FilterOp>) {
            // Filter: CPU cost for evaluating predicates on each row
            return {
                .io_cost = 0.0,
                .cpu_cost = static_cast<double>(input_rows) *
                            static_cast<double>(o.predicates.size()) *
                            config_.cost_per_predicate_eval,
                .memory_cost = 0.0
            };
        } else if constexpr (std::is_same_v<T, ProjectOp>) {
            // Project: minimal CPU cost
            return {
                .io_cost = 0.0,
                .cpu_cost = static_cast<double>(input_rows) * 0.001,
                .memory_cost = 0.0
            };
        } else if constexpr (std::is_same_v<T, AggregateOp>) {
            // Aggregate: CPU cost for processing each row
            return {
                .io_cost = 0.0,
                .cpu_cost = static_cast<double>(input_rows) * config_.aggregate_cost_factor,
                .memory_cost = config_.memory_cost_per_row  // Single accumulator
            };
        } else if constexpr (std::is_same_v<T, LimitOp>) {
            // Limit: allows early termination, reducing effective scan
            // Cost model reflects expected savings
            if (input_rows > 0 && o.count < input_rows) {
                double fraction = static_cast<double>(o.count) / static_cast<double>(input_rows);
                return {
                    .io_cost = -config_.full_scan_setup_cost * (1.0 - fraction) * 0.5,
                    .cpu_cost = 0.0,
                    .memory_cost = 0.0
                };
            }
            return {};
        }
    }, op);
}

std::size_t CostModel::find_bucket(const ScopeStatistics& stats,
                                    std::span<const std::byte> key) const noexcept {
    if (stats.histogram.empty()) {
        return 0;
    }

    // Binary search for bucket containing key
    for (std::size_t i = 0; i < stats.histogram.size(); ++i) {
        if (compare_bytes(key, stats.histogram[i].upper_bound) <= 0) {
            return i;
        }
    }
    return stats.histogram.size() - 1;
}

double CostModel::histogram_selectivity(const ScopeStatistics& stats,
                                          const Predicate& pred) const noexcept {
    if (stats.histogram.empty() || stats.key_count == 0) {
        return default_selectivity(pred);
    }

    const auto& hist = stats.histogram;
    const double total_keys = static_cast<double>(stats.key_count);

    switch (pred.op) {
        case PredicateOp::Eq: {
            // Equality: 1 / distinct_count in the relevant bucket
            std::size_t bucket_idx = find_bucket(stats, pred.operand);
            if (bucket_idx < hist.size() && hist[bucket_idx].distinct_count > 0) {
                return 1.0 / static_cast<double>(hist[bucket_idx].distinct_count);
            }
            return 1.0 / total_keys;  // Assume uniform
        }

        case PredicateOp::Lt:
        case PredicateOp::Le: {
            // Less than: sum of buckets below the key
            std::size_t bucket_idx = find_bucket(stats, pred.operand);
            double matching_keys = 0.0;

            for (std::size_t i = 0; i < bucket_idx; ++i) {
                matching_keys += static_cast<double>(hist[i].count);
            }

            // Partial bucket interpolation
            if (bucket_idx < hist.size()) {
                // Assume uniform distribution within bucket
                double bucket_fraction = 0.5;  // Conservative estimate
                if (pred.op == PredicateOp::Le) {
                    bucket_fraction = 0.6;  // Include boundary
                }
                matching_keys += static_cast<double>(hist[bucket_idx].count) * bucket_fraction;
            }

            return std::clamp(matching_keys / total_keys, 0.0, 1.0);
        }

        case PredicateOp::Gt:
        case PredicateOp::Ge: {
            // Greater than: 1 - selectivity of complement
            Predicate complement;
            complement.operand = pred.operand;
            complement.op = (pred.op == PredicateOp::Gt) ? PredicateOp::Le : PredicateOp::Lt;
            return 1.0 - histogram_selectivity(stats, complement);
        }

        case PredicateOp::Prefix: {
            // Prefix: estimate based on prefix length vs average key length
            if (pred.operand.empty()) {
                return 1.0;  // Empty prefix matches all
            }

            // Heuristic: shorter prefixes match more keys
            double avg_key_len = stats.avg_key_size();
            if (avg_key_len <= 0) {
                return 0.1;  // Default for unknown
            }

            double prefix_len = static_cast<double>(pred.operand.size());
            // Exponential decay based on prefix length
            double selectivity = std::exp(-prefix_len / avg_key_len * 2.0);
            return std::clamp(selectivity, 0.001, 1.0);
        }
    }

    return default_selectivity(pred);
}

double CostModel::default_selectivity(const Predicate& pred) const noexcept {
    // Default estimates when no histogram available
    switch (pred.op) {
        case PredicateOp::Eq:
            return 0.01;  // 1% for equality (assumes high cardinality)
        case PredicateOp::Lt:
        case PredicateOp::Le:
        case PredicateOp::Gt:
        case PredicateOp::Ge:
            return 0.33;  // 33% for range (assumes some selectivity)
        case PredicateOp::Prefix:
            return 0.1;   // 10% for prefix
    }
    return 0.5;
}

}  // namespace dotvm::core::state
