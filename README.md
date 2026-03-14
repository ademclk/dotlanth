# Dotlanth

[![CI](https://github.com/ademclk/dotlanth/actions/workflows/ci.yml/badge.svg)](https://github.com/ademclk/dotlanth/actions/workflows/ci.yml)
[![CD](https://github.com/ademclk/dotlanth/actions/workflows/cd.yml/badge.svg)](https://github.com/ademclk/dotlanth/actions/workflows/cd.yml)
[![License: GPLv3 + Commercial](https://img.shields.io/badge/license-GPLv3%20%2B%20Commercial-black)](./LICENSE)

Software should start with an idea, not a setup checklist.

Dotlanth turns one `dot` file into a running API.

No framework ceremony.
No boilerplate maze.
Just intent, executed.

## One File. One Runtime.

In Dotlanth, the API spec language is called **dot**.
A project starts with a single `dot` file that defines routes and behavior.

Dotlanth does the rest:
- **dotDSL** parses dot.
- **DotVM** executes it safely.
- **DotDB** records state and run history.

## Why Dotlanth Exists

Building software should feel like building software.
Not wiring, scaffolding, and repetitive glue code.

Dotlanth removes setup drag so teams can move from idea to endpoint fast.

## Quickstart (Current)

See `docs/quickstart.md` for a copy/paste walkthrough.

Launch the capability lab:

```bash
cargo run -p dot -- tui
```

The `dot tui` experience is now a ratatui capability lab with:
- `Demo` and `Dev` modes over the same real Dotlanth behaviors
- a capability-first rail for parse/validate, run/serve, replay, security, state, and artifacts
- real demo fixtures generated under `.dotlanth/demo/fixtures/`
- built-in inspection, export, replay, and log views backed by canonical DotDB state

The scriptable subcommands still work for automation and docs:

```bash
cargo run -p dot -- run --file examples/hello-api/hello-api.dot --max-requests 1
```

`dot run` also accepts `--determinism <default|strict>`. The selected mode is persisted in DotDB and in each bundle `manifest.json`, and `dot inspect <run_id>` prints it back. Strict mode is fail-closed today: unsupported non-deterministic host syscalls such as `net.http.serve`, `time.now`, and `random.bytes` are denied before side effects execute.

```bash
curl http://127.0.0.1:18080/hello
```

Expected runtime output:

```text
run run_<uuid> listening on http://127.0.0.1:18080
```

The bounded run writes an artifact bundle to `.dotlanth/bundles/<run_id>/` with:
- `manifest.json`
- `inputs/entry.dot`
- `trace.jsonl`
- `state_diff.json`
- `determinism_report.json`
- `capability_report.json`

DotDB also indexes that finalized bundle by external `run_id`, storing the bundle ref plus `manifest.json` SHA-256 and byte count.

`manifest.json` now also records `determinism_mode` alongside the stable run metadata.

`state_diff.json` exports a stable `state_kv` diff with deterministic ordering and omits unchanged values.

`determinism_report.json` records the informational v26.3 determinism budget counters plus any strict-mode violation summaries captured for the run.

`capability_report.json` records declared capabilities with source spans and semantic paths, plus stable `used` and `denied` accounting for capability-gated syscalls.

`trace.jsonl` keeps stable syscall boundary events and now records each registered syscall's determinism `classification`, `required_capability` when a capability gate applies, and explicit audit events for strict determinism denials plus controlled side-effect use.

The TUI also tracks demo-owned fixture and export metadata in `.dotlanth/demo/metadata/scenarios.json` so the same scenarios can be refreshed and replayed across sessions without mocking any subsystem behavior.

## A 60-Second Flow

Create and run:

```bash
dot init hello-api
cd hello-api
dot tui
```

Inside the TUI:
- `Demo` -> `Parse & Validate` to generate and validate fixtures
- `Demo` -> `Run & Serve` to launch the real hello-api demo and probe `/hello`
- `Security & Capabilities`, `State & DB`, and `Artifacts & Inspection` to inspect the recorded capability report, logs, exports, and replay flow

Or stay fully scriptable:

```bash
dot run --max-requests 1
```

Then inspect runs:

```bash
dot logs <run_id> # from `dot run` output
dot inspect <run_id>
dot export-artifacts <run_id> --out /tmp/run-bundle
dot replay <run_id>
dot replay --bundle /tmp/run-bundle
```

Illustrative `dot` file:

```dot
dot 0.1

app "hello-api"

allow log
allow net.http.listen

server listen 8080

api "public"
  route GET "/hello"
    respond 200 "Hello from Dotlanth"
  end
end
```

## Status

Dotlanth is early, by design.

Current milestone focus:
- [x] Foundation (workspace, local gates, CI/CD)
- [x] HTTP runtime (static routes)
- [ ] State model
- [ ] Database connectors
- [ ] Security model
- [ ] Plugins
- [ ] Studio hooks

## Development

Run the full local gate (matches CI):

```bash
just check
```

If you don't have `just` installed, run the underlying commands directly:

```bash
cargo fmt --check
cargo clippy --workspace --all-targets --all-features -- -D warnings
cargo test --workspace
```

## Contributing

If runtime systems, compilers, databases, and developer tools excite you:

1. Open an issue with a clear problem statement.
2. Propose one focused improvement.
3. Ship one clean change.

## License

Dotlanth is dual licensed:
- GNU General Public License v3.0 or later (GPLv3+)
- Commercial License

See [LICENSE](./LICENSE), [LICENSE-GPLv3](./LICENSE-GPLv3), and [LICENSE-COMMERCIAL](./LICENSE-COMMERCIAL).
