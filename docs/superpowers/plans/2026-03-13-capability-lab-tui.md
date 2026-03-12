# Capability Lab TUI Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Turn `dot` into a capability-first Demo/Dev ecosystem testbench with a real ratatui MVP, explicit `dot tui`, interactive launch guards, generated demo fixtures, and one real action per capability page.

**Architecture:** Keep the current `dot` CLI and shared command helpers as the stable execution surface, but split the TUI into focused modules: app state, registry, fixtures, jobs, models, and rendering. The TUI should execute real Dotlanth behaviors via shared command helpers or direct crate APIs, persist canonical run state in the current project’s `.dotlanth/`, and store demo-owned fixture/export metadata under `.dotlanth/demo/`.

**Tech Stack:** Rust 2024, clap, ratatui, crossterm, tokio, dot_db, dot_dsl, dot_rt, dot_sec, dot_artifacts, existing `dot` command helpers

---

## Chunk 1: CLI Entry And Launch Guards

### Task 1: Add explicit `dot tui` and non-interactive launch guards

**Files:**
- Modify: `crates/dot/src/main.rs`
- Modify: `crates/dot/src/tui/mod.rs`

- [ ] **Step 1: Write the failing test**

Add CLI tests that assert:
- `dot tui` parses
- bare `dot` still parses
- non-interactive launch validation returns a clear error for TUI entry

- [ ] **Step 2: Run test to verify it fails**

Run: `cargo test -p dot cli_tui -- --nocapture`
Expected: FAIL because `tui` is not a subcommand and launch guard behavior does not exist yet.

- [ ] **Step 3: Write minimal implementation**

Add a `Tui` subcommand, centralize interactive-terminal detection, and make both bare `dot` and `dot tui` route through the same guarded TUI launcher.

- [ ] **Step 4: Run test to verify it passes**

Run: `cargo test -p dot cli_tui -- --nocapture`
Expected: PASS

## Chunk 2: TUI Module Split

### Task 2: Split the TUI into focused modules with Demo/Dev and capability state

**Files:**
- Create: `crates/dot/src/tui/state.rs`
- Create: `crates/dot/src/tui/model.rs`
- Create: `crates/dot/src/tui/registry.rs`
- Create: `crates/dot/src/tui/fixtures.rs`
- Create: `crates/dot/src/tui/jobs.rs`
- Modify: `crates/dot/src/tui/app.rs`
- Modify: `crates/dot/src/tui/mod.rs`
- Modify: `crates/dot/src/tui/render.rs`

- [ ] **Step 1: Write the failing test**

Add state/render tests that assert:
- mode tabs render `Demo` and `Dev`
- capability rail lists all six capability pages
- footer shows the current selection tuple

- [ ] **Step 2: Run test to verify it fails**

Run: `cargo test -p dot capability_lab_layout -- --nocapture`
Expected: FAIL because the current mission-control layout does not expose these structures.

- [ ] **Step 3: Write minimal implementation**

Introduce explicit mode/capability/action state, selection tracking (`run`, `bundle_ref`, `export_dir`, `fixture`), and render the capability-lab layout while keeping the existing ratatui shell alive.

- [ ] **Step 4: Run test to verify it passes**

Run: `cargo test -p dot capability_lab_layout -- --nocapture`
Expected: PASS

## Chunk 3: Fixture Manager And Metadata

### Task 3: Add deterministic demo fixture generation and scenario metadata

**Files:**
- Create: `crates/dot/src/tui/fixtures.rs`
- Modify: `crates/dot/src/tui/model.rs`
- Modify: `crates/dot/src/tui/state.rs`

- [ ] **Step 1: Write the failing test**

Add fixture-manager tests that assert:
- stable directories are created under `.dotlanth/demo/`
- `scenarios.json` is written with `schema_version`, fixture root, latest run id, bundle ref, and export dir
- refreshing one fixture does not remove another fixture’s files

- [ ] **Step 2: Run test to verify it fails**

Run: `cargo test -p dot fixture_manager -- --nocapture`
Expected: FAIL because the fixture manager and metadata model do not exist yet.

- [ ] **Step 3: Write minimal implementation**

Create stable fixtures for:
- `hello-api`
- `invalid-parse`
- `capability-denial`

Persist metadata in `.dotlanth/demo/metadata/scenarios.json`.

- [ ] **Step 4: Run test to verify it passes**

Run: `cargo test -p dot fixture_manager -- --nocapture`
Expected: PASS

## Chunk 4: MVP Capability Actions

### Task 4: Implement one real action per capability page

**Files:**
- Modify: `crates/dot/src/tui/registry.rs`
- Modify: `crates/dot/src/tui/app.rs`
- Modify: `crates/dot/src/tui/jobs.rs`
- Modify: `crates/dot/src/tui/model.rs`
- Modify: `crates/dot/src/tui/render.rs`
- Modify: `crates/dot/src/commands/init.rs`
- Modify: `crates/dot/src/commands/inspect.rs`
- Modify: `crates/dot/src/commands/logs.rs`
- Modify: `crates/dot/src/commands/export_artifacts.rs`
- Modify: `crates/dot/src/commands/replay.rs`
- Modify: `crates/dot/src/commands/run.rs`

- [ ] **Step 1: Write the failing tests**

Add focused tests for the MVP action table:
- `Init / Bootstrap`
- `Parse & Validate`
- `Run & Serve`
- `Replay & Recovery`
- `Security & Capabilities`
- `State & DB`
- `Artifacts & Inspection`

- [ ] **Step 2: Run tests to verify they fail**

Run: `cargo test -p dot capability_actions -- --nocapture`
Expected: FAIL because the registry does not yet provide these actions/results.

- [ ] **Step 3: Write minimal implementation**

Implement the MVP real actions:
- scaffold sample project
- validate valid fixture
- validate invalid fixture
- run demo fixture and probe a real route
- run current project in `Dev`
- replay latest run
- run capability-denial demo
- inspect recent runs and logs
- inspect and export selected bundle

- [ ] **Step 4: Run tests to verify they pass**

Run: `cargo test -p dot capability_actions -- --nocapture`
Expected: PASS

## Chunk 5: Full TUI Flow And Verification

### Task 5: Verify the integrated capability lab flow

**Files:**
- Modify: `README.md`
- Modify: `.gitignore`

- [ ] **Step 1: Write final integration tests**

Add integration coverage for:
- `dot`
- `dot tui`
- one end-to-end demo chain from fixture generation through run, inspect/export, and replay

- [ ] **Step 2: Run tests to verify they fail**

Run: `cargo test -p dot integrated_capability_lab -- --nocapture`
Expected: FAIL until the full flow is wired together.

- [ ] **Step 3: Write minimal implementation and docs**

Update the README to describe `dot` and `dot tui`, Demo/Dev, and the capability-lab workflow.

- [ ] **Step 4: Run tests to verify they pass**

Run: `cargo test -p dot integrated_capability_lab -- --nocapture`
Expected: PASS

## Final Verification

- [ ] Run: `cargo fmt --check`
- [ ] Run: `cargo clippy --workspace --all-targets --all-features -- -D warnings`
- [ ] Run: `cargo test -p dot`
- [ ] Run: `cargo test --workspace`
