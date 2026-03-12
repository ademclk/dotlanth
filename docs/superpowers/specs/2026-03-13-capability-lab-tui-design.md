# Dotlanth Capability Lab TUI Design

## Summary

Dotlanth's ratatui CLI should evolve from a single mission-control dashboard into a unified ecosystem testbench. The TUI must let us exercise both user-facing workflows and deeper subsystem behavior using real Dotlanth code paths, real generated fixtures, and real persisted artifacts. Nothing in this experience should be mocked.

The information architecture should be capability-first, with crate ownership shown inline so the operator can see which subsystem each page is exercising. The TUI should expose two top-level modes:

- `Demo`: a polished showcase surface with curated scenarios that generate real fixtures in `.dotlanth/demo/` and prove the main capabilities work end to end.
- `Dev`: a deeper engineering surface for granular checks, raw outputs, repeatable diagnostics, and future feature verification.

Both modes must cover the same capability areas. The difference is the level of control and presentation, not the underlying behavior.

## Goals

- Make every major Dotlanth ecosystem capability testable inside the TUI.
- Keep the TUI capability-first rather than crate-first, while surfacing crate labels in parentheses.
- Execute real behavior against generated demo fixtures or the current project root, never mocked flows.
- Preserve one unified `dot` CLI binary where the TUI is the primary interactive experience and the scripted subcommands remain available.
- Make future features additive: each new ecosystem feature should register a new TUI test action or extend an existing capability page.

## Non-Goals

- Replacing scripted subcommands for CI, docs, or automation.
- Turning the TUI into a general package manager or editor.
- Designing a crate-by-crate explorer as the primary navigation model.
- Supporting remote orchestration or distributed runtime control in this iteration.

## CLI Invocation Contract

The unified `dot` binary should keep one clear interactive contract:

- `dot` with no subcommand launches the TUI.
- `dot tui` explicitly launches the TUI.
- The current explicit scripted subcommands at implementation time must remain available: `dot init`, `dot run`, `dot logs`, `dot inspect`, `dot export-artifacts`, and `dot replay`.
- The TUI is the primary interactive experience, but it must not break automation that already depends on explicit subcommands.
- Planning and tests should assume that non-interactive automation uses subcommands rather than the TUI.

### Interactive Launch Rules

- `dot` with no subcommand should launch the TUI only when stdin and stdout are interactive terminals and `TERM` is not `dumb`.
- `dot tui` should enforce the same interactive-terminal requirement and return a clear error if launched in a non-interactive environment.
- Bare `dot` in a non-interactive context should not hang waiting on a TUI. It should print a short actionable error explaining that interactive use requires a TTY and scripted use should call an explicit subcommand.
- `dot --help` must remain non-interactive and should never attempt to launch the TUI.
- Acceptance tests should cover at least:
  - interactive parsing for bare `dot`
  - explicit `dot tui`
  - existing scripted subcommands
  - non-interactive rejection for bare `dot`

## User Experience

### Top-Level Structure

The TUI should have three structural layers:

1. A top mode switch between `Demo` and `Dev`.
2. A left capability rail.
3. A main workbench pane with contextual output and action controls.

The capability rail should list:

- `Parse & Validate (dot_dsl, dot_rt)`
- `Run & Serve (dot, dot_vm, dot_ops, dot_rt)`
- `Replay & Recovery (dot, dot_artifacts, dot_db)`
- `Security & Capabilities (dot_sec, dot_vm, dot_ops)`
- `State & DB (dot_db)`
- `Artifacts & Inspection (dot_artifacts, dot_db, dot)`

The current mission-control summary should remain visible, but it becomes supporting context rather than the whole product. The landing experience should still be action-first, with a prominent launcher or quick-action strip above the deeper capability panes.

### Controls & Navigation

The interaction model should be explicit and consistent across the whole testbench.

- `q` quits the TUI.
- `Tab` and `Shift-Tab` rotate focus between the mode tabs, capability rail, action list, result pane, and run selector.
- `h` and `l` or left/right arrows move across horizontal structures such as mode tabs or split panes when relevant.
- `j` and `k` or up/down arrows move within the focused list.
- `Enter` activates the focused action or opens the focused run or bundle item.
- `r` refreshes the current capability page and re-reads any persisted DotDB or artifact state.
- `Esc` closes transient overlays such as a run picker, bundle picker, or progress overlay.

