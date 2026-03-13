# Dotlanth v26.3.0-alpha - Release Documentation

## Status
- Date: 2026-03-08
- Overall: Planning complete; implementation pending
- Phase: PHASE 2 - DETERMINISTIC MODE

## Release intent (ERA 4 steering)
- Dotlanth remains an execution fabric.
- v26.3 turns replay into a validated execution contract by adding strict deterministic mode, opcode classification, and proof-aware artifacts.
- Framework-like expansion is explicitly out of scope.

## Research notes
- v26.2 already proved artifact bundles are the right unit of execution output.
- The missing contract is replay consistency, not more features.
- Determinism policy should be metadata-first and boundary-enforced, so the system becomes more auditable without growing more framework surface.

## What is in scope (tracks)
- DET I: strict deterministic flag + rule enforcement
- OP Classification I: opcode metadata + categories + constraints
- SEC II: boundary enforcement + audit events for violations
- TL III: `dot validate-replay <run>` + deterministic UX
- VM III: deterministic hooks (time/random/network gating) + budgeting start
- AF II: artifact determinism metadata + replay proofs

## Milestone checklist
- [ ] M1: strict mode plumbing + fail-closed contract
- [ ] M2: opcode classification metadata + coverage map
- [ ] M3: deterministic hooks + boundary enforcement + audit events
- [ ] M4: artifact determinism metadata + replay proofs
- [ ] M5: replay validator + deterministic CLI UX
- [ ] M6: end-to-end docs/demo/release gate

## Backlog summary
- Epics: 7
- Stories: 14
- Planned tasks: 24
- Index: `epics/README.md`

## Demo steps (target)
Local verification gate:
```bash
just check
```

Deterministic-safe run:
```bash
cargo run -p dot -- run --deterministic=strict examples/deterministic/basic.dot
```

Inspect determinism metadata:
```bash
cargo run -p dot -- inspect <run_id>
```

Validate replay:
```bash
cargo run -p dot -- validate-replay <run_id>
```

Strict failure example:
```bash
cargo run -p dot -- run --deterministic=strict examples/deterministic/non_deterministic.dot
```

## Known limitations (expected for alpha)
- Strict mode is opt-in, not default.
- Replay proofs are non-cryptographic.
- Validation covers the documented proof surface, not every possible environmental difference.
- Budgeting starts in v26.3, but hard quotas can remain future work.

## ADR list
- `ADR-26030.md`: strict deterministic execution contract
- `ADR-26031.md`: opcode determinism classification model
- `ADR-26032.md`: replay validator proof surface + artifact metadata

## RFC list
- `RFC-26030.md`: deterministic execution mode spec
- `RFC-26031.md`: opcode determinism classification + strict constraints
- `RFC-26032.md`: deterministic hooks and audit events
- `RFC-26033.md`: replay proof + validation contract
- `RFC-26034.md`: deterministic CLI UX spec

## Milestone packs
- `milestones/README.md`
- `milestones/M1-DET/`
- `milestones/M2-OPC/`
- `milestones/M3-VMSEC/`
- `milestones/M4-AF/`
- `milestones/M5-TL/`
- `milestones/M6-DOC/`

## Backlog focus reminders
- Prefer classification and enforcement over opcode growth.
- Prefer artifact/report clarity over optimization.
- If a change feels like framework competition, reject it.
