use dot_artifacts::MANIFEST_FILE;
use serde_json::Value;
use std::fs;
use std::net::TcpListener;
use std::path::{Path, PathBuf};
use std::process::Command;
use tempfile::TempDir;

#[test]
fn strict_zero_request_run_can_be_inspected_and_validated() {
    let temp = TempDir::new().expect("temp dir must create");
    fs::write(temp.path().join("app.dot"), sample_entry_dot()).expect("dot file must write");

    let run_output = dot_command(
        temp.path(),
        &["run", "--determinism", "strict", "--max-requests", "0"],
    );
    assert!(
        run_output.status.success(),
        "strict zero-request run should succeed\nstdout:\n{}\nstderr:\n{}",
        String::from_utf8_lossy(&run_output.stdout),
        String::from_utf8_lossy(&run_output.stderr)
    );

    let run_stdout = String::from_utf8(run_output.stdout).expect("stdout must be utf-8");
    assert!(
        run_stdout.contains("completed without serving requests"),
        "strict zero-request run should explain that it skipped serving\nstdout:\n{run_stdout}"
    );
    assert!(
        run_stdout.contains("(determinism: strict)"),
        "strict zero-request run should still print the selected determinism mode\nstdout:\n{run_stdout}"
    );

    let run_id = read_single_run_id(temp.path());

    let inspect_output = dot_command(temp.path(), &["inspect", &run_id]);
    assert!(
        inspect_output.status.success(),
        "inspect should succeed for the strict dry-run bundle\nstdout:\n{}\nstderr:\n{}",
        String::from_utf8_lossy(&inspect_output.stdout),
        String::from_utf8_lossy(&inspect_output.stderr)
    );

    let inspect_stdout = String::from_utf8(inspect_output.stdout).expect("stdout must be utf-8");
    assert!(inspect_stdout.contains(&format!("run_id: {run_id}\n")));
    assert!(inspect_stdout.contains("determinism: strict\n"));
    assert!(inspect_stdout.contains("validator: eligible\n"));
    assert!(inspect_stdout.contains("determinism_summary: clean\n"));

    let validate_output = dot_command(temp.path(), &["validate-replay", &run_id]);
    assert!(
        validate_output.status.success(),
        "validate-replay should succeed for the strict dry-run bundle\nstdout:\n{}\nstderr:\n{}",
        String::from_utf8_lossy(&validate_output.stdout),
        String::from_utf8_lossy(&validate_output.stderr)
    );

    let validate_stdout = String::from_utf8(validate_output.stdout).expect("stdout must be utf-8");
    assert!(validate_stdout.contains(&format!("run_id: {run_id}\n")));
    assert!(validate_stdout.contains("replay_run_id: "));
    assert!(validate_stdout.contains("validation: valid\n"));
}

#[test]
fn strict_rejection_still_persists_bundle_for_inspection() {
    let temp = TempDir::new().expect("temp dir must create");
    fs::write(temp.path().join("app.dot"), sample_entry_dot()).expect("dot file must write");

    let run_output = dot_command(
        temp.path(),
        &["run", "--determinism", "strict", "--max-requests", "1"],
    );
    assert_eq!(
        run_output.status.code(),
        Some(1),
        "strict http serving should still be rejected\nstdout:\n{}\nstderr:\n{}",
        String::from_utf8_lossy(&run_output.stdout),
        String::from_utf8_lossy(&run_output.stderr)
    );

    let stderr = String::from_utf8(run_output.stderr).expect("stderr must be utf-8");
    assert!(
        stderr.contains(
            "determinism violation: strict mode does not support syscall `net.http.serve`"
        )
    );

    let run_id = read_single_run_id(temp.path());

    let inspect_output = dot_command(temp.path(), &["inspect", &run_id]);
    assert!(
        inspect_output.status.success(),
        "inspect should succeed for the strict rejection bundle\nstdout:\n{}\nstderr:\n{}",
        String::from_utf8_lossy(&inspect_output.stdout),
        String::from_utf8_lossy(&inspect_output.stderr)
    );

    let inspect_stdout = String::from_utf8(inspect_output.stdout).expect("stdout must be utf-8");
    assert!(inspect_stdout.contains(&format!("run_id: {run_id}\n")));
    assert!(inspect_stdout.contains("status: failed\n"));
    assert!(inspect_stdout.contains("determinism: strict\n"));
    assert!(inspect_stdout.contains("validator: eligible\n"));
    assert!(inspect_stdout.contains("determinism_summary: denied count=1"));
}

#[test]
fn strict_zero_request_run_succeeds_even_when_listener_port_is_busy() {
    let temp = TempDir::new().expect("temp dir must create");
    fs::write(temp.path().join("app.dot"), sample_entry_dot()).expect("dot file must write");

    let reserved =
        TcpListener::bind(("127.0.0.1", 18080)).expect("test should reserve the fixture port");

    let run_output = dot_command(
        temp.path(),
        &["run", "--determinism", "strict", "--max-requests", "0"],
    );
    drop(reserved);

    assert!(
        run_output.status.success(),
        "strict zero-request run should not try to bind the listener\nstdout:\n{}\nstderr:\n{}",
        String::from_utf8_lossy(&run_output.stdout),
        String::from_utf8_lossy(&run_output.stderr)
    );

    let run_stdout = String::from_utf8(run_output.stdout).expect("stdout must be utf-8");
    assert!(run_stdout.contains("completed without serving requests"));
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
app "strict-demo"
project "dotlanth"

allow log
allow net.http.listen

server listen 18080

api "public"
  route GET "/hello"
    respond 200 "Hello from Dotlanth"
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
