# Dotlanth v26.3.0-alpha - M3 (VMSEC) Plan

## Objective
Implement deterministic hooks, boundary enforcement, audit events, and budget counters.

## Tasks
- E-26030-VM-S1-T1: Implement policy-aware hooks for time and randomness surfaces.
- E-26030-VM-S1-T2: Implement policy-aware hooks for network surfaces and align emitted trace facts.
- E-26030-VM-S2-T1: Define the v26.3 deterministic budget counter set and semantics.
- E-26030-VM-S2-T2: Emit counters from runtime execution and expose them to artifacts/inspect paths.
- E-26030-SEC-S1-T1: Compose determinism and capability checks at the opcode/host boundary.
- E-26030-SEC-S1-T2: Add denial-path tests proving host execution does not occur after strict-mode rejection.
- E-26030-SEC-S2-T1: Define and emit structured audit events for deterministic denials and controlled side-effect use.
- E-26030-SEC-S2-T2: Add artifact/report integration tests for violation summaries on failed runs.

## Acceptance Criteria
- Strict-mode time/random/network violations are blocked consistently.
- Audit events are emitted for denials and relevant controlled operations.
- Budget counters appear in runtime state and artifact outputs.

## Validation Commands
```bash
cargo test -p dot_ops
cargo test -p dot_sec
```
