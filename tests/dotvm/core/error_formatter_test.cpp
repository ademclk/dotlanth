/// @file error_formatter_test.cpp
/// @brief MAINT-002 Unit tests for error enum std::formatter specializations
///
/// TDD tests for error-to-string conversions and std::format compatibility.
/// Tests written FIRST, before implementing formatters.

#include <format>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

// Error headers to test
#include "dotvm/core/asm/asm_error.hpp"
#include "dotvm/core/dsl/dsl_error.hpp"
#include "dotvm/core/policy/policy_error.hpp"
#include "dotvm/core/security/isolation_manager.hpp"
#include "dotvm/core/security/resource_limiter.hpp"
#include "dotvm/core/security/security_context.hpp"
#include "dotvm/core/state/state_backend.hpp"
#include "dotvm/core/state/wal_error.hpp"

// ============================================================================
// PolicyError Tests
// ============================================================================

namespace dotvm::core::policy {
namespace {

TEST(PolicyErrorFormatterTest, ToStringReturnsStringView) {
    // Verify to_string returns std::string_view
    auto result = to_string(PolicyError::Success);
    static_assert(std::is_same_v<decltype(result), std::string_view>);
    EXPECT_EQ(result, "Success");
}

TEST(PolicyErrorFormatterTest, ToStringAllValues) {
    EXPECT_EQ(to_string(PolicyError::Success), "Success");
    EXPECT_EQ(to_string(PolicyError::JsonSyntaxError), "JsonSyntaxError");
    EXPECT_EQ(to_string(PolicyError::UnexpectedEof), "UnexpectedEof");
    EXPECT_EQ(to_string(PolicyError::InvalidCharacter), "InvalidCharacter");
    EXPECT_EQ(to_string(PolicyError::InvalidEscapeSequence), "InvalidEscapeSequence");
    EXPECT_EQ(to_string(PolicyError::InvalidNumber), "InvalidNumber");
    EXPECT_EQ(to_string(PolicyError::InvalidUtf8), "InvalidUtf8");
    EXPECT_EQ(to_string(PolicyError::NestingTooDeep), "NestingTooDeep");
    EXPECT_EQ(to_string(PolicyError::MissingField), "MissingField");
    EXPECT_EQ(to_string(PolicyError::InvalidFieldType), "InvalidFieldType");
    EXPECT_EQ(to_string(PolicyError::InvalidRuleId), "InvalidRuleId");
    EXPECT_EQ(to_string(PolicyError::InvalidPriority), "InvalidPriority");
    EXPECT_EQ(to_string(PolicyError::UnknownOpcode), "UnknownOpcode");
    EXPECT_EQ(to_string(PolicyError::InvalidCondition), "InvalidCondition");
    EXPECT_EQ(to_string(PolicyError::InvalidAction), "InvalidAction");
    EXPECT_EQ(to_string(PolicyError::InvalidTimeWindow), "InvalidTimeWindow");
    EXPECT_EQ(to_string(PolicyError::InvalidMemoryRegion), "InvalidMemoryRegion");
    EXPECT_EQ(to_string(PolicyError::DuplicateRuleId), "DuplicateRuleId");
    EXPECT_EQ(to_string(PolicyError::NoPolicyLoaded), "NoPolicyLoaded");
    EXPECT_EQ(to_string(PolicyError::FileNotFound), "FileNotFound");
    EXPECT_EQ(to_string(PolicyError::FileReadError), "FileReadError");
    EXPECT_EQ(to_string(PolicyError::EvaluationError), "EvaluationError");
}

TEST(PolicyErrorFormatterTest, StdFormatWorks) {
    EXPECT_EQ(std::format("{}", PolicyError::Success), "Success");
    EXPECT_EQ(std::format("{}", PolicyError::JsonSyntaxError), "JsonSyntaxError");
    EXPECT_EQ(std::format("{}", PolicyError::FileNotFound), "FileNotFound");
}

TEST(PolicyErrorFormatterTest, FormatInContext) {
    auto msg = std::format("Error: {}", PolicyError::InvalidNumber);
    EXPECT_EQ(msg, "Error: InvalidNumber");
}

}  // namespace
}  // namespace dotvm::core::policy

