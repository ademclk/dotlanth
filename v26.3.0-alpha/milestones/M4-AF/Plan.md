# Dotlanth v26.3.0-alpha - M4 (AF) Plan

## Objective
Implement artifact determinism metadata and replay proof payloads.

## Tasks
- E-26030-AF-S1-T1: Extend bundle manifest/sections for determinism metadata and versioning notes.
- E-26030-AF-S1-T2: Add tests for successful and failed strict-run bundle emission.
- E-26030-AF-S2-T1: Implement replay proof payload generation and comparison-ready summaries/fingerprints.

## Acceptance Criteria
- Artifact bundles expose determinism mode, eligibility, audit summary, and replay proof fields.
- Failed strict runs still produce required determinism metadata.

## Validation Commands
```bash
cargo test -p dot_artifacts
```
