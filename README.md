# Dotlanth

**An intent-driven execution platform for building deterministic, state-centric applications.**

[![CI](https://github.com/ademclk/dotlanth/actions/workflows/ci.yml/badge.svg)](https://github.com/ademclk/dotlanth/actions/workflows/ci.yml)
[![C++26](https://img.shields.io/badge/C%2B%2B-26-blue.svg)](https://en.cppreference.com/w/cpp/26)
[![License: GPL v2](https://img.shields.io/badge/License-GPL_v2-blue.svg)](https://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html)

## Why Dotlanth?

**Coordination is the hard problem.**

Building reliable systems requires more than fast execution. It requires:
- Deterministic behavior that produces the same results across runs and machines
- First-class state management with transactional guarantees
- Clear boundaries between what code can and cannot do
- The ability to express *intent*—what you want to happen—not just the steps to get there

Existing runtimes optimize for raw speed or web compatibility. Dotlanth optimizes for *correctness* and *coordination*.

## What Dotlanth Is

- **Virtual execution environment (DotVM)** — A register-based VM with NaN-boxed values, handle-based memory, computed-goto dispatch, and optional JIT compilation
- **State-centric runtime (DotDB)** — Transactional state with write-ahead logging, Merkle Patricia tries, and snapshot support
- **Intent-based behavior system** — Declarative specification of desired outcomes, resolved at runtime
- **Platform for applications, services, and contracts** — Foundation layer for building higher-level systems

## What Dotlanth Is Not

| Category | Clarification |
|----------|--------------|
| A blockchain | No consensus mechanism, no distributed ledger, no cryptocurrency |
| A decentralized network | Single-node execution; distribution is a separate layer |
| A smart contract chain | No on-chain execution, no gas, no native token |
| A cloud wrapper | Runs locally; not a managed service |

Dotlanth provides the execution and state primitives. What you build on top is up to you.

## Core Concepts

### Intent-Based Execution

Instead of specifying step-by-step instructions, express what you want to achieve. The runtime figures out how to satisfy your intent within defined constraints.

### Deterministic State

All state changes are explicit, transactional, and reproducible. Given the same inputs, you get the same outputs—always. This enables testing, debugging, and distributed consensus without the complexity.

### Parallelism by Design

The execution model identifies independent operations and evaluates them concurrently. No manual threading, no race conditions by construction.

### No Infrastructure Coupling

Dotlanth runs locally. It doesn't require cloud services, blockchain networks, or specific deployment targets. Your code, your machine, your control.

## Architecture Overview

```
┌─────────────────────────────────────────────────────────────┐
│                        Applications                         │
├─────────────────────────────────────────────────────────────┤
│  DotDSL Compiler  │  Package Manager  │  CLI Tools          │
├───────────────────┴───────────────────┴─────────────────────┤
│                     DotVM Execution                         │
│  ┌─────────────┐  ┌─────────────┐  ┌──────────────────────┐ │
│  │ Interpreter │  │     JIT     │  │   Security Context   │ │
│  │  (computed  │  │ (x86-64)    │  │   Capabilities       │ │
│  │   goto)     │  │             │  │   Resource Limits    │ │
│  └─────────────┘  └─────────────┘  └──────────────────────┘ │
├─────────────────────────────────────────────────────────────┤
│                     DotDB State Layer                       │
│  ┌─────────────┐  ┌─────────────┐  ┌──────────────────────┐ │
│  │ Transaction │  │  Write-     │  │  Merkle Patricia     │ │
│  │  Manager    │  │  Ahead Log  │  │  Trie                │ │
│  └─────────────┘  └─────────────┘  └──────────────────────┘ │
└─────────────────────────────────────────────────────────────┘
```

For detailed architecture documentation, see [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

## Quick Start

### Requirements

- **C++26 Compiler**: Clang 19+ (recommended) or GCC 14+
- **Meson**: 1.2.0+
- **Ninja**: (recommended build backend)

### Build

```bash
# Configure and build (uses Clang+libc++ by default)
./meson-setup build
meson compile -C build

# Run tests
meson test -C build
```

### Sanitizer Builds

```bash
# AddressSanitizer + UndefinedBehaviorSanitizer
./meson-setup build-asan --native-file cross/clang-asan.ini --buildtype=debug
meson compile -C build-asan
meson test -C build-asan

# ThreadSanitizer (disable JIT for TSan compatibility)
./meson-setup build-tsan --native-file cross/clang-tsan.ini --buildtype=debug -Djit=false
```

### Build Options

| Option | Default | Description |
|--------|---------|-------------|
| `simd` | true | SIMD optimizations (-march=native) |
| `jit` | true | JIT compilation (x86-64 only) |
| `tests` | true | Build unit tests |
| `benchmarks` | false | Build benchmarks |
| `cli` | true | Build CLI tools (dotdsl, dotvm, dotdis, dotinfo, dotpkg) |
| `fuzzers` | false | Build fuzzing targets (requires Clang) |
| `sanitizers` | none | address / thread / memory / undefined |
| `postgresql` | false | Enable PostgreSQL state backend |

## Project Status

**v26.1.0-alpha** — APIs are unstable and will change.

Current state:
- Core VM execution: functional
- State layer: functional
- DSL compiler: functional
- JIT compilation: experimental (x86-64)
- Documentation: in progress

See [CHANGELOG.md](CHANGELOG.md) for version history.

## Who Is Dotlanth For?

**Systems developers** building:
- Deterministic execution environments
- State machine implementations
- Coordination systems
- Custom runtimes and languages

**Application developers** who need:
- Reproducible computation
- Transactional state semantics
- Intent-based programming models

## Philosophy

Dotlanth follows a few guiding principles:

1. **Correctness over cleverness** — Predictable behavior beats micro-optimizations. When in doubt, choose the approach that's easier to reason about.

2. **Explicit over implicit** — State changes, side effects, and failure modes should be visible in the code, not hidden in runtime magic.

3. **Composition over inheritance** — Small, focused components that combine cleanly. No god objects, no deep hierarchies.

4. **Tools over conventions** — Enforce constraints with types and static analysis, not documentation that no one reads.

## Documentation

| Document | Description |
|----------|-------------|
| [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) | VM internals and design |
| [docs/adr/](docs/adr/) | Architecture Decision Records |
| [docs/pdr/](docs/pdr/) | Product Decision Records |

## Development

### Code Style

- C++26 features preferred
- `snake_case` for functions/variables, `PascalCase` for types
- RAII for resource management—no raw `new`/`delete`
- All warnings treated as errors (`-Werror`)

### Pre-commit Hooks

```bash
pip install pre-commit
pre-commit install
pre-commit run --all-files
```

### Static Analysis

```bash
# clang-tidy
meson compile -C build clang-tidy

# cppcheck
meson compile -C build cppcheck
```

## License

GNU General Public License v2.0 — see [LICENSE](LICENSE).

---

Developed by [Synerthink](https://synerthink.com)