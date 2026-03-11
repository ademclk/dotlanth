# Dotlanth v26.2.0-alpha — M4 (DB) Plan

## Objective
DotDB artifact index + state diff generation.

## Tasks
- E-26020-DB-S1-T1: Add DotDB migrations for external `run_id` and artifact index tables/columns.
- E-26020-DB-S2-T1: Implement DotDB APIs for storing/retrieving bundle refs (plus integrity metadata).
- E-26020-DB-S3-T1: Implement before/after `state_kv` snapshot and compute deterministic diff.
- E-26020-DB-S3-T2: Export `state_diff.json` + manifest integration (`artifacts.state_diff`).

## Acceptance Criteria
- Artifact bundle ref is retrievable by `run_id`.
- State diff is stable-ordered and included in bundles.

## Validation Commands
```bash
cargo test -p dot_db
```
