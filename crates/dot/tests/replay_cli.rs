use dot_artifacts::{
    CAPABILITY_REPORT_FILE, ENTRY_DOT_FILE, MANIFEST_FILE, STATE_DIFF_FILE, TRACE_FILE,
};
use dot_db::{DotDb, RunStatus};
use serde_json::{Value, json};
use std::fs;
use std::net::TcpListener;
use std::path::{Path, PathBuf};
use std::process::Command;
use tempfile::TempDir;

#[test]
fn replay_run_id_replays_entry_snapshot_into_a_new_bundle() {
    let temp = TempDir::new().expect("temp dir must create");
    let original_run_id = "run_replay_fixture";
    create_bundle_fixture(temp.path(), original_run_id, sample_entry_dot());

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
            "replay",
            original_run_id,
        ])
        .output()
        .expect("replay command must run");

    assert!(
        output.status.success(),
        "replay should succeed\nstdout:\n{}\nstderr:\n{}",
        String::from_utf8_lossy(&output.stdout),
        String::from_utf8_lossy(&output.stderr)
    );

    let bundles_dir = temp.path().join(".dotlanth").join("bundles");
    let bundle_names = fs::read_dir(&bundles_dir)
        .expect("bundles dir must exist")
        .map(|entry| entry.expect("bundle dir entry must read").file_name())
        .collect::<Vec<_>>();
    assert_eq!(
        bundle_names.len(),
        2,
        "replay should create a second bundle"
    );
    assert!(
        bundle_names
            .iter()
            .filter_map(|name| name.to_str())
            .any(|name| name != original_run_id),
        "replay should create a new bundle name"
    );
}

#[test]
fn replay_bundle_path_replays_without_existing_dotdb_state() {
    let temp = TempDir::new().expect("temp dir must create");
    let exported_bundle_dir = temp.path().join("exported").join("bundle-copy");
    create_exported_bundle(
        &exported_bundle_dir,
        "run_exported_fixture",
        sample_entry_dot(),
    );

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
            "replay",
            "--bundle",
            &exported_bundle_dir.display().to_string(),
        ])
        .output()
        .expect("replay command must run");

    assert!(
        output.status.success(),
        "replay --bundle should succeed\nstdout:\n{}\nstderr:\n{}",
        String::from_utf8_lossy(&output.stdout),
        String::from_utf8_lossy(&output.stderr)
    );

    let bundles_dir = temp.path().join(".dotlanth").join("bundles");
    let bundle_names = fs::read_dir(&bundles_dir)
        .expect("bundles dir must exist")
        .map(|entry| entry.expect("bundle dir entry must read").file_name())
        .collect::<Vec<_>>();
    assert_eq!(
        bundle_names.len(),
        1,
        "replay should create exactly one new bundle"
    );
}

