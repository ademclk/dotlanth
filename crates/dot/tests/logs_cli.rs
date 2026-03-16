use dot_artifacts::MANIFEST_FILE;
use dot_db::DotDb;
use serde_json::Value;
use std::fs;
use std::path::{Path, PathBuf};
use std::process::Command;
use tempfile::TempDir;

#[test]
fn logs_prints_persisted_runtime_events_for_a_recorded_run() {
    let temp = TempDir::new().expect("temp dir must create");
    fs::write(temp.path().join("app.dot"), sample_entry_dot()).expect("dot file must write");

    let run_output = dot_command(
        temp.path(),
        &["run", "--file", "app.dot", "--max-requests", "0"],
    );
    assert!(
        run_output.status.success(),
        "run command should succeed\nstdout:\n{}\nstderr:\n{}",
        String::from_utf8_lossy(&run_output.stdout),
        String::from_utf8_lossy(&run_output.stderr)
    );

    let run_id = read_single_run_id(temp.path());
    let logs_output = dot_command(temp.path(), &["logs", &run_id]);
    assert!(
        logs_output.status.success(),
        "logs command should succeed\nstdout:\n{}\nstderr:\n{}",
        String::from_utf8_lossy(&logs_output.stdout),
        String::from_utf8_lossy(&logs_output.stderr)
    );

    let stdout = String::from_utf8(logs_output.stdout).expect("stdout must be utf-8");
    assert!(stdout.contains("INFO"));
    assert!(stdout.contains("\"type\":\"http.server_start\""));
    assert!(stdout.contains("\"semantic_path\":\"server\""));
}

#[test]
fn logs_reports_missing_run_in_the_current_project() {
    let temp = TempDir::new().expect("temp dir must create");
    DotDb::open_in(temp.path()).expect("db must create");

    let output = dot_command(temp.path(), &["logs", "run_missing"]);
    assert_eq!(
        output.status.code(),
        Some(1),
        "logs should fail for a missing run\nstdout:\n{}\nstderr:\n{}",
        String::from_utf8_lossy(&output.stdout),
        String::from_utf8_lossy(&output.stderr)
    );

    let stderr = String::from_utf8(output.stderr).expect("stderr must be utf-8");
    assert!(stderr.contains("failed to read logs for run run_missing: run not found: run_missing"));
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

fn read_single_run_id(root: &Path) -> String {
    let bundles_dir = root.join(".dotlanth").join("bundles");
    let mut bundle_dirs = fs::read_dir(&bundles_dir)
        .expect("bundles dir must exist")
        .map(|entry| entry.expect("bundle dir entry must read").path())
        .collect::<Vec<_>>();
    bundle_dirs.sort();
    let manifest = read_json(&bundle_dirs[0].join(MANIFEST_FILE));
    manifest["run_id"]
        .as_str()
        .expect("run id must exist")
        .to_owned()
}

fn read_json(path: &Path) -> Value {
    serde_json::from_slice(&fs::read(path).expect("json file must read"))
        .expect("json file must parse")
}

fn sample_entry_dot() -> &'static str {
    r#"dot 0.1
app "logs-demo"
project "dotlanth"

allow log
allow net.http.listen

server listen 18081

api "public"
  route GET "/hello"
    respond 200 "Hello"
  end
end
"#
}

fn workspace_manifest() -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR"))
        .join("..")
        .join("..")
        .join("Cargo.toml")
}
