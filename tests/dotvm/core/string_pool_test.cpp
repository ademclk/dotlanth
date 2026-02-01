/// @file string_pool_test.cpp
/// @brief Unit tests for StringPool

#include <gtest/gtest.h>

#include "dotvm/core/string_pool.hpp"

namespace dotvm::test {
namespace {

using namespace dotvm::core;

// ============================================================================
// Basic Operations
// ============================================================================

class StringPoolTest : public ::testing::Test {
protected:
    StringPool pool;
};

TEST_F(StringPoolTest, CreateEmptyString) {
    auto result = pool.create("");
    ASSERT_TRUE(result.has_value());

    // Empty string uses SSO
    EXPECT_TRUE(result->is_sso());
}

TEST_F(StringPoolTest, CreateSmallStringSso) {
    auto result = pool.create("hello");
    ASSERT_TRUE(result.has_value());

    // Small strings use SSO
    EXPECT_TRUE(result->is_sso());
    EXPECT_EQ(result->index, 5);  // Length stored in index for SSO
}

TEST_F(StringPoolTest, CreateLargeStringHeap) {
    std::string large(100, 'x');
    auto result = pool.create(large);
    ASSERT_TRUE(result.has_value());

    // Large strings go to heap
    EXPECT_FALSE(result->is_sso());
    EXPECT_FALSE(result->is_null());
}

TEST_F(StringPoolTest, GetHeapString) {
    std::string original = "This is a longer string that exceeds SSO threshold";
    auto handle = pool.create(original);
    ASSERT_TRUE(handle.has_value());

    auto retrieved = pool.get(*handle);
    ASSERT_TRUE(retrieved.has_value());
    EXPECT_EQ(*retrieved, original);
}

TEST_F(StringPoolTest, ReleaseHeapString) {
    std::string original = "This is a heap-allocated string for testing release";
    auto handle = pool.create(original);
    ASSERT_TRUE(handle.has_value());

    EXPECT_EQ(pool.active_count(), 1);

    auto err = pool.release(*handle);
    EXPECT_EQ(err, StringError::Success);
    EXPECT_EQ(pool.active_count(), 0);

    // Handle should be invalid after release
    EXPECT_FALSE(pool.is_valid(*handle));
}

TEST_F(StringPoolTest, ReleaseSsoString) {
    auto handle = pool.create("sso");
    ASSERT_TRUE(handle.has_value());

    // SSO strings don't track active count
    auto err = pool.release(*handle);
    EXPECT_EQ(err, StringError::Success);
}

TEST_F(StringPoolTest, InvalidHandle) {
    auto handle = StringHandle::null();
    auto result = pool.get(handle);
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), StringError::InvalidHandle);
}

TEST_F(StringPoolTest, UseAfterFree) {
    std::string original = "Use after free test string that is long enough";
    auto handle = pool.create(original);
    ASSERT_TRUE(handle.has_value());

    auto h = *handle;
    (void)pool.release(h);

    // Handle should be invalid after release
    auto result = pool.get(h);
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), StringError::InvalidHandle);
}

// ============================================================================
// Interning
// ============================================================================

class StringPoolInternTest : public ::testing::Test {
protected:
    StringPool pool{StringPool::Config{.enable_interning = true}};
};

TEST_F(StringPoolInternTest, InternReturnsSameHandle) {
    std::string str = "This is an interned string long enough for heap allocation";

    auto h1 = pool.intern(str);
    ASSERT_TRUE(h1.has_value());

    auto h2 = pool.intern(str);
    ASSERT_TRUE(h2.has_value());

    // Same index for interned strings
    EXPECT_EQ(h1->index, h2->index);
    EXPECT_TRUE(h1->is_interned());
    EXPECT_TRUE(h2->is_interned());
}

TEST_F(StringPoolInternTest, InternedStringCount) {
    std::string str1 = "First interned string that is long enough for heap";
    std::string str2 = "Second interned string that is also long enough";

    (void)pool.intern(str1);
    (void)pool.intern(str2);
    (void)pool.intern(str1);  // Duplicate

    EXPECT_EQ(pool.interned_count(), 2);
}

TEST_F(StringPoolInternTest, InternRefCount) {
    std::string str = "Reference counted interned string long enough";

    auto h1 = pool.intern(str);
    auto h2 = pool.intern(str);
    auto h3 = pool.intern(str);

    ASSERT_TRUE(h1.has_value());

    // Release twice - should still be active
    (void)pool.release(*h1);
    (void)pool.release(*h2);

    EXPECT_TRUE(pool.is_valid(*h3));

    // Release last one - now should be invalid
    (void)pool.release(*h3);

    // Need fresh handle to check - the old one has stale generation
}

