# PDR-0002: Language Choice: C++26

## Status

Approved

## Executive Summary

Dotlanth uses C++26 as its implementation language, leveraging modern language features for safety, expressiveness, and performance while maintaining low-level control essential for a high-performance virtual machine.

## Problem Statement

The implementation language for a virtual machine directly impacts:
- **Performance**: Instruction dispatch speed, memory layout control
- **Safety**: Memory safety, type safety, undefined behavior avoidance
- **Maintainability**: Code clarity, tooling support, developer productivity
- **Portability**: Cross-platform compilation, dependency management

The choice must balance these concerns for a project that requires both high performance and correctness.

## Goals

- Achieve near-native performance for the hot execution loop
- Maximize compile-time safety guarantees
- Use modern language features to reduce boilerplate and bugs
- Maintain control over memory layout and allocation
- Support major platforms (Linux, macOS, Windows)

## Non-Goals

- Maximum backwards compatibility with older compilers
- Support for platforms without C++26 compilers
- Use of language features for their own sake
- Interoperability with other languages (C API handles this separately)

## Proposed Solution

Use C++26 with the following compiler requirements:
- GCC 14+ or Clang 19+
- Meson build system for modern dependency management

Key C++26/23/20 features utilized:

| Feature | Standard | Use Case |
|---------|----------|----------|
| Concepts | C++20 | Zero-overhead interface constraints |
| `std::expected` | C++23 | Error handling without exceptions |
| `constexpr` everywhere | C++20+ | Compile-time evaluation |
| `std::span` | C++20 | Safe array views |
| `std::format` | C++20 | Type-safe formatting |
| `[[nodiscard]]` | C++17 | Enforce result checking |
| Ranges | C++20 | Composable algorithms |

Build configuration enforces strict warnings as errors (`-Werror`).

## Success Criteria

- Clean compilation with `-Wall -Wextra -Wpedantic -Werror`
- No undefined behavior detected by sanitizers (ASan, UBSan, TSan)
- Concepts provide clear error messages for interface violations
- Performance parity with C implementation of equivalent logic
- Compiles on both GCC and Clang without conditional code

## Risks and Mitigations

| Risk | Likelihood | Impact | Mitigation |
|------|------------|--------|------------|
| C++26 compiler availability | Low | Medium | Target GCC 14+/Clang 19+, widely available |
| Developer learning curve | Medium | Low | Modern C++ is more intuitive than legacy patterns |
| Build complexity | Low | Low | Meson handles dependencies cleanly |
| Compiler bugs in new features | Low | Medium | Test on multiple compilers; avoid bleeding-edge features |

## Open Questions

- When to adopt C++26 modules (pending compiler maturity)?
- What is the policy for third-party dependencies?

## References

- [CLAUDE.md: Code Style](../../CLAUDE.md)
- [ADR-0005: C++20 Concepts](../adr/0005-cpp20-concepts.md)
- [ADR-0006: std::expected](../adr/0006-std-expected-errors.md)
- [C++26 Feature Status](https://en.cppreference.com/w/cpp/26)
