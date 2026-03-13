# Dotlanth v26.3.0-alpha - M2 (OPC) Prompt

## Objective
Establish the opcode determinism classification model and make strict mode fail closed for missing coverage.

## In Scope
- Classification metadata model.
- In-scope opcode registry coverage.
- Controlled side-effect constraint documentation/tests.

## Out of Scope
- Replay validator implementation.
- Artifact proof generation.

## Deliverables
- Classification enum and metadata wiring.
- Coverage checks for in-scope opcodes.

## Done When
- In-scope opcodes expose determinism metadata.
- Strict mode rejects unclassified opcode usage.
