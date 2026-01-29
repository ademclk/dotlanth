/// @file query_optimizer.cpp
/// @brief STATE-010 Query optimizer implementation

#include "dotvm/core/state/query_optimizer.hpp"

#include <algorithm>

#include "dotvm/core/state/state_backend.hpp"

namespace dotvm::core::state {

// ============================================================================
// Helper: Add non-scan operators from query (forward declaration)
// ============================================================================

namespace {

void add_query_operators(ExecutionPlan& plan, const Query& query) {
    // Walk the query AST and add non-scan, non-filter operators
    const QueryNode* current = query.root.get();

    // Collect operators in reverse order (root to scan)
    std::vector<const QueryNode*> nodes;
    while (current) {
        nodes.push_back(current);
        current = current->input();
    }

    // Process in reverse (scan to root), skipping scan and filter (handled separately)
    for (auto it = nodes.rbegin(); it != nodes.rend(); ++it) {
        const QueryNode* node = *it;

        std::visit(
            [&plan](const auto& n) {
                using T = std::decay_t<decltype(n)>;

                if constexpr (std::is_same_v<T, ProjectNode>) {
                    plan.operators.push_back(
                        ProjectOp{.include_key = n.include_key, .include_value = n.include_value});
                } else if constexpr (std::is_same_v<T, AggregateNode>) {
                    plan.operators.push_back(AggregateOp{n.func});
                } else if constexpr (std::is_same_v<T, LimitNode>) {
                    plan.operators.push_back(LimitOp{n.count});
                }
                // Skip ScanNode and FilterNode - handled in generate_plans
            },
            node->node);
    }
}

}  // namespace

// ============================================================================
// QueryOptimizer Implementation
// ============================================================================

QueryOptimizer::QueryOptimizer(const StateBackend& backend, QueryOptimizerConfig config)
    : backend_{backend},
      config_{std::move(config)},
      stats_collector_{backend, config_.stats_config},
      cost_model_{config_.cost_config},
      executor_{backend} {}

QueryOptimizer::Result<ExecutionPlan> QueryOptimizer::optimize(const Query& query) {
    // Validate query
    if (!query.root) {
        return QueryOptimizerError::EmptyQuery;
    }

    // Get or create statistics
    auto prefix = extract_prefix(query);
    const auto* stats = stats_collector_.get_cached(prefix);

    ScopeStatistics default_stats;
    if (!stats) {
        if (config_.enable_statistics) {
            // Try to collect stats
            auto collect_result = stats_collector_.collect(prefix);
            if (collect_result.is_ok()) {
                stats = stats_collector_.get_cached(prefix);
            }
        }
        if (!stats) {
            default_stats = create_default_stats();
            stats = &default_stats;
        }
    }

    // Generate candidate plans
    auto candidates = generate_plans(query);
    if (candidates.empty()) {
        return QueryOptimizerError::PlanGenerationFailed;
    }

    // Select best plan
    return select_best_plan(candidates, *stats);
}

QueryOptimizer::Result<void> QueryOptimizer::execute(const Query& query,
                                                     const IterateCallback& callback) {
    auto plan_result = optimize(query);
    if (plan_result.is_err()) {
        return plan_result.error();
    }

    return executor_.execute(plan_result.value(), callback);
}

QueryOptimizer::Result<void> QueryOptimizer::analyze(std::span<const std::byte> prefix) {
    auto result = stats_collector_.collect(prefix);
    if (result.is_err()) {
        return result.error();
    }
    return {};
}

const ScopeStatistics*
QueryOptimizer::statistics(std::span<const std::byte> prefix) const noexcept {
    return stats_collector_.get_cached(prefix);
}

void QueryOptimizer::invalidate_statistics(std::span<const std::byte> prefix) {
    stats_collector_.invalidate(prefix);
}

// ============================================================================
// Plan Generation
// ============================================================================

std::vector<ExecutionPlan> QueryOptimizer::generate_plans(const Query& query) {
    std::vector<ExecutionPlan> plans;

    // Collect predicates from the query
    auto predicates = query.collect_predicates();

    // Separate pushable and non-pushable predicates
    std::vector<Predicate> pushable;
    std::vector<Predicate> remaining;

    for (const auto& pred : predicates) {
        if (is_pushable_to_scan(pred)) {
            pushable.push_back(pred);
        } else {
            remaining.push_back(pred);
        }
    }

    // Extract explicit prefix from query
    std::vector<std::byte> explicit_prefix;
    if (const auto* scan = std::get_if<ScanNode>(&query.root->node)) {
        explicit_prefix = scan->prefix;
    } else {
        // Walk to find scan node
        const QueryNode* current = query.root.get();
        while (current) {
            if (const auto* scan = std::get_if<ScanNode>(&current->node)) {
                explicit_prefix = scan->prefix;
                break;
            }
            current = current->input();
        }
    }

    // Plan 1: Use explicit prefix (or full scan if none)
    {
        ExecutionPlan plan;
        if (!explicit_prefix.empty()) {
            plan.operators.push_back(PrefixScanOp{explicit_prefix});
        } else {
            plan.operators.push_back(FullScanOp{});
        }

        // Add all predicates as filter
        if (!predicates.empty()) {
            plan.operators.push_back(FilterOp{predicates});
        }

        // Add other operators from query
        add_query_operators(plan, query);
        plans.push_back(std::move(plan));
    }

    // Plan 2: If we have a prefix predicate, push it to scan
    for (const auto& pred : pushable) {
        if (pred.op == PredicateOp::Prefix && !pred.operand.empty()) {
            ExecutionPlan plan;
            plan.operators.push_back(PrefixScanOp{pred.operand});

            // Filter with remaining predicates only
            std::vector<Predicate> filter_preds;
            for (const auto& p : predicates) {
                if (&p != &pred) {  // Skip the pushed predicate
                    filter_preds.push_back(p);
                }
            }
            if (!filter_preds.empty()) {
                plan.operators.push_back(FilterOp{filter_preds});
            }

            add_query_operators(plan, query);
            plans.push_back(std::move(plan));
        }
    }

    // Plan 3: Full scan (baseline)
    if (!explicit_prefix.empty()) {
        ExecutionPlan plan;
        plan.operators.push_back(FullScanOp{});

        if (!predicates.empty()) {
            plan.operators.push_back(FilterOp{predicates});
        }

        add_query_operators(plan, query);
        plans.push_back(std::move(plan));
    }

    return plans;
}

ExecutionPlan QueryOptimizer::select_best_plan(std::vector<ExecutionPlan>& candidates,
                                               const ScopeStatistics& stats) {
    if (candidates.empty()) {
        return {};
    }

    // Estimate cost for each candidate
    for (auto& plan : candidates) {
        plan.total_cost = cost_model_.estimate(plan, stats);
        plan.estimated_output_rows = stats.key_count;  // Will be refined by operators
    }

    // Sort by total cost
    std::sort(
        candidates.begin(), candidates.end(),
        [](const ExecutionPlan& a, const ExecutionPlan& b) { return a.total_cost < b.total_cost; });

    return std::move(candidates.front());
}

Operator QueryOptimizer::choose_scan_method(const Query& query, const ScopeStatistics& stats) {
    auto prefix = extract_prefix(query);

    if (prefix.empty()) {
        return FullScanOp{};
    }

    // Check selectivity
    if (config_.enable_statistics && stats.key_count > 0) {
        Predicate prefix_pred{PredicateOp::Prefix, prefix};
        double selectivity = cost_model_.estimate_selectivity(stats, prefix_pred);

        if (selectivity < config_.prefix_selectivity_threshold) {
            return PrefixScanOp{prefix};
        }
    }

    // Default to prefix scan if we have a prefix
    if (config_.prefer_prefix_scan) {
        return PrefixScanOp{prefix};
    }

    return FullScanOp{};
}

std::vector<std::byte> QueryOptimizer::extract_prefix(const Query& query) const {
    if (!query.root) {
        return {};
    }

    // Check for scan node prefix
    const QueryNode* current = query.root.get();
    while (current) {
        if (const auto* scan = std::get_if<ScanNode>(&current->node)) {
            return scan->prefix;
        }
        current = current->input();
    }

    // Check for prefix predicate in filters
    auto predicates = query.collect_predicates();
    for (const auto& pred : predicates) {
        if (pred.op == PredicateOp::Prefix) {
            return pred.operand;
        }
    }

    return {};
}

bool QueryOptimizer::is_pushable_to_scan(const Predicate& pred) const noexcept {
    // Prefix predicates can always be pushed
    return pred.op == PredicateOp::Prefix;
}

ScopeStatistics QueryOptimizer::create_default_stats() const {
    ScopeStatistics stats;
    stats.key_count = backend_.key_count();
    stats.total_key_bytes = stats.key_count * 32;     // Assume 32 byte avg key
    stats.total_value_bytes = stats.key_count * 256;  // Assume 256 byte avg value
    stats.collected_at = std::chrono::steady_clock::now();
    return stats;
}

}  // namespace dotvm::core::state
