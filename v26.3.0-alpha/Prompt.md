# Dotlanth v26.3.0-alpha - Prompt

## Purpose
Freeze the target so the team builds the next core contract, not a sideways product.

## ERA 4 STEERING (must guide all decisions)
- Dotlanth is an AI-native execution fabric, not a framework competitor.
- Pillars:
  - Deterministic Core (this release starts strict mode)
  - Artifact-First Runtime (every run outputs an artifact bundle)
  - Capability-Explicit Security (declared in DSL + reported from execution)
- Decision test:
  - framework-like -> reject
  - execution-fabric-like (artifacts, determinism, capabilities, agent operability) -> build
- Avoid:
  - random feature expansion
  - aggressive opcode pack growth
  - SDKs prematurely
  - performance optimization before determinism + artifacts
  - competing with web frameworks

## Baseline (already done in v26.2.0-alpha)
- Artifact bundles ship by default.
- Bundle sections include trace, state diff, and capability report.
- `dot inspect`, `dot replay`, and artifact export exist.
- Replay exists, but replay consistency is not yet validated as a product contract.

## Primary Goal (PHASE 2 - DETERMINISTIC MODE)
Add strict deterministic execution so Dotlanth can prove whether a replay is consistent with the original run artifact contract.

## Scope (Roman tracks)
- DET I: strict deterministic execution flag + runtime rule enforcement
- OP Classification I: opcode metadata + categories + constraints
- SEC II: boundary enforcement + audit events for violations
- TL III: `dot validate-replay <run>` + deterministic mode UX
- VM III: deterministic hooks (time/random/network gating) + budgeting start
- AF II: artifact bundle adds determinism metadata + non-cryptographic replay proofs

## Non-Goals
- No connectors or integration work.
- No large opcode expansion; classify existing behavior first.
- No SDK work.
- No performance-first tuning beyond keeping tests usable.
- No web framework features, routing features, middleware systems, or app lifecycle DSLs.
- No cryptographic attestation or remote trust model in this release.

## Hard Constraints
- Strict mode is optional, but when enabled it must fail closed.
- Determinism rules must be enforced at opcode and host boundaries.
- Classification must stay metadata-first; do not grow the opcode surface to explain policy.
- Artifact bundles remain the primary execution interface.
- Replay validation must compare stable execution facts, not best-effort logs.

## Deliverables
- Strict deterministic execution flag wired through CLI/runtime/artifacts.
- Opcode classification model: `pure`, `controlled_side_effect`, `non_deterministic`.
- Enforcement rules for strict mode and audit events for violations.
- VM hooks for time/random/network gating and first-pass determinism budgeting.
- `dot validate-replay <run>` command with actionable diagnostics and exit codes.
- Artifact bundle extensions for determinism metadata and replay proofs.
- Release docs under `v26.3.0-alpha/`.

## Done When
- [ ] Build gates pass:
  - `cargo fmt --check`
  - `cargo clippy --workspace --all-targets --all-features -- -D warnings`
  - `cargo test --workspace`
- [ ] Strict mode works:
  - `dot run --deterministic=strict ...` rejects non-deterministic execution paths.
  - violation diagnostics identify opcode/capability/source location where possible.
- [ ] Classification works:
  - capability-relevant opcodes carry determinism metadata.
  - unclassified strict-mode opcodes fail closed.
- [ ] Replay validation works:
  - `dot validate-replay <run>` replays and compares proof material.
  - success and mismatch cases are both test-covered.
- [ ] Artifact contract extends cleanly:
  - bundle contains determinism metadata and replay proof payloads.

## Demo Flow (must be reproducible)
1) Run a deterministic-safe example:
   - `cargo run -p dot -- run --deterministic=strict examples/deterministic/basic.dot`
2) Capture the printed `run_id`.
3) Inspect determinism metadata:
   - `cargo run -p dot -- inspect <run_id>`
4) Validate replay consistency:
   - `cargo run -p dot -- validate-replay <run_id>`
5) Run a known non-deterministic example and confirm strict-mode rejection.

## Notes
- v26.3 is not "determinism theater"; it must enforce and validate.
- The release should make future budgeting and stronger proofs easier, not heavier.

## Documentation Requirements
Create or update the following files under `v26.3.0-alpha/`:
- `PRD-26030.md`
- `ADR-26030.md` (+ more ADRs as needed)
- `RFC-26030.md` (+ more RFCs as needed)
- `Prompt.md`
- `Plan.md`
- `Implement.md`
- `Documentation.md`
- `milestones/` execution packs
