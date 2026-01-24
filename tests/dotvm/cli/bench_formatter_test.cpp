/// @file bench_formatter_test.cpp
/// @brief TOOL-010 Benchmark formatter unit tests

#include <sstream>

#include <gtest/gtest.h>

#include "dotvm/cli/bench_formatter.hpp"

using namespace dotvm::cli;

// ============================================================================
// BenchmarkResult Construction Tests
// ============================================================================

TEST(BenchmarkResultTest, DefaultConstruction) {
    BenchmarkResult result;
    EXPECT_TRUE(result.name.empty());
    EXPECT_EQ(result.cpu_time_ns, 0.0);
    EXPECT_EQ(result.real_time_ns, 0.0);
    EXPECT_EQ(result.iterations, 0);
    EXPECT_EQ(result.items_per_second, 0.0);
    EXPECT_EQ(result.bytes_per_second, 0.0);
}

TEST(BenchmarkResultTest, PopulatedResult) {
    BenchmarkResult result;
    result.name = "BM_Fibonacci/35";
    result.cpu_time_ns = 1245.0;
    result.real_time_ns = 1250.0;
    result.iterations = 561798;
    result.items_per_second = 803200.0;

    EXPECT_EQ(result.name, "BM_Fibonacci/35");
    EXPECT_DOUBLE_EQ(result.cpu_time_ns, 1245.0);
    EXPECT_EQ(result.iterations, 561798);
}

// ============================================================================
// BenchmarkReport Construction Tests
// ============================================================================

TEST(BenchmarkReportTest, DefaultConstruction) {
    BenchmarkReport report;
    EXPECT_TRUE(report.version.empty());
    EXPECT_TRUE(report.timestamp.empty());
    EXPECT_TRUE(report.results.empty());
}

TEST(BenchmarkReportTest, AddResults) {
    BenchmarkReport report;
    report.version = "0.1.0";
    report.timestamp = "2026-01-23T10:30:00Z";

    BenchmarkResult r1;
    r1.name = "BM_Test1";
    r1.cpu_time_ns = 100.0;
    report.results.push_back(r1);

    BenchmarkResult r2;
    r2.name = "BM_Test2";
    r2.cpu_time_ns = 200.0;
    report.results.push_back(r2);

    EXPECT_EQ(report.results.size(), 2);
    EXPECT_EQ(report.results[0].name, "BM_Test1");
    EXPECT_EQ(report.results[1].name, "BM_Test2");
}

// ============================================================================
// Console Formatter Tests
// ============================================================================

TEST(BenchFormatterTest, FormatConsoleEmpty) {
    BenchmarkReport report;
    report.version = "0.1.0";

    std::ostringstream out;
    format_bench_console(out, report, false);
    std::string output = out.str();

    EXPECT_NE(output.find("DotVM Benchmark Suite"), std::string::npos);
    EXPECT_NE(output.find("0.1.0"), std::string::npos);
}

TEST(BenchFormatterTest, FormatConsoleWithResults) {
    BenchmarkReport report;
    report.version = "0.1.0";

    BenchmarkResult r1;
    r1.name = "BM_Fibonacci/35";
    r1.cpu_time_ns = 1245.0;
    r1.real_time_ns = 1250.0;
    r1.iterations = 561798;
    r1.items_per_second = 803200.0;
    report.results.push_back(r1);

    std::ostringstream out;
    format_bench_console(out, report, false);
    std::string output = out.str();

    EXPECT_NE(output.find("BM_Fibonacci/35"), std::string::npos);
    // CPU time of 1245 ns is formatted as "1.2 us" by format_time_human()
    EXPECT_NE(output.find("us"), std::string::npos);      // Time in microseconds
    EXPECT_NE(output.find("561798"), std::string::npos);  // Iterations
}

TEST(BenchFormatterTest, FormatConsoleHeader) {
    BenchmarkReport report;
    report.version = "0.1.0";

    BenchmarkResult r1;
    r1.name = "BM_Test";
    r1.cpu_time_ns = 100.0;
    report.results.push_back(r1);

    std::ostringstream out;
    format_bench_console(out, report, false);
    std::string output = out.str();

    // Check for header columns
    EXPECT_NE(output.find("Benchmark"), std::string::npos);
    EXPECT_NE(output.find("Time"), std::string::npos);
    EXPECT_NE(output.find("Iterations"), std::string::npos);
}

TEST(BenchFormatterTest, FormatConsoleNoColor) {
    BenchmarkReport report;
    report.version = "0.1.0";

    BenchmarkResult r1;
    r1.name = "BM_Test";
    r1.cpu_time_ns = 100.0;
    report.results.push_back(r1);

    std::ostringstream out;
    format_bench_console(out, report, false);  // no color
    std::string output = out.str();

    // Should not contain ANSI escape codes
    EXPECT_EQ(output.find("\x1b["), std::string::npos);
}

// ============================================================================
// JSON Formatter Tests
// ============================================================================

TEST(BenchFormatterTest, FormatJsonEmpty) {
    BenchmarkReport report;
    report.version = "0.1.0";
    report.timestamp = "2026-01-23T10:30:00Z";

    std::ostringstream out;
    format_bench_json(out, report);
    std::string output = out.str();

    EXPECT_NE(output.find("\"version\""), std::string::npos);
    EXPECT_NE(output.find("\"0.1.0\""), std::string::npos);
    EXPECT_NE(output.find("\"timestamp\""), std::string::npos);
    EXPECT_NE(output.find("\"benchmarks\""), std::string::npos);
}

