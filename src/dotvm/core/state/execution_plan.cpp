/// @file execution_plan.cpp
/// @brief STATE-010 Execution plan implementation

#include "dotvm/core/state/execution_plan.hpp"

#include <cstring>
#include <sstream>

#include "dotvm/core/state/state_backend.hpp"

namespace dotvm::core::state {

// ============================================================================
// ExecutionPlan Implementation
// ============================================================================

std::string ExecutionPlan::to_string() const {
    std::ostringstream oss;

    oss << "ExecutionPlan {\n";
    oss << "  operators: [\n";

    for (const auto& op : operators) {
        oss << "    ";
        std::visit([&oss](const auto& o) {
            using T = std::decay_t<decltype(o)>;

            if constexpr (std::is_same_v<T, FullScanOp>) {
                oss << "FullScan";
            } else if constexpr (std::is_same_v<T, PrefixScanOp>) {
                oss << "PrefixScan(prefix_len=" << o.prefix.size() << ")";
            } else if constexpr (std::is_same_v<T, FilterOp>) {
                oss << "Filter(predicates=" << o.predicates.size() << ")";
            } else if constexpr (std::is_same_v<T, ProjectOp>) {
                oss << "Project(key=" << (o.include_key ? "true" : "false")
                    << ", value=" << (o.include_value ? "true" : "false") << ")";
            } else if constexpr (std::is_same_v<T, AggregateOp>) {
                oss << "Aggregate(" << state::to_string(o.func) << ")";
            } else if constexpr (std::is_same_v<T, LimitOp>) {
                oss << "Limit(" << o.count << ")";
            }
        }, op);
        oss << "\n";
    }

    oss << "  ]\n";
    oss << "  cost: " << total_cost.total()
        << " (io=" << total_cost.io_cost
        << ", cpu=" << total_cost.cpu_cost
        << ", mem=" << total_cost.memory_cost << ")\n";
    oss << "  estimated_rows: " << estimated_output_rows << "\n";
    oss << "}\n";

    return oss.str();
}

// ============================================================================
// PlanExecutor Implementation
// ============================================================================

PlanExecutor::PlanExecutor(const StateBackend& backend) : backend_{backend} {}

PlanExecutor::Result<void> PlanExecutor::execute(const ExecutionPlan& plan,
                                                   const IterateCallback& callback) const {
    if (!plan.is_valid()) {
        return QueryOptimizerError::InvalidPlan;
    }

    // Build pipeline state
    struct PipelineState {
        bool include_key{true};
        bool include_value{true};
        std::size_t limit{std::numeric_limits<std::size_t>::max()};
        std::size_t count{0};
        std::vector<const FilterOp*> filters;
        const AggregateOp* aggregate{nullptr};

        // Aggregate state
        std::size_t agg_count{0};
        std::vector<std::byte> agg_min;
        std::vector<std::byte> agg_max;
        double agg_sum{0.0};
    };

    PipelineState state;

    // Process operators to build pipeline
    for (std::size_t i = 1; i < plan.operators.size(); ++i) {
        std::visit([&state](const auto& o) {
            using T = std::decay_t<decltype(o)>;

            if constexpr (std::is_same_v<T, FilterOp>) {
                state.filters.push_back(&o);
            } else if constexpr (std::is_same_v<T, ProjectOp>) {
                state.include_key = o.include_key;
                state.include_value = o.include_value;
            } else if constexpr (std::is_same_v<T, LimitOp>) {
                state.limit = o.count;
            } else if constexpr (std::is_same_v<T, AggregateOp>) {
                state.aggregate = &o;
            }
        }, plan.operators[i]);
    }

    // Determine scan parameters
    std::span<const std::byte> scan_prefix;
    const auto* scan_op = plan.scan_operator();
    if (const auto* prefix_scan = std::get_if<PrefixScanOp>(scan_op)) {
        scan_prefix = prefix_scan->prefix;
    }

    // Execute scan with pipeline processing
    auto iterate_result = backend_.iterate(scan_prefix,
        [&state, &callback](StateBackend::Key key, StateBackend::Value value) -> bool {
            // Check limit
            if (state.count >= state.limit) {
                return false;
            }

            // Apply filters
            for (const auto* filter : state.filters) {
                bool passes = true;
                for (const auto& pred : filter->predicates) {
                    if (!pred.matches(key)) {
                        passes = false;
                        break;
                    }
                }
                if (!passes) {
                    return true;  // Skip this row, continue iteration
                }
            }

            // Handle aggregate
            if (state.aggregate != nullptr) {
                state.agg_count++;

                switch (state.aggregate->func) {
                    case AggregateFunc::Count:
                        // Just count
                        break;
                    case AggregateFunc::Min:
                        if (state.agg_min.empty() ||
                            std::vector<std::byte>(value.begin(), value.end()) < state.agg_min) {
                            state.agg_min.assign(value.begin(), value.end());
                        }
                        break;
                    case AggregateFunc::Max:
                        if (state.agg_max.empty() ||
                            std::vector<std::byte>(value.begin(), value.end()) > state.agg_max) {
                            state.agg_max.assign(value.begin(), value.end());
                        }
                        break;
                    case AggregateFunc::Sum:
                        // Sum requires numeric interpretation - simplified here
                        break;
                }
                return true;  // Continue to process all rows for aggregate
            }

            // Apply projection
            std::span<const std::byte> out_key = state.include_key ? key : std::span<const std::byte>{};
            std::span<const std::byte> out_value = state.include_value ? value : std::span<const std::byte>{};

            // Output row
            state.count++;
            return callback(out_key, out_value);
        });

    // Handle aggregate output
    if (state.aggregate != nullptr) {
        std::string result_str;

        switch (state.aggregate->func) {
            case AggregateFunc::Count:
                result_str = std::to_string(state.agg_count);
                break;
            case AggregateFunc::Min:
                result_str = std::string(
                    reinterpret_cast<const char*>(state.agg_min.data()),
                    state.agg_min.size());
                break;
            case AggregateFunc::Max:
                result_str = std::string(
                    reinterpret_cast<const char*>(state.agg_max.data()),
                    state.agg_max.size());
                break;
            case AggregateFunc::Sum:
                result_str = std::to_string(state.agg_sum);
                break;
        }

        std::vector<std::byte> result_bytes(result_str.size());
        std::memcpy(result_bytes.data(), result_str.data(), result_str.size());

        callback({}, result_bytes);
    }

    // Check iteration result
    if (iterate_result.is_err()) {
        // Ignore IterationAborted - that's expected for limits and early termination
        if (iterate_result.error() != StateBackendError::IterationAborted) {
            return QueryOptimizerError::ExecutionFailed;
        }
    }

    return {};
}

}  // namespace dotvm::core::state
