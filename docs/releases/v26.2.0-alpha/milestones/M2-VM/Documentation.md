# Dotlanth v26.2.0-alpha — M2 (VM) Documentation

## Status
- Overall: Complete

## Validation
- `cargo test -p dot_vm`
- `cargo test -p dot_db`
- `cargo test -p dot_ops`
- `cargo test -p dot_artifacts`
- `cargo test -p dot`
- `cargo test --workspace`

## Checklist
- [x] E-26020-VM-S1-T1
- [x] E-26020-VM-S2-T1
- [x] E-26020-VM-S2-T2
- [x] E-26020-VM-S3-T1

## Notes / Decisions
- Run id scheme: ADR-26022
- Trace schema: RFC-26021
- External run ids are generated as stable string ids with a `run_` prefix and stored separately from DotDB's internal SQLite row ids.
- `dot logs <run_id>` now resolves the external run id through DotDB's indexed `runs.run_id` column.
- Successful bounded runs write bundles under `.dotlanth/bundles/<run_id>/`, and the bundle manifest uses the same external `run_id` shown by the CLI.
- `trace.jsonl` is JSONL with gap-free `seq` values and includes `run.start`, syscall attempt/result events, host runtime events, and `run.finish`.
- Trace events include `source.span` and `source.semantic_path` when the runtime knows the mapping: `server`, matched route blocks, and `respond` blocks.
- `manifest.json` now records the trace artifact's byte count and SHA-256 when `trace.jsonl` is present.
- Bundle export snapshots the input `.dot` file, writes `trace.jsonl`, and records granted capabilities in `capability_report.json`. `state_diff.json` remains explicitly unavailable.
