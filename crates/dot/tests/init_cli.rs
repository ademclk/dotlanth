use dot_artifacts::MANIFEST_FILE;
use serde_json::Value;
use std::fs;
use std::path::{Path, PathBuf};
use std::process::Command;
use tempfile::TempDir;

#[test]
fn init_scaffolds_a_project_that_can_complete_strict_dry_run_validation() {
    let temp = TempDir::new().expect("temp dir must create");
    let project_dir = temp.path().join("hello-api");
    let project_dir_str = project_dir.display().to_string();

    let init_output = dot_command(temp.path(), &["init", &project_dir_str]);
    assert!(
        init_output.status.success(),
        "init command should succeed\nstdout:\n{}\nstderr:\n{}",
        String::from_utf8_lossy(&init_output.stdout),
        String::from_utf8_lossy(&init_output.stderr)
    );
    assert!(project_dir.join("app.dot").is_file());
    assert!(project_dir.join(".gitignore").is_file());

    let run_output = dot_command(
        &project_dir,
        &["run", "--determinism", "strict", "--max-requests", "0"],
    );
    assert!(
        run_output.status.success(),
        "strict dry-run should succeed for the scaffolded project\nstdout:\n{}\nstderr:\n{}",
        String::from_utf8_lossy(&run_output.stdout),
        String::from_utf8_lossy(&run_output.stderr)
    );

    let run_stdout = String::from_utf8(run_output.stdout).expect("stdout must be utf-8");
    assert!(run_stdout.contains("completed without serving requests"));

    let run_id = read_single_run_id(&project_dir);
    let validate_output = dot_command(&project_dir, &["validate-replay", &run_id]);
    assert!(
        validate_output.status.success(),
        "validate-replay should succeed for the scaffolded project\nstdout:\n{}\nstderr:\n{}",
        String::from_utf8_lossy(&validate_output.stdout),
        String::from_utf8_lossy(&validate_output.stderr)
    );

    let validate_stdout = String::from_utf8(validate_output.stdout).expect("stdout must be utf-8");
    assert!(validate_stdout.contains("validation: valid\n"));
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

fn workspace_manifest() -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR"))
        .join("..")
        .join("..")
        .join("Cargo.toml")
}
