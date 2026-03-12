#![forbid(unsafe_code)]

use crate::tui::model::{ActionItem, ActionKind};
use crate::tui::state::{Capability, Mode};

pub(crate) fn actions_for(mode: Mode, capability: Capability) -> Vec<ActionItem> {
    match (mode, capability) {
        (Mode::Demo, Capability::ParseValidate) => vec![
            action(
                ActionKind::ValidateDemoFixture,
                "Validate Demo Fixture",
                "Validate the generated hello-api fixture using real parser and runtime context rules.",
            ),
            action(
                ActionKind::ValidateInvalidFixture,
                "Validate Invalid Fixture",
                "Run the invalid demo input and surface its real diagnostics.",
            ),
            action(
                ActionKind::Refresh,
                "Refresh Page",
                "Reload runs, state, and selection context.",
            ),
        ],
        (Mode::Dev, Capability::ParseValidate) => vec![
            action(
                ActionKind::ValidateCurrentProject,
                "Validate Current app.dot",
                "Validate the current project input without starting the runtime.",
            ),
            action(
                ActionKind::ValidateInvalidFixture,
                "Validate Invalid Fixture",
                "Re-run the invalid demo fixture for comparison.",
            ),
            action(
                ActionKind::Refresh,
                "Refresh Page",
                "Reload runs, state, and selection context.",
            ),
        ],
        (Mode::Demo, Capability::RunServe) => vec![
            action(
                ActionKind::InitHelloApi,
                "Init ./hello-api",
                "Scaffold a sample project action representing `dot init`.",
            ),
            action(
                ActionKind::SmokeRun,
                "Run Demo Hello API",
                "Run a real demo API and collect canonical DotDB and artifact output.",
            ),
            action(
                ActionKind::Refresh,
                "Refresh Page",
                "Reload runs, state, and selection context.",
            ),
        ],
        (Mode::Dev, Capability::RunServe) => vec![
            action(
                ActionKind::RunCurrentProject,
                "Run Current Project",
                "Run the current project with a bounded request count.",
            ),
            action(
                ActionKind::Refresh,
                "Refresh Page",
                "Reload runs, state, and selection context.",
            ),
        ],
        (_, Capability::ReplayRecovery) => vec![
            action(
                ActionKind::ReplaySelected,
                "Replay Selected Run",
                "Replay the selected recorded run into a fresh run.",
            ),
            action(
                ActionKind::Refresh,
                "Refresh Page",
                "Reload runs, state, and selection context.",
            ),
        ],
        (Mode::Demo, Capability::SecurityCapabilities) => vec![
            action(
                ActionKind::RunCapabilityDenial,
                "Run Capability Denial Demo",
                "Generate and execute the real denial fixture to surface the capability failure clearly.",
            ),
            action(
                ActionKind::ShowSecuritySummary,
                "Show Capability Evidence",
                "Surface capability report evidence from the selected successful run.",
            ),
            action(
                ActionKind::Refresh,
                "Refresh Page",
                "Reload runs, state, and selection context.",
            ),
        ],
        (Mode::Dev, Capability::SecurityCapabilities) => vec![
            action(
                ActionKind::RunCapabilityDenial,
                "Re-run Denial Fixture",
                "Re-run the capability denial fixture and show the real validator/runtime message.",
            ),
            action(
                ActionKind::ShowSecuritySummary,
                "Show Capability Evidence",
                "Surface capability report evidence from the selected run.",
            ),
            action(
                ActionKind::Refresh,
                "Refresh Page",
                "Reload runs, state, and selection context.",
            ),
        ],
        (_, Capability::StateDb) => vec![
            action(
                ActionKind::ViewLogs,
                "View Selected Logs",
                "Read persisted DotDB logs for the selected run.",
            ),
            action(
                ActionKind::Refresh,
                "Refresh Page",
                "Reload runs, state, and selection context.",
            ),
        ],
        (_, Capability::ArtifactsInspection) => vec![
            action(
                ActionKind::InspectSelected,
                "Inspect Selected Run",
                "Open the stable artifact summary for the selected run.",
            ),
            action(
                ActionKind::ExportSelected,
                "Export Selected Bundle",
                "Copy the selected bundle into a local export directory.",
            ),
            action(
                ActionKind::Refresh,
                "Refresh Page",
                "Reload runs, state, and selection context.",
            ),
        ],
    }
}

fn action(kind: ActionKind, label: &'static str, description: &'static str) -> ActionItem {
    ActionItem {
        kind,
        label,
        description,
    }
}
