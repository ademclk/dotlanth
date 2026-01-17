/// @file audit_event_test.cpp
/// @brief Unit tests for SEC-006 Audit Event types

#include <sstream>

#include <gtest/gtest.h>

#include "dotvm/core/security/audit_event.hpp"
#include "dotvm/core/security/audit_logger.hpp"
#include "dotvm/core/security/audit_serializer.hpp"

namespace dotvm::core::security::audit {
namespace {

// ============================================================================
// AuditSeverity Tests
// ============================================================================

TEST(AuditSeverityTest, ToStringAllValues) {
    EXPECT_STREQ(to_string(AuditSeverity::Debug), "Debug");
    EXPECT_STREQ(to_string(AuditSeverity::Info), "Info");
    EXPECT_STREQ(to_string(AuditSeverity::Warning), "Warning");
    EXPECT_STREQ(to_string(AuditSeverity::Error), "Error");
    EXPECT_STREQ(to_string(AuditSeverity::Critical), "Critical");
}

TEST(AuditSeverityTest, Ordering) {
    EXPECT_LT(static_cast<int>(AuditSeverity::Debug), static_cast<int>(AuditSeverity::Info));
    EXPECT_LT(static_cast<int>(AuditSeverity::Info), static_cast<int>(AuditSeverity::Warning));
    EXPECT_LT(static_cast<int>(AuditSeverity::Warning), static_cast<int>(AuditSeverity::Error));
    EXPECT_LT(static_cast<int>(AuditSeverity::Error), static_cast<int>(AuditSeverity::Critical));
}

// ============================================================================
// AuditEventType Tests
// ============================================================================

TEST(AuditEventTypeTest, ToStringAllValues) {
    // Permission events
    EXPECT_STREQ(to_string(AuditEventType::PermissionGranted), "PermissionGranted");
    EXPECT_STREQ(to_string(AuditEventType::PermissionDenied), "PermissionDenied");

    // Resource events
    EXPECT_STREQ(to_string(AuditEventType::AllocationAttempt), "AllocationAttempt");
    EXPECT_STREQ(to_string(AuditEventType::AllocationDenied), "AllocationDenied");
    EXPECT_STREQ(to_string(AuditEventType::DeallocationAttempt), "DeallocationAttempt");
    EXPECT_STREQ(to_string(AuditEventType::InstructionLimitHit), "InstructionLimitHit");
    EXPECT_STREQ(to_string(AuditEventType::StackDepthLimitHit), "StackDepthLimitHit");
    EXPECT_STREQ(to_string(AuditEventType::TimeLimitHit), "TimeLimitHit");

    // Context lifecycle
    EXPECT_STREQ(to_string(AuditEventType::ContextCreated), "ContextCreated");
    EXPECT_STREQ(to_string(AuditEventType::ContextDestroyed), "ContextDestroyed");
    EXPECT_STREQ(to_string(AuditEventType::ContextReset), "ContextReset");
    EXPECT_STREQ(to_string(AuditEventType::OpcodeDenied), "OpcodeDenied");

    // SEC-006 new event types
    EXPECT_STREQ(to_string(AuditEventType::DotStarted), "DotStarted");
    EXPECT_STREQ(to_string(AuditEventType::DotCompleted), "DotCompleted");
    EXPECT_STREQ(to_string(AuditEventType::DotFailed), "DotFailed");
    EXPECT_STREQ(to_string(AuditEventType::CapabilityCreated), "CapabilityCreated");
    EXPECT_STREQ(to_string(AuditEventType::CapabilityRevoked), "CapabilityRevoked");
    EXPECT_STREQ(to_string(AuditEventType::MemoryLimitExceeded), "MemoryLimitExceeded");
    EXPECT_STREQ(to_string(AuditEventType::SecurityViolation), "SecurityViolation");
}

TEST(AuditEventTypeTest, DefaultSeverity) {
    // Critical severity
    EXPECT_EQ(default_severity(AuditEventType::SecurityViolation), AuditSeverity::Critical);

    // Error severity
    EXPECT_EQ(default_severity(AuditEventType::PermissionDenied), AuditSeverity::Error);
    EXPECT_EQ(default_severity(AuditEventType::AllocationDenied), AuditSeverity::Error);
    EXPECT_EQ(default_severity(AuditEventType::DotFailed), AuditSeverity::Error);
    EXPECT_EQ(default_severity(AuditEventType::MemoryLimitExceeded), AuditSeverity::Error);

    // Warning severity
    EXPECT_EQ(default_severity(AuditEventType::CapabilityRevoked), AuditSeverity::Warning);

    // Info severity
    EXPECT_EQ(default_severity(AuditEventType::DotStarted), AuditSeverity::Info);
    EXPECT_EQ(default_severity(AuditEventType::DotCompleted), AuditSeverity::Info);
    EXPECT_EQ(default_severity(AuditEventType::ContextCreated), AuditSeverity::Info);
}

// ============================================================================
// AuditEvent Tests
// ============================================================================

TEST(AuditEventTest, DefaultConstruction) {
    AuditEvent event;
    EXPECT_EQ(event.type, AuditEventType::ContextCreated);
    EXPECT_EQ(event.severity, AuditSeverity::Info);
    EXPECT_EQ(event.permission, Permission::None);
    EXPECT_EQ(event.value, 0);
    EXPECT_EQ(event.dot_id, 0);
    EXPECT_EQ(event.capability_id, 0);
    EXPECT_TRUE(event.message.empty());
    EXPECT_TRUE(event.metadata.empty());
}

TEST(AuditEventTest, NowFactory) {
    auto before = std::chrono::steady_clock::now();
    auto event =
        AuditEvent::now(AuditEventType::DotStarted, AuditSeverity::Info, Permission::Execute, 42);
    auto after = std::chrono::steady_clock::now();

    EXPECT_EQ(event.type, AuditEventType::DotStarted);
    EXPECT_EQ(event.severity, AuditSeverity::Info);
    EXPECT_EQ(event.permission, Permission::Execute);
    EXPECT_EQ(event.value, 42);
    EXPECT_GE(event.timestamp, before);
    EXPECT_LE(event.timestamp, after);
}

TEST(AuditEventTest, NowDefaultFactory) {
    auto event = AuditEvent::now_default(AuditEventType::SecurityViolation);
    EXPECT_EQ(event.type, AuditEventType::SecurityViolation);
    EXPECT_EQ(event.severity, AuditSeverity::Critical);
}

TEST(AuditEventTest, ForDotFactory) {
    auto event = AuditEvent::for_dot(123, AuditEventType::DotStarted, AuditSeverity::Info,
                                     "Starting dot 123");
    EXPECT_EQ(event.type, AuditEventType::DotStarted);
    EXPECT_EQ(event.dot_id, 123);
    EXPECT_EQ(event.message, "Starting dot 123");
}

TEST(AuditEventTest, ForCapabilityFactory) {
    auto event = AuditEvent::for_capability(456, AuditEventType::CapabilityCreated,
                                            AuditSeverity::Info, "New capability");
    EXPECT_EQ(event.type, AuditEventType::CapabilityCreated);
    EXPECT_EQ(event.capability_id, 456);
    EXPECT_EQ(event.message, "New capability");
}

TEST(AuditEventTest, FluentAPIMetadata) {
    auto event = AuditEvent::now(AuditEventType::AllocationAttempt)
                     .with_metadata("size", "1024")
                     .with_metadata("allocator", "heap");

    EXPECT_EQ(event.metadata.size(), 2);
    EXPECT_EQ(event.metadata[0].first, "size");
    EXPECT_EQ(event.metadata[0].second, "1024");
    EXPECT_EQ(event.metadata[1].first, "allocator");
    EXPECT_EQ(event.metadata[1].second, "heap");
}

TEST(AuditEventTest, FluentAPIMessage) {
    auto event = AuditEvent::now(AuditEventType::DotStarted).with_message("Execution started");
    EXPECT_EQ(event.message, "Execution started");
}

TEST(AuditEventTest, FluentAPIChaining) {
    auto event = AuditEvent::now(AuditEventType::DotFailed)
                     .with_dot(100)
                     .with_capability(200)
                     .with_permission(Permission::Execute)
                     .with_value(42)
                     .with_severity(AuditSeverity::Error)
                     .with_message("Failed due to error")
                     .with_metadata("error_code", "E001");

    EXPECT_EQ(event.dot_id, 100);
    EXPECT_EQ(event.capability_id, 200);
    EXPECT_EQ(event.permission, Permission::Execute);
    EXPECT_EQ(event.value, 42);
    EXPECT_EQ(event.severity, AuditSeverity::Error);
    EXPECT_EQ(event.message, "Failed due to error");
    EXPECT_EQ(event.metadata.size(), 1);
}

// ============================================================================
// AuditQuery Tests
// ============================================================================

TEST(AuditQueryTest, DefaultConstruction) {
    AuditQuery query;
    EXPECT_FALSE(query.type.has_value());
    EXPECT_FALSE(query.min_severity.has_value());
    EXPECT_FALSE(query.dot_id.has_value());
    EXPECT_EQ(query.limit, 100);
}

TEST(AuditQueryTest, AllFactory) {
    auto query = AuditQuery::all(500);
    EXPECT_FALSE(query.type.has_value());
    EXPECT_EQ(query.limit, 500);
}

TEST(AuditQueryTest, ByTypeFactory) {
    auto query = AuditQuery::by_type(AuditEventType::DotStarted, 50);
    EXPECT_TRUE(query.type.has_value());
    EXPECT_EQ(*query.type, AuditEventType::DotStarted);
    EXPECT_EQ(query.limit, 50);
}

TEST(AuditQueryTest, BySeverityFactory) {
    auto query = AuditQuery::by_severity(AuditSeverity::Error);
    EXPECT_TRUE(query.min_severity.has_value());
    EXPECT_EQ(*query.min_severity, AuditSeverity::Error);
}

TEST(AuditQueryTest, ByDotFactory) {
    auto query = AuditQuery::by_dot(123);
    EXPECT_TRUE(query.dot_id.has_value());
    EXPECT_EQ(*query.dot_id, 123);
}

// ============================================================================
// SimpleBufferedAuditLogger Tests
// ============================================================================

TEST(SimpleBufferedAuditLoggerTest, DefaultConstruction) {
    SimpleBufferedAuditLogger logger;
    EXPECT_TRUE(logger.is_enabled());
    EXPECT_EQ(logger.size(), 0);
    EXPECT_EQ(logger.capacity(), 1024);
}

TEST(SimpleBufferedAuditLoggerTest, CustomCapacity) {
    SimpleBufferedAuditLogger logger(100);
    EXPECT_EQ(logger.capacity(), 100);
}

TEST(SimpleBufferedAuditLoggerTest, LogAndRetrieve) {
    SimpleBufferedAuditLogger logger(10);

    logger.log(AuditEvent::now(AuditEventType::DotStarted).with_dot(1).with_message("Start"));
    logger.log(AuditEvent::now(AuditEventType::DotCompleted).with_dot(1).with_message("Complete"));

    EXPECT_EQ(logger.size(), 2);

    auto events = logger.query(AuditQuery::all());
    EXPECT_EQ(events.size(), 2);
    EXPECT_EQ(events[0].type, AuditEventType::DotStarted);
    EXPECT_EQ(events[1].type, AuditEventType::DotCompleted);
}

TEST(SimpleBufferedAuditLoggerTest, QueryByType) {
    SimpleBufferedAuditLogger logger(10);

    logger.log(AuditEvent::now(AuditEventType::DotStarted));
    logger.log(AuditEvent::now(AuditEventType::AllocationAttempt));
    logger.log(AuditEvent::now(AuditEventType::DotCompleted));
    logger.log(AuditEvent::now(AuditEventType::AllocationAttempt));

    auto events = logger.query(AuditQuery::by_type(AuditEventType::AllocationAttempt));
    EXPECT_EQ(events.size(), 2);
    for (const auto& e : events) {
        EXPECT_EQ(e.type, AuditEventType::AllocationAttempt);
    }
}

TEST(SimpleBufferedAuditLoggerTest, QueryBySeverity) {
    SimpleBufferedAuditLogger logger(10);

    logger.log(AuditEvent::now(AuditEventType::DotStarted, AuditSeverity::Info));
    logger.log(AuditEvent::now(AuditEventType::DotFailed, AuditSeverity::Error));
    logger.log(AuditEvent::now(AuditEventType::SecurityViolation, AuditSeverity::Critical));

    auto events = logger.query(AuditQuery::by_severity(AuditSeverity::Error));
    EXPECT_EQ(events.size(), 2);  // Error and Critical
}

TEST(SimpleBufferedAuditLoggerTest, QueryByDot) {
    SimpleBufferedAuditLogger logger(10);

    logger.log(AuditEvent::now(AuditEventType::DotStarted).with_dot(1));
    logger.log(AuditEvent::now(AuditEventType::DotStarted).with_dot(2));
    logger.log(AuditEvent::now(AuditEventType::DotCompleted).with_dot(1));

    auto events = logger.query(AuditQuery::by_dot(1));
    EXPECT_EQ(events.size(), 2);
    for (const auto& e : events) {
        EXPECT_EQ(e.dot_id, 1);
    }
}

TEST(SimpleBufferedAuditLoggerTest, QueryWithLimit) {
    SimpleBufferedAuditLogger logger(100);

    for (int i = 0; i < 50; ++i) {
        logger.log(AuditEvent::now(AuditEventType::AllocationAttempt));
    }

    AuditQuery query;
    query.limit = 10;
    auto events = logger.query(query);
    EXPECT_EQ(events.size(), 10);
}

TEST(SimpleBufferedAuditLoggerTest, RingBufferOverwrite) {
    SimpleBufferedAuditLogger logger(5);

    for (int i = 0; i < 10; ++i) {
        logger.log(AuditEvent::now(AuditEventType::AllocationAttempt)
                       .with_value(static_cast<std::uint64_t>(i)));
    }

    EXPECT_EQ(logger.size(), 5);  // Oldest events overwritten
}

TEST(SimpleBufferedAuditLoggerTest, Clear) {
    SimpleBufferedAuditLogger logger(10);

    logger.log(AuditEvent::now(AuditEventType::DotStarted));
    logger.log(AuditEvent::now(AuditEventType::DotCompleted));
    EXPECT_EQ(logger.size(), 2);

    logger.clear();
    EXPECT_EQ(logger.size(), 0);
}

TEST(SimpleBufferedAuditLoggerTest, Stats) {
    SimpleBufferedAuditLogger logger(10);

    logger.log(AuditEvent::now(AuditEventType::DotStarted));
    logger.log(AuditEvent::now(AuditEventType::DotCompleted));

    auto stats = logger.stats();
    EXPECT_EQ(stats.events_logged, 2);
    EXPECT_EQ(stats.buffer_capacity, 10);
    EXPECT_EQ(stats.buffer_used, 2);
}

// ============================================================================
// AuditSerializer Tests
// ============================================================================

TEST(AuditSerializerTest, ToJson) {
    auto event = AuditEvent::now(AuditEventType::DotStarted, AuditSeverity::Info)
                     .with_dot(123)
                     .with_capability(456)
                     .with_message("Test event")
                     .with_metadata("key", "value");

    auto json = AuditSerializer::to_json(event);

    EXPECT_NE(json.find("\"type\":\"DotStarted\""), std::string::npos);
    EXPECT_NE(json.find("\"severity\":\"Info\""), std::string::npos);
    EXPECT_NE(json.find("\"dot_id\":123"), std::string::npos);
    EXPECT_NE(json.find("\"capability_id\":456"), std::string::npos);
    EXPECT_NE(json.find("\"message\":\"Test event\""), std::string::npos);
    EXPECT_NE(json.find("\"metadata\":{"), std::string::npos);
    EXPECT_NE(json.find("\"key\":\"value\""), std::string::npos);
}

TEST(AuditSerializerTest, JsonEscaping) {
    auto event = AuditEvent::now(AuditEventType::DotStarted).with_message("Line1\nLine2\tTabbed");

    auto json = AuditSerializer::to_json(event);
    EXPECT_NE(json.find("\\n"), std::string::npos);
    EXPECT_NE(json.find("\\t"), std::string::npos);
}

TEST(AuditSerializerTest, ToJsonLines) {
    std::vector<AuditEvent> events;
    events.push_back(AuditEvent::now(AuditEventType::DotStarted));
    events.push_back(AuditEvent::now(AuditEventType::DotCompleted));

    std::ostringstream oss;
    AuditSerializer::to_json_lines(events, oss);

    std::string output = oss.str();
    // Should have two lines
    auto first_newline = output.find('\n');
    auto second_newline = output.find('\n', first_newline + 1);
    EXPECT_NE(first_newline, std::string::npos);
    EXPECT_NE(second_newline, std::string::npos);
}

TEST(AuditSerializerTest, ToText) {
    auto event = AuditEvent::now(AuditEventType::DotStarted, AuditSeverity::Info)
                     .with_dot(123)
                     .with_message("Starting");

    auto text = AuditSerializer::to_text(event);

    EXPECT_NE(text.find("[Info]"), std::string::npos);
    EXPECT_NE(text.find("[DotStarted]"), std::string::npos);
    EXPECT_NE(text.find("dot=123"), std::string::npos);
    EXPECT_NE(text.find("\"Starting\""), std::string::npos);
}

TEST(AuditSerializerTest, BinaryRoundTrip) {
    auto original = AuditEvent::now(AuditEventType::DotStarted, AuditSeverity::Info)
                        .with_dot(123)
                        .with_capability(456)
                        .with_permission(Permission::Execute)
                        .with_value(789)
                        .with_message("Test message")
                        .with_metadata("key1", "value1")
                        .with_metadata("key2", "value2");

    auto binary = AuditSerializer::to_binary(original);
    auto result = AuditSerializer::from_binary(binary);

    ASSERT_TRUE(result.is_ok());
    auto& parsed = result.value();

    EXPECT_EQ(parsed.type, original.type);
    EXPECT_EQ(parsed.severity, original.severity);
    EXPECT_EQ(parsed.dot_id, original.dot_id);
    EXPECT_EQ(parsed.capability_id, original.capability_id);
    EXPECT_EQ(parsed.permission, original.permission);
    EXPECT_EQ(parsed.value, original.value);
    EXPECT_EQ(parsed.message, original.message);
    EXPECT_EQ(parsed.metadata.size(), original.metadata.size());
    EXPECT_EQ(parsed.metadata[0].first, original.metadata[0].first);
    EXPECT_EQ(parsed.metadata[0].second, original.metadata[0].second);
}

// ============================================================================
// Export Tests
// ============================================================================

TEST(SimpleBufferedAuditLoggerTest, ExportToJson) {
    SimpleBufferedAuditLogger logger(10);

    logger.log(AuditEvent::now(AuditEventType::DotStarted).with_dot(1));
    logger.log(AuditEvent::now(AuditEventType::DotCompleted).with_dot(1));

    std::ostringstream oss;
    auto count = logger.export_to(oss, ExportFormat::Json, AuditQuery::all());

    EXPECT_EQ(count, 2);
    EXPECT_FALSE(oss.str().empty());
}

TEST(SimpleBufferedAuditLoggerTest, ExportToText) {
    SimpleBufferedAuditLogger logger(10);

    logger.log(AuditEvent::now(AuditEventType::DotStarted).with_dot(1));
    logger.log(AuditEvent::now(AuditEventType::DotCompleted).with_dot(1));

    std::ostringstream oss;
    auto count = logger.export_to(oss, ExportFormat::Text, AuditQuery::all());

    EXPECT_EQ(count, 2);
    EXPECT_NE(oss.str().find("[DotStarted]"), std::string::npos);
}

// ============================================================================
// NullAuditLogger Tests
// ============================================================================

TEST(NullAuditLoggerTest, Singleton) {
    auto& instance1 = NullAuditLogger::instance();
    auto& instance2 = NullAuditLogger::instance();
    EXPECT_EQ(&instance1, &instance2);
}

TEST(NullAuditLoggerTest, IsDisabled) {
    EXPECT_FALSE(NullAuditLogger::instance().is_enabled());
}

TEST(NullAuditLoggerTest, QueryReturnsEmpty) {
    auto events = NullAuditLogger::instance().query(AuditQuery::all());
    EXPECT_TRUE(events.empty());
}

TEST(NullAuditLoggerTest, ExportReturnsZero) {
    std::ostringstream oss;
    auto count = NullAuditLogger::instance().export_to(oss, ExportFormat::Json, AuditQuery::all());
    EXPECT_EQ(count, 0);
}

}  // namespace
}  // namespace dotvm::core::security::audit