#[test]
fn replayed_run_matches_original_replay_proof_fingerprint() {
    let temp = TempDir::new().expect("temp dir must create");
    fs::write(temp.path().join("app.dot"), sample_entry_dot()).expect("dot file must write");

    let manifest_path = workspace_manifest();
    let manifest_path = manifest_path.display().to_string();
    let original_run = Command::new("cargo")
        .current_dir(temp.path())
        .args([
            "run",
            "--quiet",
            "--manifest-path",
            &manifest_path,
            "-p",
            "dot",
            "--",
            "run",
            "--max-requests",
            "0",
        ])
        .output()
        .expect("original run command must run");

    assert!(
        original_run.status.success(),
        "original run should succeed\nstdout:\n{}\nstderr:\n{}",
        String::from_utf8_lossy(&original_run.stdout),
        String::from_utf8_lossy(&original_run.stderr)
    );

    let original_bundle_dir = single_bundle_dir(temp.path());
    let original_manifest = read_json(&original_bundle_dir.join(MANIFEST_FILE));
    let original_run_id = original_manifest["run_id"]
        .as_str()
        .expect("original run id must exist")
        .to_owned();
    let original_proof = original_manifest["replay_proof"]["comparison_fingerprint"]
        .as_str()
        .expect("original replay proof fingerprint must exist")
        .to_owned();

    let replay_output = Command::new("cargo")
        .current_dir(temp.path())
        .args([
            "run",
            "--quiet",
            "--manifest-path",
            &manifest_path,
            "-p",
            "dot",
            "--",
            "replay",
            &original_run_id,
        ])
        .output()
        .expect("replay command must run");

    assert!(
        replay_output.status.success(),
        "replay command should succeed\nstdout:\n{}\nstderr:\n{}",
        String::from_utf8_lossy(&replay_output.stdout),
        String::from_utf8_lossy(&replay_output.stderr)
    );

    let replay_bundle_dir = other_bundle_dir(temp.path(), &original_run_id);
    let replay_manifest = read_json(&replay_bundle_dir.join(MANIFEST_FILE));
    let replay_proof = replay_manifest["replay_proof"]["comparison_fingerprint"]
        .as_str()
        .expect("replayed run fingerprint must exist");

    assert_eq!(
        replay_manifest["determinism_eligibility"]["status"],
        "eligible"
    );
    assert_eq!(original_proof, replay_proof);
    assert_eq!(
        original_manifest["replay_proof"]["canonical_surface"]["trace"]["fingerprint"],
        replay_manifest["replay_proof"]["canonical_surface"]["trace"]["fingerprint"]
    );
    assert_eq!(
        original_manifest["replay_proof"]["canonical_surface"]["state_diff"]["fingerprint"],
        replay_manifest["replay_proof"]["canonical_surface"]["state_diff"]["fingerprint"]
    );
    assert_eq!(
        original_manifest["replay_proof"]["canonical_surface"]["capability_report"]["fingerprint"],
        replay_manifest["replay_proof"]["canonical_surface"]["capability_report"]["fingerprint"]
    );
}

fn create_bundle_fixture(root: &Path, run_id: &str, entry_dot: String) {
    let bundle_dir = root.join(".dotlanth").join("bundles").join(run_id);
    create_exported_bundle(&bundle_dir, run_id, entry_dot.clone());

    let manifest_bytes = fs::read(bundle_dir.join(MANIFEST_FILE)).expect("manifest must read");

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
}

fn create_exported_bundle(bundle_dir: &Path, run_id: &str, entry_dot: String) {
    write_file(bundle_dir, ENTRY_DOT_FILE, entry_dot.as_bytes());
    write_file(
        bundle_dir,
        TRACE_FILE,
        concat!(
            "{\"seq\":0,\"event\":\"run.start\"}\n",
            "{\"seq\":1,\"event\":\"run.finish\",\"status\":\"succeeded\"}\n"
        )
        .as_bytes(),
    );
    let state_diff_bytes = serde_json::to_vec_pretty(&json!({
        "schema_version": "1",
        "scope": "state_kv",
        "changes": []
    }))
    .expect("state diff must serialize");
    write_file(bundle_dir, STATE_DIFF_FILE, &state_diff_bytes);
    let capability_report_bytes = serde_json::to_vec_pretty(&json!({
        "schema_version": "1",
        "declared": [],
        "used": [],
        "denied": []
    }))
    .expect("capability report must serialize");
    write_file(bundle_dir, CAPABILITY_REPORT_FILE, &capability_report_bytes);

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
                "bytes": 71
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
    write_file(bundle_dir, MANIFEST_FILE, &manifest_bytes);
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

fn other_bundle_dir(root: &Path, original_run_id: &str) -> PathBuf {
    bundle_dirs(root)
        .into_iter()
        .find(|path| path.file_name().and_then(|name| name.to_str()) != Some(original_run_id))
        .expect("expected replay bundle directory to exist")
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
app "replay-fixture"
allow net.http.listen

server listen {port}

api "public"
  route GET "/hello"
    respond 200 "Hello from Replay"
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
