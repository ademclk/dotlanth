# Quickstart: Run hello-api

## Prerequisites
- Rust `1.93` (see `rust-toolchain.toml`)
- `curl`

## Run the example

From the repo root:

```bash
cargo run -p dot -- run examples/hello-api/hello-api.dot
```

Expected output:

```text
Listening on http://127.0.0.1:18080 (Ctrl+C to stop)
```

In a second terminal:

```bash
curl http://127.0.0.1:18080/hello
```

Expected output:

```text
Hello from Dotlanth
```

## Troubleshooting

### Port already in use
If `18080` is busy, change the port in `examples/hello-api/hello-api.dot` and re-run the command.