The visual layout should be:

1. top row: mode tabs plus a small always-visible mission-control summary strip
2. left column: capability rail
3. upper center: action list for the selected mode and capability
4. lower center / right: result pane with summary and evidence
5. optional bottom or popover region: run and bundle selector overlays when the current action depends on previously recorded data

An MVP footer should also show the current selection tuple:

- mode
- capability
- action
- run
- bundle
- export
- fixture

The mission-control summary strip should remain visible on every page and show:

- current project root
- whether DotDB is available
- the current `app.dot` path when present
- counts for recent runs by status
- the currently selected run id when relevant

The footer should continue to show core keybindings. A lightweight help affordance such as `?` may be added later, but MVP can keep this in the always-visible footer as long as the current selection context is visible there.

### Selection Model

The TUI needs one clear source of truth for run- and bundle-dependent actions.

The application state should track:

- `selected_mode`
- `selected_capability`
- `selected_action`
- `selected_run_id`
- `selected_bundle_ref`
- `selected_export_dir`
- `selected_fixture_id`

The run selector should be sourced from DotDB recent runs plus any demo-generated run ids known to the fixture workspace. The bundle selector should be derived from the currently selected run where possible, and fall back to exported bundles under the demo workspace when relevant.

The fixture selector should be sourced from the current capability page's known scenarios. Each capability page should define a default fixture, and `selected_fixture_id` should initialize to that default when the page is entered.

The UX rules should be:

- If an action needs a run and none is selected, opening the action should open a run picker overlay instead of failing silently.
- If an action needs a fixture and the current capability has more than one valid fixture choice, opening the action should open a fixture picker overlay.
- Selecting a run should automatically update the selected bundle when an indexed artifact bundle exists.
- Selecting an exported bundle copy should update `selected_export_dir` without mutating the canonical `selected_bundle_ref`.
- Capability pages that produce new runs should promote the newly produced run to `selected_run_id`.
- Capability pages that resolve a canonical bundle from a run should promote that canonical bundle reference to `selected_bundle_ref`.
- Capability pages that export bundles should also promote the copied export directory to `selected_export_dir`.
- Capability pages that generate or refresh demo fixtures should promote that fixture to `selected_fixture_id`.
- The result pane should always show which run, bundle, or fixture the current output came from.

Input-source rules should be deterministic:

- `demo fixture` means use `selected_fixture_id` for the current capability, defaulting to that page's primary scenario if none was chosen yet.
- `current project` means operate against the current working directory and its `.dotlanth/` state when present.
- `selected run` means use `selected_run_id` from application state.
- `selected bundle` means use `selected_bundle_ref` from application state.
- `selected export` means use `selected_export_dir` from application state.

### Demo Mode

`Demo` is optimized for confidence, storytelling, and quick proof that the system works. It should prefer curated scenarios with clear labels and minimal setup. Each scenario should generate or refresh real fixtures in `.dotlanth/demo/`, then run actual Dotlanth code paths and show the resulting outputs.

Examples:

- A valid hello API fixture that parses, validates, runs, responds, emits a bundle, and can be replayed.
- An invalid fixture that demonstrates validation errors and capability denial behavior.
- A security fixture that shows both allowed and denied capability outcomes.

Each demo scenario should render:

- what it is testing
- which crates it exercises
- which files or bundles it generated
- the final result
- the most important evidence, such as run ids, manifest paths, trace summaries, or denial messages

The quick-action strip in `Demo` should prefer a small number of high-value actions per capability, usually between two and four. These actions should be optimized for one-key demonstration rather than wide parameter editing.

### Dev Mode

`Dev` is optimized for engineering verification and future feature growth. It should expose narrower checks, more detailed raw output, and tighter control over data sources. It should allow using either generated demo fixtures or the current project root when that makes sense.

Examples:

- Run only parser/validator checks without running the runtime.
- Inspect raw logs or summaries for a selected run.
- Re-run replay against a selected run id or exported bundle.
- Exercise capability-specific denial or allow cases directly.
- Verify state, bundle, and database information independently.

