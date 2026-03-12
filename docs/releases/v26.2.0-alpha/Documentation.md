# Dotlanth v26.2.0-alpha — Release Documentation

## Status
- Date: 2026-03-12
- Milestone: M6 (DOC)
- Overall: Complete

## What’s in this alpha
- Artifact Bundle schema v1 with required-file placeholders and deterministic manifest metadata (`crates/dot_artifacts`)
- Record-first `dot run` execution with DotDB-indexed bundles (`crates/dot`, `crates/dot_db`, `crates/dot_ops`)
- dotDSL capability validation plus runtime capability usage reporting (`crates/dot_dsl`, `crates/dot_sec`)
- Stable `trace.jsonl`, `state_diff.json`, and `capability_report.json` bundle sections with inspect/export/replay CLI support
- Always-on bundle finalization for runs that resolve a `.dot` file, including validation failures and runtime failures

## Milestone checklist (M6)
- [x] E-26020-AF-S3-T1: Wire always-on bundle generation into the `dot run` success path
- [x] E-26020-AF-S3-T2: Wire always-on bundle generation into failure paths (validation errors, runtime errors)
- [x] E-26020-DS-S3-T1: Lock capability diagnostics contract with stable fixture tests
- [x] E-26020-SEC-S3-T2: Lock denial/report behavior and run/trace status consistency with tests
- [x] E-26020-DOC-S3-T1: Add invariant coverage and align docs/demo flow/known issues

## Validation

Local verification gate:

```bash
just check
```

## Demo steps (hello-api)

Run the bounded demo:

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

If the CLI exits before printing the run id, inspect `.dotlanth/bundles/` or open the bundle `manifest.json` to recover it.

## Safe sharing

Treat `.dotlanth/bundles/`, exported bundles, and `.dotlanth/dotdb.sqlite` as sensitive artifacts. They can contain source inputs, runtime traces, logs, and state data. Review and scrub secrets or user data before sharing them outside the intended audience.

## Known issues / limitations

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
