/// @file collections_test.cpp
/// @brief Unit tests for DotList, DotMap, DotSet, and CollectionRegistry

#include <gtest/gtest.h>

#include "dotvm/core/collections/list.hpp"
#include "dotvm/core/collections/map.hpp"
#include "dotvm/core/collections/registry.hpp"
#include "dotvm/core/collections/set.hpp"

namespace dotvm::test {
namespace {

using namespace dotvm::core;
using namespace dotvm::core::collections;

// ============================================================================
// DotList Tests
// ============================================================================

class DotListTest : public ::testing::Test {
protected:
    DotList list;
};

TEST_F(DotListTest, EmptyList) {
    EXPECT_TRUE(list.empty());
    EXPECT_EQ(list.size(), 0);
}

TEST_F(DotListTest, PushAndGet) {
    list.push(Value::from_int(42));
    list.push(Value::from_float(3.14));

    EXPECT_EQ(list.size(), 2);
    EXPECT_EQ(list.get(0).as_integer(), 42);
    EXPECT_NEAR(list.get(1).as_float(), 3.14, 0.001);
}

TEST_F(DotListTest, Pop) {
    list.push(Value::from_int(1));
    list.push(Value::from_int(2));
    list.push(Value::from_int(3));

    auto val = list.pop();
    EXPECT_EQ(val.as_integer(), 3);
    EXPECT_EQ(list.size(), 2);
}

TEST_F(DotListTest, PopEmpty) {
    auto val = list.pop();
    EXPECT_TRUE(val.is_nil());
}

TEST_F(DotListTest, GetOutOfBounds) {
    list.push(Value::from_int(1));
    auto val = list.get(10);
    EXPECT_TRUE(val.is_nil());
}

TEST_F(DotListTest, Set) {
    list.push(Value::from_int(1));
    list.push(Value::from_int(2));

    EXPECT_TRUE(list.set(0, Value::from_int(100)));
    EXPECT_EQ(list.get(0).as_integer(), 100);
}

TEST_F(DotListTest, SetOutOfBounds) {
    EXPECT_FALSE(list.set(0, Value::from_int(1)));
}

TEST_F(DotListTest, Insert) {
    list.push(Value::from_int(1));
    list.push(Value::from_int(3));

    EXPECT_TRUE(list.insert(1, Value::from_int(2)));

    EXPECT_EQ(list.size(), 3);
    EXPECT_EQ(list.get(0).as_integer(), 1);
    EXPECT_EQ(list.get(1).as_integer(), 2);
    EXPECT_EQ(list.get(2).as_integer(), 3);
}

TEST_F(DotListTest, Remove) {
    list.push(Value::from_int(1));
    list.push(Value::from_int(2));
    list.push(Value::from_int(3));

    auto removed = list.remove(1);
    EXPECT_EQ(removed.as_integer(), 2);
    EXPECT_EQ(list.size(), 2);
    EXPECT_EQ(list.get(1).as_integer(), 3);
}

TEST_F(DotListTest, Clear) {
    list.push(Value::from_int(1));
    list.push(Value::from_int(2));
    list.clear();

    EXPECT_TRUE(list.empty());
}

TEST_F(DotListTest, Iteration) {
    list.push(Value::from_int(1));
    list.push(Value::from_int(2));
    list.push(Value::from_int(3));

    std::int64_t sum = 0;
    for (const auto& v : list) {
        sum += v.as_integer();
    }
    EXPECT_EQ(sum, 6);
}

// ============================================================================
// DotMap Tests
// ============================================================================

class DotMapTest : public ::testing::Test {
protected:
    DotMap map;
};

TEST_F(DotMapTest, EmptyMap) {
    EXPECT_TRUE(map.empty());
    EXPECT_EQ(map.size(), 0);
}

TEST_F(DotMapTest, SetAndGet) {
    map.set(Value::from_int(1), Value::from_int(100));
    map.set(Value::from_int(2), Value::from_int(200));

    EXPECT_EQ(map.size(), 2);
    EXPECT_EQ(map.get(Value::from_int(1)).as_integer(), 100);
    EXPECT_EQ(map.get(Value::from_int(2)).as_integer(), 200);
}

TEST_F(DotMapTest, GetMissing) {
    auto val = map.get(Value::from_int(999));
    EXPECT_TRUE(val.is_nil());
}

TEST_F(DotMapTest, Has) {
    map.set(Value::from_int(42), Value::from_int(100));

    EXPECT_TRUE(map.has(Value::from_int(42)));
    EXPECT_FALSE(map.has(Value::from_int(43)));
}

TEST_F(DotMapTest, Remove) {
    map.set(Value::from_int(1), Value::from_int(100));
    map.set(Value::from_int(2), Value::from_int(200));

    EXPECT_TRUE(map.remove(Value::from_int(1)));
    EXPECT_FALSE(map.has(Value::from_int(1)));
    EXPECT_EQ(map.size(), 1);
}

TEST_F(DotMapTest, RemoveMissing) {
    EXPECT_FALSE(map.remove(Value::from_int(999)));
}

TEST_F(DotMapTest, Overwrite) {
    map.set(Value::from_int(1), Value::from_int(100));
    map.set(Value::from_int(1), Value::from_int(999));

    EXPECT_EQ(map.size(), 1);
    EXPECT_EQ(map.get(Value::from_int(1)).as_integer(), 999);
}

TEST_F(DotMapTest, Keys) {
    map.set(Value::from_int(1), Value::from_int(100));
    map.set(Value::from_int(2), Value::from_int(200));

    auto keys = map.keys();
    EXPECT_EQ(keys.size(), 2);
}

TEST_F(DotMapTest, Values) {
    map.set(Value::from_int(1), Value::from_int(100));
    map.set(Value::from_int(2), Value::from_int(200));

    auto values = map.values();
    EXPECT_EQ(values.size(), 2);

    std::int64_t sum = 0;
    for (const auto& v : values) {
        sum += v.as_integer();
    }
    EXPECT_EQ(sum, 300);
}

TEST_F(DotMapTest, BoolKeys) {
    map.set(Value::from_bool(true), Value::from_int(1));
    map.set(Value::from_bool(false), Value::from_int(0));

    EXPECT_EQ(map.get(Value::from_bool(true)).as_integer(), 1);
    EXPECT_EQ(map.get(Value::from_bool(false)).as_integer(), 0);
}

TEST_F(DotMapTest, Clear) {
    map.set(Value::from_int(1), Value::from_int(100));
    map.clear();

    EXPECT_TRUE(map.empty());
}

// ============================================================================
// DotSet Tests
// ============================================================================

class DotSetTest : public ::testing::Test {
protected:
    DotSet set;
};

TEST_F(DotSetTest, EmptySet) {
    EXPECT_TRUE(set.empty());
    EXPECT_EQ(set.size(), 0);
}

TEST_F(DotSetTest, Add) {
    EXPECT_TRUE(set.add(Value::from_int(1)));
    EXPECT_TRUE(set.add(Value::from_int(2)));
    EXPECT_EQ(set.size(), 2);
}

TEST_F(DotSetTest, AddDuplicate) {
    set.add(Value::from_int(1));
    EXPECT_FALSE(set.add(Value::from_int(1)));
    EXPECT_EQ(set.size(), 1);
}

TEST_F(DotSetTest, Has) {
    set.add(Value::from_int(42));

    EXPECT_TRUE(set.has(Value::from_int(42)));
    EXPECT_FALSE(set.has(Value::from_int(43)));
}

TEST_F(DotSetTest, Remove) {
    set.add(Value::from_int(1));
    set.add(Value::from_int(2));

    EXPECT_TRUE(set.remove(Value::from_int(1)));
    EXPECT_FALSE(set.has(Value::from_int(1)));
    EXPECT_EQ(set.size(), 1);
}

TEST_F(DotSetTest, ToVector) {
    set.add(Value::from_int(1));
    set.add(Value::from_int(2));
    set.add(Value::from_int(3));

    auto vec = set.to_vector();
    EXPECT_EQ(vec.size(), 3);
}

TEST_F(DotSetTest, Union) {
    DotSet a;
    a.add(Value::from_int(1));
    a.add(Value::from_int(2));

    DotSet b;
    b.add(Value::from_int(2));
    b.add(Value::from_int(3));

    auto u = a.union_with(b);
    EXPECT_EQ(u.size(), 3);
    EXPECT_TRUE(u.has(Value::from_int(1)));
    EXPECT_TRUE(u.has(Value::from_int(2)));
    EXPECT_TRUE(u.has(Value::from_int(3)));
}

TEST_F(DotSetTest, Intersection) {
    DotSet a;
    a.add(Value::from_int(1));
    a.add(Value::from_int(2));

    DotSet b;
    b.add(Value::from_int(2));
    b.add(Value::from_int(3));

    auto i = a.intersection_with(b);
    EXPECT_EQ(i.size(), 1);
    EXPECT_TRUE(i.has(Value::from_int(2)));
}

TEST_F(DotSetTest, Difference) {
    DotSet a;
    a.add(Value::from_int(1));
    a.add(Value::from_int(2));

    DotSet b;
    b.add(Value::from_int(2));
    b.add(Value::from_int(3));

    auto d = a.difference_with(b);
    EXPECT_EQ(d.size(), 1);
    EXPECT_TRUE(d.has(Value::from_int(1)));
}

// ============================================================================
// CollectionRegistry Tests
// ============================================================================

class CollectionRegistryTest : public ::testing::Test {
protected:
    CollectionRegistry registry;
};

TEST_F(CollectionRegistryTest, CreateList) {
    auto result = registry.create_list();
    ASSERT_TRUE(result.has_value());

    EXPECT_FALSE(result->is_null());
    EXPECT_EQ(result->type, CollectionType::List);
    EXPECT_TRUE(registry.is_valid(*result));
}

TEST_F(CollectionRegistryTest, CreateMap) {
    auto result = registry.create_map();
    ASSERT_TRUE(result.has_value());

    EXPECT_EQ(result->type, CollectionType::Map);
}

TEST_F(CollectionRegistryTest, CreateSet) {
    auto result = registry.create_set();
    ASSERT_TRUE(result.has_value());

    EXPECT_EQ(result->type, CollectionType::Set);
}

TEST_F(CollectionRegistryTest, GetList) {
    auto handle = registry.create_list();
    ASSERT_TRUE(handle.has_value());

    auto list = registry.get_list(*handle);
    ASSERT_TRUE(list.has_value());

    (*list)->push(Value::from_int(42));
    EXPECT_EQ((*list)->size(), 1);
}

TEST_F(CollectionRegistryTest, GetMap) {
    auto handle = registry.create_map();
    ASSERT_TRUE(handle.has_value());

    auto map = registry.get_map(*handle);
    ASSERT_TRUE(map.has_value());

    (*map)->set(Value::from_int(1), Value::from_int(100));
    EXPECT_TRUE((*map)->has(Value::from_int(1)));
}

TEST_F(CollectionRegistryTest, GetSet) {
    auto handle = registry.create_set();
    ASSERT_TRUE(handle.has_value());

    auto set = registry.get_set(*handle);
    ASSERT_TRUE(set.has_value());

    (*set)->add(Value::from_int(42));
    EXPECT_TRUE((*set)->has(Value::from_int(42)));
}

TEST_F(CollectionRegistryTest, TypeMismatch) {
    auto list_handle = registry.create_list();
    ASSERT_TRUE(list_handle.has_value());

    auto map_result = registry.get_map(*list_handle);
    EXPECT_FALSE(map_result.has_value());
    EXPECT_EQ(map_result.error(), CollectionError::TypeMismatch);
}

TEST_F(CollectionRegistryTest, Release) {
    auto handle = registry.create_list();
    ASSERT_TRUE(handle.has_value());

    EXPECT_EQ(registry.active_count(), 1);

    auto err = registry.release(*handle);
    EXPECT_EQ(err, CollectionError::Success);
    EXPECT_EQ(registry.active_count(), 0);
}

TEST_F(CollectionRegistryTest, InvalidHandle) {
    auto handle = CollectionHandle::null();
    auto result = registry.get_list(handle);
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), CollectionError::InvalidHandle);
}

