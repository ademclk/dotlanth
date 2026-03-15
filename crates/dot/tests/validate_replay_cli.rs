use dot_artifacts::{
    CAPABILITY_REPORT_FILE, DETERMINISM_REPORT_FILE, ENTRY_DOT_FILE, MANIFEST_FILE,
    STATE_DIFF_FILE, TRACE_FILE,
};
use dot_db::{DotDb, RunStatus};
use serde_json::{Value, json};
use std::fs;
use std::net::TcpListener;
use std::path::{Path, PathBuf};
use std::process::Command;
use tempfile::TempDir;

#[test]
fn validate_replay_reports_valid_for_matching_replay_proof() {
    let temp = TempDir::new().expect("temp dir must create");
    fs::write(temp.path().join("app.dot"), sample_entry_dot()).expect("dot file must write");

    let run_id = run_fixture(temp.path(), &["run", "--max-requests", "0"]);
    let output = dot_command(temp.path(), &["validate-replay", &run_id]);

    assert!(
        output.status.success(),
        "validate-replay should succeed for a matching replay\nstdout:\n{}\nstderr:\n{}",
        String::from_utf8_lossy(&output.stdout),
        String::from_utf8_lossy(&output.stderr)
    );

    let stdout = String::from_utf8(output.stdout).expect("stdout must be utf-8");
    assert!(stdout.contains(&format!("run_id: {run_id}\n")));
    assert!(stdout.contains("replay_run_id: "));
    assert!(stdout.contains("validation: valid\n"));
    assert!(stdout.lines().all(|line| !line.starts_with("reason: ")));
}

#[test]
fn validate_replay_reports_replay_mismatch_for_changed_trace_surface() {
    let temp = TempDir::new().expect("temp dir must create");
    fs::write(temp.path().join("app.dot"), sample_entry_dot()).expect("dot file must write");

    let run_id = run_fixture(temp.path(), &["run", "--max-requests", "0"]);
    mutate_manifest_replay_proof(temp.path(), &run_id, |manifest| {
        manifest["replay_proof"]["canonical_surface"]["trace"]["fingerprint"] =
            json!("bogus-trace");
        manifest["replay_proof"]["comparison_fingerprint"] = json!("bogus-proof");
    });

    let output = dot_command(temp.path(), &["validate-replay", &run_id]);
    assert_eq!(
        output.status.code(),
        Some(2),
        "replay mismatches should use the stable invalid exit code\nstdout:\n{}\nstderr:\n{}",
        String::from_utf8_lossy(&output.stdout),
        String::from_utf8_lossy(&output.stderr)
    );

    let stdout = String::from_utf8(output.stdout).expect("stdout must be utf-8");
    assert!(stdout.contains(&format!("run_id: {run_id}\n")));
    assert!(stdout.contains("replay_run_id: "));
    assert!(stdout.contains("validation: invalid\n"));
    assert!(stdout.contains("reason: replay_mismatch\n"));
    assert!(stdout.contains("detail: trace fingerprint changed\n"));
}

#[test]
fn validate_replay_reports_determinism_denial_separately_from_mismatch() {
    let temp = TempDir::new().expect("temp dir must create");
    fs::write(temp.path().join("app.dot"), sample_entry_dot()).expect("dot file must write");

    let run_id = run_fixture(temp.path(), &["run", "--max-requests", "0"]);
    mutate_manifest_replay_proof(temp.path(), &run_id, |manifest| {
        manifest["replay_proof"]["canonical_surface"]["determinism"]["budget"]["denied_total"] =
            json!(1);
        manifest["replay_proof"]["canonical_surface"]["determinism"]["violations"]["count"] =
            json!(1);
        manifest["replay_proof"]["canonical_surface"]["determinism"]["fingerprint"] =
            json!("bogus-determinism");
        manifest["replay_proof"]["comparison_fingerprint"] = json!("bogus-proof");
    });

    let output = dot_command(temp.path(), &["validate-replay", &run_id]);
    assert_eq!(
        output.status.code(),
        Some(2),
        "determinism denials should use the stable invalid exit code\nstdout:\n{}\nstderr:\n{}",
        String::from_utf8_lossy(&output.stdout),
        String::from_utf8_lossy(&output.stderr)
    );

    let stdout = String::from_utf8(output.stdout).expect("stdout must be utf-8");
    assert!(stdout.contains("validation: invalid\n"));
    assert!(stdout.contains("reason: determinism_denial\n"));
    assert!(stdout.contains("detail: determinism denied count changed"));
}

#[test]
fn validate_replay_reports_unsupported_without_replaying_when_original_run_is_ineligible() {
    let temp = TempDir::new().expect("temp dir must create");
    create_unsupported_bundle_fixture(temp.path(), "run_validate_unsupported");

    let output = dot_command(
        temp.path(),
        &["validate-replay", "run_validate_unsupported"],
    );

    assert!(
        output.status.success(),
        "unsupported validations should not fail the command\nstdout:\n{}\nstderr:\n{}",
        String::from_utf8_lossy(&output.stdout),
        String::from_utf8_lossy(&output.stderr)
    );

    let stdout = String::from_utf8(output.stdout).expect("stdout must be utf-8");
    assert_eq!(
        stdout,
        concat!(
            "run_id: run_validate_unsupported\n",
            "validation: unsupported\n",
            "reason: execution_not_started\n"
        )
    );

    let bundle_dirs = bundle_dirs(temp.path());
    assert_eq!(
        bundle_dirs.len(),
        1,
        "unsupported validation should not create a replay bundle"
    );
}

