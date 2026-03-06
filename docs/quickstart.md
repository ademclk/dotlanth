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

After the request completes, the bounded run exits and writes an artifact bundle to `.dotlanth/bundles/<run_id>/`. The bundle includes `manifest.json`, `inputs/entry.dot`, `trace.jsonl`, and `capability_report.json`.

## Troubleshooting

### Port already in use
If `18080` is busy, change the port in `examples/hello-api/hello-api.dot` and re-run the command.
