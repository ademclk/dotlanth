# Quickstart: Run hello-api

## Prerequisites
- Rust `1.93` (see `rust-toolchain.toml`)
- `curl`

## Run the example

From the repo root:

```bash
cargo run -p dot -- run --file examples/hello-api/hello-api.dot --max-requests 1
```

`dot run` also accepts `--determinism <default|strict>`. The chosen mode is persisted in DotDB, recorded in the bundle `manifest.json`, and echoed by `dot inspect <run_id>`. The bundle manifest now also records `determinism_eligibility`, `determinism_audit_summary`, and a `replay_proof` payload so replay comparisons can use stable bundle metadata.

Expected output:

```text
run run_<uuid> listening on http://127.0.0.1:18080
```

In a second terminal:

```bash
curl http://127.0.0.1:18080/hello
```

Expected output:

```text
Hello from Dotlanth
```

After the request completes, the bounded run exits and writes an artifact bundle to `.dotlanth/bundles/<run_id>/`. The bundle includes `manifest.json`, `inputs/entry.dot`, `trace.jsonl`, `state_diff.json`, `determinism_report.json`, and `capability_report.json`.

If `dot run` resolves a `.dot` file and then fails during validation or runtime, Dotlanth still finalizes a bundle for that run id. Sections that could not be recorded stay present with explicit `unavailable` markers. On early failures, the CLI may return before printing the run id, so inspect `.dotlanth/bundles/` or the bundle `manifest.json` to recover it.

DotDB indexes the finalized bundle by external `run_id`, storing the bundle ref plus `manifest.json` SHA-256 and byte count.

Strict mode is fail-closed for opcode classification coverage. `log.emit` is classified as a `controlled_side_effect`, so strict mode does not reject it on determinism grounds, but the call still needs the `log` capability to succeed. `net.http.serve`, `time.now`, and `random.bytes` are classified as `non_deterministic` and fail before their host hooks execute. Unclassified syscall ids are also rejected before execution.

`state_diff.json` exports a stable `state_kv` diff with deterministic ordering and no unchanged entries. In v26.3 it can include the persisted determinism budget snapshot under `runtime/determinism_budget.v26_3`.

`determinism_report.json` records the selected mode, replay eligibility, an `audit_summary`, the informational v26.3 counters, and any strict-mode violation summaries for the run.

The manifest `replay_proof` payload summarizes normalized fingerprints for the trace, state diff, capability report, and determinism audit surface. It strips run-local trace fields such as `run_id`, `seq`, and `run.start.entry_dot`, and it ignores the internal `runtime/determinism_budget.v26_3` state bookkeeping change when calculating the proof's `state_diff` fingerprint so original and replayed runs can compare cleanly.

The capability report includes declared capabilities with source metadata, along with stable `used` and `denied` counts for capability-gated operations. `trace.jsonl` also records syscall `classification` facts, `required_capability` when a registered syscall is capability-gated, `audit.determinism_denial` events for strict failures, and `audit.controlled_side_effect` events when controlled operations pass determinism policy.

You can inspect the recorded bundle summary directly from the run id:

```bash
cargo run -p dot -- inspect <run_id>
```

To materialize a copy of the indexed bundle somewhere else:

```bash
cargo run -p dot -- export-artifacts <run_id> --out /tmp/run-bundle
```

To replay the saved `inputs/entry.dot` into a fresh run:

```bash
cargo run -p dot -- replay <run_id>
cargo run -p dot -- replay --bundle /tmp/run-bundle
```

Replay reuses the saved input snapshot; it is helpful for reproducing the input document, but it is not a claim of full deterministic re-execution across environments.

## Safe sharing

Treat `.dotlanth/bundles/` and `.dotlanth/dotdb.sqlite` as sensitive data. They can contain source inputs, runtime traces, logs, and state snapshots or diffs. Scrub secrets and user data before exporting or sharing bundles outside your machine or team.

## Troubleshooting

### Port already in use
If `18080` is busy, change the port in `examples/hello-api/hello-api.dot` and re-run the command.
