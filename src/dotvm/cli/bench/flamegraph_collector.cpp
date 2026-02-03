/// @file flamegraph_collector.cpp
/// @brief Implementation of stack sampling for flamegraphs

#include "dotvm/cli/bench/flamegraph_collector.hpp"

#include <fstream>
#include <sstream>

namespace dotvm::cli::bench {

void FlameGraphCollector::record_stack(const std::vector<std::size_t>& stack) {
    ++stack_counts_[stack];
    ++total_samples_;
}

void FlameGraphCollector::set_symbol_resolver(SymbolResolver resolver) {
    symbol_resolver_ = std::move(resolver);
}

void FlameGraphCollector::write_folded_stacks(std::ostream& out) const {
    for (const auto& [stack, count] : stack_counts_) {
        out << format_stack(stack) << " " << count << "\n";
    }
}

bool FlameGraphCollector::write_to_file(const std::string& path) const {
    std::ofstream out(path);
    if (!out) {
        return false;
    }

    write_folded_stacks(out);
    return out.good();
}

void FlameGraphCollector::clear() {
    stack_counts_.clear();
    total_samples_ = 0;
}

std::string FlameGraphCollector::format_stack(const std::vector<std::size_t>& stack) const {
    if (stack.empty()) {
        return "[root]";
    }

    std::ostringstream oss;
    bool first = true;

    // Format from bottom of stack (first element) to top (last element)
    for (std::size_t pc : stack) {
        if (!first) {
            oss << ";";
        }
        first = false;

        if (symbol_resolver_) {
            oss << symbol_resolver_(pc);
        } else {
            oss << default_pc_format(pc);
        }
    }

    return oss.str();
}

std::string FlameGraphCollector::default_pc_format(std::size_t pc) {
    return "pc:" + std::to_string(pc);
}

}  // namespace dotvm::cli::bench
