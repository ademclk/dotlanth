#pragma once

/// @file flamegraph_collector.hpp
/// @brief Stack sampling and folded-stack output for flamegraphs (CLI-005 Benchmark Runner)
///
/// Collects call stack samples during bytecode execution and generates
/// folded-stack format compatible with flamegraph.pl.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace dotvm::cli::bench {

/// @brief Function type for resolving PC values to symbol names
using SymbolResolver = std::function<std::string(std::size_t pc)>;

/// @brief Collects stack samples and generates flamegraph data
///
/// During benchmark execution, call record_stack() at configured intervals
/// to sample the current call stack. After execution, write_folded_stacks()
/// outputs the collected data in a format compatible with flamegraph.pl.
///
/// Folded stack format: "frame1;frame2;frame3 count\n"
/// Each line represents a unique stack trace with the number of times it was sampled.
class FlameGraphCollector {
public:
    FlameGraphCollector() = default;

    /// @brief Record a stack sample
    ///
    /// The stack should contain PC values from innermost to outermost frame.
    /// For example: {current_pc, caller_pc, caller_caller_pc, ...}
    ///
    /// @param stack Vector of program counter values representing the call stack
    void record_stack(const std::vector<std::size_t>& stack);

    /// @brief Set a custom symbol resolver for PC-to-name mapping
    ///
    /// If not set, PCs are formatted as "pc:N".
    ///
    /// @param resolver Function that converts PC to symbol name
    void set_symbol_resolver(SymbolResolver resolver);

    /// @brief Write folded stacks to an output stream
    /// @param out Output stream
    void write_folded_stacks(std::ostream& out) const;

    /// @brief Write folded stacks to a file
    /// @param path Output file path
    /// @return true if written successfully
    [[nodiscard]] bool write_to_file(const std::string& path) const;

    /// @brief Get the total number of samples collected
    [[nodiscard]] std::size_t sample_count() const noexcept { return total_samples_; }

    /// @brief Clear all collected samples
    void clear();

private:
    /// @brief Format a single stack as a folded stack string
    [[nodiscard]] std::string format_stack(const std::vector<std::size_t>& stack) const;

    /// @brief Default PC formatter when no symbol resolver is set
    [[nodiscard]] static std::string default_pc_format(std::size_t pc);

    /// Map of stack traces to their sample counts
    std::map<std::vector<std::size_t>, std::size_t> stack_counts_;

    /// Custom symbol resolver (optional)
    SymbolResolver symbol_resolver_;

    /// Total number of samples
    std::size_t total_samples_ = 0;
};

}  // namespace dotvm::cli::bench
