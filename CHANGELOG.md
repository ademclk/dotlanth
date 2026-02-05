# Changelog

All notable changes to DotVM will be documented in this file.

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/) conventions.

---

## [26.1.0-alpha] — February 4, 2026

This is the first public alpha release of DotVM, a deterministic execution platform for state-centric applications.

### Core Platform

- **Execution Engine** — Register-based VM with computed-goto dispatch achieving ~2.5x performance over switch-based interpreters. NaN-boxed values provide type-safe 64-bit representations with minimal overhead.

- **Memory Management** — Handle-based allocation with generational indices prevents use-after-free at the type level. No garbage collector pauses—deterministic deallocation.

- **State Layer** — Transactional state with write-ahead logging (WAL) ensures durability. Merkle Patricia Tries provide cryptographic state proofs for verification.

### Compilation & Tooling

- **DSL Compiler** — Full pipeline from source to bytecode: lexer → parser → IR builder → optimizer → code generator. SSA-based IR enables standard optimization passes.

- **JIT Compilation** — x86-64 copy-and-patch JIT for hot paths. Profiler-guided tier-up from interpreter to native code. *Experimental—API may change.*

- **CLI Tools** — Complete toolchain: `dotdsl` (compiler), `dotvm` (runtime), `dotdis` (disassembler), `dotinfo` (inspector), `dotpkg` (package manager).

### Security

- **Capability System** — Fine-grained permissions (filesystem, network, state) with explicit grants. Security contexts track capability lineage for auditing.

- **Audit Logging** — Structured event logging for security-relevant operations. Queryable, exportable, with configurable retention policies.

- **Resource Limits** — CPU time, memory, and instruction count limits prevent runaway execution.

### Experimental Features

These features are functional but APIs will change:

- **Replication Framework** — Delta-based state synchronization with Raft consensus foundation. Not production-ready.
- **Debug Protocol** — Client/server debugging with breakpoints and watch expressions.
- **Package Registry** — Dependency resolution and version constraints. Archive extraction not yet implemented.

### Known Limitations

| Area | Status | Notes |
|------|--------|-------|
| JIT | x86-64 only | ARM64 planned |
| Replication | Experimental | In-memory only, no persistence |
| PostgreSQL backend | Compile-time flag | Requires `postgresql=true` |
| Crypto operations | Some tests disabled | Ed25519/AES-GCM validation pending |

### Platform Requirements

- **Compiler**: Clang 19+ with libc++ (recommended) or GCC 14+
- **Build System**: Meson 1.2.0+ with Ninja
- **Architecture**: x86-64 (JIT requires this)

### API Stability

> **This is an alpha release.** All APIs are unstable and will change without notice.
> DotVM provides execution and state primitives—no governance, consensus, or policy mechanisms.

---

*For architecture details, see [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).*
*Report issues at [github.com/ademclk/dotlanth/issues](https://github.com/ademclk/dotlanth/issues).*
