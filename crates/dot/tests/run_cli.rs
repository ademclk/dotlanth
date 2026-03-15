use dot_artifacts::MANIFEST_FILE;
use serde_json::Value;
use std::fs;
use std::net::TcpListener;
use std::path::{Path, PathBuf};
use std::process::Command;
use tempfile::TempDir;

#[test]
fn run_announcement_includes_selected_determinism_mode() {
    let temp = TempDir::new().expect("temp dir must create");
    fs::write(temp.path().join("app.dot"), sample_entry_dot()).expect("dot file must write");

    let output = dot_command(temp.path(), &["run", "--max-requests", "0"]);
    assert!(
        output.status.success(),
        "run command should succeed\nstdout:\n{}\nstderr:\n{}",
        String::from_utf8_lossy(&output.stdout),
        String::from_utf8_lossy(&output.stderr)
    );

    let stdout = String::from_utf8(output.stdout).expect("stdout must be utf-8");
    let run_id = read_single_run_id(temp.path());
    assert!(
        stdout.contains(&format!("run {run_id} listening on http://")),
        "stdout should still announce the listening address\nstdout:\n{stdout}"
    );
    assert!(
        stdout.contains("(determinism: default)"),
        "stdout should include the selected determinism mode\nstdout:\n{stdout}"
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

fn sample_entry_dot() -> String {
    let listener = TcpListener::bind("127.0.0.1:0").expect("listener must bind");
    let port = listener
        .local_addr()
        .expect("listener addr must read")
        .port();
    drop(listener);

    format!(
        r#"dot 0.1
app "run-output-fixture"
allow net.http.listen

server listen {port}

api "public"
  route GET "/hello"
    respond 200 "Hello from Run"
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
