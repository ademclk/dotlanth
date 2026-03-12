#![forbid(unsafe_code)]

use dot_artifacts::{
    CAPABILITY_REPORT_FILE, ENTRY_DOT_FILE, MANIFEST_FILE, STATE_DIFF_FILE, TRACE_FILE,
};
use dot_db::DotDb;
use serde_json::Value as JsonValue;
use std::fs;
use std::path::{Path, PathBuf};

pub(crate) fn run(run_id: &str) -> Result<(), String> {
    let summary = render_summary(run_id)?;
    print!("{summary}");
    Ok(())
}

fn render_summary(run_id: &str) -> Result<String, String> {
    let run_id = parse_run_id(run_id)?;
    let cwd = std::env::current_dir()
        .map_err(|error| format!("failed to read current directory: {error}"))?;
    let mut db = DotDb::open_in(&cwd).map_err(|error| format!("failed to open DotDB: {error}"))?;
    let run = db
        .run_record(&run_id)
        .map_err(|error| format!("failed to read run {run_id}: {error}"))?;
    let bundle = db
        .artifact_bundle(&run_id)
        .map_err(|error| format!("failed to resolve bundle for run {run_id}: {error}"))?;
    let bundle_dir = resolve_bundle_dir(&cwd, &bundle.bundle_ref);

    let manifest_bytes = fs::read(bundle_dir.join(MANIFEST_FILE)).map_err(|error| {
        format!(
            "failed to read `{}`: {error}",
            bundle_dir.join(MANIFEST_FILE).display()
        )
    })?;
    let manifest: JsonValue = serde_json::from_slice(&manifest_bytes)
        .map_err(|error| format!("failed to parse `{MANIFEST_FILE}`: {error}"))?;
    let schema_version = json_string_field(&manifest, &["schema_version"])?;

    let entry_dot_bytes = file_len(bundle_dir.join(ENTRY_DOT_FILE))?;
    let trace_bytes = file_len(bundle_dir.join(TRACE_FILE))?;
    let state_diff_bytes = file_len(bundle_dir.join(STATE_DIFF_FILE))?;
    let capability_report_bytes = file_len(bundle_dir.join(CAPABILITY_REPORT_FILE))?;

    let capability_report: JsonValue = serde_json::from_slice(
        &fs::read(bundle_dir.join(CAPABILITY_REPORT_FILE)).map_err(|error| {
            format!(
                "failed to read `{}`: {error}",
                bundle_dir.join(CAPABILITY_REPORT_FILE).display()
            )
        })?,
    )
    .map_err(|error| format!("failed to parse `{CAPABILITY_REPORT_FILE}`: {error}"))?;
    let declared = json_array_len(&capability_report, &["declared"])?;
    let used = json_array_len(&capability_report, &["used"])?;
    let denied = json_array_len(&capability_report, &["denied"])?;

    let trace_events = count_jsonl_events(&bundle_dir.join(TRACE_FILE))?;

    let state_diff: JsonValue = serde_json::from_slice(
        &fs::read(bundle_dir.join(STATE_DIFF_FILE)).map_err(|error| {
            format!(
                "failed to read `{}`: {error}",
                bundle_dir.join(STATE_DIFF_FILE).display()
            )
        })?,
    )
    .map_err(|error| format!("failed to parse `{STATE_DIFF_FILE}`: {error}"))?;
    let changes = state_diff_changes(&state_diff)?;

    Ok(format!(
        concat!(
            "run_id: {}\n",
            "status: {}\n",
            "schema_version: {}\n",
            "artifacts:\n",
            "  manifest.json: present ({} bytes)\n",
            "  inputs/entry.dot: present ({} bytes)\n",
            "  trace.jsonl: present ({} bytes)\n",
            "  state_diff.json: present ({} bytes)\n",
            "  capability_report.json: present ({} bytes)\n",
            "capabilities: declared={} used={} denied={}\n",
            "trace: events={}\n",
            "state_diff: changes={} added={} updated={} removed={}\n"
        ),
        run.run_id,
        run.status,
        schema_version,
        manifest_bytes.len(),
        entry_dot_bytes,
        trace_bytes,
        state_diff_bytes,
        capability_report_bytes,
        declared,
        used,
        denied,
        trace_events,
        changes.total,
        changes.added,
        changes.updated,
        changes.removed,
    ))
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

fn file_len(path: PathBuf) -> Result<u64, String> {
    fs::metadata(&path)
        .map(|metadata| metadata.len())
        .map_err(|error| format!("failed to read `{}` metadata: {error}", path.display()))
}

fn json_string_field(value: &JsonValue, path: &[&str]) -> Result<String, String> {
    json_lookup(value, path)?
        .as_str()
        .map(str::to_owned)
        .ok_or_else(|| format!("expected `{}` to be a string", path.join(".")))
}

fn json_array_len(value: &JsonValue, path: &[&str]) -> Result<usize, String> {
    Ok(json_lookup(value, path)?
        .as_array()
        .ok_or_else(|| format!("expected `{}` to be an array", path.join(".")))?
        .len())
}

fn json_lookup<'a>(value: &'a JsonValue, path: &[&str]) -> Result<&'a JsonValue, String> {
    let mut current = value;
    for key in path {
        current = current
            .get(*key)
            .ok_or_else(|| format!("missing required field `{}`", path.join(".")))?;
    }
    Ok(current)
}

fn count_jsonl_events(path: &Path) -> Result<usize, String> {
    let raw = fs::read_to_string(path)
        .map_err(|error| format!("failed to read `{}`: {error}", path.display()))?;
    Ok(raw.lines().filter(|line| !line.trim().is_empty()).count())
}

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
struct StateDiffCounts {
    total: usize,
    added: usize,
    updated: usize,
    removed: usize,
}

fn state_diff_changes(value: &JsonValue) -> Result<StateDiffCounts, String> {
    let mut counts = StateDiffCounts::default();
    for change in json_lookup(value, &["changes"])?
        .as_array()
        .ok_or_else(|| "expected `changes` to be an array".to_owned())?
    {
        let kind = change
            .get("change")
            .and_then(JsonValue::as_str)
            .ok_or_else(|| {
                "expected each state diff change to include a string `change`".to_owned()
            })?;
        counts.total += 1;
        match kind {
            "added" => counts.added += 1,
            "updated" => counts.updated += 1,
            "removed" => counts.removed += 1,
            _ => {}
        }
    }
    Ok(counts)
}

#[cfg(test)]
mod tests {
    use super::{parse_run_id, state_diff_changes};
    use serde_json::json;

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
    fn state_diff_changes_counts_each_change_kind() {
        let counts = state_diff_changes(&json!({
            "changes": [
                { "change": "added" },
                { "change": "updated" },
                { "change": "removed" },
                { "change": "updated" }
            ]
        }))
        .expect("counts should build");

        assert_eq!(counts.total, 4);
        assert_eq!(counts.added, 1);
        assert_eq!(counts.updated, 2);
        assert_eq!(counts.removed, 1);
    }
}
