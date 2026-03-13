# Dotlanth v26.2.0-alpha — Release Documentation

## Status
- Date: 2026-03-13
- Milestone: Capability Lab TUI
- Overall: Complete

## What’s in this alpha
- Explicit `dot tui` ratatui workbench for exercising the Dotlanth ecosystem from one CLI entrypoint (`crates/dot/src/tui`)
- Capability-first `Demo` and `Dev` navigation across parse/validate, run/serve, replay, security, state, and artifact workflows
- Deterministic demo fixtures and scenario metadata under `.dotlanth/demo/` with real subsystem execution, not mocks
- Integrated validation, run, inspect, export, log, replay, and capability-evidence actions backed by DotDB and artifact bundles
- Hardened TUI behavior for recoverable action failures, repeat exports, bind conflicts, and corrupt local metadata / DotDB state

## Release Notes
- Introduces the first unified Dotlanth capability lab in `dot tui`, with section labels that keep the underlying create-name capabilities visible while you test them.
- Preserves the existing scripted CLI surface while aligning the developer entrypoint around `just tui` for the interactive workbench.
- Ships deterministic demo fixtures so capability coverage can be exercised locally without mocking the real subsystem paths.
- Packages this alpha as an internal validation drop for capability testing, release workflow verification, and follow-on TUI expansion.

## Validation

Local verification gate:

```bash
just check
```

Focused CLI/TUI checks:

```bash
cargo test -p dot
```

## Demo steps (capability lab)

Launch the TUI:

```bash
just tui
```

Or directly:

```bash
cargo run -p dot -- tui
```

Inside the TUI:
- `Demo` -> `Parse & Validate` to generate and validate the `hello-api` and `invalid-parse` fixtures
- `Demo` -> `Run & Serve` to launch the demo API, probe `/hello`, and persist a real run
- `Security & Capabilities` to inspect the recorded capability report
- `Artifacts & Inspection` to inspect or export the selected bundle
- `Replay & Recovery` to replay the selected recorded run into a fresh run id

## Scripted commands still available

Run the bounded hello-api demo without the TUI:

```bash
cargo run -p dot -- run --file examples/hello-api/hello-api.dot --max-requests 1
```

Call it from another terminal:

```bash
curl http://127.0.0.1:18080/hello
```

Expected response:

```text
Hello from Dotlanth
```

Inspect the bundle summary by run id:

```bash
cargo run -p dot -- inspect <run_id>
```

Export the indexed bundle:

```bash
cargo run -p dot -- export-artifacts <run_id> --out /tmp/run-bundle
```

Replay the saved input snapshot:

```bash
cargo run -p dot -- replay <run_id>
cargo run -p dot -- replay --bundle /tmp/run-bundle
```

Validation-failure demo:

```bash
cargo run -p dot -- run --file examples/hello-api/hello-api-deny.dot
```

That command fails validation, but it still records a failed run id and finalizes the required bundle files with explicit `unavailable` markers for sections that never executed.

## Safe sharing

Treat `.dotlanth/bundles/`, exported bundles, and `.dotlanth/dotdb.sqlite` as sensitive artifacts. They can contain source inputs, runtime traces, logs, and state data. Review and scrub secrets or user data before sharing them outside the intended audience.

## Known issues / limitations

- The TUI currently launches only through `dot tui` or `just tui`; bare `dot` requires an explicit subcommand.
- Demo fixture ports are reserved and retried opportunistically for local runs, but they are still local-process demos rather than a general service manager.
- `dot replay` is input replay, not a full determinism guarantee across environments or side effects.
- Bundle creation starts only after Dotlanth resolves a concrete `.dot` file path. Commands that fail before that point, such as running in an empty directory without `--file`, do not emit a bundle.
- HTTP serving is intentionally minimal: plain-text responses, exact method/path matching, no TLS, and loopback binding only.
- dotDSL remains intentionally small in `0.1`: no variables, middleware, headers, request-body parsing, or dynamic responses yet.

## ADR list (major decisions)

- ADR-26022: External run id scheme
- ADR-26023: Capability declaration contract
- ADR-26024: Capability enforcement point
- ADR-26025: Artifact index + state scope

## RFC list (design writeups)

- RFC-26021: Trace schema
- RFC-26022: Capability report schema
