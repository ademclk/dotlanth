# Contributing to DotLanth

Thank you for your interest in contributing to DotLanth! This document provides guidelines and best practices for contributing to the project.

## Table of Contents

- [Code of Conduct](#code-of-conduct)
- [Getting Started](#getting-started)
- [Development Workflow](#development-workflow)
- [Code Style Guidelines](#code-style-guidelines)
- [Pull Request Process](#pull-request-process)
- [Security Checklist](#security-checklist)
- [Testing Requirements](#testing-requirements)
- [Documentation Standards](#documentation-standards)

## Code of Conduct

By participating in this project, you agree to maintain a respectful and inclusive environment for all contributors.

## Getting Started

### Prerequisites

- C++26 compatible compiler (Clang 18+ or GCC 14+)
- CMake 3.28 or later
- Ninja build system (recommended)

### Building the Project

```bash
# Configure with Clang (recommended)
cmake --preset ci-clang-release

# Build
cmake --build --preset ci-clang-release

# Run tests
ctest --preset ci-clang-release --output-on-failure
```

### Sanitizer Builds

Always run sanitizer builds before submitting a PR:

```bash
cmake --preset ci-clang-sanitizers
cmake --build --preset ci-clang-sanitizers
ctest --preset ci-clang-sanitizers --output-on-failure
```

## Development Workflow

1. Fork the repository and create a feature branch from `main`
2. Make your changes following the code style guidelines
3. Write or update tests for your changes
4. Ensure all tests pass (including sanitizer builds)
5. Submit a pull request

### Branch Naming

Use descriptive branch names following this pattern:
- `feature/<ticket-id>-<description>` for new features
- `fix/<ticket-id>-<description>` for bug fixes
- `docs/<description>` for documentation changes

## Code Style Guidelines

### C++ Style

- Follow the existing code style in the repository
- Use `clang-format` with the project's `.clang-format` configuration
- Use `clang-tidy` to check for common issues

### Naming Conventions

- **Types**: `PascalCase` (e.g., `VmContext`, `ExecutionEngine`)
- **Functions**: `snake_case` (e.g., `execute_bytecode`, `read_file`)
- **Variables**: `snake_case` (e.g., `instruction_count`, `max_memory`)
- **Constants**: `kPascalCase` or `SCREAMING_SNAKE_CASE` for macros
- **Member variables**: `snake_case_` with trailing underscore
- **Namespaces**: `lowercase` (e.g., `dotvm::core`, `dotvm::exec`)

### Documentation

- Use Doxygen-style comments (`///` or `/** */`) for public APIs
- Include `@brief`, `@param`, and `@return` tags
- Document security implications for sensitive functions

### Modern C++ Practices

- Prefer `std::expected` over exceptions for error handling
- Use `[[nodiscard]]` for functions where ignoring the return value is likely a bug
- Prefer `constexpr` and `consteval` where possible
- Use `std::span` instead of pointer + size pairs
- Avoid raw `new`/`delete` - use smart pointers

## Pull Request Process

### Before Submitting

1. Run `clang-format` on all modified files
2. Run all tests with the release preset
3. Run all tests with the sanitizer preset
4. Review the security checklist below
5. Update documentation if needed

### PR Description

Include:
- Summary of changes
- Related issue/ticket numbers
- Testing performed
- Security considerations (if applicable)

### Review Process

- All PRs require at least one approval
- Security-sensitive changes require security team review
- CI must pass before merging

## Security Checklist

Before submitting any PR, review this security checklist:

### Memory Safety

- [ ] No raw pointer arithmetic without bounds checking
- [ ] All allocations have corresponding deallocations
- [ ] Buffer sizes validated before access
- [ ] Handle generation counters checked for use-after-free protection

### Capability System (SEC-001 through SEC-010)

- [ ] Permission checks use `has_permission()` helper
- [ ] New syscalls specify required capabilities
- [ ] Capability-sensitive operations are audited
- [ ] No capability escalation vulnerabilities

### Input Validation

- [ ] All external inputs are validated
- [ ] Bytecode is validated before execution
- [ ] File paths are sanitized
- [ ] Integer overflow is prevented

### Control Flow Integrity

- [ ] CFI checks are not bypassed
- [ ] Jump targets are validated
- [ ] Call/return pairs are balanced
- [ ] No unbounded recursion

### Resource Limits

- [ ] Instruction limits are enforced
- [ ] Memory limits are enforced
- [ ] Allocation counts are tracked
- [ ] Timeouts are implemented for long operations

### Cryptographic Operations

- [ ] Use only approved algorithms (AES-256-GCM, SHA-256, Ed25519, etc.)
- [ ] Keys are properly generated and stored
- [ ] Sensitive data is cleared after use
- [ ] No timing side channels

### Audit Logging

- [ ] Security-sensitive operations are logged
- [ ] Audit events include sufficient context
- [ ] Log injection is prevented

## Testing Requirements

### Unit Tests

- All new functions should have unit tests
- Test both success and failure cases
- Test edge cases and boundary conditions

### Integration Tests

- Test component interactions
- Test full execution paths
- Test error propagation

### Property-Based Tests

For complex logic, consider property-based testing:

```cpp
#include "test_utils/property_testing.hpp"

TEST(ValueTest, PropertyAdditionCommutative) {
    property::forAll<int64_t, int64_t>([](int64_t a, int64_t b) {
        return a + b == b + a;
    }, 100);
}
```

### Sanitizer Testing

All tests must pass under:
- AddressSanitizer (ASan)
- UndefinedBehaviorSanitizer (UBSan)
- ThreadSanitizer (TSan) for concurrent code

### Coverage

- Aim for high code coverage on new code
- Critical security paths should have 100% coverage

## Documentation Standards

### Code Documentation

- All public APIs must have Doxygen documentation
- Include examples for complex functions
- Document thread safety guarantees
- Document error conditions

### Architecture Documentation

- Update architecture docs for significant changes
- Include diagrams where helpful
- Document design decisions and tradeoffs

### Changelog

- Update CHANGELOG.md for user-facing changes
- Follow Keep a Changelog format
- Reference issue/ticket numbers

## Questions?

If you have questions about contributing, please open an issue or reach out to the maintainers.

---

Thank you for contributing to DotLanth!