The quick-action strip in `Dev` can be denser than `Demo`, but it should still be bounded and readable. When a capability grows large, the page should group actions into focused clusters rather than listing a flat wall of commands.

## Capability Pages

### Parse & Validate (dot_dsl, dot_rt)

This page should test parsing, validation, and capability declaration visibility.

Demo actions:

- Generate a valid `app.dot` fixture and confirm parse/validation succeeds.
- Generate an invalid fixture and confirm a readable validation failure is shown.
- Show declared capability metadata that flows into runtime context.

Dev actions:

- Validate the current project's `app.dot`.
- Re-run validation against generated invalid fixtures.
- Show raw diagnostics, spans, and relevant semantic paths.

### Run & Serve (dot, dot_vm, dot_ops, dot_rt)

This page should prove the runtime path works end to end.

Demo actions:

- Launch a real demo API fixture.
- Hit a real route.
- Confirm the run persisted.
- Show the resulting run id, HTTP behavior, and emitted artifact evidence.

Dev actions:

- Run the current project with bounded requests when supported.
- Re-run specific demo fixtures.
- Show runtime summaries and selected log or trace output.

For all server actions, the lifecycle contract should be explicit:

- The TUI should bind demo fixtures to an ephemeral local port by first reserving a free port and writing it into the generated fixture before launch.
- The TUI should treat server actions as background jobs with a bounded lifecycle, not as blocking foreground calls.
- Readiness should be determined either from a known run announcement or by retrying the local HTTP connection for a short bounded window.
- Demo HTTP checks should use a real local request against the started route.
- Bounded request counts and explicit timeouts should ensure the server exits without hanging.
- If readiness or the HTTP probe times out, the action should fail and preserve all captured diagnostics in the result pane.

The runtime should read that port from the generated fixture itself, not from an environment override. The fixture generator should materialize the final `server listen <port>` statement before launch.

The MVP implementation must confirm that generated fixtures can deterministically express `server listen <port>` before the runtime starts. If that assumption fails during implementation, the plan should explicitly fall back to a documented alternative rather than silently widening scope.

Teardown rules should be:

- the launched process is owned by the action job
- success or failure must always attempt child shutdown
- timed-out jobs must terminate the owned child before reporting failure
- no action may leave a background server running after the job completes

### Replay & Recovery (dot, dot_artifacts, dot_db)

This page should prove recorded runs can be replayed from real stored inputs.

Demo actions:

- Replay a selected demo run id.
- Replay from an exported bundle.
- Confirm a new run was recorded.

Dev actions:

- Replay the currently selected run from DotDB.
- Replay a selected exported bundle path.
- Show replay lineage, run ids, and resulting bundle evidence.

### Security & Capabilities (dot_sec, dot_vm, dot_ops)

This page should test both allowed and denied capability behavior.

Demo actions:

- Run a fixture with the right capabilities and show success.
- Run a fixture that intentionally triggers a denial and show the denial clearly.

Dev actions:

- Re-run capability allow/deny cases individually.
- Show denial metadata, capability names, and count information sourced from the recorded report.

### State & DB (dot_db)

This page should surface persisted system state and database-backed diagnostics.

Demo actions:

- Show recent runs after demo activity.
- Show logs for the currently selected run.
- Show whether state diff data exists and summarize it.

Dev actions:

- Browse recent runs more deeply.
- Inspect logs, status, timestamps, and indexed bundle references.
- Expand state diff information or state snapshot details when present.

### Artifacts & Inspection (dot_artifacts, dot_db, dot)

This page should verify artifact integrity and usability.

Demo actions:

- Inspect a selected bundle summary.
- Export a selected bundle.
- Confirm the exported contents match expectations.

Dev actions:

- Show manifest, trace, state diff, and capability report summaries.
- Allow re-inspecting exported bundles or selected runs.
- Highlight unavailable or missing artifact sections without crashing the TUI.

## Fixtures

The TUI should be allowed to generate real fixtures on demand inside `.dotlanth/demo/`. Generated fixtures should be stable enough to reuse across sessions until explicitly refreshed, but also easy to regenerate when the user wants a clean demonstration.

