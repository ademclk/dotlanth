#![forbid(unsafe_code)]

use dot_db::DotDb;
use std::fs;
use std::path::{Path, PathBuf};

pub(crate) fn run(run_id: &str, out: &Path) -> Result<(), String> {
    let run_id = parse_run_id(run_id)?;
    let cwd = std::env::current_dir()
        .map_err(|error| format!("failed to read current directory: {error}"))?;
    let mut db = DotDb::open_in(&cwd).map_err(|error| format!("failed to open DotDB: {error}"))?;
    let bundle = db
        .artifact_bundle(&run_id)
        .map_err(|error| format!("failed to resolve bundle for run {run_id}: {error}"))?;
    let bundle_dir = resolve_bundle_dir(&cwd, &bundle.bundle_ref);

    if !bundle_dir.is_dir() {
        return Err(format!(
            "bundle directory does not exist: `{}`",
            bundle_dir.display()
        ));
    }

    prepare_output_dir(out)?;
    copy_dir_recursive(&bundle_dir, out)?;
    println!("exported {run_id} to {}", out.display());
    Ok(())
}

fn parse_run_id(raw: &str) -> Result<String, String> {
    let trimmed = raw.trim();
    if trimmed.is_empty() {
        return Err("run id must not be empty".to_owned());
    }

    Ok(trimmed.to_owned())
}

fn resolve_bundle_dir(project_root: &Path, bundle_ref: &str) -> PathBuf {
    let path = Path::new(bundle_ref);
    if path.is_absolute() {
        path.to_path_buf()
    } else {
        project_root.join(path)
    }
}

fn prepare_output_dir(path: &Path) -> Result<(), String> {
    if path.exists() {
        if !path.is_dir() {
            return Err(format!(
                "export output path must be a directory: `{}`",
                path.display()
            ));
        }

        let mut entries = fs::read_dir(path).map_err(|error| {
            format!(
                "failed to read export directory `{}`: {error}",
                path.display()
            )
        })?;
        if entries.next().is_some() {
            return Err(format!(
                "export output directory must be empty: `{}`",
                path.display()
            ));
        }
        return Ok(());
    }

    fs::create_dir_all(path).map_err(|error| {
        format!(
            "failed to create export directory `{}`: {error}",
            path.display()
        )
    })
}

fn copy_dir_recursive(source: &Path, target: &Path) -> Result<(), String> {
    for entry in fs::read_dir(source).map_err(|error| {
        format!(
            "failed to read bundle directory `{}`: {error}",
            source.display()
        )
    })? {
        let entry = entry.map_err(|error| {
            format!(
                "failed to read entry in bundle directory `{}`: {error}",
                source.display()
            )
        })?;
        let source_path = entry.path();
        let target_path = target.join(entry.file_name());
        let file_type = entry.file_type().map_err(|error| {
            format!(
                "failed to read entry type for `{}`: {error}",
                source_path.display()
            )
        })?;

        if file_type.is_dir() {
            fs::create_dir_all(&target_path).map_err(|error| {
                format!(
                    "failed to create export directory `{}`: {error}",
                    target_path.display()
                )
            })?;
            copy_dir_recursive(&source_path, &target_path)?;
        } else if file_type.is_file() {
            fs::copy(&source_path, &target_path).map_err(|error| {
                format!(
                    "failed to copy `{}` to `{}`: {error}",
                    source_path.display(),
                    target_path.display()
                )
            })?;
        }
    }

    Ok(())
}

#[cfg(test)]
mod tests {
    use super::parse_run_id;

    #[test]
    fn parse_run_id_rejects_blank_values() {
        assert!(parse_run_id("").is_err());
        assert!(parse_run_id("   ").is_err());
    }

    #[test]
    fn parse_run_id_accepts_external_string() {
        let id = parse_run_id("run_123").expect("run id should parse");
        assert_eq!(id, "run_123");
    }
}
