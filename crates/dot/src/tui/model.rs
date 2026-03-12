#![forbid(unsafe_code)]

use serde::{Deserialize, Serialize};
use std::collections::BTreeMap;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub(crate) enum ActionKind {
    Refresh,
    InitHelloApi,
    ValidateDemoFixture,
    ValidateInvalidFixture,
    ValidateCurrentProject,
    SmokeRun,
    RunCapabilityDenial,
    RunCurrentProject,
    InspectSelected,
    ViewLogs,
    ReplaySelected,
    ExportSelected,
    ShowSecuritySummary,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub(crate) struct ActionItem {
    pub(crate) kind: ActionKind,
    pub(crate) label: &'static str,
    pub(crate) description: &'static str,
}

pub(crate) const SCENARIO_SCHEMA_VERSION: &str = "1";

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub(crate) struct ScenarioCatalog {
    pub(crate) schema_version: String,
    pub(crate) scenarios: BTreeMap<String, ScenarioRecord>,
}

impl Default for ScenarioCatalog {
    fn default() -> Self {
        Self {
            schema_version: SCENARIO_SCHEMA_VERSION.to_owned(),
            scenarios: BTreeMap::new(),
        }
    }
}

#[derive(Clone, Debug, Default, PartialEq, Eq, Serialize, Deserialize)]
pub(crate) struct ScenarioRecord {
    pub(crate) fixture_root: String,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub(crate) latest_run_id: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub(crate) latest_bundle_ref: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub(crate) latest_export_dir: Option<String>,
}
