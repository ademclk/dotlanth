# Dotlanth v26.2.0-alpha — M3 (SEC) Plan

## Objective
Capability declarations + opcode-boundary enforcement + capability usage report.

## Tasks
- E-26020-DS-S1-T1: Tighten dotDSL capability validation (duplicates + stable representation) without adding new framework-like syntax.
- E-26020-DS-S2-T1: Expose declared capability set + spans/paths from dotDSL document into runtime context.
- E-26020-SEC-S1-T1: Add opcode-boundary capability checks for capability-gated operations (pre-syscall).
- E-26020-SEC-S2-T1: Implement capability usage accumulator (used counts + denied attempts + optional seq linkage).
- E-26020-SEC-S3-T1: Emit `capability_report.json` (schema v1) with stable ordering and manifest integration.

## Acceptance Criteria
- Missing capabilities are denied deterministically at opcode boundary.
- Report lists declared/used/denied with stable sorting.

## Validation Commands
```bash
cargo test -p dot_dsl
cargo test -p dot_sec
```
