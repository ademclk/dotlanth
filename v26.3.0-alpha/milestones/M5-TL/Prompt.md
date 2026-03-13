# Dotlanth v26.3.0-alpha - M5 (TL) Prompt

## Objective
Make deterministic execution and replay validation visible and usable from the CLI.

## In Scope
- `dot validate-replay <run>` command.
- Determinism mode and validation status in CLI output.
- Stable validator result states.

## Out of Scope
- Interactive UI.
- Remote validation services.

## Deliverables
- Validator command.
- Updated `run`/`inspect` UX.

## Done When
- Validator reports `valid`, `invalid`, or `unsupported`.
- CLI output is concise and distinguishes denial vs mismatch.