// ============================================================================
// ResourceLimitError Tests
// ============================================================================

namespace dotvm::core::security {
namespace {

TEST(ResourceLimitErrorFormatterTest, ToStringReturnsStringView) {
    auto result = to_string(ResourceLimitError::Success);
    static_assert(std::is_same_v<decltype(result), std::string_view>);
    EXPECT_EQ(result, "Success");
}

TEST(ResourceLimitErrorFormatterTest, ToStringAllValues) {
    EXPECT_EQ(to_string(ResourceLimitError::Success), "Success");
    EXPECT_EQ(to_string(ResourceLimitError::MemoryLimitExceeded), "MemoryLimitExceeded");
    EXPECT_EQ(to_string(ResourceLimitError::InstructionLimitExceeded), "InstructionLimitExceeded");
    EXPECT_EQ(to_string(ResourceLimitError::StackDepthExceeded), "StackDepthExceeded");
    EXPECT_EQ(to_string(ResourceLimitError::AllocationSizeExceeded), "AllocationSizeExceeded");
    EXPECT_EQ(to_string(ResourceLimitError::TimeExpired), "TimeExpired");
    EXPECT_EQ(to_string(ResourceLimitError::Throttled), "Throttled");
}

TEST(ResourceLimitErrorFormatterTest, StdFormatWorks) {
    EXPECT_EQ(std::format("{}", ResourceLimitError::MemoryLimitExceeded), "MemoryLimitExceeded");
    EXPECT_EQ(std::format("{}", ResourceLimitError::Throttled), "Throttled");
}

// ============================================================================
// EnforcementAction Tests
// ============================================================================

TEST(EnforcementActionFormatterTest, ToStringReturnsStringView) {
    auto result = to_string(EnforcementAction::Allow);
    static_assert(std::is_same_v<decltype(result), std::string_view>);
    EXPECT_EQ(result, "Allow");
}

TEST(EnforcementActionFormatterTest, ToStringAllValues) {
    EXPECT_EQ(to_string(EnforcementAction::Allow), "Allow");
    EXPECT_EQ(to_string(EnforcementAction::Deny), "Deny");
    EXPECT_EQ(to_string(EnforcementAction::Throttle), "Throttle");
    EXPECT_EQ(to_string(EnforcementAction::Terminate), "Terminate");
}

TEST(EnforcementActionFormatterTest, StdFormatWorks) {
    EXPECT_EQ(std::format("{}", EnforcementAction::Allow), "Allow");
    EXPECT_EQ(std::format("{}", EnforcementAction::Terminate), "Terminate");
}

// ============================================================================
// SecurityContextError Tests
// ============================================================================

TEST(SecurityContextErrorFormatterTest, ToStringReturnsStringView) {
    auto result = to_string(SecurityContextError::Success);
    static_assert(std::is_same_v<decltype(result), std::string_view>);
    EXPECT_EQ(result, "Success");
}

TEST(SecurityContextErrorFormatterTest, ToStringAllValues) {
    EXPECT_EQ(to_string(SecurityContextError::Success), "Success");
    EXPECT_EQ(to_string(SecurityContextError::PermissionDenied), "PermissionDenied");
    EXPECT_EQ(to_string(SecurityContextError::MemoryLimitExceeded), "MemoryLimitExceeded");
    EXPECT_EQ(to_string(SecurityContextError::AllocationCountExceeded), "AllocationCountExceeded");
    EXPECT_EQ(to_string(SecurityContextError::AllocationSizeExceeded), "AllocationSizeExceeded");
    EXPECT_EQ(to_string(SecurityContextError::InstructionLimitExceeded),
              "InstructionLimitExceeded");
    EXPECT_EQ(to_string(SecurityContextError::StackDepthExceeded), "StackDepthExceeded");
    EXPECT_EQ(to_string(SecurityContextError::TimeLimitExceeded), "TimeLimitExceeded");
    EXPECT_EQ(to_string(SecurityContextError::ContextInvalid), "ContextInvalid");
}

TEST(SecurityContextErrorFormatterTest, StdFormatWorks) {
    EXPECT_EQ(std::format("{}", SecurityContextError::PermissionDenied), "PermissionDenied");
    EXPECT_EQ(std::format("{}", SecurityContextError::ContextInvalid), "ContextInvalid");
}

// ============================================================================
// AuditEventType Tests
// ============================================================================

TEST(AuditEventTypeFormatterTest, ToStringReturnsStringView) {
    auto result = to_string(AuditEventType::PermissionGranted);
    static_assert(std::is_same_v<decltype(result), std::string_view>);
    EXPECT_EQ(result, "PermissionGranted");
}

TEST(AuditEventTypeFormatterTest, StdFormatWorks) {
    EXPECT_EQ(std::format("{}", AuditEventType::PermissionGranted), "PermissionGranted");
    EXPECT_EQ(std::format("{}", AuditEventType::IsolationViolation), "IsolationViolation");
}

// ============================================================================
// IsolationError Tests
// ============================================================================

TEST(IsolationErrorFormatterTest, ToStringReturnsStringView) {
    auto result = to_string(IsolationError::Success);
    static_assert(std::is_same_v<decltype(result), std::string_view>);
    EXPECT_EQ(result, "Success");
}

TEST(IsolationErrorFormatterTest, ToStringAllValues) {
    EXPECT_EQ(to_string(IsolationError::Success), "Success");
    EXPECT_EQ(to_string(IsolationError::DotNotFound), "DotNotFound");
    EXPECT_EQ(to_string(IsolationError::DotAlreadyExists), "DotAlreadyExists");
    EXPECT_EQ(to_string(IsolationError::ParentNotFound), "ParentNotFound");
    EXPECT_EQ(to_string(IsolationError::AccessDenied), "AccessDenied");
    EXPECT_EQ(to_string(IsolationError::GrantNotFound), "GrantNotFound");
    EXPECT_EQ(to_string(IsolationError::GrantRevoked), "GrantRevoked");
    EXPECT_EQ(to_string(IsolationError::SyscallDenied), "SyscallDenied");
    EXPECT_EQ(to_string(IsolationError::HandleNotOwned), "HandleNotOwned");
    EXPECT_EQ(to_string(IsolationError::InvalidRelationship), "InvalidRelationship");
    EXPECT_EQ(to_string(IsolationError::HasActiveChildren), "HasActiveChildren");
    EXPECT_EQ(to_string(IsolationError::NetworkDenied), "NetworkDenied");
    EXPECT_EQ(to_string(IsolationError::FilesystemDenied), "FilesystemDenied");
    EXPECT_EQ(to_string(IsolationError::InternalError), "InternalError");
}

TEST(IsolationErrorFormatterTest, StdFormatWorks) {
    EXPECT_EQ(std::format("{}", IsolationError::AccessDenied), "AccessDenied");
    EXPECT_EQ(std::format("{}", IsolationError::SyscallDenied), "SyscallDenied");
}

// ============================================================================
// AccessType Tests
// ============================================================================

TEST(AccessTypeFormatterTest, ToStringReturnsStringView) {
    auto result = to_string(AccessType::Read);
    static_assert(std::is_same_v<decltype(result), std::string_view>);
    EXPECT_EQ(result, "Read");
}

TEST(AccessTypeFormatterTest, StdFormatWorks) {
    EXPECT_EQ(std::format("{}", AccessType::Read), "Read");
    EXPECT_EQ(std::format("{}", AccessType::Write), "Write");
}

}  // namespace
}  // namespace dotvm::core::security