TEST(BenchFormatterTest, FormatJsonWithResults) {
    BenchmarkReport report;
    report.version = "0.1.0";
    report.timestamp = "2026-01-23T10:30:00Z";

    BenchmarkResult r1;
    r1.name = "BM_Fibonacci/35";
    r1.cpu_time_ns = 1245.0;
    r1.iterations = 561798;
    report.results.push_back(r1);

    std::ostringstream out;
    format_bench_json(out, report);
    std::string output = out.str();

    EXPECT_NE(output.find("\"name\""), std::string::npos);
    EXPECT_NE(output.find("\"BM_Fibonacci/35\""), std::string::npos);
    EXPECT_NE(output.find("\"cpu_time_ns\""), std::string::npos);
    EXPECT_NE(output.find("1245"), std::string::npos);
    EXPECT_NE(output.find("\"iterations\""), std::string::npos);
    EXPECT_NE(output.find("561798"), std::string::npos);
}

TEST(BenchFormatterTest, FormatJsonValidStructure) {
    BenchmarkReport report;
    report.version = "0.1.0";
    report.timestamp = "2026-01-23T10:30:00Z";

    BenchmarkResult r1;
    r1.name = "BM_Test";
    r1.cpu_time_ns = 100.0;
    report.results.push_back(r1);

    std::ostringstream out;
    format_bench_json(out, report);
    std::string output = out.str();

    // Basic JSON structure checks
    EXPECT_EQ(output.front(), '{');
    EXPECT_EQ(output.back(), '\n');
    // Check for closing brace before newline
    EXPECT_NE(output.find("}\n"), std::string::npos);
}

// ============================================================================
// CSV Formatter Tests
// ============================================================================

TEST(BenchFormatterTest, FormatCsvHeader) {
    BenchmarkReport report;
    report.version = "0.1.0";

    std::ostringstream out;
    format_bench_csv(out, report);
    std::string output = out.str();

    // CSV should have header row
    EXPECT_NE(output.find("name,"), std::string::npos);
    EXPECT_NE(output.find("cpu_time_ns"), std::string::npos);
    EXPECT_NE(output.find("real_time_ns"), std::string::npos);
    EXPECT_NE(output.find("iterations"), std::string::npos);
}

TEST(BenchFormatterTest, FormatCsvWithResults) {
    BenchmarkReport report;
    report.version = "0.1.0";

    BenchmarkResult r1;
    r1.name = "BM_Fibonacci/35";
    r1.cpu_time_ns = 1245.0;
    r1.real_time_ns = 1250.0;
    r1.iterations = 561798;
    r1.items_per_second = 803200.0;
    r1.bytes_per_second = 0.0;
    report.results.push_back(r1);

    std::ostringstream out;
    format_bench_csv(out, report);
    std::string output = out.str();

    // Check data row
    EXPECT_NE(output.find("BM_Fibonacci/35"), std::string::npos);
    EXPECT_NE(output.find("1245"), std::string::npos);
    EXPECT_NE(output.find("1250"), std::string::npos);
    EXPECT_NE(output.find("561798"), std::string::npos);
}

TEST(BenchFormatterTest, FormatCsvMultipleRows) {
    BenchmarkReport report;
    report.version = "0.1.0";

    BenchmarkResult r1;
    r1.name = "BM_Test1";
    r1.cpu_time_ns = 100.0;
    report.results.push_back(r1);

    BenchmarkResult r2;
    r2.name = "BM_Test2";
    r2.cpu_time_ns = 200.0;
    report.results.push_back(r2);

    std::ostringstream out;
    format_bench_csv(out, report);
    std::string output = out.str();

    // Should have header + 2 data rows
    EXPECT_NE(output.find("BM_Test1"), std::string::npos);
    EXPECT_NE(output.find("BM_Test2"), std::string::npos);
}

// ============================================================================
// Time Formatting Helper Tests
// ============================================================================

TEST(BenchFormatterTest, FormatTimeNanoseconds) {
    // Test that small times are formatted in ns
    std::string result = format_time_human(100.0);
    EXPECT_NE(result.find("ns"), std::string::npos);
}

TEST(BenchFormatterTest, FormatTimeMicroseconds) {
    // Test that microsecond-range times are formatted in us
    std::string result = format_time_human(5000.0);  // 5 microseconds
    EXPECT_NE(result.find("us"), std::string::npos);
}

TEST(BenchFormatterTest, FormatTimeMilliseconds) {
    // Test that millisecond-range times are formatted in ms
    std::string result = format_time_human(5000000.0);  // 5 milliseconds
    EXPECT_NE(result.find("ms"), std::string::npos);
}

TEST(BenchFormatterTest, FormatTimeSeconds) {
    // Test that second-range times are formatted in s
    std::string result = format_time_human(5000000000.0);  // 5 seconds
    EXPECT_NE(result.find(" s"), std::string::npos);
}

// ============================================================================
// Throughput Formatting Helper Tests
// ============================================================================

TEST(BenchFormatterTest, FormatThroughput) {
    // Test throughput formatting (items/sec)
    std::string result = format_throughput(1000000.0);  // 1M items/sec
    EXPECT_NE(result.find("M"), std::string::npos);
}

TEST(BenchFormatterTest, FormatThroughputKilo) {
    std::string result = format_throughput(5000.0);  // 5K items/sec
    EXPECT_NE(result.find("K"), std::string::npos);
}

TEST(BenchFormatterTest, FormatBytesPerSecond) {
    // Test bytes/sec formatting
    std::string result = format_bytes_per_second(100000000.0);  // 100 MB/s
    EXPECT_TRUE(result.find("MB/s") != std::string::npos ||
                result.find("M/s") != std::string::npos);
}
