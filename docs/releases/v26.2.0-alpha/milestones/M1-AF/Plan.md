# Dotlanth v26.2.0-alpha — M1 (AF) Plan

## Objective
Implement Artifact Bundle schema v1 + writer skeleton.

## Tasks
- E-26020-AF-S1-T1: Implement schema v1 types + deterministic manifest serialization and required-file layout constants.
- E-26020-AF-S2-T1: Implement BundleWriter with atomic finalize and section `status`/`error` markers.
- E-26020-AF-S2-T2: Implement input snapshot capture (`inputs/entry.dot`) + input file hash manifest integration.

## Acceptance Criteria
- A bundle directory can be created and always contains:
  - `manifest.json`
  - `inputs/entry.dot`
  - `trace.jsonl`
  - `state_diff.json`
  - `capability_report.json`
- Required files are present even when data is unavailable (explicit error markers).

## Validation Commands
```bash
cargo test -p dot_artifacts
```

