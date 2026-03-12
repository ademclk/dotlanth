# Dotlanth v26.2.0-alpha — M6 (DOC) Plan

## Objective
End-to-end artifact bundle invariant enforcement plus documentation alignment.

## Tasks
- E-26020-AF-S3-T1: Wire always-on bundle generation into the `dot run` success path.
- E-26020-AF-S3-T2: Wire always-on bundle generation into failure paths (validation errors, runtime errors).
- E-26020-DS-S3-T1: Add or adjust dotDSL fixtures and tests to lock the diagnostics contract for capability errors.
- E-26020-SEC-S3-T2: Add tests for denial behavior plus report/trace status consistency.
- E-26020-DOC-S3-T1: Add end-to-end invariant tests and align release docs demo flow, known issues, and safe-sharing guidance.

## Acceptance Criteria
- Success and failure paths both yield a bundle with required files after a `.dot` file is resolved.
- Tests enforce the invariant and prevent regressions.
- Docs match the current CLI behavior and known limitations.

## Validation Commands
```bash
just check
```