fn dot_command(cwd: &Path, args: &[&str]) -> std::process::Output {
    let manifest_path = workspace_manifest();
    let manifest_path = manifest_path.display().to_string();
    let mut command_args = vec![
        "run".to_owned(),
        "--quiet".to_owned(),
        "--manifest-path".to_owned(),
        manifest_path,
        "-p".to_owned(),
        "dot".to_owned(),
        "--".to_owned(),
    ];
    command_args.extend(args.iter().map(|value| (*value).to_owned()));

    Command::new("cargo")
        .current_dir(cwd)
        .args(command_args)
        .output()
        .expect("dot command must run")
}

fn run_fixture(cwd: &Path, args: &[&str]) -> String {
    let output = dot_command(cwd, args);
    assert!(
        output.status.success(),
        "fixture run should succeed\nstdout:\n{}\nstderr:\n{}",
        String::from_utf8_lossy(&output.stdout),
        String::from_utf8_lossy(&output.stderr)
    );

    let bundle_dir = single_bundle_dir(cwd);
    let manifest = read_json(&bundle_dir.join(MANIFEST_FILE));
    manifest["run_id"]
        .as_str()
        .expect("run id must exist")
        .to_owned()
}

fn mutate_manifest_replay_proof(root: &Path, run_id: &str, mutate: impl FnOnce(&mut Value)) {
    let bundle_dir = root.join(".dotlanth").join("bundles").join(run_id);
    let manifest_path = bundle_dir.join(MANIFEST_FILE);
    let mut manifest = read_json(&manifest_path);
    mutate(&mut manifest);
    let mut bytes = serde_json::to_vec_pretty(&manifest).expect("manifest must serialize");
    bytes.push(b'\n');
    fs::write(manifest_path, bytes).expect("mutated manifest must write");
}

fn create_unsupported_bundle_fixture(root: &Path, run_id: &str) {
    let bundle_dir = root.join(".dotlanth").join("bundles").join(run_id);
    let entry_dot = br#"dot 0.1
app "fixture"
end
"#;
    write_file(&bundle_dir, ENTRY_DOT_FILE, entry_dot);

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
            "trace": {
                "path": TRACE_FILE,
                "status": "unavailable",
                "error": {
                    "code": "execution_not_started",
                    "message": "execution did not start"
                }
            },
            "state_diff": {
                "path": STATE_DIFF_FILE,
                "status": "unavailable",
                "error": {
                    "code": "execution_not_started",
                    "message": "execution did not start"
                }
            },
            "determinism_report": {
                "path": DETERMINISM_REPORT_FILE,
                "status": "unavailable",
                "error": {
                    "code": "execution_not_started",
                    "message": "execution did not start"
                }
            },
            "capability_report": {
                "path": CAPABILITY_REPORT_FILE,
                "status": "unavailable",
                "error": {
                    "code": "execution_not_started",
                    "message": "execution did not start"
                }
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
    write_file(&bundle_dir, MANIFEST_FILE, &manifest_bytes);

    let mut db = DotDb::open_in(root).expect("db must open");
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
}

fn single_bundle_dir(root: &Path) -> PathBuf {
    let bundle_dirs = bundle_dirs(root);
    assert_eq!(
        bundle_dirs.len(),
        1,
        "expected exactly one bundle directory"
    );
    bundle_dirs
        .into_iter()
        .next()
        .expect("bundle dir must exist")
}

fn bundle_dirs(root: &Path) -> Vec<PathBuf> {
    let mut bundle_dirs = fs::read_dir(root.join(".dotlanth").join("bundles"))
        .expect("bundles dir must exist")
        .map(|entry| entry.expect("bundle dir entry must read").path())
        .collect::<Vec<_>>();
    bundle_dirs.sort();
    bundle_dirs
}

fn read_json(path: &Path) -> Value {
    serde_json::from_slice(&fs::read(path).expect("json file must read"))
        .expect("json file must parse")
}

fn sample_entry_dot() -> String {
    let listener = TcpListener::bind("127.0.0.1:0").expect("listener must bind");
    let port = listener
        .local_addr()
        .expect("listener addr must read")
        .port();
    drop(listener);

    format!(
        r#"dot 0.1
app "validate-fixture"
allow net.http.listen

server listen {port}

api "public"
  route GET "/hello"
    respond 200 "Hello from Validate"
  end
end
"#
    )
}

fn workspace_manifest() -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR"))
        .join("..")
        .join("..")
        .join("Cargo.toml")
}

fn write_file(bundle_dir: &Path, relative: &str, contents: &[u8]) {
    let path = bundle_dir.join(relative);
    if let Some(parent) = path.parent() {
        fs::create_dir_all(parent).expect("parent dir must create");
    }
    fs::write(path, contents).expect("fixture file must write");
}
