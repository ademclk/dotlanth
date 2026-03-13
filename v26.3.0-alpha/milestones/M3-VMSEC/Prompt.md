# Dotlanth v26.3.0-alpha - M3 (VMSEC) Prompt

## Objective
Gate deterministic-sensitive host access and emit deterministic audit data consistently.

## In Scope
- Time/random/network policy hooks.
- Boundary enforcement composition with capabilities.
- Audit events and budget counters.

## Out of Scope
- Replay proof comparison.
- CLI validator polishing.

## Deliverables
- Policy-aware host hooks.
- Boundary denial before host execution.
- Audit events and v26.3 informational budget counters.

## Done When
- Strict-mode time/random/network violations are blocked consistently.
- Denials and controlled side effects emit audit/report signals.