// ============================================================================
// AsmError Tests
// ============================================================================

namespace dotvm::core::asm_ {
namespace {

TEST(AsmErrorFormatterTest, ToStringReturnsStringView) {
    auto result = to_string(AsmError::Success);
    static_assert(std::is_same_v<decltype(result), std::string_view>);
    EXPECT_EQ(result, "Success");
}

TEST(AsmErrorFormatterTest, ToStringAllValues) {
    // Lexer errors
    EXPECT_EQ(to_string(AsmError::Success), "Success");
    EXPECT_EQ(to_string(AsmError::UnexpectedCharacter), "UnexpectedCharacter");
    EXPECT_EQ(to_string(AsmError::InvalidRegister), "InvalidRegister");
    EXPECT_EQ(to_string(AsmError::InvalidImmediate), "InvalidImmediate");
    EXPECT_EQ(to_string(AsmError::UnterminatedString), "UnterminatedString");
    EXPECT_EQ(to_string(AsmError::InvalidEscapeSequence), "InvalidEscapeSequence");
    EXPECT_EQ(to_string(AsmError::InvalidHexNumber), "InvalidHexNumber");
    EXPECT_EQ(to_string(AsmError::InvalidBinaryNumber), "InvalidBinaryNumber");
    EXPECT_EQ(to_string(AsmError::InvalidLabel), "InvalidLabel");
    EXPECT_EQ(to_string(AsmError::InvalidDirective), "InvalidDirective");
    EXPECT_EQ(to_string(AsmError::UnexpectedEof), "UnexpectedEof");
    EXPECT_EQ(to_string(AsmError::NumberOutOfRange), "NumberOutOfRange");

    // Parser errors
    EXPECT_EQ(to_string(AsmError::UnexpectedToken), "UnexpectedToken");
    EXPECT_EQ(to_string(AsmError::ExpectedOpcode), "ExpectedOpcode");
    EXPECT_EQ(to_string(AsmError::ExpectedRegister), "ExpectedRegister");
    EXPECT_EQ(to_string(AsmError::ExpectedImmediate), "ExpectedImmediate");
    EXPECT_EQ(to_string(AsmError::ExpectedLabel), "ExpectedLabel");
    EXPECT_EQ(to_string(AsmError::ExpectedComma), "ExpectedComma");
    EXPECT_EQ(to_string(AsmError::ExpectedNewline), "ExpectedNewline");
    EXPECT_EQ(to_string(AsmError::ExpectedColon), "ExpectedColon");
    EXPECT_EQ(to_string(AsmError::ExpectedRBracket), "ExpectedRBracket");
    EXPECT_EQ(to_string(AsmError::InvalidOperandCount), "InvalidOperandCount");
    EXPECT_EQ(to_string(AsmError::InvalidOperandType), "InvalidOperandType");
    EXPECT_EQ(to_string(AsmError::UnknownOpcode), "UnknownOpcode");
    EXPECT_EQ(to_string(AsmError::ExpectedDirectiveArg), "ExpectedDirectiveArg");
    EXPECT_EQ(to_string(AsmError::ExpectedString), "ExpectedString");

    // Semantic errors
    EXPECT_EQ(to_string(AsmError::DuplicateLabel), "DuplicateLabel");
    EXPECT_EQ(to_string(AsmError::UndefinedLabel), "UndefinedLabel");
    EXPECT_EQ(to_string(AsmError::CircularInclude), "CircularInclude");
    EXPECT_EQ(to_string(AsmError::IncludeDepthExceeded), "IncludeDepthExceeded");
    EXPECT_EQ(to_string(AsmError::FileNotFound), "FileNotFound");
    EXPECT_EQ(to_string(AsmError::InvalidSection), "InvalidSection");
}

TEST(AsmErrorFormatterTest, StdFormatWorks) {
    EXPECT_EQ(std::format("{}", AsmError::Success), "Success");
    EXPECT_EQ(std::format("{}", AsmError::UnexpectedToken), "UnexpectedToken");
    EXPECT_EQ(std::format("{}", AsmError::DuplicateLabel), "DuplicateLabel");
}

}  // namespace
}  // namespace dotvm::core::asm_

