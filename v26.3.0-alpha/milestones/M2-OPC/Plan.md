# Dotlanth v26.3.0-alpha - M2 (OPC) Plan

## Objective
Implement opcode determinism classification and coverage enforcement.

## Tasks
- E-26030-OPC-S1-T1: Implement the classification enum/metadata shape in runtime or VM code.
- E-26030-OPC-S1-T2: Attach metadata to the in-scope opcode registry and expose it to enforcement/tooling.
- E-26030-OPC-S2-T1: Add CI/test coverage checks for classification completeness across the in-scope opcode surface.
- E-26030-OPC-S2-T2: Document and test controlled side-effect constraints used by strict mode.

## Acceptance Criteria
- In-scope opcodes carry determinism metadata.
- Strict mode fails closed for missing classification.
- Controlled side-effect constraints are documented and tested.

## Validation Commands
```bash
cargo test -p dot_vm
```
