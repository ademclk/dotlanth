use serde::{Deserialize, Serialize};
use std::collections::BTreeMap;

pub const BUNDLE_SCHEMA_VERSION_V1: &str = "1";
pub const MANIFEST_FILE: &str = "manifest.json";
pub const INPUTS_DIR: &str = "inputs";
pub const ENTRY_DOT_FILE: &str = "inputs/entry.dot";
pub const TRACE_FILE: &str = "trace.jsonl";
pub const STATE_DIFF_FILE: &str = "state_diff.json";
pub const DETERMINISM_REPORT_FILE: &str = "determinism_report.json";
pub const CAPABILITY_REPORT_FILE: &str = "capability_report.json";
pub const REQUIRED_FILE_PATHS: [&str; 6] = [
    MANIFEST_FILE,
    ENTRY_DOT_FILE,
    TRACE_FILE,
    STATE_DIFF_FILE,
    DETERMINISM_REPORT_FILE,
    CAPABILITY_REPORT_FILE,
];

pub(crate) const INPUT_ENTRY_DOT_KEY: &str = "entry_dot";
pub(crate) const DEFAULT_UNAVAILABLE_CODE: &str = "unavailable";
pub(crate) const DEFAULT_SECTION_UNAVAILABLE_MESSAGE: &str = "section data unavailable";
pub(crate) const DEFAULT_INPUT_UNAVAILABLE_MESSAGE: &str = "entry.dot snapshot not captured";

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum BundleSection {
    Trace,
    StateDiff,
    DeterminismReport,
    CapabilityReport,
}

impl BundleSection {
    pub const ALL: [Self; 4] = [
        Self::Trace,
        Self::StateDiff,
        Self::DeterminismReport,
        Self::CapabilityReport,
    ];

