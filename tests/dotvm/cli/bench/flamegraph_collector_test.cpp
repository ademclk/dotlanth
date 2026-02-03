/// @file flamegraph_collector_test.cpp
/// @brief Unit tests for FlameGraphCollector (CLI-005 Benchmark Runner)

#include <filesystem>
#include <fstream>
#include <sstream>

#include <gtest/gtest.h>

#include "dotvm/cli/bench/flamegraph_collector.hpp"

using namespace dotvm::cli::bench;

// ============================================================================
// Test Fixture
// ============================================================================

class FlameGraphCollectorTest : public ::testing::Test {
protected:
    std::filesystem::path temp_dir_;
    std::vector<std::filesystem::path> temp_files_;

    void SetUp() override {
        temp_dir_ = std::filesystem::temp_directory_path() /
                    ("flamegraph_test_" + std::to_string(std::time(nullptr)));
        std::filesystem::create_directories(temp_dir_);
    }

    void TearDown() override {
        for (const auto& path : temp_files_) {
            std::filesystem::remove(path);
        }
        std::filesystem::remove_all(temp_dir_);
    }

    std::filesystem::path get_temp_path(const std::string& name) {
        auto path = temp_dir_ / name;
        temp_files_.push_back(path);
        return path;
    }
};

// ============================================================================
// Stack Recording Tests
// ============================================================================

TEST_F(FlameGraphCollectorTest, RecordStack_SingleSample) {
    FlameGraphCollector collector;

    std::vector<std::size_t> stack = {0, 10, 20};  // main;func1;func2
    collector.record_stack(stack);

    EXPECT_EQ(collector.sample_count(), 1);
}

TEST_F(FlameGraphCollectorTest, RecordStack_MultipleSamples) {
    FlameGraphCollector collector;

    std::vector<std::size_t> stack1 = {0, 10};
    std::vector<std::size_t> stack2 = {0, 10, 20};
    std::vector<std::size_t> stack3 = {0, 10};  // Same as stack1

    collector.record_stack(stack1);
    collector.record_stack(stack2);
    collector.record_stack(stack3);

    EXPECT_EQ(collector.sample_count(), 3);
}

TEST_F(FlameGraphCollectorTest, RecordStack_EmptyStack) {
    FlameGraphCollector collector;

    std::vector<std::size_t> empty_stack;
    collector.record_stack(empty_stack);

    // Empty stacks should be recorded but may show as root
    EXPECT_EQ(collector.sample_count(), 1);
}

// ============================================================================
// Output Generation Tests
// ============================================================================

TEST_F(FlameGraphCollectorTest, WriteFoldedStacks_SingleStack) {
    FlameGraphCollector collector;

    std::vector<std::size_t> stack = {0, 10, 20};
    collector.record_stack(stack);

    std::ostringstream out;
    collector.write_folded_stacks(out);

    std::string output = out.str();
    // Folded format: "pc:0;pc:10;pc:20 1\n"
    EXPECT_NE(output.find("pc:"), std::string::npos);
    EXPECT_NE(output.find(" 1"), std::string::npos);  // count of 1
}

TEST_F(FlameGraphCollectorTest, WriteFoldedStacks_AggregatesSameStacks) {
    FlameGraphCollector collector;

    std::vector<std::size_t> stack = {0, 10};
    collector.record_stack(stack);
    collector.record_stack(stack);
    collector.record_stack(stack);

    std::ostringstream out;
    collector.write_folded_stacks(out);

    std::string output = out.str();
    // Should aggregate to count 3
    EXPECT_NE(output.find(" 3"), std::string::npos);
}

TEST_F(FlameGraphCollectorTest, WriteFoldedStacks_DifferentStacks) {
    FlameGraphCollector collector;

    std::vector<std::size_t> stack1 = {0, 10};
    std::vector<std::size_t> stack2 = {0, 20};

    collector.record_stack(stack1);
    collector.record_stack(stack2);

    std::ostringstream out;
    collector.write_folded_stacks(out);

    std::string output = out.str();
    // Should have two lines
    auto newline_count = static_cast<std::size_t>(std::count(output.begin(), output.end(), '\n'));
    EXPECT_EQ(newline_count, 2);
}

TEST_F(FlameGraphCollectorTest, WriteToFile_Success) {
    auto path = get_temp_path("test.folded");
    FlameGraphCollector collector;

    std::vector<std::size_t> stack = {0, 10, 20};
    collector.record_stack(stack);

    bool result = collector.write_to_file(path.string());
    EXPECT_TRUE(result);
    EXPECT_TRUE(std::filesystem::exists(path));

    // Read and verify content
    std::ifstream in(path);
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    EXPECT_FALSE(content.empty());
}

TEST_F(FlameGraphCollectorTest, WriteToFile_InvalidPath_ReturnsFalse) {
    FlameGraphCollector collector;

    std::vector<std::size_t> stack = {0, 10};
    collector.record_stack(stack);

    bool result = collector.write_to_file("/nonexistent/dir/file.folded");
    EXPECT_FALSE(result);
}

// ============================================================================
// Symbol Resolution Tests
// ============================================================================

TEST_F(FlameGraphCollectorTest, SetSymbolResolver_CustomNames) {
    FlameGraphCollector collector;

    // Set custom symbol resolver
    collector.set_symbol_resolver([](std::size_t pc) -> std::string {
        switch (pc) {
            case 0:
                return "main";
            case 10:
                return "compute";
            case 20:
                return "add";
            default:
                return "unknown:" + std::to_string(pc);
        }
    });

    std::vector<std::size_t> stack = {0, 10, 20};
    collector.record_stack(stack);

    std::ostringstream out;
    collector.write_folded_stacks(out);

    std::string output = out.str();
    EXPECT_NE(output.find("main"), std::string::npos);
    EXPECT_NE(output.find("compute"), std::string::npos);
    EXPECT_NE(output.find("add"), std::string::npos);
}

// ============================================================================
// Clear/Reset Tests
// ============================================================================

TEST_F(FlameGraphCollectorTest, Clear_ResetsSamples) {
    FlameGraphCollector collector;

    std::vector<std::size_t> stack = {0, 10};
    collector.record_stack(stack);
    collector.record_stack(stack);

    EXPECT_EQ(collector.sample_count(), 2);

    collector.clear();

    EXPECT_EQ(collector.sample_count(), 0);
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST_F(FlameGraphCollectorTest, VeryDeepStack) {
    FlameGraphCollector collector;

    std::vector<std::size_t> deep_stack;
    for (std::size_t i = 0; i < 100; ++i) {
        deep_stack.push_back(i * 4);
    }

    collector.record_stack(deep_stack);

    std::ostringstream out;
    collector.write_folded_stacks(out);

    std::string output = out.str();
    // Should contain 99 semicolons (100 frames - 1)
    auto semicolon_count = static_cast<std::size_t>(std::count(output.begin(), output.end(), ';'));
    EXPECT_EQ(semicolon_count, 99);
}