The fixture layer should support at least:

- a valid hello API fixture
- an invalid validation fixture
- a capability denial fixture
- a replayable recorded run fixture generated from an actual run

Fixtures are not mocks. They are real Dotlanth inputs and outputs stored in a dedicated testbench workspace.

### Demo Execution Context

Demo fixtures are generated under `.dotlanth/demo/fixtures/<fixture_id>/`, but demo actions should still execute with the current project root as the owning runtime root unless a specific action explicitly declares an isolated demo project root.

The default rule is:

- fixture files live in `.dotlanth/demo/fixtures/...`
- runtime execution writes canonical DotDB and bundle state under the current project root's `.dotlanth/`
- exported demo copies live under `.dotlanth/demo/exports/...`

The effective input resolution rule should be explicit:

- when an action declares `demo fixture` input, the action passes the selected fixture's `app.dot` path as the execution input
- when an action declares `current project` input, the action resolves `app.dot` from the current working directory
- regardless of which `app.dot` input is used, canonical runtime state for MVP writes into the current project root's `.dotlanth/`

This keeps demo runs visible to the same recent-run, inspect, export, and replay surfaces as normal current-project activity.

If an action needs a fully isolated demo project root, that action must declare it explicitly and own a dedicated subtree under `.dotlanth/demo/tmp/`. MVP does not require isolated demo project roots by default.

### Fixture Workspace Layout

The demo workspace should live at `.dotlanth/demo/` and use stable subdirectories:

- `.dotlanth/demo/fixtures/`
- `.dotlanth/demo/exports/`
- `.dotlanth/demo/tmp/`
- `.dotlanth/demo/metadata/`

The fixture manager should keep deterministic names for stable scenarios, for example:

- `fixtures/hello-api/`
- `fixtures/invalid-parse/`
- `fixtures/capability-denial/`

Generated run and export directories can remain run-id-based because they represent real output, but the fixture metadata should map those outputs back to the originating scenario.

The metadata file should live at `.dotlanth/demo/metadata/scenarios.json` and track the latest known outputs per scenario. Its schema should be stable JSON shaped like:

```json
{
  "schema_version": "1",
  "scenarios": {
    "hello-api": {
      "fixture_root": ".dotlanth/demo/fixtures/hello-api",
      "latest_run_id": "run_x",
      "latest_bundle_ref": ".dotlanth/bundles/run_x",
      "latest_export_dir": ".dotlanth/demo/exports/hello-api-run_x"
    }
  }
}
```

Update rules:

- generating or refreshing a fixture updates only that scenario's metadata entry
- running a scenario updates `latest_run_id` and `latest_bundle_ref` when a run is produced
- exporting a scenario updates `latest_export_dir`
- missing paths should be tolerated and repaired on the next successful action rather than crashing metadata loading

Demo-generated runs remain canonical project runs:

- DotDB lives at `.dotlanth/dotdb.sqlite`
- real run bundles live at `.dotlanth/bundles/<run_id>/`
- demo-owned exported copies live at `.dotlanth/demo/exports/...`
- `.dotlanth/demo/` stores fixtures, metadata, temp inputs, and exported demo copies, not the canonical run bundle store

`bundle_ref` should have one stable meaning throughout the TUI:

- it is the canonical bundle reference string recorded for a run
- in current Dotlanth behavior, it is typically a project-relative filesystem path such as `.dotlanth/bundles/<run_id>`
- selectors and action inputs should treat it as an opaque stored reference first, resolving it to a filesystem path only when they need local file access

Refresh semantics should be safe and predictable:

- Refreshing a fixture should replace only that fixture's owned directory tree.
- Refreshing a fixture must never delete unrelated user files outside `.dotlanth/demo/`.
- A dedicated `Reset Demo Workspace` action may delete and recreate `.dotlanth/demo/` only after an explicit confirmation step in the TUI.
- Demo fixture generation should be idempotent enough that repeated runs produce the same structural layout even if the run ids differ.

## Execution Model

Every capability test should be represented as a real action with a consistent structure. The TUI should define a registry of test actions rather than hardcoding one-off buttons in scattered UI code.

