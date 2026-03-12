#![forbid(unsafe_code)]

use crate::commands::support::open_existing_dotdb_in;
use std::fmt::Write as _;
use std::path::Path;

pub(crate) fn run(run_id: &str) -> Result<(), String> {
    let cwd = std::env::current_dir()
        .map_err(|error| format!("failed to read current directory: {error}"))?;
    let rendered = render_logs_in(&cwd, run_id)?;
    print!("{rendered}");
    Ok(())
}

pub(crate) fn render_logs_in(project_root: &Path, run_id: &str) -> Result<String, String> {
    let run_id = parse_run_id(run_id)?;
    let mut db = open_existing_dotdb_in(project_root)?;
    let logs = db
        .run_logs(&run_id)
        .map_err(|error| format!("failed to read logs for run {run_id}: {error}"))?;

    let mut rendered = String::new();
    for entry in logs {
        let _ = writeln!(rendered, "{} INFO {}", entry.created_at_ms, entry.line);
    }
    Ok(rendered)
}

fn parse_run_id(raw: &str) -> Result<String, String> {
    let trimmed = raw.trim();
    if trimmed.is_empty() {
        return Err("run id must not be empty".to_owned());
    }

    Ok(trimmed.to_owned())
}

#[cfg(test)]
mod tests {
    use super::{parse_run_id, render_logs_in};
    use dot_db::DotDb;
    use tempfile::TempDir;

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

    #[test]
    fn render_logs_in_formats_stable_lines() {
        let temp = TempDir::new().expect("temp dir must create");
        let mut db = DotDb::open_in(temp.path()).expect("db must open");
        let created = db.create_run_with_id("run_logs").expect("run must create");
        db.append_run_logs(created.row_id(), ["first", "second"])
            .expect("logs must append");

        let rendered = render_logs_in(temp.path(), "run_logs").expect("logs must render");
        assert!(rendered.contains("INFO first"));
        assert!(rendered.contains("INFO second"));
    }
}
