# Dotlanth v26.3.0-alpha - M2 (OPC) Documentation

## Status
- Overall: Complete

## Validation
- `cargo fmt --all`
- `cargo test -p dot_vm`
- `cargo test -p dot_ops`
- `cargo test -p dot`

## Checklist
- [x] E-26030-OPC-S1-T1
- [x] E-26030-OPC-S1-T2
- [x] E-26030-OPC-S2-T1
- [x] E-26030-OPC-S2-T2

## Notes / Decisions
- Track category semantics in `ADR-26031.md` and `RFC-26031.md`.
- Shared vocabulary:
  - `pure`
  - `controlled_side_effect`
  - `non_deterministic`
- In-scope opcode metadata is centralized in `dot_vm`, while syscall registry metadata lives in `dot_ops` so runtime enforcement and tooling share one classification surface.
- Strict mode now fails closed for unclassified syscall usage before execution and keeps a runtime backstop in the host dispatcher.
- Current in-scope classifications:
  - `halt`, `load_const`, `mov` => `pure`
  - `log.emit` => `controlled_side_effect` with required capability `log`
  - `net.http.serve` => `non_deterministic` with required capability `net.http.listen`
