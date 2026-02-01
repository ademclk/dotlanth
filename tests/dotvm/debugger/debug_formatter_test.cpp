/// @file debug_formatter_test.cpp
/// @brief Unit tests for DebugFormatter - TOOL-011 Debug Client

#include <gtest/gtest.h>

#include "dotvm/debugger/debug_formatter.hpp"

namespace dotvm::debugger {
namespace {

class DebugFormatterTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Use no colors for easier testing
        formatter_ = std::make_unique<DebugFormatter>(FormatOptions{
            .show_hex = true, .show_decimal = true, .use_colors = false, .hex_width = 16});
    }

    std::unique_ptr<DebugFormatter> formatter_;
};

TEST_F(DebugFormatterTest, FormatHex) {
    EXPECT_EQ(formatter_->format_hex(0x10, 4), "0x0010");
    EXPECT_EQ(formatter_->format_hex(0xABCD, 4), "0xabcd");
    EXPECT_EQ(formatter_->format_hex(0, 2), "0x00");
}

TEST_F(DebugFormatterTest, FormatAddress) {
    auto addr = formatter_->format_address(0x10);
    EXPECT_TRUE(addr.find("0x0010") != std::string::npos);
}

TEST_F(DebugFormatterTest, FormatRegister) {
    core::Value value(static_cast<std::int64_t>(42));
    auto output = formatter_->format_register(1, value);

    EXPECT_TRUE(output.find("R1") != std::string::npos);
    EXPECT_TRUE(output.find("42") != std::string::npos);  // Decimal
}

TEST_F(DebugFormatterTest, FormatRegistersMultiple) {
    std::vector<std::pair<std::uint8_t, core::Value>> regs = {
        {0, core::Value(static_cast<std::int64_t>(0))},
        {1, core::Value(static_cast<std::int64_t>(100))},
        {2, core::Value(static_cast<std::int64_t>(-1))}};

    auto output = formatter_->format_registers(regs);
    EXPECT_TRUE(output.find("R0") != std::string::npos);
    EXPECT_TRUE(output.find("R1") != std::string::npos);
    EXPECT_TRUE(output.find("R2") != std::string::npos);
}

TEST_F(DebugFormatterTest, FormatMemory) {
    std::vector<std::uint8_t> data = {0x48, 0x65, 0x6C, 0x6C, 0x6F};  // "Hello"
    auto output = formatter_->format_memory(0, data);

    EXPECT_TRUE(output.find("48") != std::string::npos);
    EXPECT_TRUE(output.find("Hello") != std::string::npos || output.find("H") != std::string::npos);
}

TEST_F(DebugFormatterTest, FormatBreakpoint) {
    Breakpoint bp{.id = 1,
                  .address = 0x10,
                  .enabled = true,
                  .condition = "",
                  .hit_count = 0,
                  .ignore_count = 0,
                  .comment = ""};

    auto output = formatter_->format_breakpoint(bp);
    EXPECT_TRUE(output.find("1:") != std::string::npos);
    EXPECT_TRUE(output.find("0x") != std::string::npos);
    EXPECT_TRUE(output.find("enabled") != std::string::npos);
}

TEST_F(DebugFormatterTest, FormatBreakpointDisabled) {
    Breakpoint bp{.id = 2,
                  .address = 0x20,
                  .enabled = false,
                  .condition = "",
                  .hit_count = 0,
                  .ignore_count = 0,
                  .comment = ""};

    auto output = formatter_->format_breakpoint(bp);
    EXPECT_TRUE(output.find("disabled") != std::string::npos);
}

TEST_F(DebugFormatterTest, FormatBreakpointWithCondition) {
    Breakpoint bp{.id = 3,
                  .address = 0x30,
                  .enabled = true,
                  .condition = "r1 > 10",
                  .hit_count = 5,
                  .ignore_count = 0,
                  .comment = ""};

    auto output = formatter_->format_breakpoint(bp);
    EXPECT_TRUE(output.find("r1 > 10") != std::string::npos);
    EXPECT_TRUE(output.find("hit 5") != std::string::npos);
}

TEST_F(DebugFormatterTest, FormatBreakpointsEmpty) {
    std::vector<const Breakpoint*> empty;
    auto output = formatter_->format_breakpoints(empty);
    EXPECT_TRUE(output.find("No breakpoints") != std::string::npos);
}

