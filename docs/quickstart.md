# Quickstart: Run hello-api

## Prerequisites
- Rust `1.93` (see `rust-toolchain.toml`)
- `curl`

## Run the example

From the repo root:

```bash
cargo run -p dot -- run --file examples/hello-api/hello-api.dot --max-requests 1
```

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

After the request completes, the bounded run exits and writes an artifact bundle to `.dotlanth/bundles/<run_id>/`. The bundle includes `manifest.json`, `inputs/entry.dot`, `trace.jsonl`, `state_diff.json`, and `capability_report.json`.

DotDB indexes the finalized bundle by external `run_id`, storing the bundle ref plus `manifest.json` SHA-256 and byte count.

`state_diff.json` exports a stable `state_kv` diff with deterministic ordering and no unchanged entries.

The capability report includes declared capabilities with source metadata, along with stable `used` and `denied` counts for capability-gated operations.

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

## Troubleshooting

### Port already in use
If `18080` is busy, change the port in `examples/hello-api/hello-api.dot` and re-run the command.
