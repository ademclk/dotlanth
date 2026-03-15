use dot_artifacts::{
    CAPABILITY_REPORT_FILE, DETERMINISM_REPORT_FILE, ENTRY_DOT_FILE, MANIFEST_FILE,
    STATE_DIFF_FILE, TRACE_FILE,
};
use dot_db::{DotDb, RunStatus};
use serde_json::json;
use std::fs;
use std::path::Path;
use std::process::Command;
use tempfile::TempDir;

#[test]
fn inspect_prints_stable_bundle_summary_for_run_id() {
    let temp = TempDir::new().expect("temp dir must create");
    let run_id = "run_inspect_fixture";
    let bundle_dir = temp.path().join(".dotlanth").join("bundles").join(run_id);
    let inputs_dir = bundle_dir.join("inputs");
    fs::create_dir_all(&inputs_dir).expect("bundle inputs dir must create");

    let entry_dot = br#"dot 0.1
app "fixture"
end
"#;
    let trace = concat!(
        "{\"seq\":0,\"event\":\"run.start\"}\n",
        "{\"seq\":1,\"event\":\"http.request\"}\n",
        "{\"seq\":2,\"event\":\"syscall.result\",\"status\":\"ok\"}\n",
        "{\"seq\":3,\"event\":\"run.finish\",\"status\":\"succeeded\"}\n"
    );
    let state_diff = json!({
        "schema_version": "1",
        "scope": "state_kv",
        "changes": [
            {
                "namespace": "session",
                "key": "new",
                "change": "added",
                "value": [97]
            },
            {
                "namespace": "session",
                "key": "counter",
                "change": "updated",
                "previous_value": [49],
                "value": [50]
            }
        ]
    });
    let capability_report = json!({
        "schema_version": "1",
        "declared": [
            {
                "capability": "log",
                "source": {
                    "semantic_path": "capabilities[0]",
                    "span": {
                        "line": 2,
                        "column": 7,
                        "length": 3
                    }
                }
            },
            {
                "capability": "net.http.listen",
                "source": {
                    "semantic_path": "capabilities[1]",
                    "span": {
                        "line": 3,
                        "column": 7,
                        "length": 15
                    }
                }
            }
        ],
        "used": [
            {
                "capability": "net.http.listen",
                "count": 1
            }
        ],
        "denied": [
            {
                "capability": "log",
                "count": 1,
                "message": "capability denied",
                "seq": 2
            }
        ]
    });
    let determinism_report = json!({
        "schema_version": "1",
        "informational": true,
        "counters": {
            "gated_total": 1,
            "allowed_total": 0,
            "denied_total": 1,
            "controlled_side_effect_total": 0,
            "non_deterministic_total": 1
        },
        "violations": [
            {
                "syscall": "net.http.serve",
                "classification": "non_deterministic",
                "required_capability": "net.http.listen",
                "count": 1,
                "first_seq": 2,
                "message": "determinism violation"
            }
        ]
    });

    write_file(temp.path(), ENTRY_DOT_FILE, entry_dot);
    write_file(temp.path(), TRACE_FILE, trace.as_bytes());
    let state_diff_bytes =
        serde_json::to_vec_pretty(&state_diff).expect("state diff must serialize");
    write_file(temp.path(), STATE_DIFF_FILE, &state_diff_bytes);
    let capability_report_bytes =
        serde_json::to_vec_pretty(&capability_report).expect("report must serialize");
    write_file(
        temp.path(),
        CAPABILITY_REPORT_FILE,
        &capability_report_bytes,
    );
    let determinism_report_bytes =
        serde_json::to_vec_pretty(&determinism_report).expect("report must serialize");
    write_file(
        temp.path(),
        DETERMINISM_REPORT_FILE,
        &determinism_report_bytes,
    );

    let manifest = json!({
        "schema_version": "1",
        "run_id": run_id,
        "created_at_ms": 1,
        "determinism_mode": "default",
        "determinism_eligibility": {
            "status": "eligible"
        },
        "determinism_audit_summary": {
            "budget": {
                "gated_total": 1,
                "allowed_total": 0,
                "denied_total": 1,
                "controlled_side_effect_total": 0,
                "non_deterministic_total": 1
            },
            "violations": {
                "count": 1,
                "first_seq": 2
            }
        },
        "replay_proof": {
            "status": "ready",
            "schema_version": "1",
            "eligibility": {
                "status": "eligible"
            },
            "canonical_surface": {
                "trace": {
                    "event_count": 4,
                    "fingerprint": "fixture-trace"
                },
                "state_diff": {
                    "status": "ok",
                    "change_count": 2,
                    "fingerprint": "fixture-state"
                },
                "capability_report": {
                    "status": "ok",
                    "declared_count": 2,
                    "used_count": 1,
                    "denied_count": 1,
                    "fingerprint": "fixture-capability"
                },
                "determinism": {
                    "mode": "default",
                    "budget": {
                        "gated_total": 1,
                        "allowed_total": 0,
                        "denied_total": 1,
                        "controlled_side_effect_total": 0,
                        "non_deterministic_total": 1
                    },
                    "violations": {
                        "count": 1,
                        "first_seq": 2
                    },
                    "fingerprint": "fixture-determinism"
                }
            },
            "comparison_fingerprint": "fixture-proof"
        },
        "required_files": [
            MANIFEST_FILE,
            ENTRY_DOT_FILE,
            TRACE_FILE,
            STATE_DIFF_FILE,
            DETERMINISM_REPORT_FILE,
            CAPABILITY_REPORT_FILE
        ],
        "sections": {
            "capability_report": {
                "path": CAPABILITY_REPORT_FILE,
                "status": "ok",
                "bytes": capability_report_bytes.len()
            },
            "determinism_report": {
                "path": DETERMINISM_REPORT_FILE,
                "status": "ok",
                "bytes": determinism_report_bytes.len()
            },
            "state_diff": {
                "path": STATE_DIFF_FILE,
                "status": "ok",
                "bytes": state_diff_bytes.len()
            },
            "trace": {
                "path": TRACE_FILE,
                "status": "ok",
                "bytes": trace.len()
            }
        },
        "inputs": {
            "entry_dot": {
                "path": ENTRY_DOT_FILE,
                "status": "ok",
                "bytes": entry_dot.len()
            }
        }
    });
    let mut manifest_bytes = serde_json::to_vec_pretty(&manifest).expect("manifest must serialize");
    manifest_bytes.push(b'\n');
    write_file(temp.path(), MANIFEST_FILE, &manifest_bytes);

    let mut db = DotDb::open_in(temp.path()).expect("db must open");
    let created = db.create_run_with_id(run_id).expect("run must create");
    db.finalize_run(created.row_id(), RunStatus::Succeeded)
        .expect("run must finalize");
    db.set_artifact_bundle(
        run_id,
        &format!(".dotlanth/bundles/{run_id}"),
        "fixture-manifest-sha256",
        manifest_bytes.len() as u64,
    )
    .expect("bundle index must store");

    let manifest_path = workspace_manifest();
    let manifest_path = manifest_path.display().to_string();
    let output = Command::new(dot_bin())
        .current_dir(temp.path())
        .args([
            "run",
            "--quiet",
            "--manifest-path",
            &manifest_path,
            "-p",
            "dot",
            "--",
            "inspect",
            run_id,
        ])
        .output()
        .expect("inspect command must run");

    assert!(
        output.status.success(),
        "inspect should succeed\nstdout:\n{}\nstderr:\n{}",
        String::from_utf8_lossy(&output.stdout),
        String::from_utf8_lossy(&output.stderr)
    );

    let stdout = String::from_utf8(output.stdout).expect("stdout must be utf-8");
    assert_eq!(
        stdout,
        format!(
            concat!(
                "run_id: {}\n",
                "status: succeeded\n",
                "determinism: default\n",
                "validator: eligible\n",
                "schema_version: 1\n",
                "artifacts:\n",
                "  manifest.json: present ({} bytes)\n",
                "  inputs/entry.dot: present ({} bytes)\n",
                "  trace.jsonl: present ({} bytes)\n",
                "  state_diff.json: present ({} bytes)\n",
                "  determinism_report.json: present ({} bytes)\n",
                "  capability_report.json: present ({} bytes)\n",
                "determinism_summary: denied count=1 first_seq=2\n",
                "capabilities: declared=2 used=1 denied=1\n",
                "trace: events=4\n",
                "state_diff: changes=2 added=1 updated=1 removed=0\n"
            ),
            run_id,
            manifest_bytes.len(),
            entry_dot.len(),
            trace.len(),
            state_diff_bytes.len(),
            determinism_report_bytes.len(),
            capability_report_bytes.len(),
        )
    );
}

