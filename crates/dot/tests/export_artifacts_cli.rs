use dot_artifacts::{
    CAPABILITY_REPORT_FILE, ENTRY_DOT_FILE, MANIFEST_FILE, STATE_DIFF_FILE, TRACE_FILE,
};
use dot_db::{DotDb, RunStatus};
use serde_json::json;
use std::fs;
use std::path::{Path, PathBuf};
use std::process::Command;
use tempfile::TempDir;

#[test]
fn export_artifacts_materializes_bundle_into_requested_directory() {
    let temp = TempDir::new().expect("temp dir must create");
    let fixture = create_bundle_fixture(temp.path(), "run_export_fixture");
    let export_dir = temp.path().join("exports").join("bundle-copy");

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
            "export-artifacts",
            &fixture.run_id,
            "--out",
            &export_dir.display().to_string(),
        ])
        .output()
        .expect("export command must run");

    assert!(
        output.status.success(),
        "export should succeed\nstdout:\n{}\nstderr:\n{}",
        String::from_utf8_lossy(&output.stdout),
        String::from_utf8_lossy(&output.stderr)
    );

    for relative in [
        MANIFEST_FILE,
        ENTRY_DOT_FILE,
        TRACE_FILE,
        STATE_DIFF_FILE,
        CAPABILITY_REPORT_FILE,
    ] {
        assert_eq!(
            fs::read(export_dir.join(relative)).expect("exported file must read"),
            fs::read(fixture.bundle_dir.join(relative)).expect("source file must read"),
            "exported `{relative}` should match source bundle"
        );
    }
}

#[test]
fn export_artifacts_rejects_non_empty_output_directory() {
    let temp = TempDir::new().expect("temp dir must create");
    let fixture = create_bundle_fixture(temp.path(), "run_export_reject_fixture");
    let export_dir = temp.path().join("exports").join("bundle-copy");
    fs::create_dir_all(&export_dir).expect("export dir must create");
    fs::write(export_dir.join("existing.txt"), "occupied").expect("existing file must write");

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
            "export-artifacts",
            &fixture.run_id,
            "--out",
            &export_dir.display().to_string(),
        ])
        .output()
        .expect("export command must run");

    assert!(
        !output.status.success(),
        "export should fail for a non-empty directory"
    );
    assert!(
        String::from_utf8_lossy(&output.stderr).contains("must be empty"),
        "stderr should explain the output directory safety check\nstderr:\n{}",
        String::from_utf8_lossy(&output.stderr)
    );
}

struct BundleFixture {
    run_id: String,
    bundle_dir: PathBuf,
}

fn create_bundle_fixture(root: &Path, run_id: &str) -> BundleFixture {
    let bundle_dir = root.join(".dotlanth").join("bundles").join(run_id);
    let inputs_dir = bundle_dir.join("inputs");
    fs::create_dir_all(&inputs_dir).expect("bundle inputs dir must create");

    let entry_dot = br#"dot 0.1
app "fixture"
end
"#;
    let trace = concat!(
        "{\"seq\":0,\"event\":\"run.start\"}\n",
        "{\"seq\":1,\"event\":\"run.finish\",\"status\":\"succeeded\"}\n"
    );
    let state_diff = json!({
        "schema_version": "1",
        "scope": "state_kv",
        "changes": []
    });
    let capability_report = json!({
        "schema_version": "1",
        "declared": [],
        "used": [],
        "denied": []
    });

    write_file(&bundle_dir, ENTRY_DOT_FILE, entry_dot);
    write_file(&bundle_dir, TRACE_FILE, trace.as_bytes());
    let state_diff_bytes =
        serde_json::to_vec_pretty(&state_diff).expect("state diff must serialize");
    write_file(&bundle_dir, STATE_DIFF_FILE, &state_diff_bytes);
    let capability_report_bytes =
        serde_json::to_vec_pretty(&capability_report).expect("report must serialize");
    write_file(
        &bundle_dir,
        CAPABILITY_REPORT_FILE,
        &capability_report_bytes,
    );

    let manifest = json!({
        "schema_version": "1",
        "run_id": run_id,
        "created_at_ms": 1,
        "required_files": [
            MANIFEST_FILE,
            ENTRY_DOT_FILE,
            TRACE_FILE,
            STATE_DIFF_FILE,
            CAPABILITY_REPORT_FILE
        ],
        "sections": {
            "capability_report": {
                "path": CAPABILITY_REPORT_FILE,
                "status": "ok",
                "bytes": capability_report_bytes.len()
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
    write_file(&bundle_dir, MANIFEST_FILE, &manifest_bytes);

    let mut db = DotDb::open_in(root).expect("db must open");
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

    BundleFixture {
        run_id: run_id.to_owned(),
        bundle_dir,
    }
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