TEST_F(DebugFormatterTest, FormatWatchpoint) {
    Watchpoint wp{.id = 1,
                  .handle = core::Handle{0, 0},
                  .offset = 0x100,
                  .size = 4,
                  .type = WatchType::Write,
                  .enabled = true,
                  .hit_count = 0,
                  .comment = "",
                  .previous_value = {}};

    auto output = formatter_->format_watchpoint(wp);
    EXPECT_TRUE(output.find("1:") != std::string::npos);
    EXPECT_TRUE(output.find("handle=0") != std::string::npos);
    EXPECT_TRUE(output.find("size=4") != std::string::npos);
    EXPECT_TRUE(output.find("write") != std::string::npos);
}

TEST_F(DebugFormatterTest, FormatWatchpointsEmpty) {
    std::vector<const Watchpoint*> empty;
    auto output = formatter_->format_watchpoints(empty);
    EXPECT_TRUE(output.find("No watchpoints") != std::string::npos);
}

TEST_F(DebugFormatterTest, FormatFrame) {
    StackFrame frame{.frame_index = 0,
                     .pc = 0x10,
                     .return_pc = 0x04,
                     .function_name = "main",
                     .source_file = "test.dot",
                     .line_number = 10};

    auto output = formatter_->format_frame(frame, false);
    EXPECT_TRUE(output.find("#0") != std::string::npos);
    EXPECT_TRUE(output.find("main") != std::string::npos);
    EXPECT_TRUE(output.find("test.dot") != std::string::npos);
}

TEST_F(DebugFormatterTest, FormatFrameSelected) {
    StackFrame frame{0, 0x10, 0x04, "main", "", 0};

    auto output = formatter_->format_frame(frame, true);
    EXPECT_TRUE(output.find("*") != std::string::npos);  // Selection marker
}

TEST_F(DebugFormatterTest, FormatBacktraceEmpty) {
    std::vector<StackFrame> empty;
    auto output = formatter_->format_backtrace(empty, 0);
    EXPECT_TRUE(output.find("No call stack") != std::string::npos);
}

TEST_F(DebugFormatterTest, FormatBacktrace) {
    std::vector<StackFrame> frames = {{0, 0x10, 0x04, "inner", "", 0},
                                      {1, 0x04, 0, "outer", "", 0}};

    auto output = formatter_->format_backtrace(frames, 0);
    EXPECT_TRUE(output.find("#0") != std::string::npos);
    EXPECT_TRUE(output.find("#1") != std::string::npos);
    EXPECT_TRUE(output.find("inner") != std::string::npos);
    EXPECT_TRUE(output.find("outer") != std::string::npos);
}

TEST_F(DebugFormatterTest, FormatPromptNotStarted) {
    auto prompt = formatter_->format_prompt(SessionState::NotStarted, 0);
    EXPECT_TRUE(prompt.find("dotdb") != std::string::npos);
}

TEST_F(DebugFormatterTest, FormatPromptPaused) {
    auto prompt = formatter_->format_prompt(SessionState::Paused, 0x10);
    EXPECT_TRUE(prompt.find("dotdb") != std::string::npos);
    EXPECT_TRUE(prompt.find("0x") != std::string::npos);
}

TEST_F(DebugFormatterTest, FormatPromptHalted) {
    auto prompt = formatter_->format_prompt(SessionState::Halted, 0);
    EXPECT_TRUE(prompt.find("halted") != std::string::npos);
}

TEST_F(DebugFormatterTest, FormatHelp) {
    auto help = formatter_->format_help();
    EXPECT_TRUE(help.find("run") != std::string::npos);
    EXPECT_TRUE(help.find("continue") != std::string::npos);
    EXPECT_TRUE(help.find("step") != std::string::npos);
    EXPECT_TRUE(help.find("breakpoint") != std::string::npos);
    EXPECT_TRUE(help.find("register") != std::string::npos);
    EXPECT_TRUE(help.find("memory") != std::string::npos);
    EXPECT_TRUE(help.find("quit") != std::string::npos);
}

TEST_F(DebugFormatterTest, OptionsGetSet) {
    FormatOptions opts{.show_hex = false, .show_decimal = true, .use_colors = true};
    formatter_->set_options(opts);

    EXPECT_FALSE(formatter_->options().show_hex);
    EXPECT_TRUE(formatter_->options().show_decimal);
    EXPECT_TRUE(formatter_->options().use_colors);
}

}  // namespace
}  // namespace dotvm::debugger