#[test]
fn inspect_reports_unavailable_artifacts_without_failing() {
    let temp = TempDir::new().expect("temp dir must create");
    let run_id = "run_inspect_unavailable";
    let bundle_dir = temp.path().join(".dotlanth").join("bundles").join(run_id);
    let entry_dot = br#"dot 0.1
app "fixture"
end
"#;
    write_fixture_file(&bundle_dir, ENTRY_DOT_FILE, entry_dot);

    let manifest = json!({
        "schema_version": "1",
        "run_id": run_id,
        "created_at_ms": 1,
        "determinism_mode": "default",
        "determinism_eligibility": {
            "status": "unsupported",
            "reason": "execution_not_started"
        },
        "determinism_audit_summary": {
            "budget": {
                "gated_total": 0,
                "allowed_total": 0,
                "denied_total": 0,
                "controlled_side_effect_total": 0,
                "non_deterministic_total": 0
            },
            "violations": {
                "count": 0,
                "first_seq": null
            }
        },
        "replay_proof": {
            "status": "ready",
            "schema_version": "1",
            "eligibility": {
                "status": "unsupported",
                "reason": "execution_not_started"
            },
            "canonical_surface": {
                "trace": {
                    "event_count": 0,
                    "fingerprint": "unsupported"
                },
                "state_diff": {
                    "status": "unavailable",
                    "reason": "execution_not_started"
                },
                "capability_report": {
                    "status": "unavailable",
                    "reason": "execution_not_started"
                },
                "determinism": {
                    "mode": "default",
                    "budget": {
                        "gated_total": 0,
                        "allowed_total": 0,
                        "denied_total": 0,
                        "controlled_side_effect_total": 0,
                        "non_deterministic_total": 0
                    },
                    "violations": {
                        "count": 0,
                        "first_seq": null
                    },
                    "fingerprint": "unsupported"
                }
            },
            "comparison_fingerprint": "unsupported"
        },
        "required_files": [
            MANIFEST_FILE,
            ENTRY_DOT_FILE,
            TRACE_FILE,
            STATE_DIFF_FILE,
            DETERMINISM_REPORT_FILE,
            CAPABILITY_REPORT_FILE
        ],
        "sections": {
            "capability_report": {
                "path": CAPABILITY_REPORT_FILE,
                "status": "unavailable"
            },
            "determinism_report": {
                "path": DETERMINISM_REPORT_FILE,
                "status": "unavailable"
            },
            "state_diff": {
                "path": STATE_DIFF_FILE,
                "status": "unavailable"
            },
            "trace": {
                "path": TRACE_FILE,
                "status": "unavailable"
            }
        },
        "inputs": {
            "entry_dot": {
                "path": ENTRY_DOT_FILE,
                "status": "ok",
                "bytes": entry_dot.len()
            }
        }
    });
    let mut manifest_bytes = serde_json::to_vec_pretty(&manifest).expect("manifest must serialize");
    manifest_bytes.push(b'\n');
    write_fixture_file(&bundle_dir, MANIFEST_FILE, &manifest_bytes);

    let mut db = DotDb::open_in(temp.path()).expect("db must open");
    let created = db.create_run_with_id(run_id).expect("run must create");
    db.finalize_run(created.row_id(), RunStatus::Failed)
        .expect("run must finalize");
    db.set_artifact_bundle(
        run_id,
        &format!(".dotlanth/bundles/{run_id}"),
        "fixture-manifest-sha256",
        manifest_bytes.len() as u64,
    )
    .expect("bundle index must store");

    let manifest_path = workspace_manifest();
    let manifest_path = manifest_path.display().to_string();
    let output = Command::new("cargo")
        .current_dir(temp.path())
        .args([
            "run",
            "--quiet",
            "--manifest-path",
            &manifest_path,
            "-p",
            "dot",
            "--",
            "inspect",
            run_id,
        ])
        .output()
        .expect("inspect command must run");

    assert!(
        output.status.success(),
        "inspect should succeed on unavailable artifacts\nstdout:\n{}\nstderr:\n{}",
        String::from_utf8_lossy(&output.stdout),
        String::from_utf8_lossy(&output.stderr)
    );

    let stdout = String::from_utf8(output.stdout).expect("stdout must be utf-8");
    assert_eq!(
        stdout,
        format!(
            concat!(
                "run_id: {}\n",
                "status: failed\n",
                "determinism: default\n",
                "validator: unsupported (execution_not_started)\n",
                "schema_version: 1\n",
                "artifacts:\n",
                "  manifest.json: present ({} bytes)\n",
                "  inputs/entry.dot: present ({} bytes)\n",
                "  trace.jsonl: unavailable\n",
                "  state_diff.json: unavailable\n",
                "  determinism_report.json: unavailable\n",
                "  capability_report.json: unavailable\n",
                "determinism_summary: unavailable\n",
                "capabilities: unavailable\n",
                "trace: events=unavailable\n",
                "state_diff: unavailable\n"
            ),
            run_id,
            manifest_bytes.len(),
            entry_dot.len(),
        )
    );
}

fn dot_bin() -> &'static str {
    "cargo"
}

fn workspace_manifest() -> std::path::PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR"))
        .join("..")
        .join("..")
        .join("Cargo.toml")
}

fn write_file(root: &Path, relative: &str, contents: &[u8]) {
    let path = root
        .join(".dotlanth")
        .join("bundles")
        .join("run_inspect_fixture")
        .join(relative);
    if let Some(parent) = path.parent() {
        fs::create_dir_all(parent).expect("parent dir must create");
    }
    fs::write(path, contents).expect("fixture file must write");
}

fn write_fixture_file(bundle_dir: &Path, relative: &str, contents: &[u8]) {
    let path = bundle_dir.join(relative);
    if let Some(parent) = path.parent() {
        fs::create_dir_all(parent).expect("parent dir must create");
    }
    fs::write(path, contents).expect("fixture file must write");
}