// ============================================================================
// DslError Tests
// ============================================================================

namespace dotvm::core::dsl {
namespace {

TEST(DslErrorFormatterTest, ToStringReturnsStringView) {
    auto result = to_string(DslError::Success);
    static_assert(std::is_same_v<decltype(result), std::string_view>);
    EXPECT_EQ(result, "Success");
}

TEST(DslErrorFormatterTest, ToStringAllValues) {
    // Lexer errors
    EXPECT_EQ(to_string(DslError::Success), "Success");
    EXPECT_EQ(to_string(DslError::UnexpectedCharacter), "UnexpectedCharacter");
    EXPECT_EQ(to_string(DslError::UnterminatedString), "UnterminatedString");
    EXPECT_EQ(to_string(DslError::InvalidEscapeSequence), "InvalidEscapeSequence");
    EXPECT_EQ(to_string(DslError::InvalidNumber), "InvalidNumber");
    EXPECT_EQ(to_string(DslError::UnterminatedInterpolation), "UnterminatedInterpolation");
    EXPECT_EQ(to_string(DslError::InvalidInterpolation), "InvalidInterpolation");
    EXPECT_EQ(to_string(DslError::InconsistentIndentation), "InconsistentIndentation");
    EXPECT_EQ(to_string(DslError::InvalidIndentation), "InvalidIndentation");
    EXPECT_EQ(to_string(DslError::UnexpectedEof), "UnexpectedEof");
    EXPECT_EQ(to_string(DslError::InvalidIdentifier), "InvalidIdentifier");

    // Parser errors
    EXPECT_EQ(to_string(DslError::UnexpectedToken), "UnexpectedToken");
    EXPECT_EQ(to_string(DslError::ExpectedIdentifier), "ExpectedIdentifier");
    EXPECT_EQ(to_string(DslError::ExpectedColon), "ExpectedColon");
    EXPECT_EQ(to_string(DslError::ExpectedExpression), "ExpectedExpression");
    EXPECT_EQ(to_string(DslError::ExpectedKeyword), "ExpectedKeyword");
    EXPECT_EQ(to_string(DslError::ExpectedIndent), "ExpectedIndent");
    EXPECT_EQ(to_string(DslError::ExpectedDedent), "ExpectedDedent");
    EXPECT_EQ(to_string(DslError::ExpectedString), "ExpectedString");
    EXPECT_EQ(to_string(DslError::ExpectedNewline), "ExpectedNewline");
    EXPECT_EQ(to_string(DslError::InvalidAssignment), "InvalidAssignment");
    EXPECT_EQ(to_string(DslError::ExpectedArrow), "ExpectedArrow");
    EXPECT_EQ(to_string(DslError::InvalidTriggerCondition), "InvalidTriggerCondition");
    EXPECT_EQ(to_string(DslError::InvalidAction), "InvalidAction");

    // Structural errors
    EXPECT_EQ(to_string(DslError::DuplicateState), "DuplicateState");
    EXPECT_EQ(to_string(DslError::DuplicateVariable), "DuplicateVariable");
    EXPECT_EQ(to_string(DslError::UndefinedVariable), "UndefinedVariable");
    EXPECT_EQ(to_string(DslError::InvalidDotDef), "InvalidDotDef");
    EXPECT_EQ(to_string(DslError::NestingTooDeep), "NestingTooDeep");
    EXPECT_EQ(to_string(DslError::InvalidImportPath), "InvalidImportPath");
    EXPECT_EQ(to_string(DslError::DuplicateImport), "DuplicateImport");
    EXPECT_EQ(to_string(DslError::CircularImport), "CircularImport");
}

TEST(DslErrorFormatterTest, StdFormatWorks) {
    EXPECT_EQ(std::format("{}", DslError::Success), "Success");
    EXPECT_EQ(std::format("{}", DslError::UnexpectedToken), "UnexpectedToken");
    EXPECT_EQ(std::format("{}", DslError::CircularImport), "CircularImport");
}

}  // namespace
}  // namespace dotvm::core::dsl

