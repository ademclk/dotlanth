#![forbid(unsafe_code)]

use crate::commands::run;
use crate::commands::support::open_existing_dotdb_in;
use dot_artifacts::ENTRY_DOT_FILE;
use std::path::{Path, PathBuf};

pub(crate) fn run(run_id: Option<&str>, bundle: Option<&Path>) -> Result<(), String> {
    let project_root = std::env::current_dir()
        .map_err(|error| format!("failed to read current directory: {error}"))?;
    let entry_dot = match (run_id, bundle) {
        (Some(run_id), None) => entry_dot_for_run_id(&project_root, run_id)?,
        (None, Some(bundle)) => bundle.join(ENTRY_DOT_FILE),
        (Some(_), Some(_)) => {
            return Err("replay accepts either a run id or `--bundle <path>`, not both".to_owned());
        }
        (None, None) => {
            return Err("replay requires a run id or `--bundle <path>`".to_owned());
        }
    };

    run::run_resolved(entry_dot, project_root, Some(0))
}

fn entry_dot_for_run_id(project_root: &Path, run_id: &str) -> Result<PathBuf, String> {
    let trimmed = run_id.trim();
    if trimmed.is_empty() {
        return Err("run id must not be empty".to_owned());
    }

    let mut db = open_existing_dotdb_in(project_root)?;
    let bundle = db
        .artifact_bundle(trimmed)
        .map_err(|error| format!("failed to resolve bundle for run {trimmed}: {error}"))?;
    Ok(resolve_bundle_dir(project_root, &bundle.bundle_ref).join(ENTRY_DOT_FILE))
}

fn resolve_bundle_dir(project_root: &Path, bundle_ref: &str) -> PathBuf {
    let path = Path::new(bundle_ref);
    if path.is_absolute() {
        path.to_path_buf()
    } else {
        project_root.join(path)
    }
}

#[cfg(test)]
mod tests {
    use super::entry_dot_for_run_id;
    use dot_artifacts::ENTRY_DOT_FILE;
    use dot_db::{DotDb, RunStatus};
    use std::fs;
    use tempfile::TempDir;

    #[test]
    fn entry_dot_for_run_id_rejects_blank_values() {
        let temp = TempDir::new().expect("temp dir must create");
        assert!(entry_dot_for_run_id(temp.path(), "").is_err());
        assert!(entry_dot_for_run_id(temp.path(), "   ").is_err());
    }

    #[test]
    fn entry_dot_for_run_id_resolves_bundle_snapshot_path() {
        let temp = TempDir::new().expect("temp dir must create");
        let bundle_dir = temp
            .path()
            .join(".dotlanth")
            .join("bundles")
            .join("run_123");
        fs::create_dir_all(bundle_dir.join("inputs")).expect("inputs dir must create");
        fs::write(bundle_dir.join(ENTRY_DOT_FILE), "dot 0.1\napp \"x\"\nend\n")
            .expect("entry dot must write");

        let mut db = DotDb::open_in(temp.path()).expect("db must open");
        let created = db.create_run_with_id("run_123").expect("run must create");
        db.finalize_run(created.row_id(), RunStatus::Succeeded)
            .expect("run must finalize");
        db.set_artifact_bundle(
            "run_123",
            ".dotlanth/bundles/run_123",
            "fixture-manifest-sha256",
            1,
        )
        .expect("bundle must index");

        let entry_dot =
            entry_dot_for_run_id(temp.path(), "run_123").expect("entry dot should resolve");
        assert_eq!(entry_dot, bundle_dir.join(ENTRY_DOT_FILE));
    }
}
