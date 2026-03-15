#![forbid(unsafe_code)]

use crate::commands::run::{self, RunAnnouncement};
use crate::commands::support::open_existing_dotdb_in;
use dot_artifacts::{
    BundleManifestV1, ENTRY_DOT_FILE, MANIFEST_FILE, ReplayProofArtifactSurfaceManifest,
    ReplayProofDeterminismSurfaceManifest, ReplayProofTraceSurfaceManifest,
};
use dot_rt::DeterminismMode;
use std::fs;
use std::path::{Path, PathBuf};

pub(crate) const INVALID_EXIT_CODE: u8 = 2;

pub(crate) fn run(run_id: &str) -> Result<u8, String> {
    let cwd = std::env::current_dir()
        .map_err(|error| format!("failed to read current directory: {error}"))?;
    let report = validate_in(&cwd, run_id)?;
    print!("{}", report.render());
    Ok(report.exit_code())
}

fn validate_in(project_root: &Path, run_id: &str) -> Result<ValidationReport, String> {
    let original_run_id = parse_run_id(run_id)?;
    let original = load_run_bundle(project_root, &original_run_id)?;

    if let Some(reason) = unsupported_reason(&original.manifest) {
        return Ok(ValidationReport {
            run_id: original_run_id,
            replay_run_id: None,
            state: ValidationState::Unsupported { reason },
        });
    }

    let replay_run_id = replay_original_bundle(project_root, &original)?;
    let replay = load_run_bundle(project_root, &replay_run_id)?;
    let state = compare_manifests(&original.manifest, &replay.manifest);

    Ok(ValidationReport {
        run_id: original_run_id,
        replay_run_id: Some(replay_run_id),
        state,
    })
}

fn parse_run_id(raw: &str) -> Result<String, String> {
    let trimmed = raw.trim();
    if trimmed.is_empty() {
        return Err("run id must not be empty".to_owned());
    }
    Ok(trimmed.to_owned())
}

fn replay_original_bundle(project_root: &Path, original: &RunBundle) -> Result<String, String> {
    let determinism = parse_determinism_mode(&original.manifest.determinism_mode)?;
    run::run_resolved_capture_with_announcement(
        Some((
            original.bundle_dir.join(ENTRY_DOT_FILE),
            project_root.to_path_buf(),
        )),
        determinism,
        Some(0),
        RunAnnouncement::Silent,
    )
}

fn parse_determinism_mode(raw: &str) -> Result<DeterminismMode, String> {
    match raw {
        "default" => Ok(DeterminismMode::Default),
        "strict" => Ok(DeterminismMode::Strict),
        other => Err(format!(
            "run manifest recorded unsupported determinism mode `{other}`"
        )),
    }
}

fn load_run_bundle(project_root: &Path, run_id: &str) -> Result<RunBundle, String> {
    let mut db = open_existing_dotdb_in(project_root)?;
    let bundle = db
        .artifact_bundle(run_id)
        .map_err(|error| format!("failed to resolve bundle for run {run_id}: {error}"))?;
    let bundle_dir = resolve_bundle_dir(project_root, &bundle.bundle_ref);
    let manifest = read_manifest(&bundle_dir.join(MANIFEST_FILE))?;

    Ok(RunBundle {
        bundle_dir,
        manifest,
    })
}

fn resolve_bundle_dir(project_root: &Path, bundle_ref: &str) -> PathBuf {
    let path = Path::new(bundle_ref);
    if path.is_absolute() {
        path.to_path_buf()
    } else {
        project_root.join(path)
    }
}

fn read_manifest(path: &Path) -> Result<BundleManifestV1, String> {
    serde_json::from_slice(
        &fs::read(path).map_err(|error| format!("failed to read `{}`: {error}", path.display()))?,
    )
    .map_err(|error| format!("failed to parse `{}`: {error}", path.display()))
}

