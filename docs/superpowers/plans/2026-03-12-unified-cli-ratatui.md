# Unified Ratatui CLI Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a ratatui-based Mission Control experience for `dot` that becomes the primary interactive UX while preserving the existing scriptable command surface under the same binary.

**Architecture:** Keep the current command modules as the behavior authority, but extract reusable helpers so both the ratatui app and subcommands call the same code paths. Add a small dashboard data layer over DotDB for recent runs and selected-run details, then layer a ratatui app on top with an action launcher, recent runs list, and observability panels.

**Tech Stack:** Rust 2024, clap, ratatui, crossterm, tokio, existing DotDB / artifact / runtime crates

---

## Chunk 1: Shared Backend Surfaces

### Task 1: Add recent-run dashboard support to DotDB

**Files:**
- Modify: `crates/dot_db/src/lib.rs`

- [ ] **Step 1: Write the failing test**

Add a DotDB test that creates multiple runs and asserts `recent_runs(limit)` returns newest-first rows with stable status metadata.

- [ ] **Step 2: Run test to verify it fails**

Run: `cargo test -p dot_db recent_runs -- --nocapture`
Expected: FAIL because `recent_runs` does not exist yet.

- [ ] **Step 3: Write minimal implementation**

Add a `recent_runs(limit)` query that returns `Vec<StoredRun>` ordered by `created_at_ms DESC, id DESC`.

- [ ] **Step 4: Run test to verify it passes**

Run: `cargo test -p dot_db recent_runs -- --nocapture`
Expected: PASS

### Task 2: Extract reusable command helpers for TUI parity

**Files:**
- Modify: `crates/dot/src/commands/init.rs`
- Modify: `crates/dot/src/commands/inspect.rs`
- Modify: `crates/dot/src/commands/logs.rs`
- Modify: `crates/dot/src/commands/export_artifacts.rs`
- Modify: `crates/dot/src/commands/replay.rs`
- Modify: `crates/dot/src/commands/run.rs`

- [ ] **Step 1: Write failing helper-focused tests**

Add unit tests for helper functions that return strings or structured values without writing directly to stdout.

- [ ] **Step 2: Run targeted tests to verify failure**

Run: `cargo test -p dot helper -- --nocapture`
Expected: FAIL because helper functions are missing.

- [ ] **Step 3: Write minimal implementation**

Expose render/helper functions for inspect/logs/init/export and add a non-printing run announcement path for TUI-triggered execution.

- [ ] **Step 4: Run targeted tests to verify pass**

Run: `cargo test -p dot helper -- --nocapture`
Expected: PASS

## Chunk 2: Ratatui Mission Control

### Task 3: Implement TUI state, rendering, and interactions

**Files:**
- Create: `crates/dot/src/tui/mod.rs`
- Create: `crates/dot/src/tui/app.rs`
- Create: `crates/dot/src/tui/render.rs`
- Create: `crates/dot/src/tui/actions.rs`
- Create: `crates/dot/src/tui/theme.rs`

- [ ] **Step 1: Write failing render/state tests**

Add tests that render the dashboard into a test backend and assert the launcher, recent runs, and observability sections are visible.

- [ ] **Step 2: Run targeted tests to verify failure**

Run: `cargo test -p dot tui -- --nocapture`
Expected: FAIL because the TUI modules do not exist yet.

- [ ] **Step 3: Write minimal implementation**

Build the Mission Control screen with:
- action-first launcher
- recent runs list
- selected-run inspector / logs output
- status and help footer
- keyboard navigation and prompt overlays for parameterized actions

- [ ] **Step 4: Run targeted tests to verify pass**

Run: `cargo test -p dot tui -- --nocapture`
Expected: PASS

## Chunk 3: CLI Integration

### Task 4: Make `dot` launch TUI by default and preserve scripted subcommands

**Files:**
- Modify: `crates/dot/Cargo.toml`
- Modify: `crates/dot/src/main.rs`
- Modify: `README.md`

- [ ] **Step 1: Write failing CLI tests**

Add tests that verify no-subcommand startup routes into the TUI bootstrap path while existing subcommands still parse.

- [ ] **Step 2: Run targeted tests to verify failure**

Run: `cargo test -p dot cli -- --nocapture`
Expected: FAIL because `Cli` still requires a subcommand.

- [ ] **Step 3: Write minimal implementation**

Add ratatui dependencies, make the subcommand optional, and route `dot` with no arguments to the TUI runner.

- [ ] **Step 4: Run targeted tests to verify pass**

Run: `cargo test -p dot cli -- --nocapture`
Expected: PASS

## Final Verification

- [ ] Run: `cargo test -p dot_db`
- [ ] Run: `cargo test -p dot`
- [ ] Run: `cargo test --workspace`
