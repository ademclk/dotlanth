# Dotlanth v26.3.0-alpha - M1 (DET) Prompt

## Objective
Introduce strict deterministic mode as a first-class execution option and ensure unsupported behavior fails closed.

## In Scope
- CLI/runtime determinism mode plumbing.
- Persisting selected mode in run records and artifacts.
- Fail-closed strict-mode semantics for unsupported operations.

## Out of Scope
- Full opcode coverage across the runtime.
- Replay proof generation.
- CLI validator UX beyond basic mode display.

## Deliverables
- `DeterminismMode` plumbing.
- Strict-mode denial/error behavior.
- Baseline tests for strict vs default execution.

## Done When
- Strict mode can be selected from the CLI.
- Mode is visible in run metadata.
- Unsupported strict-mode behavior is denied before side effects execute.
