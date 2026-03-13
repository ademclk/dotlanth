# Dotlanth v26.3.0-alpha - M1 (DET) Plan

## Objective
Implement strict deterministic mode plumbing and fail-closed execution semantics.

## Tasks
- E-26030-DET-S1-T1: Add CLI parsing and runtime plumbing for `DeterminismMode`.
- E-26030-DET-S1-T2: Persist determinism mode in run records and artifact metadata.
- E-26030-DET-S2-T1: Define strict-mode error types and fail-closed behavior for unsupported operations.
- E-26030-DET-S2-T2: Add tests for strict-pass, strict-deny, and default-mode compatibility.

## Acceptance Criteria
- Users can opt into strict deterministic mode from the CLI.
- Mode is visible in inspectable run metadata.
- Forbidden or unsupported strict-mode behavior fails before side effects execute.

## Validation Commands
```bash
cargo test -p dot
cargo test -p dot_rt
```
