# Dotlanth v26.2.0-alpha — M3 (SEC) Documentation

## Status
- Overall: Complete

## Validation
- `cargo fmt --all`
- `cargo test -p dot_dsl`
- `cargo test -p dot_sec`
- `cargo test --workspace`

## Checklist
- [x] E-26020-DS-S1-T1
- [x] E-26020-DS-S2-T1
- [x] E-26020-SEC-S1-T1
- [x] E-26020-SEC-S2-T1
- [x] E-26020-SEC-S3-T1

## Notes / Decisions
- Declaration contract: ADR-26023
- Enforcement point: ADR-26024
- Report schema: RFC-26022
- dotDSL now rejects duplicate capability declarations before runtime.
- Runtime context preserves declared capability source spans and semantic paths as `capabilities[n]`.
- Capability-gated syscalls are denied at the VM opcode boundary before host dispatch, while host-side enforcement remains in place as defense-in-depth.
- `capability_report.json` schema v1 records declared capabilities plus stable `used` and `denied` accounting; denied entries keep a representative error message and trace `seq` when available.
