# Dotlanth v26.3.0-alpha - Plan

## Rules
- Stop-and-fix: if validation fails, repair before moving on.
- Keep diffs scoped to determinism mode, classification, replay validation, and required docs.
- ERA 4 steering applies continuously: framework-like -> reject.
- Do not answer deterministic gaps by adding connectors or large opcode packs.

## Research Synthesis
- v26.2 established artifacts as the execution contract; v26.3 should now validate that contract under a strict policy.
- Determinism should be metadata-driven so runtime, CLI, and artifacts share one vocabulary.
- Replay validation should compare a canonical proof surface, not every incidental output byte.
- Capability enforcement and determinism enforcement must meet at the same boundary but remain conceptually separate.

## Intended Architecture (for this version)
### Crates impacted (existing)
- `crates/dot` (CLI surface for strict mode and validator)
- `crates/dot_dsl` (validation surface if deterministic mode becomes expressible in manifests later)
- `crates/dot_vm` (opcode metadata, trace facts, deterministic hooks, budget counters)
- `crates/dot_ops` (host calls for time/random/network gating)
- `crates/dot_sec` (boundary enforcement + audit events)
- `crates/dot_db` (run metadata / artifact retrieval for validator)
- `crates/dot_rt` (wiring execution mode through runtime)
- `crates/dot_artifacts` or equivalent artifact module (determinism metadata + proof payloads)

### Key interfaces (target)
- `DeterminismMode` enum with at least `default` and `strict`
- `OpcodeDeterminism` metadata attached to executable opcodes
- `DeterminismPolicy` boundary check used before host execution
- `DeterminismAuditEvent` emitted on violations and allowed controlled operations
- `ReplayProof` / `ReplayProofSummary` stored in artifact bundle
- `dot validate-replay <run>` validator entrypoint

### Data flow (target)
1) CLI parses execution mode.
2) Runtime initializes determinism mode + artifact writer.
3) VM resolves opcode metadata and enforces strict policy before host execution.
4) Time/random/network requests pass through deterministic hooks.
5) Enforcement emits audit events and budget counters.
6) Artifact bundle stores determinism metadata + replay proof payload.
7) Validator replays the run and compares original vs replay proof surfaces.

## Milestones

> Each milestone also gets its own `Prompt.md`, `Plan.md`, `Implement.md`, and `Documentation.md` under `v26.3.0-alpha/milestones/` so execution can stay scoped.

### M1 - DET I: Strict mode plumbing + fail-closed contract
**Objective**
- Introduce strict deterministic mode as a first-class execution option and ensure unsupported paths fail closed.

**Tasks**
- Add CLI/runtime mode plumbing and persist mode in run/artifact metadata.
- Define strict-mode error types and fail-closed behavior for unclassified/prohibited operations.
- Add baseline tests for strict vs default execution behavior.

**Acceptance Criteria**
- Users can opt into strict deterministic mode from the CLI.
- Mode is visible in inspectable run metadata.
- Unclassified or forbidden strict-mode behavior fails before side effects execute.

**Validation Commands**
```bash
cargo test -p dot
cargo test -p dot_rt
```

### M2 - OP Classification I: Metadata + coverage map
**Objective**
- Establish the classification model and apply it to the in-scope opcode surface.

**Tasks**
- Implement metadata model for `pure`, `controlled_side_effect`, `non_deterministic`.
- Add a coverage checklist/test so strict mode cannot silently run unclassified opcodes.
- Document constraints for controlled side effects.

**Acceptance Criteria**
- In-scope opcodes carry determinism metadata.
- Strict mode fails closed for missing classification.
- Classification rules are documented in code/tests/docs.

**Validation Commands**
```bash
cargo test -p dot_vm
```

### M3 - VM III + SEC II: Hooks, boundary enforcement, audit events
**Objective**
- Gate deterministic-sensitive host access and emit audit data consistently.

**Tasks**
- Add deterministic hooks for time/random/network.
- Apply policy checks before host execution and emit violation audit events.
- Start deterministic budget counters for allowed/denied/gated operations.

**Acceptance Criteria**
- Strict-mode time/random/network violations are blocked consistently.
- Audit events are emitted for denials and relevant controlled operations.
- Budget counters appear in runtime state and artifact outputs.

**Validation Commands**
```bash
cargo test -p dot_ops
cargo test -p dot_sec
```

### M4 - AF II: Artifact determinism metadata + replay proofs
**Objective**
- Extend the bundle contract so replay validation has a stable proof surface.

**Tasks**
- Add determinism metadata sections and versioning rules.
- Write proof summaries/fingerprints derived from deterministic execution facts.
- Ensure failed strict runs still emit required metadata with explicit markers.

**Acceptance Criteria**
- Artifact bundles expose determinism mode, eligibility, audit summary, and replay proof fields.
- Failed strict runs still produce required determinism metadata.

**Validation Commands**
```bash
cargo test -p dot_artifacts
```

### M5 - TL III: Replay validator + deterministic UX
**Objective**
- Make deterministic execution and replay validation visible and usable from the CLI.

**Tasks**
- Implement `dot validate-replay <run>`.
- Add concise validator output and stable exit codes.
- Update `dot inspect` and run output to surface deterministic status and validator eligibility.

**Acceptance Criteria**
- Validator reports `valid`, `invalid`, or `unsupported` with actionable reasons.
- CLI output is concise enough for humans and stable enough for scripts.

**Validation Commands**
```bash
cargo test -p dot
```

### M6 - DOC: End-to-end release gate
**Objective**
- Lock the release with docs, examples, and end-to-end validation.

**Tasks**
- Add/update deterministic-safe and deterministic-failure demo flows.
- Refresh release docs and ensure backlog/ADRs match implemented scope.
- Add one end-to-end validation test that covers run -> inspect -> validate-replay.

**Acceptance Criteria**
- Docs and demos reflect real commands.
- The release can demonstrate both successful validation and strict-mode rejection.

**Validation Commands**
```bash
just check
```

## Backlog Index
- See `epics/README.md` for epics, stories, tasks, dependencies, and task-count tracking.
- See `milestones/README.md` for per-milestone execution packs.

## Decision Notes (avoid oscillation)
- Do not promise full universal determinism; promise strict-mode validation for the supported proof surface.
- If a policy decision could drift, update ADR-26030 through ADR-26032 instead of improvising in code.
- Prefer small metadata extensions over new language/runtime product surface.
