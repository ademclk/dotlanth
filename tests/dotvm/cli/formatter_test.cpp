/// @file formatter_test.cpp
/// @brief DSL-003 Formatter class unit tests (skeleton)

#include <gtest/gtest.h>

#include "dotvm/cli/formatter.hpp"

using namespace dotvm::cli;

// ============================================================================
// Construction Tests
// ============================================================================

TEST(FormatterTest, DefaultConstruction) {
    Formatter fmt;
    EXPECT_EQ(fmt.config().indent_size, 4);
    EXPECT_FALSE(fmt.config().use_tabs);
    EXPECT_TRUE(fmt.config().trim_trailing);
    EXPECT_TRUE(fmt.config().ensure_final_newline);
}

TEST(FormatterTest, CustomConfig) {
    FormatConfig config;
    config.indent_size = 2;
    config.use_tabs = true;
    config.trim_trailing = false;
    config.ensure_final_newline = false;

    Formatter fmt(config);

    EXPECT_EQ(fmt.config().indent_size, 2);
    EXPECT_TRUE(fmt.config().use_tabs);
    EXPECT_FALSE(fmt.config().trim_trailing);
    EXPECT_FALSE(fmt.config().ensure_final_newline);
}

TEST(FormatterTest, SetConfig) {
    Formatter fmt;

    FormatConfig config;
    config.indent_size = 8;
    fmt.set_config(config);

    EXPECT_EQ(fmt.config().indent_size, 8);
}

// ============================================================================
// Format Tests (Skeleton - will be expanded in Phase 3)
// ============================================================================

TEST(FormatterTest, FormatPassthrough) {
    // Current skeleton implementation just returns source unchanged
    Formatter fmt;
    std::string source = "dot Test:\n    state:\n        x: 0\n";
    std::string result = fmt.format(source);
    EXPECT_EQ(result, source);
}

TEST(FormatterTest, FormatEmptySource) {
    Formatter fmt;
    std::string source;
    std::string result = fmt.format(source);
    EXPECT_TRUE(result.empty());
}
