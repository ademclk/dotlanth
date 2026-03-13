# Hello API walkthrough (dotDSL v0.1)

The hello API lives in `examples/hello-api/hello-api.dot`.

## The full file

```dot
dot 0.1

app "hello-api"
project "dotlanth"

allow log
allow net.http.listen

server listen 18080

api "public"
  route GET "/hello"
    respond 200 "Hello from Dotlanth"
  end
end
```

## What each statement means

### `dot 0.1`
Selects the dotDSL version. This repo currently supports `0.1` only.

### `app` and `project`
Provides metadata for the document:
- `app "hello-api"`: the application name
- `project "dotlanth"`: the project name

At least one of `app` or `project` must be present.

### `allow ...` (capabilities)
Dotlanth is **deny-by-default** for side effects. Capabilities must be explicitly granted:
- `allow log`: enables emitting host logs (used by the runtime/ops layer)
- `allow net.http.listen`: enables binding and serving an HTTP listener

### `server listen <port>`
Declares the HTTP listener port for the API runtime.

In the current runtime, this binds to `127.0.0.1:<port>`.

### `api "public" ... end`
Starts an API block (a named group of routes).

### `route <VERB> "<path>" ... end`
Declares a route. Current constraints:
- verbs: `GET`, `POST`, `PUT`, `PATCH`, `DELETE`
- path must be quoted and start with `/`

### `respond <status> "<body>"`
Declares a static response for the route:
- status must be in `100..=599`
- body must be quoted

## Capability denied / missing capability troubleshooting

If you forget the listen capability, validation fails clearly. Try:

```bash
cargo run -p dot -- run --file examples/hello-api/hello-api-deny.dot
```

Expected output:

```text
examples/hello-api/hello-api-deny.dot:8:1:19 capabilities | missing required capability `net.http.listen` for `server listen`
```

This validation failure still records a failed run bundle under `.dotlanth/bundles/<run_id>/`. The input snapshot and trace are preserved, and sections that never executed are left in place with explicit `unavailable` markers. On early failures, the CLI may not print the run id before exiting, so inspect `.dotlanth/bundles/` or the bundle `manifest.json` to recover it.

## Runtime artifacts

A successful bounded run such as:

```bash
cargo run -p dot -- run --file examples/hello-api/hello-api.dot --max-requests 1
```

prints a stable external `run_id` and writes an artifact bundle under `.dotlanth/bundles/<run_id>/`.

You can also pass `--determinism <default|strict>`. The selected mode is persisted into DotDB, stored in the bundle `manifest.json` as `determinism_mode`, and printed back by `dot inspect <run_id>`.

The bundle captures:
- `inputs/entry.dot`
- `trace.jsonl`
- `state_diff.json`
- `manifest.json`
- `capability_report.json`

`trace.jsonl` is ordered by `seq` and includes lifecycle events, syscall boundaries, and source mappings back to the `server`, `route`, and `respond` statements when that mapping is known.
`state_diff.json` exports a stable `state_kv` diff ordered by `(namespace, key)` and skips unchanged values.
`capability_report.json` lists declared capabilities with their source spans/semantic paths, plus stable `used` and `denied` accounting. When a capability-gated syscall is denied, the report keeps a representative error message and the matching trace `seq` when available.
DotDB indexes the finalized bundle by the external `run_id`, storing the bundle ref plus `manifest.json` SHA-256 and byte count.

Strict mode is fail-closed for opcode classification coverage. In the current milestone:
- pure VM opcodes such as `halt`, `load_const`, and `mov` remain allowed
- `log.emit` is classified as a `controlled_side_effect`, so strict mode does not reject it on determinism grounds; the call still needs the `log` capability to succeed
- `net.http.serve` is classified as `non_deterministic`, so strict mode rejects it as a determinism violation before the side effect executes
- unclassified syscall ids are rejected before execution instead of silently falling through policy

Use the run id to print a stable bundle summary:

```bash
cargo run -p dot -- inspect <run_id>
```

To copy the recorded bundle to another directory:

```bash
cargo run -p dot -- export-artifacts <run_id> --out /tmp/run-bundle
```

To replay the saved input snapshot into a fresh run:

```bash
cargo run -p dot -- replay <run_id>
cargo run -p dot -- replay --bundle /tmp/run-bundle
```

Replay uses the recorded `inputs/entry.dot` snapshot as a fresh input. It is useful for reproducing the source document, but it is not a strict determinism guarantee for every side effect or environment detail.

## Safe sharing

Bundles and `.dotlanth/dotdb.sqlite` should be treated as sensitive artifacts. They can include source snapshots, runtime traces, logs, and state data. Review and scrub those files before sending them to anyone outside the intended audience.
