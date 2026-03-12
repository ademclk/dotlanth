# Dotlanth v26.2.0-alpha — M5 (TL) Documentation

## Status
- Overall: Complete

## Validation
- `cargo test -p dot_db`
- `cargo test -p dot`

## Checklist
- [x] E-26020-TL-S1-T1
- [x] E-26020-TL-S2-T1
- [x] E-26020-TL-S3-T1
- [x] E-26020-TL-S3-T2

## Notes / Decisions
- CLI UX: RFC-26024
- Replay semantics: RFC-26023
- `dot inspect <run_id>` resolves the bundle through DotDB and prints a stable summary covering run status, schema version, artifact byte sizes, capability counts, trace event count, and state diff counts.
- `dot export-artifacts <run_id> --out <dir>` copies the indexed bundle into a target directory and refuses output paths that already contain files.
- `dot replay <run_id>` resolves the indexed bundle and replays `inputs/entry.dot` into a fresh run rooted in the current project.
- `dot replay --bundle <path>` replays an exported bundle without needing an existing DotDB entry, still writing the new run and bundle into the current project.