    pub const fn key(self) -> &'static str {
        match self {
            Self::Trace => "trace",
            Self::StateDiff => "state_diff",
            Self::DeterminismReport => "determinism_report",
            Self::CapabilityReport => "capability_report",
        }
    }

    pub const fn path(self) -> &'static str {
        match self {
            Self::Trace => TRACE_FILE,
            Self::StateDiff => STATE_DIFF_FILE,
            Self::DeterminismReport => DETERMINISM_REPORT_FILE,
            Self::CapabilityReport => CAPABILITY_REPORT_FILE,
        }
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum SectionStatus {
    Ok,
    Unavailable,
    Error,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub struct SectionErrorMarker {
    pub code: String,
    pub message: String,
}

impl SectionErrorMarker {
    pub fn new(code: impl Into<String>, message: impl Into<String>) -> Self {
        Self {
            code: code.into(),
            message: message.into(),
        }
    }
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub struct SectionManifest {
    pub path: String,
    pub status: SectionStatus,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub sha256: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub bytes: Option<u64>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub error: Option<SectionErrorMarker>,
}

impl SectionManifest {
    fn unavailable(path: &str) -> Self {
        Self {
            path: path.to_owned(),
            status: SectionStatus::Unavailable,
            sha256: None,
            bytes: None,
            error: Some(SectionErrorMarker::new(
                DEFAULT_UNAVAILABLE_CODE,
                DEFAULT_SECTION_UNAVAILABLE_MESSAGE,
            )),
        }
    }
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub struct InputSnapshotManifest {
    pub path: String,
    pub status: SectionStatus,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub sha256: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub bytes: Option<u64>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub error: Option<SectionErrorMarker>,
}

impl InputSnapshotManifest {
    fn unavailable(path: &str) -> Self {
        Self {
            path: path.to_owned(),
            status: SectionStatus::Unavailable,
            sha256: None,
            bytes: None,
            error: Some(SectionErrorMarker::new(
                DEFAULT_UNAVAILABLE_CODE,
                DEFAULT_INPUT_UNAVAILABLE_MESSAGE,
            )),
        }
    }
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub struct BundleManifestV1 {
    pub schema_version: String,
    pub run_id: String,
    pub determinism_mode: String,
    pub determinism_eligibility: DeterminismEligibilityManifest,
    pub determinism_audit_summary: DeterminismAuditSummaryManifest,
    pub replay_proof: ReplayProofManifest,
    pub created_at_ms: u64,
    pub required_files: Vec<String>,
    pub sections: BTreeMap<String, SectionManifest>,
    pub inputs: BTreeMap<String, InputSnapshotManifest>,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub struct DeterminismEligibilityManifest {
    pub status: String,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub reason: Option<String>,
}

impl DeterminismEligibilityManifest {
    pub fn unknown() -> Self {
        Self {
            status: "unknown".to_owned(),
            reason: None,
        }
    }
}

#[derive(Clone, Debug, Default, PartialEq, Eq, Serialize, Deserialize)]
pub struct DeterminismBudgetSummaryManifest {
    pub gated_total: u64,
    pub allowed_total: u64,
    pub denied_total: u64,
    pub controlled_side_effect_total: u64,
    pub non_deterministic_total: u64,
}

#[derive(Clone, Debug, Default, PartialEq, Eq, Serialize, Deserialize)]
pub struct DeterminismViolationSummaryManifest {
    pub count: u64,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub first_seq: Option<u64>,
}

#[derive(Clone, Debug, Default, PartialEq, Eq, Serialize, Deserialize)]
pub struct DeterminismAuditSummaryManifest {
    pub budget: DeterminismBudgetSummaryManifest,
    pub violations: DeterminismViolationSummaryManifest,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub struct ReplayProofManifest {
    pub status: String,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub reason: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub schema_version: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub eligibility: Option<DeterminismEligibilityManifest>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub canonical_surface: Option<ReplayProofCanonicalSurfaceManifest>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub comparison_fingerprint: Option<String>,
}

impl ReplayProofManifest {
    pub fn unavailable() -> Self {
        Self {
            status: "unavailable".to_owned(),
            reason: Some("not_generated".to_owned()),
            schema_version: None,
            eligibility: None,
            canonical_surface: None,
            comparison_fingerprint: None,
        }
    }
}

#[derive(Clone, Debug, Default, PartialEq, Eq, Serialize, Deserialize)]
pub struct ReplayProofCanonicalSurfaceManifest {
    #[serde(skip_serializing_if = "Option::is_none")]
    pub trace: Option<ReplayProofTraceSurfaceManifest>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub state_diff: Option<ReplayProofArtifactSurfaceManifest>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub capability_report: Option<ReplayProofArtifactSurfaceManifest>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub determinism: Option<ReplayProofDeterminismSurfaceManifest>,
}

#[derive(Clone, Debug, Default, PartialEq, Eq, Serialize, Deserialize)]
pub struct ReplayProofTraceSurfaceManifest {
    pub event_count: u64,
    pub fingerprint: String,
}

#[derive(Clone, Debug, Default, PartialEq, Eq, Serialize, Deserialize)]
pub struct ReplayProofArtifactSurfaceManifest {
    pub status: String,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub change_count: Option<u64>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub declared_count: Option<u64>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub used_count: Option<u64>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub denied_count: Option<u64>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub fingerprint: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub reason: Option<String>,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub struct ReplayProofDeterminismSurfaceManifest {
    pub mode: String,
    pub budget: DeterminismBudgetSummaryManifest,
    pub violations: DeterminismViolationSummaryManifest,
    pub fingerprint: String,
}

impl BundleManifestV1 {
    pub fn new(run_id: impl Into<String>) -> Self {
        Self::new_with_created_at(run_id, crate::util::now_ms())
    }

    pub fn new_with_created_at(run_id: impl Into<String>, created_at_ms: u64) -> Self {
        let mut sections = BTreeMap::new();
        for section in BundleSection::ALL {
            sections.insert(
                section.key().to_owned(),
                SectionManifest::unavailable(section.path()),
            );
        }

        let mut inputs = BTreeMap::new();
        inputs.insert(
            INPUT_ENTRY_DOT_KEY.to_owned(),
            InputSnapshotManifest::unavailable(ENTRY_DOT_FILE),
        );

        Self {
            schema_version: BUNDLE_SCHEMA_VERSION_V1.to_owned(),
            run_id: run_id.into(),
            determinism_mode: "default".to_owned(),
            determinism_eligibility: DeterminismEligibilityManifest::unknown(),
            determinism_audit_summary: DeterminismAuditSummaryManifest::default(),
            replay_proof: ReplayProofManifest::unavailable(),
            created_at_ms,
            required_files: REQUIRED_FILE_PATHS
                .iter()
                .map(|value| (*value).to_owned())
                .collect(),
            sections,
            inputs,
        }
    }

    pub fn serialize_pretty_json(&self) -> Result<Vec<u8>, serde_json::Error> {
        let mut output = serde_json::to_vec_pretty(self)?;
        output.push(b'\n');
        Ok(output)
    }

    pub(crate) fn section_mut(&mut self, section: BundleSection) -> Option<&mut SectionManifest> {
        self.sections.get_mut(section.key())
    }

    pub(crate) fn input_entry_dot_mut(&mut self) -> Option<&mut InputSnapshotManifest> {
        self.inputs.get_mut(INPUT_ENTRY_DOT_KEY)
    }

    pub fn set_determinism_mode(&mut self, determinism_mode: impl Into<String>) {
        self.determinism_mode = determinism_mode.into();
    }

    pub fn set_determinism_eligibility(
        &mut self,
        determinism_eligibility: DeterminismEligibilityManifest,
    ) {
        self.determinism_eligibility = determinism_eligibility;
    }

    pub fn set_determinism_audit_summary(
        &mut self,
        determinism_audit_summary: DeterminismAuditSummaryManifest,
    ) {
        self.determinism_audit_summary = determinism_audit_summary;
    }

    pub fn set_replay_proof(&mut self, replay_proof: ReplayProofManifest) {
        self.replay_proof = replay_proof;
    }
}
