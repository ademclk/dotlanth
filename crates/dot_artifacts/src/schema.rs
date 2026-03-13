use serde::{Deserialize, Serialize};
use std::collections::BTreeMap;

pub const BUNDLE_SCHEMA_VERSION_V1: &str = "1";
pub const MANIFEST_FILE: &str = "manifest.json";
pub const INPUTS_DIR: &str = "inputs";
pub const ENTRY_DOT_FILE: &str = "inputs/entry.dot";
pub const TRACE_FILE: &str = "trace.jsonl";
pub const STATE_DIFF_FILE: &str = "state_diff.json";
pub const CAPABILITY_REPORT_FILE: &str = "capability_report.json";
pub const REQUIRED_FILE_PATHS: [&str; 5] = [
    MANIFEST_FILE,
    ENTRY_DOT_FILE,
    TRACE_FILE,
    STATE_DIFF_FILE,
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
    CapabilityReport,
}

impl BundleSection {
    pub const ALL: [Self; 3] = [Self::Trace, Self::StateDiff, Self::CapabilityReport];

    pub const fn key(self) -> &'static str {
        match self {
            Self::Trace => "trace",
            Self::StateDiff => "state_diff",
            Self::CapabilityReport => "capability_report",
        }
    }

    pub const fn path(self) -> &'static str {
        match self {
            Self::Trace => TRACE_FILE,
            Self::StateDiff => STATE_DIFF_FILE,
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
pub struct DeterminismManifest {
    pub mode: String,
}

impl DeterminismManifest {
    pub fn new(mode: impl Into<String>) -> Self {
        Self { mode: mode.into() }
    }
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub struct BundleManifestV1 {
    pub schema_version: String,
    pub run_id: String,
    pub created_at_ms: u64,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub determinism: Option<DeterminismManifest>,
    pub required_files: Vec<String>,
    pub sections: BTreeMap<String, SectionManifest>,
    pub inputs: BTreeMap<String, InputSnapshotManifest>,
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
            created_at_ms,
            determinism: Some(DeterminismManifest::new("default")),
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

    pub fn set_determinism_mode(&mut self, mode: impl Into<String>) {
        self.determinism = Some(DeterminismManifest::new(mode));
    }
}
