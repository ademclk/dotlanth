# Dotlanth v26.2.0-alpha — M4 (DB) Documentation

## Status
- Overall: Complete

## Validation
- `cargo test -p dot_db`
- `cargo test -p dot`
- `cargo test --workspace`

## Checklist
- [x] E-26020-DB-S1-T1
- [x] E-26020-DB-S2-T1
- [x] E-26020-DB-S3-T1
- [x] E-26020-DB-S3-T2

## Notes / Decisions
- Artifact index + state scope: ADR-26025
- DotDB now migrates an `artifact_bundles` table keyed by the internal run row id and resolved through the external `run_id`.
- Bundle indexing stores the project-relative ref `.dotlanth/bundles/<run_id>` plus `manifest.json` SHA-256 and byte count.
- `state_diff.json` now exports a stable `{ schema_version, scope, changes }` document for `state_kv` only.
- State diff ordering is deterministic by `(namespace, key)`, and unchanged values are omitted even if `updated_at_ms` changed.
- Bundle manifest schema v1 continues to expose this artifact under `sections.state_diff`.
