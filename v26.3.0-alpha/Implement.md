# Dotlanth v26.3.0-alpha - Implement Runbook

## Source of Truth
- `Plan.md` in this folder is the source of truth.
- Story/task files under `epics/` define the execution backlog.

## Operating Mode
- Implement one milestone at a time.
- Keep only one story actively in flight unless work is clearly parallelizable.
- After each story:
  - run its validation commands
  - fix failures immediately
  - update `Documentation.md`

## Diff Discipline
- Keep changes deterministic-mode focused.
- No connector work, SDK work, or framework-like feature work.
- Avoid speculative performance work.

## Validation Loop
1) Read the current milestone in `Plan.md`
2) Read the active story in `epics/`
3) Implement tasks
4) Run the listed validation commands
5) If failing: fix and rerun
6) Update `Documentation.md` with status/notes

## Decision Handling
- If a choice changes determinism semantics, classification semantics, or replay proof semantics:
  - write/update an ADR
  - link it from `Documentation.md`

## Documentation Discipline
- Keep `Documentation.md` current with:
  - milestone status
  - demo commands
  - validator behavior
  - known limitations
  - ADR index

## Release Order
- DET I + OP Classification I first
- VM III + SEC II next
- AF II after proof inputs are stable enough
- TL III after proof surface exists
- DOC release gate last
