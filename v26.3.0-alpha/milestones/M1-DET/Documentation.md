# Dotlanth v26.3.0-alpha - M1 (DET) Documentation

## Status
- Overall: In progress

## Checklist
- [x] E-26030-DET-S1-T1
- [x] E-26030-DET-S1-T2
- [x] E-26030-DET-S2-T1
- [x] E-26030-DET-S2-T2

## Notes / Decisions
- Track mode semantics in `ADR-26030.md` and `RFC-26030.md`.
- `dot run` now accepts `--deterministic=strict` and `--deterministic strict`; omitted mode defaults to `default`.
- `manifest.json` now records `determinism.mode`, and `dot inspect <run_id>` renders `determinism_mode` when metadata is present while remaining compatible with legacy manifests.
- `M1` fail-closed semantics are intentionally conservative: strict mode rejects any generated VM program containing host syscalls before runtime side effects execute. This is a temporary contract until opcode classification lands in `M2-OPC`.
- Validation evidence gathered during implementation:
  - `cargo test -p dot cli_run_`
  - `cargo test -p dot commands::run::tests::run_exports_ordered_trace_bundle_with_source_mapping -- --exact`
  - `cargo test -p dot commands::run::tests::strict_mode_denies_host_syscalls_before_side_effects_execute -- --exact`
  - `cargo test -p dot --test inspect_cli inspect_`
  - `cargo test -p dot_rt determinism_mode_`
  - `cargo test -p dot_artifacts tests::manifest_serialization_records_determinism_mode -- --exact`
- Deferred to later milestones:
  - opcode-level classification and selective allowlists (`M2-OPC`)
  - host hook and audit/counter surface (`M3-VM`, `M3-SEC`)
  - replay proof metadata and validator UX (`M4-AF`, `M5-TL`)