TEST_F(CollectionRegistryTest, UseAfterFree) {
    auto handle = registry.create_list();
    ASSERT_TRUE(handle.has_value());

    auto h = *handle;
    (void)registry.release(h);

    auto result = registry.get_list(h);
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), CollectionError::InvalidHandle);
}

TEST_F(CollectionRegistryTest, SlotReuse) {
    auto h1 = registry.create_list();
    ASSERT_TRUE(h1.has_value());
    auto index1 = h1->index;
    auto gen1 = h1->generation;

    (void)registry.release(*h1);

    auto h2 = registry.create_list();
    ASSERT_TRUE(h2.has_value());

    // Should reuse the same slot
    EXPECT_EQ(h2->index, index1);
    // But with different generation
    EXPECT_NE(h2->generation, gen1);
}

// ============================================================================
// Value Integration Tests
// ============================================================================

TEST(CollectionValueTest, EncodeDecodeList) {
    CollectionRegistry registry;
    auto handle = registry.create_list();
    ASSERT_TRUE(handle.has_value());

    auto val = encode_collection_value(*handle);
    EXPECT_TRUE(is_collection_value(val));

    auto decoded = decode_collection_handle(val);
    EXPECT_EQ(decoded.index, handle->index);
    EXPECT_EQ(decoded.generation, handle->generation);
    EXPECT_EQ(decoded.type, CollectionType::List);
}

TEST(CollectionValueTest, EncodeDecodeMap) {
    CollectionRegistry registry;
    auto handle = registry.create_map();
    ASSERT_TRUE(handle.has_value());

    auto val = encode_collection_value(*handle);
    auto decoded = decode_collection_handle(val);

    EXPECT_EQ(decoded.type, CollectionType::Map);
}

TEST(CollectionValueTest, NonCollectionValueIsNotCollection) {
    auto int_val = Value::from_int(42);
    EXPECT_FALSE(is_collection_value(int_val));

    auto nil_val = Value::nil();
    EXPECT_FALSE(is_collection_value(nil_val));
}

}  // namespace
}  // namespace dotvm::test
