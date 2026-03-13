# Dotlanth v26.3.0-alpha - M1 (DET) Documentation

## Status
- Overall: Complete

## Checklist
- [x] E-26030-DET-S1-T1
- [x] E-26030-DET-S1-T2
- [x] E-26030-DET-S2-T1
- [x] E-26030-DET-S2-T2

## Notes / Decisions
- `dot run` accepts `--determinism <default|strict>`.
- Run records and `manifest.json` persist the selected `determinism_mode`.
- `dot inspect <run_id>` prints the persisted determinism mode.
- Strict mode is fail-closed today: supported deterministic syscalls may run, while unsupported syscalls such as `net.http.serve` are denied before side effects execute.
