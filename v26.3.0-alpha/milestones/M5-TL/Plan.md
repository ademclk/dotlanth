# Dotlanth v26.3.0-alpha - M5 (TL) Plan

## Objective
Implement replay validator UX and deterministic CLI output.

## Tasks
- E-26030-TL-S1-T1: Implement validator command flow and comparison engine wiring.
- E-26030-TL-S1-T2: Add tests for valid, invalid, and unsupported validator outcomes.
- E-26030-TL-S2-T1: Update run/inspect output and error text for determinism mode, eligibility, and mismatch explanations.

## Acceptance Criteria
- `dot validate-replay <run>` loads original artifacts, replays, and compares proof material.
- CLI reports `valid`, `invalid`, or `unsupported` with actionable reasons.

## Validation Commands
```bash
cargo test -p dot
```