fn unsupported_reason(manifest: &BundleManifestV1) -> Option<String> {
    if manifest.replay_proof.status != "ready" {
        return Some(
            manifest
                .replay_proof
                .reason
                .clone()
                .unwrap_or_else(|| manifest.replay_proof.status.clone()),
        );
    }

    let eligibility = manifest
        .replay_proof
        .eligibility
        .as_ref()
        .unwrap_or(&manifest.determinism_eligibility);
    if eligibility.status != "eligible" {
        return Some(
            eligibility
                .reason
                .clone()
                .unwrap_or_else(|| eligibility.status.clone()),
        );
    }

    if manifest.replay_proof.canonical_surface.is_none()
        || manifest.replay_proof.comparison_fingerprint.is_none()
    {
        return Some("proof_incomplete".to_owned());
    }

    None
}

fn compare_manifests(original: &BundleManifestV1, replay: &BundleManifestV1) -> ValidationState {
    if let Some(reason) = unsupported_reason(replay) {
        return ValidationState::Unsupported { reason };
    }

    if original.replay_proof.comparison_fingerprint == replay.replay_proof.comparison_fingerprint {
        return ValidationState::Valid;
    }

    let original_surface = original
        .replay_proof
        .canonical_surface
        .as_ref()
        .expect("validated original manifest must include a canonical surface");
    let replay_surface = replay
        .replay_proof
        .canonical_surface
        .as_ref()
        .expect("validated replay manifest must include a canonical surface");

    if let Some(detail) = compare_determinism_surface(
        original_surface.determinism.as_ref(),
        replay_surface.determinism.as_ref(),
    ) {
        return ValidationState::Invalid {
            reason: InvalidReason::DeterminismDenial,
            detail,
        };
    }

    if let Some(detail) = compare_trace_surface(
        original_surface.trace.as_ref(),
        replay_surface.trace.as_ref(),
    ) {
        return ValidationState::Invalid {
            reason: InvalidReason::ReplayMismatch,
            detail,
        };
    }

    if let Some(detail) = compare_artifact_surface(
        "state diff",
        original_surface.state_diff.as_ref(),
        replay_surface.state_diff.as_ref(),
    ) {
        return ValidationState::Invalid {
            reason: InvalidReason::ReplayMismatch,
            detail,
        };
    }

    if let Some(detail) = compare_artifact_surface(
        "capability report",
        original_surface.capability_report.as_ref(),
        replay_surface.capability_report.as_ref(),
    ) {
        return ValidationState::Invalid {
            reason: InvalidReason::ReplayMismatch,
            detail,
        };
    }

    ValidationState::Invalid {
        reason: InvalidReason::ReplayMismatch,
        detail: "replay proof fingerprint changed".to_owned(),
    }
}

fn compare_determinism_surface(
    original: Option<&ReplayProofDeterminismSurfaceManifest>,
    replay: Option<&ReplayProofDeterminismSurfaceManifest>,
) -> Option<String> {
    let (Some(original), Some(replay)) = (original, replay) else {
        return compare_presence("determinism surface", original.is_some(), replay.is_some());
    };

    if original.mode != replay.mode {
        return Some(format!(
            "determinism mode changed (original={} replay={})",
            original.mode, replay.mode
        ));
    }
    if original.budget.denied_total != replay.budget.denied_total {
        return Some(format!(
            "determinism denied count changed (original={} replay={})",
            original.budget.denied_total, replay.budget.denied_total
        ));
    }
    if original.violations.count != replay.violations.count {
        return Some(format!(
            "determinism violation count changed (original={} replay={})",
            original.violations.count, replay.violations.count
        ));
    }
    if original.budget.non_deterministic_total != replay.budget.non_deterministic_total {
        return Some(format!(
            "determinism non_deterministic count changed (original={} replay={})",
            original.budget.non_deterministic_total, replay.budget.non_deterministic_total
        ));
    }
    if original.fingerprint != replay.fingerprint {
        return Some("determinism fingerprint changed".to_owned());
    }

    None
}

fn compare_trace_surface(
    original: Option<&ReplayProofTraceSurfaceManifest>,
    replay: Option<&ReplayProofTraceSurfaceManifest>,
) -> Option<String> {
    let (Some(original), Some(replay)) = (original, replay) else {
        return compare_presence("trace surface", original.is_some(), replay.is_some());
    };

    if original.event_count != replay.event_count {
        return Some(format!(
            "trace event count changed (original={} replay={})",
            original.event_count, replay.event_count
        ));
    }
    if original.fingerprint != replay.fingerprint {
        return Some("trace fingerprint changed".to_owned());
    }

    None
}

