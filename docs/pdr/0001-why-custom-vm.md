# PDR-0001: Why Build a Custom Virtual Machine

## Status

Approved

## Executive Summary

Dotlanth builds a custom virtual machine (DotVM) rather than targeting existing runtimes (JVM, WASM, BEAM, etc.) to achieve precise control over execution semantics, state management, and security guarantees that existing VMs cannot provide without significant compromise.

## Problem Statement

Dotlanth requires an execution environment with specific properties:
- **Deterministic execution**: Identical inputs must produce identical outputs across runs and machines
- **State-centric design**: First-class support for transactional state with rollback
- **Intent-based execution**: Native support for declarative intent resolution
- **Security boundaries**: Fine-grained capability-based access control
- **Parallelism**: Execution model designed for concurrent evaluation from the ground up

Existing virtual machines were designed for different goals:
- JVM: Object-oriented enterprise applications
- WASM: Portable browser execution with web security model
- BEAM: Fault-tolerant distributed messaging
- V8: Fast web scripting

Retrofitting these properties onto existing VMs would require invasive modifications while fighting their fundamental design assumptions.

## Goals

- Build an execution environment optimized for intent-driven, state-centric workloads
- Achieve deterministic execution suitable for distributed consensus
- Enable fine-grained resource control and security isolation
- Support efficient parallel execution of independent operations
- Maintain full control over the execution model as requirements evolve

## Non-Goals

- Replace general-purpose VMs for their target domains
- Support all features of existing bytecode formats
- Achieve maximum single-threaded raw throughput at all costs
- Build a distributed runtime (DotVM is the local execution component)

## Proposed Solution

Build DotVM as a register-based virtual machine with:

1. **Custom instruction set** designed for intent execution and state manipulation
2. **Handle-based memory** with generation counters for deterministic safety
3. **Transactional state interface** as a first-class concept
4. **Capability system** for fine-grained permission control
5. **Deterministic execution** with reproducible semantics

The VM serves as the foundation layer; higher-level components (DotDB, DotAgent) build on top.

## Success Criteria

- Deterministic execution: Same bytecode + inputs = same outputs, always
- Security: No escape from capability restrictions
- Performance: Competitive with interpreted tier of production VMs
- State integration: Transactional reads/writes with atomic commit/rollback
- Testability: Full observability for debugging and verification

## Risks and Mitigations

| Risk | Likelihood | Impact | Mitigation |
|------|------------|--------|------------|
| Development time exceeds estimates | High | High | Focus on minimal viable feature set; iterate |
| Performance targets not met | Medium | Medium | Profiling-driven optimization; JIT compilation for hot paths |
| Ecosystem disadvantage vs established VMs | Medium | Low | Target niche where existing VMs are poor fit |
| Security vulnerabilities in custom code | Medium | High | Extensive testing, fuzzing, security audits |

## Open Questions

- What is the long-term FFI story for calling external code?
- How will DotVM bytecode be distributed and versioned?
- What tooling (debugger, profiler) is essential for v1?

## References

- [ADR-0001: NaN-Boxing](../adr/0001-nan-boxing-values.md)
- [ADR-0002: Handle-Based Memory](../adr/0002-handle-based-memory.md)
- [PDR-0002: Language Choice C++26](0002-language-choice-cpp26.md)