Each action should define:

- stable id
- display title
- capability group
- crate labels
- mode visibility (`Demo`, `Dev`, or both)
- input source (`demo fixture`, `current project`, or `selected run`)
- execution function
- expected evidence
- result renderer

This registry becomes the contract for current and future ecosystem coverage.

### Action Interfaces

The implementation should formalize a small execution contract so capability pages remain consistent.

`ActionContext` should provide at least:

- current project root
- demo workspace root
- selected mode
- selected capability
- selected fixture id
- selected run id
- selected bundle ref
- current `app.dot` path when present

`ActionResult` should provide at least:

- high-level status (`success`, `failure`, `partial`)
- title
- summary lines
- evidence entries
- related paths
- produced run ids
- produced bundle refs
- raw output sections

Action execution behavior should follow these rules:

- Actions may be synchronous or background jobs, but the result model must hide that distinction from the page renderer.
- Long-running actions should stream progress and logs into the result pane or a transient progress overlay.
- Actions should support cancellation when they own a long-running child job such as a temporary local server.
- Actions should declare a timeout appropriate to their job type.
- Actions may retry readiness polling for local server startup, but should not retry semantic failures such as parse or capability denial failures.
- Result renderers should always show a concise summary first, then expandable or scrollable evidence sections below.

The stable supporting types should be small and explicit:

- `EvidenceEntry`
  - label
  - kind (`text`, `path`, `run_id`, `bundle_ref`, `metric`, `diagnostic`)
  - value

- `RawOutputSection`
  - title
  - lines
  - truncated flag

- `ActionError`
  - kind (`io`, `parse`, `validation`, `runtime`, `timeout`, `cancelled`, `missing_prerequisite`, `artifact`, `db`, `http_probe`)
  - message

- `JobUpdate`
  - status message
  - optional progress step
  - optional appended log lines

The renderer does not need to expose Rust type names, but planning should preserve these conceptual boundaries between execution and display.

## Glossary And Storage Map

- `run_id`
  - a real Dotlanth run identifier indexed in DotDB
  - source of truth: `.dotlanth/dotdb.sqlite`

- `bundle_ref`
  - the real artifact bundle reference associated with a `run_id`
  - stored as the canonical reference string from DotDB
  - in current local behavior, it usually resolves to `.dotlanth/bundles/<run_id>/`

- `export_dir`
  - a copied bundle directory produced by an export action
  - stored separately from `bundle_ref`
  - demo export location: `.dotlanth/demo/exports/<scenario>-<run_id>/`

- `export_dir`
  - a copied bundle directory produced by an export action
  - demo export location: `.dotlanth/demo/exports/<scenario>-<run_id>/`

- `fixture_id`
  - a stable scenario identifier for a generated demo input set
  - fixture location: `.dotlanth/demo/fixtures/<fixture_id>/`

## Prerequisites And Detection

The TUI should detect project-local state conservatively:

- DotDB is considered available when `.dotlanth/dotdb.sqlite` exists and can be opened.
- current-project runtime actions are considered available when the relevant `app.dot` exists.
- bundle-dependent actions are considered available when either the selected run resolves to a canonical bundle ref or a selected exported bundle exists.

Unavailable prerequisites should disable or gate the relevant action with an actionable message rather than producing a generic failure.

### Fresh Project Bootstrap

Demo mode needs a deterministic first-run story even when the current project root has no existing `.dotlanth/` state.

The MVP bootstrap rules should be:

- Demo fixture generation may create `.dotlanth/demo/` even when DotDB does not exist yet.
- The first successful demo run creates canonical DotDB and bundle state under the current project root's `.dotlanth/`.
- Demo actions that only need fixture generation or validation should still work before DotDB exists.
- Actions that require runs, logs, or bundles should gate on produced state and guide the user toward the required earlier demo action instead of failing generically.
- MVP should not auto-run `dot init` on the user's behalf for the current project root.

This keeps the bootstrap path simple: generate fixture, run real demo action, then unlock the run-dependent surfaces.

## Capability Execution Policy

To keep the “real behavior, no fragile scraping” rule enforceable, each capability page should prefer the most direct stable execution path:

- `Parse & Validate`
  - prefer direct crate API calls into parsing and validation code

- `Run & Serve`
  - prefer shared command/runtime helpers already used by the CLI

- `Replay & Recovery`
  - prefer shared replay helpers and real indexed bundle lookups

- `Security & Capabilities`
  - prefer direct or near-direct runtime/capability execution helpers, not shelling out

- `State & DB`
  - prefer direct DotDB queries and structured data helpers

- `Artifacts & Inspection`
  - prefer shared inspect/export helpers and direct artifact reads when needed

Shelling out to `dot ...` should be avoided inside the TUI unless a capability has no stable internal seam yet and the plan explicitly calls out that temporary compromise.

## Module Boundaries

The MVP implementation should keep the capability lab split into explicit modules:

- CLI entry and launch guard in the `dot` binary entrypoint
- TUI shell and event loop in `crates/dot/src/tui/`
- mode/capability/action state in a focused app-state module
- action registry in a dedicated registry module
- fixture manager in a dedicated fixture module
- background job runner in a dedicated jobs module
- structured result and evidence types in a dedicated model module
- page or component renderers grouped under the TUI module

Planning should name exact files for these boundaries rather than letting them collapse into one large TUI file.

### Background Jobs

Server starts, replay chains, and multi-step demo actions should run through a background job model.

Each job should support:

- start
- progress updates
- streamed logs
- completion result
- timeout
- cancellation

For v1, one foreground active job at a time is enough. The TUI does not need a full concurrent job scheduler, but it does need a visible active-job state so the user understands when the system is busy and what it is doing.

## Architecture

The implementation should separate the testbench into smaller units with clear responsibilities:

- a mode and capability navigation model
- a fixture manager for `.dotlanth/demo/`
- a capability action registry
- execution helpers that call existing shared command or subsystem code
- result models for summaries, diagnostics, and evidence
- ratatui rendering components for tabs, capability rail, action lists, and result panes

The existing command helpers should remain the authority for user-facing CLI parity where possible. Deeper subsystem checks can call lower-level crate APIs directly when that gives better coverage and avoids fragile output scraping.

## Data Flow

1. The user chooses `Demo` or `Dev`.
2. The user selects a capability group.
3. The TUI loads available actions for that mode and capability.
4. When an action runs, the TUI resolves its input source:
   - current project root
   - generated demo fixture
   - selected run or bundle
5. The action executes real Dotlanth behavior.
6. The TUI collects evidence:
   - stdout-like summaries
   - run ids
   - logs
   - manifest and artifact summaries
   - validation diagnostics
   - capability denial details
7. The workbench renders the result in both human-readable and subsystem-aware form.

If an action produces a new run or export, the resulting identifiers should flow back into the selection model so follow-up actions can chain naturally.

## Error Handling

The testbench should fail visibly but gracefully.

- Missing `app.dot` in the current project should be actionable, not fatal to the whole TUI.
- Missing DotDB should disable only the DB-dependent paths until a run is created.
- Missing artifact sections should render as unavailable or absent where appropriate.
- Failed demo actions should keep their diagnostics visible in-pane.
- Failed fixture generation should explain which file or directory operation broke.
- Unsupported current-project actions should point the user to the corresponding demo fixture action when possible.
- Cancelled background jobs should clean up any owned child process state they started for the action.
- Oversized raw output should be truncated in the default summary view, with a scrollable raw section for the full captured text when available.

## Testing Strategy

This feature should be expanded with test-first development.

At minimum, the implementation should add:

- render tests for mode tabs, capability rail, and action/result panes
- state tests for navigation and action filtering by mode/capability
- fixture manager tests for generated demo workspace layout
- action registry tests that verify required coverage exists
- capability-specific tests for each ecosystem group
- integration tests for the `dot` CLI defaulting into the TUI while preserving subcommands

The long-term rule should be explicit: every new feature added to Dotlanth must either register a new TUI action or extend an existing capability page so it becomes testable from the unified CLI.

### Test Boundaries

“Nothing mocked” applies to Dotlanth subsystem behavior, not to every layer of UI testing.

Allowed testing techniques:

- temporary directories for fixture and project isolation
- deterministic generated fixture content
- in-memory ratatui render backends for layout verification
- direct calls into real Dotlanth crate APIs
- real local loopback HTTP requests for runtime demo flows

Not allowed for subsystem verification:

- mocking DotDB query results instead of creating real database state
- faking artifact manifests instead of producing or materializing real bundle content when the action is supposed to exercise artifact logic
- stubbing parser, runtime, VM, or capability outcomes when those behaviors are the subject of the action

Planning should assume a layered test suite:

- UI-state and render tests may stay local and deterministic
- fixture-manager tests should operate on temp directories
- capability action tests should execute real subsystem paths
- end-to-end tests should cover at least one full demo chain from fixture generation through run, inspect, export, and replay

## v1 MVP

This design is broader than a single giant rewrite, so the first implementation plan should target a concrete MVP while preserving the long-term architecture.

The v1 MVP must include:

- mode tabs for `Demo` and `Dev`
- capability rail with all six capability groups visible
- an action registry architecture
- fixture workspace and metadata support
- run selection state
- result pane with summary plus raw evidence sections
- one or more real actions for every capability page

The minimum real action set for v1 is:

- `Init / Bootstrap`
  - scaffold a sample project action representing `dot init`

- `Parse & Validate`
  - run valid demo validation
  - run invalid demo validation

- `Run & Serve`
  - run demo API and probe a real route
  - run the current project in `Dev`

- `Replay & Recovery`
  - replay the latest demo run

- `Security & Capabilities`
  - run a capability denial demo

- `State & DB`
  - inspect recent runs and logs for the selected run

- `Artifacts & Inspection`
  - inspect and export the selected run bundle

### MVP Action Table

| Capability Page | Required MVP Actions | Input Source | Expected Evidence |
|---|---|---|---|
| `Init / Bootstrap` | scaffold sample project | demo fixture or target dir | created `app.dot`, `.gitignore`, success summary |
| `Parse & Validate` | validate valid fixture, validate invalid fixture | selected fixture | diagnostics or success summary |
| `Run & Serve` | run demo API, run current project in `Dev` | selected fixture or current project | run id, HTTP result, persisted bundle evidence |
| `Replay & Recovery` | replay latest run, replay exported bundle | selected run or selected export | new run id, replay result summary |
| `Security & Capabilities` | run capability denial demo | selected fixture | denial summary, capability evidence |
| `State & DB` | inspect recent runs, inspect logs | DotDB plus selected run | run list, log lines, state presence summary |
| `Artifacts & Inspection` | inspect selected bundle, export selected bundle | selected run or selected bundle | manifest summary, export path, artifact presence |

The v1 MVP may defer:

- advanced inline parameter editing
- multiple simultaneous background jobs
- crate-deep custom pages beyond the capability-first model
- richer filtering, searching, and help overlays beyond a minimal footer or prerequisite message

The follow-on implementation work should extend the registry and pages until every new feature added to Dotlanth is represented in the TUI, but the MVP plan should focus on shipping one end-to-end testbench slice across all six capability groups.

### MVP Acceptance Checklist

The MVP should ship only if these mappings exist:

- `dot init`
  - represented by at least one `Demo` or `Dev` action for sample project scaffolding

- `dot run`
  - represented by at least one `Demo` action and one `Dev` action

- `dot logs`
  - represented by at least one `State & DB` action

- `dot inspect`
  - represented by at least one `Artifacts & Inspection` action

- `dot export-artifacts`
  - represented by at least one `Artifacts & Inspection` action

- `dot replay`
  - represented by at least one `Replay & Recovery` action

In addition, each capability page must expose at least one real action even if it does not map one-to-one to an existing top-level subcommand.

## Open Boundaries

This design intentionally defers a few details to implementation planning rather than leaving them ambiguous:

- whether `Demo` and `Dev` should share one unified result pane component or use separate variants
- how much parameter editing `Dev` should expose in v1 versus later
- how far to push direct lower-level crate integration in the first pass beyond the existing shared command helpers

These are planning decisions, not product ambiguities. The core requirement remains fixed: capability-first navigation, Demo/Dev split, real fixtures, and real subsystem testing across the ecosystem.