// ============================================================================
// StateBackendError Tests
// ============================================================================

namespace dotvm::core::state {
namespace {

TEST(StateBackendErrorFormatterTest, ToStringReturnsStringView) {
    auto result = to_string(StateBackendError::KeyNotFound);
    static_assert(std::is_same_v<decltype(result), std::string_view>);
    EXPECT_EQ(result, "KeyNotFound");
}

TEST(StateBackendErrorFormatterTest, ToStringAllValues) {
    // Key/Value errors
    EXPECT_EQ(to_string(StateBackendError::KeyNotFound), "KeyNotFound");
    EXPECT_EQ(to_string(StateBackendError::KeyTooLarge), "KeyTooLarge");
    EXPECT_EQ(to_string(StateBackendError::ValueTooLarge), "ValueTooLarge");
    EXPECT_EQ(to_string(StateBackendError::InvalidKey), "InvalidKey");

    // Transaction errors
    EXPECT_EQ(to_string(StateBackendError::TransactionNotActive), "TransactionNotActive");
    EXPECT_EQ(to_string(StateBackendError::TransactionConflict), "TransactionConflict");
    EXPECT_EQ(to_string(StateBackendError::InvalidTransaction), "InvalidTransaction");
    EXPECT_EQ(to_string(StateBackendError::DeadlockDetected), "DeadlockDetected");
    EXPECT_EQ(to_string(StateBackendError::TooManyTransactions), "TooManyTransactions");
    EXPECT_EQ(to_string(StateBackendError::TransactionTimeout), "TransactionTimeout");
    EXPECT_EQ(to_string(StateBackendError::ReadSetValidationFailed), "ReadSetValidationFailed");

    // Backend errors
    EXPECT_EQ(to_string(StateBackendError::StorageFull), "StorageFull");
    EXPECT_EQ(to_string(StateBackendError::BackendClosed), "BackendClosed");

    // Iteration errors
    EXPECT_EQ(to_string(StateBackendError::IterationAborted), "IterationAborted");
    EXPECT_EQ(to_string(StateBackendError::InvalidPrefix), "InvalidPrefix");

    // Configuration errors
    EXPECT_EQ(to_string(StateBackendError::InvalidConfig), "InvalidConfig");
    EXPECT_EQ(to_string(StateBackendError::UnsupportedOperation), "UnsupportedOperation");
}

TEST(StateBackendErrorFormatterTest, StdFormatWorks) {
    EXPECT_EQ(std::format("{}", StateBackendError::KeyNotFound), "KeyNotFound");
    EXPECT_EQ(std::format("{}", StateBackendError::TransactionConflict), "TransactionConflict");
}

// ============================================================================
// TransactionIsolationLevel Tests
// ============================================================================

TEST(TransactionIsolationLevelFormatterTest, ToStringReturnsStringView) {
    auto result = to_string(TransactionIsolationLevel::ReadCommitted);
    static_assert(std::is_same_v<decltype(result), std::string_view>);
    EXPECT_EQ(result, "ReadCommitted");
}

TEST(TransactionIsolationLevelFormatterTest, StdFormatWorks) {
    EXPECT_EQ(std::format("{}", TransactionIsolationLevel::ReadCommitted), "ReadCommitted");
    EXPECT_EQ(std::format("{}", TransactionIsolationLevel::Snapshot), "Snapshot");
}

// ============================================================================
// WalError Tests
// ============================================================================

TEST(WalErrorFormatterTest, ToStringReturnsStringView) {
    auto result = to_string(WalError::WalWriteFailed);
    static_assert(std::is_same_v<decltype(result), std::string_view>);
    EXPECT_EQ(result, "WalWriteFailed");
}

TEST(WalErrorFormatterTest, ToStringAllValues) {
    EXPECT_EQ(to_string(WalError::WalWriteFailed), "WalWriteFailed");
    EXPECT_EQ(to_string(WalError::WalReadFailed), "WalReadFailed");
    EXPECT_EQ(to_string(WalError::WalSyncFailed), "WalSyncFailed");
    EXPECT_EQ(to_string(WalError::WalCorrupted), "WalCorrupted");
    EXPECT_EQ(to_string(WalError::WalTruncateFailed), "WalTruncateFailed");
    EXPECT_EQ(to_string(WalError::CheckpointFailed), "CheckpointFailed");
    EXPECT_EQ(to_string(WalError::CheckpointCorrupted), "CheckpointCorrupted");
    EXPECT_EQ(to_string(WalError::RecoveryFailed), "RecoveryFailed");
    EXPECT_EQ(to_string(WalError::PartialRecovery), "PartialRecovery");
}

TEST(WalErrorFormatterTest, StdFormatWorks) {
    EXPECT_EQ(std::format("{}", WalError::WalWriteFailed), "WalWriteFailed");
    EXPECT_EQ(std::format("{}", WalError::CheckpointFailed), "CheckpointFailed");
}

}  // namespace
}  // namespace dotvm::core::state
