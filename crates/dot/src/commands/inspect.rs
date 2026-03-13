#![forbid(unsafe_code)]

use crate::commands::support::open_existing_dotdb_in;
use dot_artifacts::{
    CAPABILITY_REPORT_FILE, ENTRY_DOT_FILE, MANIFEST_FILE, STATE_DIFF_FILE, TRACE_FILE,
};
use serde_json::Value as JsonValue;
use std::fs;
use std::path::{Path, PathBuf};

pub(crate) fn run(run_id: &str) -> Result<(), String> {
    let cwd = std::env::current_dir()
        .map_err(|error| format!("failed to read current directory: {error}"))?;
    let summary = render_summary_in(&cwd, run_id)?;
    print!("{summary}");
    Ok(())
}

pub(crate) fn render_summary_in(project_root: &Path, run_id: &str) -> Result<String, String> {
    let run_id = parse_run_id(run_id)?;
    let mut db = open_existing_dotdb_in(project_root)?;
    let run = db
        .run_record(&run_id)
        .map_err(|error| format!("failed to read run {run_id}: {error}"))?;
    let bundle = db
        .artifact_bundle(&run_id)
        .map_err(|error| format!("failed to resolve bundle for run {run_id}: {error}"))?;
    let bundle_dir = resolve_bundle_dir(project_root, &bundle.bundle_ref);

    let manifest_bytes = fs::read(bundle_dir.join(MANIFEST_FILE)).map_err(|error| {
        format!(
            "failed to read `{}`: {error}",
            bundle_dir.join(MANIFEST_FILE).display()
        )
    })?;
    let manifest: JsonValue = serde_json::from_slice(&manifest_bytes)
        .map_err(|error| format!("failed to parse `{MANIFEST_FILE}`: {error}"))?;
    let schema_version = json_string_field(&manifest, &["schema_version"])?;
    let determinism_mode = manifest
        .get("determinism")
        .and_then(|value| value.get("mode"))
        .and_then(JsonValue::as_str)
        .map(str::to_owned);
    let entry_dot = artifact_summary(
        &bundle_dir,
        Some(json_lookup(&manifest, &["inputs", "entry_dot"])?),
        ENTRY_DOT_FILE,
    );
    let trace = artifact_summary(
        &bundle_dir,
        Some(json_lookup(&manifest, &["sections", "trace"])?),
        TRACE_FILE,
    );
    let state_diff = artifact_summary(
        &bundle_dir,
        Some(json_lookup(&manifest, &["sections", "state_diff"])?),
        STATE_DIFF_FILE,
    );
    let capability_report = artifact_summary(
        &bundle_dir,
        Some(json_lookup(&manifest, &["sections", "capability_report"])?),
        CAPABILITY_REPORT_FILE,
    );

    let capability_summary = match capability_report.availability {
        ArtifactAvailability::Present => {
            let capability_report: JsonValue = serde_json::from_slice(
                &fs::read(bundle_dir.join(CAPABILITY_REPORT_FILE)).map_err(|error| {
                    format!(
                        "failed to read `{}`: {error}",
                        bundle_dir.join(CAPABILITY_REPORT_FILE).display()
                    )
                })?,
            )
            .map_err(|error| format!("failed to parse `{CAPABILITY_REPORT_FILE}`: {error}"))?;
            format!(
                "capabilities: declared={} used={} denied={}\n",
                json_array_len(&capability_report, &["declared"])?,
                json_array_len(&capability_report, &["used"])?,
                json_array_len(&capability_report, &["denied"])?,
            )
        }
        ArtifactAvailability::Unavailable | ArtifactAvailability::Absent => {
            "capabilities: unavailable\n".to_owned()
        }
    };

    let trace_summary = match trace.availability {
        ArtifactAvailability::Present => {
            format!(
                "trace: events={}\n",
                count_jsonl_events(&bundle_dir.join(TRACE_FILE))?
            )
        }
        ArtifactAvailability::Unavailable | ArtifactAvailability::Absent => {
            "trace: events=unavailable\n".to_owned()
        }
    };

    let state_diff_summary = match state_diff.availability {
        ArtifactAvailability::Present => {
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
            format!(
                "state_diff: changes={} added={} updated={} removed={}\n",
                changes.total, changes.added, changes.updated, changes.removed
            )
        }
        ArtifactAvailability::Unavailable | ArtifactAvailability::Absent => {
            "state_diff: unavailable\n".to_owned()
        }
    };

    Ok(format!(
        concat!(
            "run_id: {}\n",
            "status: {}\n",
            "schema_version: {}\n",
            "{}",
            "artifacts:\n",
            "  manifest.json: present ({} bytes)\n",
            "  inputs/entry.dot: {}\n",
            "  trace.jsonl: {}\n",
            "  state_diff.json: {}\n",
            "  capability_report.json: {}\n",
            "{}",
            "{}",
            "{}"
        ),
        run.run_id,
        run.status,
        schema_version,
        determinism_mode
            .map(|mode| format!("determinism_mode: {mode}\n"))
            .unwrap_or_default(),
        manifest_bytes.len(),
        entry_dot.render(),
        trace.render(),
        state_diff.render(),
        capability_report.render(),
        capability_summary,
        trace_summary,
        state_diff_summary,
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

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum ArtifactAvailability {
    Present,
    Unavailable,
    Absent,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
struct ArtifactSummary {
    availability: ArtifactAvailability,
    bytes: Option<u64>,
}

impl ArtifactSummary {
    fn render(self) -> String {
        match (self.availability, self.bytes) {
            (ArtifactAvailability::Present, Some(bytes)) => format!("present ({bytes} bytes)"),
            (ArtifactAvailability::Present, None) => "present".to_owned(),
            (ArtifactAvailability::Unavailable, Some(bytes)) => {
                format!("unavailable ({bytes} bytes recorded)")
            }
            (ArtifactAvailability::Unavailable, None) => "unavailable".to_owned(),
            (ArtifactAvailability::Absent, _) => "absent".to_owned(),
        }
    }
}

fn artifact_summary(
    bundle_dir: &Path,
    manifest_entry: Option<&JsonValue>,
    relative: &str,
) -> ArtifactSummary {
    let path = bundle_dir.join(relative);
    let exists = path.is_file();
    let manifest_status = manifest_entry
        .and_then(|entry| entry.get("status"))
        .and_then(JsonValue::as_str);
    let manifest_bytes = manifest_entry
        .and_then(|entry| entry.get("bytes"))
        .and_then(JsonValue::as_u64);

    match manifest_status {
        Some("ok") if exists => ArtifactSummary {
            availability: ArtifactAvailability::Present,
            bytes: manifest_bytes.or_else(|| file_len(path).ok()),
        },
        Some("ok") => ArtifactSummary {
            availability: ArtifactAvailability::Absent,
            bytes: manifest_bytes,
        },
        Some("unavailable") | Some("error") => ArtifactSummary {
            availability: ArtifactAvailability::Unavailable,
            bytes: manifest_bytes,
        },
        _ if exists => ArtifactSummary {
            availability: ArtifactAvailability::Present,
            bytes: file_len(path).ok(),
        },
        _ => ArtifactSummary {
            availability: ArtifactAvailability::Absent,
            bytes: manifest_bytes,
        },
    }
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
    use super::{ArtifactAvailability, artifact_summary, parse_run_id, state_diff_changes};
    use dot_artifacts::TRACE_FILE;
    use serde_json::json;
    use std::fs;
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

    #[test]
    fn artifact_summary_marks_unavailable_manifest_sections_without_failing() {
        let temp = TempDir::new().expect("temp dir must create");
        let summary = artifact_summary(
            temp.path(),
            Some(&json!({
                "status": "unavailable"
            })),
            TRACE_FILE,
        );

        assert_eq!(summary.availability, ArtifactAvailability::Unavailable);
        assert_eq!(summary.render(), "unavailable");
    }

    #[test]
    fn artifact_summary_marks_missing_ok_file_as_absent() {
        let temp = TempDir::new().expect("temp dir must create");
        let summary = artifact_summary(
            temp.path(),
            Some(&json!({
                "status": "ok",
                "bytes": 12
            })),
            TRACE_FILE,
        );

        assert_eq!(summary.availability, ArtifactAvailability::Absent);
        assert_eq!(summary.render(), "absent");
    }

    #[test]
    fn artifact_summary_uses_existing_file_length_when_present() {
        let temp = TempDir::new().expect("temp dir must create");
        fs::write(temp.path().join(TRACE_FILE), "{}\n").expect("trace file must write");

        let summary = artifact_summary(temp.path(), None, TRACE_FILE);

        assert_eq!(summary.availability, ArtifactAvailability::Present);
        assert_eq!(summary.render(), "present (3 bytes)");
    }
}
