/// @file baseline_manager.cpp
/// @brief Implementation of baseline management for benchmark comparison

#include "dotvm/cli/bench/baseline_manager.hpp"

#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace dotvm::cli::bench {

namespace {

/// @brief Simple JSON parser for baseline files
/// This is a minimal JSON parser sufficient for our baseline format
class SimpleJsonParser {
public:
    explicit SimpleJsonParser(const std::string& content) : content_(content), pos_(0) {}

    bool parse(Baseline& baseline) {
        skip_whitespace();
        if (!expect('{'))
            return false;

        while (pos_ < content_.size()) {
            skip_whitespace();
            if (peek() == '}') {
                ++pos_;
                return true;
            }

            std::string key = parse_string();
            if (key.empty())
                return false;

            skip_whitespace();
            if (!expect(':'))
                return false;
            skip_whitespace();

            if (key == "version") {
                baseline.version = parse_string();
            } else if (key == "file") {
                baseline.file = parse_string();
            } else if (key == "timestamp") {
                baseline.timestamp = parse_number_u64();
            } else if (key == "mean_ns") {
                baseline.stats.mean_ns = parse_number_double();
            } else if (key == "median_ns") {
                baseline.stats.median_ns = parse_number_double();
            } else if (key == "stddev_ns") {
                baseline.stats.stddev_ns = parse_number_double();
            } else if (key == "min_ns") {
                baseline.stats.min_ns = parse_number_double();
            } else if (key == "max_ns") {
                baseline.stats.max_ns = parse_number_double();
            } else if (key == "p50_ns") {
                baseline.stats.p50_ns = parse_number_double();
            } else if (key == "p90_ns") {
                baseline.stats.p90_ns = parse_number_double();
            } else if (key == "p95_ns") {
                baseline.stats.p95_ns = parse_number_double();
            } else if (key == "p99_ns") {
                baseline.stats.p99_ns = parse_number_double();
            } else if (key == "instructions_per_run") {
                baseline.stats.instructions_per_run = parse_number_u64();
            } else if (key == "total_cycles") {
                baseline.stats.total_cycles = parse_number_u64();
            } else if (key == "sample_count") {
                baseline.stats.sample_count = static_cast<std::size_t>(parse_number_u64());
            } else {
                // Skip unknown field
                skip_value();
            }

            skip_whitespace();
            if (peek() == ',') {
                ++pos_;
            }
        }

        return false;  // Unexpected end
    }

private:
    void skip_whitespace() {
        while (pos_ < content_.size() && std::isspace(content_[pos_])) {
            ++pos_;
        }
    }

    char peek() const { return pos_ < content_.size() ? content_[pos_] : '\0'; }

    bool expect(char c) {
        if (peek() == c) {
            ++pos_;
            return true;
        }
        return false;
    }

    std::string parse_string() {
        if (!expect('"'))
            return "";
        std::string result;
        while (pos_ < content_.size() && content_[pos_] != '"') {
            if (content_[pos_] == '\\' && pos_ + 1 < content_.size()) {
                ++pos_;
                result += content_[pos_];
            } else {
                result += content_[pos_];
            }
            ++pos_;
        }
        expect('"');
        return result;
    }

    double parse_number_double() {
        std::string num_str;
        while (pos_ < content_.size() &&
               (std::isdigit(content_[pos_]) || content_[pos_] == '.' || content_[pos_] == '-' ||
                content_[pos_] == 'e' || content_[pos_] == 'E' || content_[pos_] == '+')) {
            num_str += content_[pos_];
            ++pos_;
        }
        return std::stod(num_str);
    }

    std::uint64_t parse_number_u64() {
        std::string num_str;
        while (pos_ < content_.size() && std::isdigit(content_[pos_])) {
            num_str += content_[pos_];
            ++pos_;
        }
        return std::stoull(num_str);
    }

    void skip_value() {
        skip_whitespace();
        char c = peek();
        if (c == '"') {
            parse_string();
        } else if (c == '{') {
            int depth = 1;
            ++pos_;
            while (pos_ < content_.size() && depth > 0) {
                if (content_[pos_] == '{')
                    ++depth;
                else if (content_[pos_] == '}')
                    --depth;
                ++pos_;
            }
        } else if (c == '[') {
            int depth = 1;
            ++pos_;
            while (pos_ < content_.size() && depth > 0) {
                if (content_[pos_] == '[')
                    ++depth;
                else if (content_[pos_] == ']')
                    --depth;
                ++pos_;
            }
        } else {
            // Number or boolean
            while (pos_ < content_.size() && content_[pos_] != ',' && content_[pos_] != '}') {
                ++pos_;
            }
        }
    }

    const std::string& content_;
    std::size_t pos_;
};

}  // namespace

bool BaselineManager::save_baseline(const BenchmarkStatistics& stats, const std::string& path,
                                    const std::string& input_file) {
    std::ofstream out(path);
    if (!out) {
        return false;
    }

    auto now = std::chrono::system_clock::now();
    auto timestamp =
        std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();

    out << std::fixed << std::setprecision(6);
    out << "{\n";
    out << "  \"version\": \"" << kVersion << "\",\n";
    out << "  \"file\": \"" << input_file << "\",\n";
    out << "  \"timestamp\": " << timestamp << ",\n";
    out << "  \"mean_ns\": " << stats.mean_ns << ",\n";
    out << "  \"median_ns\": " << stats.median_ns << ",\n";
    out << "  \"stddev_ns\": " << stats.stddev_ns << ",\n";
    out << "  \"min_ns\": " << stats.min_ns << ",\n";
    out << "  \"max_ns\": " << stats.max_ns << ",\n";
    out << "  \"p50_ns\": " << stats.p50_ns << ",\n";
    out << "  \"p90_ns\": " << stats.p90_ns << ",\n";
    out << "  \"p95_ns\": " << stats.p95_ns << ",\n";
    out << "  \"p99_ns\": " << stats.p99_ns << ",\n";
    out << "  \"instructions_per_run\": " << stats.instructions_per_run << ",\n";
    out << "  \"total_cycles\": " << stats.total_cycles << ",\n";
    out << "  \"sample_count\": " << stats.sample_count << "\n";
    out << "}\n";

    return out.good();
}

std::expected<Baseline, std::string> BaselineManager::load_baseline(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        return std::unexpected("Failed to open baseline file: " + path);
    }

    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

    Baseline baseline{};
    SimpleJsonParser parser(content);

    if (!parser.parse(baseline)) {
        return std::unexpected("Failed to parse baseline JSON");
    }

    return baseline;
}

ComparisonResult BaselineManager::compare(const BenchmarkStatistics& current,
                                          const BenchmarkStatistics& baseline,
                                          double threshold_percent) const {
    ComparisonResult result;
    result.baseline_mean_ns = baseline.mean_ns;
    result.current_mean_ns = current.mean_ns;
    result.threshold_percent = threshold_percent;

    // Handle edge case of zero baseline
    if (baseline.mean_ns <= 0.0) {
        result.delta_percent = 0.0;
        result.is_regression = false;
        return result;
    }

    // Calculate percentage change
    // Positive = current is slower (regression)
    // Negative = current is faster (improvement)
    result.delta_percent = ((current.mean_ns - baseline.mean_ns) / baseline.mean_ns) * 100.0;

    // Only mark as regression if current is slower AND exceeds threshold
    result.is_regression = result.delta_percent > threshold_percent;

    return result;
}

}  // namespace dotvm::cli::bench