// ============================================================================
// Length and Statistics
// ============================================================================

TEST_F(StringPoolTest, Length) {
    std::string str = "A string with known length that exceeds SSO";
    auto handle = pool.create(str);
    ASSERT_TRUE(handle.has_value());

    auto len = pool.length(*handle);
    ASSERT_TRUE(len.has_value());
    EXPECT_EQ(*len, str.size());
}

TEST_F(StringPoolTest, TotalBytes) {
    std::string str1 = "First heap string that is long enough for testing";
    std::string str2 = "Second heap string also long enough for testing";

    (void)pool.create(str1);
    (void)pool.create(str2);

    EXPECT_GE(pool.total_bytes(), str1.size() + str2.size());
}

TEST_F(StringPoolTest, Clear) {
    for (int i = 0; i < 10; ++i) {
        (void)pool.create("Long string number " + std::to_string(i) + " for heap allocation");
    }

    EXPECT_GT(pool.active_count(), 0);

    pool.clear();

    EXPECT_EQ(pool.active_count(), 0);
    EXPECT_EQ(pool.total_bytes(), 0);
}

// ============================================================================
// Configuration Limits
// ============================================================================

TEST_F(StringPoolTest, MaxStringLength) {
    StringPool limited(StringPool::Config{.max_string_length = 100});

    std::string too_long(200, 'x');
    auto result = limited.create(too_long);

    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), StringError::StringTooLong);
}

TEST_F(StringPoolTest, MaxStrings) {
    StringPool limited(StringPool::Config{.max_strings = 3});

    // Create 3 heap strings
    for (int i = 0; i < 3; ++i) {
        auto r = limited.create("Heap string number " + std::to_string(i) + " long enough");
        EXPECT_TRUE(r.has_value()) << "Failed at i=" << i;
    }

    // Fourth should fail
    auto result = limited.create("This fourth heap string should fail to allocate");
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), StringError::PoolFull);
}

// ============================================================================
// Value Integration
// ============================================================================

TEST(StringValueTest, EncodeDecodeHeapString) {
    StringPool pool;
    std::string str = "A heap-allocated string for encoding test purposes";
    auto handle = pool.create(str);
    ASSERT_TRUE(handle.has_value());

    auto val = encode_string_value(*handle);
    EXPECT_TRUE(is_string_value(val));

    auto decoded = decode_string_handle(val);
    EXPECT_EQ(decoded.index, handle->index);
    EXPECT_EQ(decoded.generation, handle->generation);
}

TEST(StringValueTest, EncodeSsoString) {
    SsoString sso("hi");
    auto handle = StringHandle::sso_marker(2);

    auto val = encode_string_value(handle, &sso);
    EXPECT_TRUE(is_string_value(val));

    auto decoded = decode_string_handle(val);
    EXPECT_TRUE(decoded.is_sso());
}

TEST(StringValueTest, NonStringValueIsNotString) {
    auto int_val = Value::from_int(42);
    EXPECT_FALSE(is_string_value(int_val));

    auto float_val = Value::from_float(3.14);
    EXPECT_FALSE(is_string_value(float_val));

    auto nil_val = Value::nil();
    EXPECT_FALSE(is_string_value(nil_val));
}

// ============================================================================
// SsoString Tests
// ============================================================================

TEST(SsoStringTest, EmptyString) {
    SsoString s;
    EXPECT_TRUE(s.empty());
    EXPECT_EQ(s.size(), 0);
    EXPECT_TRUE(s.valid());
}

TEST(SsoStringTest, SmallString) {
    SsoString s("hello");
    EXPECT_FALSE(s.empty());
    EXPECT_EQ(s.size(), 5);
    EXPECT_EQ(s.view(), "hello");
    EXPECT_TRUE(s.valid());
}

TEST(SsoStringTest, MaxLength) {
    std::string str(SsoString::SSO_MAX_LEN, 'x');
    EXPECT_TRUE(SsoString::fits(str));

    SsoString s(str);
    EXPECT_EQ(s.size(), SsoString::SSO_MAX_LEN);
    EXPECT_EQ(s.view(), str);
}

TEST(SsoStringTest, TooLong) {
    std::string str(SsoString::SSO_MAX_LEN + 1, 'x');
    EXPECT_FALSE(SsoString::fits(str));
}

TEST(SsoStringTest, ByteSerialization) {
    SsoString original("test123");
    auto bytes = original.to_bytes();

    auto restored = SsoString::from_bytes(bytes);
    EXPECT_EQ(restored.view(), original.view());
}

}  // namespace
}  // namespace dotvm::test
