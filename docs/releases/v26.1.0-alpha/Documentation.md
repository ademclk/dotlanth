# Dotlanth v26.1.0-alpha — Release Documentation

## Status
- Date: 2026-03-01
- Milestone: M8 (DOC)
- Overall: Complete (hello-api demo is reproducible)

## What’s in this alpha
- dotDSL `0.1` parsing + semantic validation (`crates/dot_dsl`)
- Capabilities + deny-by-default enforcement (`crates/dot_sec`)
- Local-first SQLite storage for runs/logs/state (`crates/dot_db`)
- Ops host syscalls: logging + minimal HTTP serving (`crates/dot_ops`)
- Deterministic VM core + syscall boundary (`crates/dot_vm`)
- CLI runner: `dot run <file.dot>` (`crates/dot`)
- Docs: quickstart + hello-api walkthrough (`docs/`)

## Milestone checklist (M8)
- [x] E-26010-DOC-S1-T1: Quickstart (`docs/quickstart.md`)
- [x] E-26010-DOC-S2-T1: Hello API walkthrough (`docs/hello-api.md`)
- [x] E-26010-DOC-S3-T1: Release docs (this file)

## Demo steps (hello-api)

Local verification gate (matches CI):

```bash
just check
```

Run the API:

```bash
cargo run -p dot -- run examples/hello-api/hello-api.dot
```

Call it:

```bash
curl http://127.0.0.1:18080/hello
```

Expected response:

```text
Hello from Dotlanth
```

Capability error demo:

```bash
cargo run -p dot -- run examples/hello-api/hello-api-deny.dot
```

## Packaging / publishing

Local build:

```bash
cargo build --release -p dot --locked
```

Local tarball (current host only):

```bash
just dist
```

## Known issues / limitations
- `dot` CLI is minimal: supports `run`, `--help`, and `--version` only.
- HTTP server is intentionally tiny: plain-text responses; exact match on method + path; no TLS; no graceful shutdown; binds to `127.0.0.1`.
- dotDSL is intentionally small (v0.1): no variables, middleware, request body parsing, headers, or dynamic responses yet.
- Some capability checks happen at validation time (e.g. `server listen` requires `allow net.http.listen`).

## ADR list (major decisions)

Not released yet

## RFC list (design writeups)

Not released yet
