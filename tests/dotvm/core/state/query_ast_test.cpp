/// @file query_ast_test.cpp
/// @brief STATE-010 Query AST unit tests (TDD)
///
/// Tests for Query AST providing query representation:
/// - Builder pattern for fluent query construction
/// - Predicate matching semantics
/// - AST node composition
/// - Range bounds derivation from predicates

#include <cstring>
#include <vector>

#include <gtest/gtest.h>

#include "dotvm/core/state/query_ast.hpp"

namespace dotvm::core::state {
namespace {

// ============================================================================
// Helper Functions
// ============================================================================

/// @brief Create a byte span from a string literal
[[nodiscard]] std::span<const std::byte> to_bytes(std::string_view str) {
    return {reinterpret_cast<const std::byte*>(str.data()), str.size()};
}

/// @brief Create a byte vector from a string
[[nodiscard]] std::vector<std::byte> make_bytes(std::string_view str) {
    std::vector<std::byte> result(str.size());
    std::memcpy(result.data(), str.data(), str.size());
    return result;
}

// ============================================================================
// Predicate Tests
// ============================================================================

class PredicateTest : public ::testing::Test {};

/// @test Equality predicate matches exact key
TEST_F(PredicateTest, EqualityMatchesExact) {
    Predicate pred{PredicateOp::Eq, make_bytes("test_key")};

    EXPECT_TRUE(pred.matches(to_bytes("test_key")));
    EXPECT_FALSE(pred.matches(to_bytes("test_key_other")));
    EXPECT_FALSE(pred.matches(to_bytes("test")));
    EXPECT_FALSE(pred.matches(to_bytes("")));
}

/// @test Less-than predicate
TEST_F(PredicateTest, LessThanMatches) {
    Predicate pred{PredicateOp::Lt, make_bytes("m")};

    EXPECT_TRUE(pred.matches(to_bytes("a")));
    EXPECT_TRUE(pred.matches(to_bytes("hello")));
    EXPECT_TRUE(pred.matches(to_bytes("l")));
    EXPECT_FALSE(pred.matches(to_bytes("m")));
    EXPECT_FALSE(pred.matches(to_bytes("n")));
    EXPECT_FALSE(pred.matches(to_bytes("zebra")));
}

/// @test Less-than-or-equal predicate
TEST_F(PredicateTest, LessThanOrEqualMatches) {
    Predicate pred{PredicateOp::Le, make_bytes("m")};

    EXPECT_TRUE(pred.matches(to_bytes("a")));
    EXPECT_TRUE(pred.matches(to_bytes("m")));
    EXPECT_FALSE(pred.matches(to_bytes("n")));
    EXPECT_FALSE(pred.matches(to_bytes("zebra")));
}

/// @test Greater-than predicate
TEST_F(PredicateTest, GreaterThanMatches) {
    Predicate pred{PredicateOp::Gt, make_bytes("m")};

    EXPECT_FALSE(pred.matches(to_bytes("a")));
    EXPECT_FALSE(pred.matches(to_bytes("m")));
    EXPECT_TRUE(pred.matches(to_bytes("n")));
    EXPECT_TRUE(pred.matches(to_bytes("zebra")));
}

/// @test Greater-than-or-equal predicate
TEST_F(PredicateTest, GreaterThanOrEqualMatches) {
    Predicate pred{PredicateOp::Ge, make_bytes("m")};

    EXPECT_FALSE(pred.matches(to_bytes("a")));
    EXPECT_TRUE(pred.matches(to_bytes("m")));
    EXPECT_TRUE(pred.matches(to_bytes("n")));
    EXPECT_TRUE(pred.matches(to_bytes("zebra")));
}

/// @test Prefix predicate matches keys starting with prefix
TEST_F(PredicateTest, PrefixMatches) {
    Predicate pred{PredicateOp::Prefix, make_bytes("user:")};

    EXPECT_TRUE(pred.matches(to_bytes("user:")));
    EXPECT_TRUE(pred.matches(to_bytes("user:123")));
    EXPECT_TRUE(pred.matches(to_bytes("user:alice:profile")));
    EXPECT_FALSE(pred.matches(to_bytes("users")));
    EXPECT_FALSE(pred.matches(to_bytes("admin:bob")));
    EXPECT_FALSE(pred.matches(to_bytes("")));
}

/// @test Empty predicate operand edge cases
TEST_F(PredicateTest, EmptyOperandEdgeCases) {
    // Empty equality - only matches empty key
    Predicate eq_empty{PredicateOp::Eq, {}};
    EXPECT_TRUE(eq_empty.matches(to_bytes("")));
    EXPECT_FALSE(eq_empty.matches(to_bytes("a")));

    // Empty prefix - matches everything
    Predicate prefix_empty{PredicateOp::Prefix, {}};
    EXPECT_TRUE(prefix_empty.matches(to_bytes("")));
    EXPECT_TRUE(prefix_empty.matches(to_bytes("anything")));
}

// ============================================================================
// RangeBounds Tests
// ============================================================================

class RangeBoundsTest : public ::testing::Test {};

/// @test RangeBounds from_predicates with no predicates
TEST_F(RangeBoundsTest, FromPredicatesEmpty) {
    std::vector<Predicate> preds;
    auto bounds = RangeBounds::from_predicates(preds);

    EXPECT_FALSE(bounds.lower.has_value());
    EXPECT_FALSE(bounds.upper.has_value());
}

/// @test RangeBounds from equality predicate
TEST_F(RangeBoundsTest, FromPredicatesEquality) {
    std::vector<Predicate> preds = {
        {PredicateOp::Eq, make_bytes("exact")}
    };
    auto bounds = RangeBounds::from_predicates(preds);

    ASSERT_TRUE(bounds.lower.has_value());
    ASSERT_TRUE(bounds.upper.has_value());
    EXPECT_EQ(*bounds.lower, make_bytes("exact"));
    EXPECT_EQ(*bounds.upper, make_bytes("exact"));
    EXPECT_TRUE(bounds.lower_inclusive);
    EXPECT_TRUE(bounds.upper_inclusive);
}

/// @test RangeBounds from range predicates (Ge and Lt)
TEST_F(RangeBoundsTest, FromPredicatesRange) {
    std::vector<Predicate> preds = {
        {PredicateOp::Ge, make_bytes("a")},
        {PredicateOp::Lt, make_bytes("m")}
    };
    auto bounds = RangeBounds::from_predicates(preds);

    ASSERT_TRUE(bounds.lower.has_value());
    ASSERT_TRUE(bounds.upper.has_value());
    EXPECT_EQ(*bounds.lower, make_bytes("a"));
    EXPECT_EQ(*bounds.upper, make_bytes("m"));
    EXPECT_TRUE(bounds.lower_inclusive);
    EXPECT_FALSE(bounds.upper_inclusive);
}

/// @test RangeBounds from Gt and Le
TEST_F(RangeBoundsTest, FromPredicatesGtLe) {
    std::vector<Predicate> preds = {
        {PredicateOp::Gt, make_bytes("a")},
        {PredicateOp::Le, make_bytes("m")}
    };
    auto bounds = RangeBounds::from_predicates(preds);

    ASSERT_TRUE(bounds.lower.has_value());
    ASSERT_TRUE(bounds.upper.has_value());
    EXPECT_EQ(*bounds.lower, make_bytes("a"));
    EXPECT_EQ(*bounds.upper, make_bytes("m"));
    EXPECT_FALSE(bounds.lower_inclusive);
    EXPECT_TRUE(bounds.upper_inclusive);
}

/// @test RangeBounds from prefix predicate
TEST_F(RangeBoundsTest, FromPredicatesPrefix) {
    std::vector<Predicate> preds = {
        {PredicateOp::Prefix, make_bytes("user:")}
    };
    auto bounds = RangeBounds::from_predicates(preds);

    // Prefix should derive lower bound at prefix, upper bound at prefix+1
    ASSERT_TRUE(bounds.lower.has_value());
    ASSERT_TRUE(bounds.upper.has_value());
    EXPECT_EQ(*bounds.lower, make_bytes("user:"));
    EXPECT_TRUE(bounds.lower_inclusive);
    // Upper bound should be "user;" (the character after ':')
    EXPECT_FALSE(bounds.upper_inclusive);
}

/// @test RangeBounds contains method
TEST_F(RangeBoundsTest, ContainsMethod) {
    RangeBounds bounds;
    bounds.lower = make_bytes("b");
    bounds.upper = make_bytes("f");
    bounds.lower_inclusive = true;
    bounds.upper_inclusive = false;

    EXPECT_FALSE(bounds.contains(to_bytes("a")));
    EXPECT_TRUE(bounds.contains(to_bytes("b")));
    EXPECT_TRUE(bounds.contains(to_bytes("c")));
    EXPECT_TRUE(bounds.contains(to_bytes("e")));
    EXPECT_FALSE(bounds.contains(to_bytes("f")));
    EXPECT_FALSE(bounds.contains(to_bytes("g")));
}

/// @test RangeBounds unbounded contains
TEST_F(RangeBoundsTest, UnboundedContains) {
    // No bounds - contains everything
    RangeBounds unbounded;
    EXPECT_TRUE(unbounded.contains(to_bytes("")));
    EXPECT_TRUE(unbounded.contains(to_bytes("anything")));
    EXPECT_TRUE(unbounded.contains(to_bytes("zzzzz")));
}

// ============================================================================
// Query Builder Tests
// ============================================================================

class QueryBuilderTest : public ::testing::Test {};

/// @test Build simple scan query
TEST_F(QueryBuilderTest, BuildSimpleScan) {
    auto query = Query::Builder()
        .scan()
        .build();

    ASSERT_NE(query.root, nullptr);
    EXPECT_TRUE(std::holds_alternative<ScanNode>(query.root->node));

    const auto& scan = std::get<ScanNode>(query.root->node);
    EXPECT_TRUE(scan.prefix.empty());
}

/// @test Build scan with prefix
TEST_F(QueryBuilderTest, BuildScanWithPrefix) {
    auto query = Query::Builder()
        .scan(make_bytes("user:"))
        .build();

    ASSERT_NE(query.root, nullptr);
    EXPECT_TRUE(std::holds_alternative<ScanNode>(query.root->node));

    const auto& scan = std::get<ScanNode>(query.root->node);
    EXPECT_EQ(scan.prefix, make_bytes("user:"));
}

/// @test Build scan with filter
TEST_F(QueryBuilderTest, BuildFilteredScan) {
    auto query = Query::Builder()
        .scan()
        .filter(PredicateOp::Ge, make_bytes("a"))
        .filter(PredicateOp::Lt, make_bytes("m"))
        .build();

    ASSERT_NE(query.root, nullptr);
    EXPECT_TRUE(std::holds_alternative<FilterNode>(query.root->node));

    const auto& filter = std::get<FilterNode>(query.root->node);
    EXPECT_EQ(filter.predicates.size(), 2u);
    EXPECT_EQ(filter.predicates[0].op, PredicateOp::Ge);
    EXPECT_EQ(filter.predicates[1].op, PredicateOp::Lt);

    // Filter should have scan as input
    ASSERT_NE(filter.input, nullptr);
    EXPECT_TRUE(std::holds_alternative<ScanNode>(filter.input->node));
}

/// @test Build scan with project
TEST_F(QueryBuilderTest, BuildWithProject) {
    auto query = Query::Builder()
        .scan()
        .project(true, false)  // keys only
        .build();

    ASSERT_NE(query.root, nullptr);
    EXPECT_TRUE(std::holds_alternative<ProjectNode>(query.root->node));

    const auto& project = std::get<ProjectNode>(query.root->node);
    EXPECT_TRUE(project.include_key);
    EXPECT_FALSE(project.include_value);
}

/// @test Build scan with limit
TEST_F(QueryBuilderTest, BuildWithLimit) {
    auto query = Query::Builder()
        .scan()
        .limit(100)
        .build();

    ASSERT_NE(query.root, nullptr);
    EXPECT_TRUE(std::holds_alternative<LimitNode>(query.root->node));

    const auto& limit_node = std::get<LimitNode>(query.root->node);
    EXPECT_EQ(limit_node.count, 100u);
}

/// @test Build scan with aggregate
TEST_F(QueryBuilderTest, BuildWithAggregate) {
    auto query = Query::Builder()
        .scan(make_bytes("order:"))
        .aggregate(AggregateFunc::Count)
        .build();

    ASSERT_NE(query.root, nullptr);
    EXPECT_TRUE(std::holds_alternative<AggregateNode>(query.root->node));

    const auto& agg = std::get<AggregateNode>(query.root->node);
    EXPECT_EQ(agg.func, AggregateFunc::Count);
}

/// @test Build complex query with multiple operations
TEST_F(QueryBuilderTest, BuildComplexQuery) {
    auto query = Query::Builder()
        .scan(make_bytes("user:"))
        .filter(PredicateOp::Ge, make_bytes("user:100"))
        .filter(PredicateOp::Lt, make_bytes("user:200"))
        .project(true, true)
        .limit(50)
        .build();

    ASSERT_NE(query.root, nullptr);

    // Root should be limit
    EXPECT_TRUE(std::holds_alternative<LimitNode>(query.root->node));
    const auto& limit_node = std::get<LimitNode>(query.root->node);
    EXPECT_EQ(limit_node.count, 50u);

    // Under limit should be project
    ASSERT_NE(limit_node.input, nullptr);
    EXPECT_TRUE(std::holds_alternative<ProjectNode>(limit_node.input->node));

    // Under project should be filter
    const auto& project = std::get<ProjectNode>(limit_node.input->node);
    ASSERT_NE(project.input, nullptr);
    EXPECT_TRUE(std::holds_alternative<FilterNode>(project.input->node));
}

// ============================================================================
// Query Clone Tests
// ============================================================================

TEST_F(QueryBuilderTest, QueryClone) {
    auto original = Query::Builder()
        .scan(make_bytes("test:"))
        .filter(PredicateOp::Ge, make_bytes("test:a"))
        .limit(10)
        .build();

    auto cloned = original.clone();

    ASSERT_NE(cloned.root, nullptr);
    EXPECT_TRUE(std::holds_alternative<LimitNode>(cloned.root->node));

    // Verify it's a deep copy (different pointers)
    EXPECT_NE(original.root.get(), cloned.root.get());
}

// ============================================================================
// QueryNode Traversal Tests
// ============================================================================

TEST_F(QueryBuilderTest, CollectPredicates) {
    auto query = Query::Builder()
        .scan()
        .filter(PredicateOp::Ge, make_bytes("a"))
        .filter(PredicateOp::Lt, make_bytes("z"))
        .build();

    auto predicates = query.collect_predicates();

    // Should have collected both predicates
    EXPECT_EQ(predicates.size(), 2u);
}

TEST_F(QueryBuilderTest, CollectPredicatesNoFilter) {
    auto query = Query::Builder()
        .scan()
        .limit(10)
        .build();

    auto predicates = query.collect_predicates();

    // No filters, should be empty
    EXPECT_TRUE(predicates.empty());
}

// ============================================================================
// to_string Tests
// ============================================================================

TEST_F(QueryBuilderTest, PredicateOpToString) {
    EXPECT_EQ(to_string(PredicateOp::Eq), "Eq");
    EXPECT_EQ(to_string(PredicateOp::Lt), "Lt");
    EXPECT_EQ(to_string(PredicateOp::Le), "Le");
    EXPECT_EQ(to_string(PredicateOp::Gt), "Gt");
    EXPECT_EQ(to_string(PredicateOp::Ge), "Ge");
    EXPECT_EQ(to_string(PredicateOp::Prefix), "Prefix");
}

TEST_F(QueryBuilderTest, AggregateFuncToString) {
    EXPECT_EQ(to_string(AggregateFunc::Count), "Count");
    EXPECT_EQ(to_string(AggregateFunc::Sum), "Sum");
    EXPECT_EQ(to_string(AggregateFunc::Min), "Min");
    EXPECT_EQ(to_string(AggregateFunc::Max), "Max");
}

}  // namespace
}  // namespace dotvm::core::state