fn compare_artifact_surface(
    label: &str,
    original: Option<&ReplayProofArtifactSurfaceManifest>,
    replay: Option<&ReplayProofArtifactSurfaceManifest>,
) -> Option<String> {
    let (Some(original), Some(replay)) = (original, replay) else {
        return compare_presence(
            &format!("{label} surface"),
            original.is_some(),
            replay.is_some(),
        );
    };

    if original.status != replay.status {
        return Some(format!(
            "{label} status changed (original={} replay={})",
            original.status, replay.status
        ));
    }
    if original.change_count != replay.change_count {
        return Some(format!("{label} change count changed"));
    }
    if original.declared_count != replay.declared_count {
        return Some(format!("{label} declared count changed"));
    }
    if original.used_count != replay.used_count {
        return Some(format!("{label} used count changed"));
    }
    if original.denied_count != replay.denied_count {
        return Some(format!("{label} denied count changed"));
    }
    if original.fingerprint != replay.fingerprint {
        return Some(format!("{label} fingerprint changed"));
    }
    if original.reason != replay.reason {
        return Some(format!("{label} reason changed"));
    }

    None
}

fn compare_presence(label: &str, original_present: bool, replay_present: bool) -> Option<String> {
    if original_present == replay_present {
        None
    } else {
        Some(format!("{label} changed"))
    }
}

struct RunBundle {
    bundle_dir: PathBuf,
    manifest: BundleManifestV1,
}

struct ValidationReport {
    run_id: String,
    replay_run_id: Option<String>,
    state: ValidationState,
}

impl ValidationReport {
    fn exit_code(&self) -> u8 {
        match self.state {
            ValidationState::Valid | ValidationState::Unsupported { .. } => 0,
            ValidationState::Invalid { .. } => INVALID_EXIT_CODE,
        }
    }

    fn render(&self) -> String {
        let mut output = format!("run_id: {}\n", self.run_id);
        if let Some(replay_run_id) = self.replay_run_id.as_deref() {
            output.push_str(&format!("replay_run_id: {replay_run_id}\n"));
        }
        match &self.state {
            ValidationState::Valid => {
                output.push_str("validation: valid\n");
            }
            ValidationState::Invalid { reason, detail } => {
                output.push_str("validation: invalid\n");
                output.push_str(&format!("reason: {}\n", reason.as_str()));
                output.push_str(&format!("detail: {detail}\n"));
            }
            ValidationState::Unsupported { reason } => {
                output.push_str("validation: unsupported\n");
                output.push_str(&format!("reason: {reason}\n"));
            }
        }
        output
    }
}

enum ValidationState {
    Valid,
    Invalid {
        reason: InvalidReason,
        detail: String,
    },
    Unsupported {
        reason: String,
    },
}

enum InvalidReason {
    DeterminismDenial,
    ReplayMismatch,
}

impl InvalidReason {
    const fn as_str(&self) -> &'static str {
        match self {
            Self::DeterminismDenial => "determinism_denial",
            Self::ReplayMismatch => "replay_mismatch",
        }
    }
}

#[cfg(test)]
mod tests {
    use super::{compare_trace_surface, parse_run_id};
    use dot_artifacts::ReplayProofTraceSurfaceManifest;

    #[test]
    fn parse_run_id_rejects_blank_values() {
        assert!(parse_run_id("").is_err());
        assert!(parse_run_id("   ").is_err());
    }

    #[test]
    fn compare_trace_surface_reports_fingerprint_changes() {
        let original = ReplayProofTraceSurfaceManifest {
            event_count: 2,
            fingerprint: "abc".to_owned(),
        };
        let replay = ReplayProofTraceSurfaceManifest {
            event_count: 2,
            fingerprint: "xyz".to_owned(),
        };

        assert_eq!(
            compare_trace_surface(Some(&original), Some(&replay)).as_deref(),
            Some("trace fingerprint changed")
        );
    }
}
