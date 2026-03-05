# Dotlanth v26.2.0-alpha — M1 (AF) Documentation

## Status
- Overall: Complete

## Validation
- `cargo test -p dot_artifacts`
- `cargo test --workspace`

## Checklist
- [x] E-26020-AF-S1-T1
- [x] E-26020-AF-S2-T1
- [x] E-26020-AF-S2-T2

## Notes / Decisions
- Added new crate `crates/dot_artifacts` with bundle schema v1 types and required file layout constants.
- `manifest.json` serialization is deterministic via stable struct field order + `BTreeMap` key ordering.
- `BundleWriter` writes to a staging directory and atomically finalizes via directory rename.
- Required files are always created: `manifest.json`, `inputs/entry.dot`, `trace.jsonl`, `state_diff.json`, `capability_report.json`.
- Missing artifact sections are explicit: files contain `status` + `error` markers and the manifest mirrors those markers.
- Input snapshot supports `inputs/entry.dot` capture and records `sha256` + byte count in manifest input metadata.
