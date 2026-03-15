#![forbid(unsafe_code)]

use crate::commands::{export_artifacts, init, inspect, logs, replay, run};
use crate::tui::fixtures::{CapabilityFixture, DemoScenario, DemoWorkspace, FixtureManager};
use crate::tui::jobs::JobState;
use crate::tui::model::{ActionItem, ActionKind};
use crate::tui::registry;
use crate::tui::state::{Capability, Focus, Mode};
use dot_artifacts::CAPABILITY_REPORT_FILE;
use dot_db::{DotDb, RunStatus, StoredRun};
use dot_rt::{DeterminismMode, RuntimeContext};
use serde_json::Value as JsonValue;
use std::fs;
use std::io::{Read, Write};
use std::net::{TcpListener, TcpStream};
use std::path::{Path, PathBuf};
use std::sync::Arc;
use std::sync::atomic::{AtomicBool, Ordering};
use std::thread;
use std::time::{Duration, Instant};

pub(crate) struct App {
    project_root: PathBuf,
    pub(crate) mode: Mode,
    pub(crate) capability: Capability,
    pub(crate) focus: Focus,
    pub(crate) actions: Vec<ActionItem>,
    pub(crate) action_index: usize,
    pub(crate) recent_runs: Vec<StoredRun>,
    pub(crate) run_index: usize,
    pub(crate) detail_title: String,
    pub(crate) detail_lines: Vec<String>,
    pub(crate) status_line: String,
    pub(crate) output_scroll: u16,
    pub(crate) db_available: bool,
    pub(crate) bundle_count: usize,
    pub(crate) dot_path: Option<PathBuf>,
    pub(crate) selected_bundle_ref: Option<String>,
    pub(crate) selected_export_dir: Option<PathBuf>,
    pub(crate) selected_fixture_id: String,
    pub(crate) job_state: JobState,
}

impl App {
    pub(crate) fn load(project_root: PathBuf) -> Result<Self, String> {
        let mode = Mode::Demo;
        let capability = Capability::ParseValidate;
        let mut app = Self {
            project_root,
            mode,
            capability,
            focus: Focus::Modes,
            actions: registry::actions_for(mode, capability),
            action_index: 0,
            recent_runs: Vec::new(),
            run_index: 0,
            detail_title: "Capability Lab".to_owned(),
            detail_lines: Vec::new(),
            status_line: "Loading capability lab...".to_owned(),
            output_scroll: 0,
            db_available: false,
            bundle_count: 0,
            dot_path: None,
            selected_bundle_ref: None,
            selected_export_dir: None,
            selected_fixture_id: default_fixture_id_for(capability).to_owned(),
            job_state: JobState::Idle,
        };
        app.refresh()?;
        Ok(app)
    }

    pub(crate) fn next_focus(&mut self) {
        self.focus = self.focus.next();
    }

    pub(crate) fn previous_focus(&mut self) {
        self.focus = self.focus.previous();
    }

    pub(crate) fn move_next(&mut self) {
        match self.focus {
            Focus::Modes => {
                self.mode = self.mode.next();
                self.sync_registry();
            }
            Focus::Capabilities => {
                self.capability = self.capability.next();
                self.selected_fixture_id = default_fixture_id_for(self.capability).to_owned();
                self.sync_registry();
            }
            Focus::Actions => {
                if !self.actions.is_empty() {
                    self.action_index = (self.action_index + 1) % self.actions.len();
                }
            }
            Focus::Runs => {
                if !self.recent_runs.is_empty() {
                    self.run_index = (self.run_index + 1) % self.recent_runs.len();
                    let _ = self.promote_selected_run_context();
                }
            }
            Focus::Output => {
                self.output_scroll = self.output_scroll.saturating_add(1);
            }
        }
    }

    pub(crate) fn move_previous(&mut self) {
        match self.focus {
            Focus::Modes => {
                self.mode = self.mode.previous();
                self.sync_registry();
            }
            Focus::Capabilities => {
                self.capability = self.capability.previous();
                self.selected_fixture_id = default_fixture_id_for(self.capability).to_owned();
                self.sync_registry();
            }
            Focus::Actions => {
                if !self.actions.is_empty() {
                    self.action_index = if self.action_index == 0 {
                        self.actions.len().saturating_sub(1)
                    } else {
                        self.action_index.saturating_sub(1)
                    };
                }
            }
            Focus::Runs => {
                if !self.recent_runs.is_empty() {
                    self.run_index = if self.run_index == 0 {
                        self.recent_runs.len().saturating_sub(1)
                    } else {
                        self.run_index.saturating_sub(1)
                    };
                    let _ = self.promote_selected_run_context();
                }
            }
            Focus::Output => {
                self.output_scroll = self.output_scroll.saturating_sub(1);
            }
        }
    }

    pub(crate) fn activate(&mut self) -> Result<(), String> {
        match self.focus {
            Focus::Actions => self.execute_current_action(),
            Focus::Runs => self.show_selected_default_view(),
            Focus::Modes | Focus::Capabilities | Focus::Output => Ok(()),
        }
    }

    pub(crate) fn refresh(&mut self) -> Result<(), String> {
        self.dot_path = detect_dot_path(&self.project_root);
        self.bundle_count = count_bundles(&self.project_root);
        let db_path = DotDb::default_path_in(&self.project_root);
        let mut db_warning = None;
        self.db_available = db_path.is_file();

        self.recent_runs = if self.db_available {
            match DotDb::open(&db_path) {
                Ok(db) => match db.recent_runs(8) {
                    Ok(runs) => runs,
                    Err(error) => {
                        self.db_available = false;
                        db_warning = Some(format!("failed to load recent runs: {error}"));
                        Vec::new()
                    }
                },
                Err(error) => {
                    self.db_available = false;
                    db_warning = Some(format!("failed to open DotDB: {error}"));
                    Vec::new()
                }
            }
        } else {
            Vec::new()
        };

        if self.run_index >= self.recent_runs.len() {
            self.run_index = self.recent_runs.len().saturating_sub(1);
        }

        self.promote_selected_run_context()?;

        if self.recent_runs.is_empty() {
            self.show_welcome();
        } else {
            self.show_selected_default_view()?;
        }

        self.status_line = if let Some(warning) = db_warning {
            format!("DotDB present but unavailable: {warning}")
        } else if self.db_available {
            format!(
                "Capability lab ready: {} runs loaded from {}",
                self.recent_runs.len(),
                db_path.display()
            )
        } else {
            "DotDB not found yet. Start with a Demo action to create real run state.".to_owned()
        };
        Ok(())
    }

    pub(crate) fn project_root(&self) -> &Path {
        &self.project_root
    }

    pub(crate) fn dot_path_label(&self) -> String {
        self.dot_path
            .as_ref()
            .map(|path| path.display().to_string())
            .unwrap_or_else(|| "missing app.dot".to_owned())
    }

    pub(crate) fn success_count(&self) -> usize {
        self.recent_runs
            .iter()
            .filter(|run| run.status == RunStatus::Succeeded)
            .count()
    }

    pub(crate) fn failed_count(&self) -> usize {
        self.recent_runs
            .iter()
            .filter(|run| run.status == RunStatus::Failed)
            .count()
    }

    pub(crate) fn running_count(&self) -> usize {
        self.recent_runs
            .iter()
            .filter(|run| run.status == RunStatus::Running)
            .count()
    }

    pub(crate) fn selected_run(&self) -> Option<&StoredRun> {
        self.recent_runs.get(self.run_index)
    }

    pub(crate) fn selected_action(&self) -> Option<ActionItem> {
        self.actions.get(self.action_index).copied()
    }

    pub(crate) fn mode_label(&self) -> &'static str {
        self.mode.label()
    }

    pub(crate) fn capability_label(&self) -> &'static str {
        self.capability.label()
    }

    pub(crate) fn selected_run_label(&self) -> String {
        self.selected_run()
            .map(|run| run.run_id.clone())
            .unwrap_or_else(|| "none".to_owned())
    }

    pub(crate) fn selected_bundle_label(&self) -> String {
        self.selected_bundle_ref
            .clone()
            .unwrap_or_else(|| "none".to_owned())
    }

    pub(crate) fn selected_fixture_label(&self) -> &str {
        &self.selected_fixture_id
    }

    pub(crate) fn execute_current_action(&mut self) -> Result<(), String> {
        let Some(action) = self.selected_action() else {
            return Ok(());
        };

        let result = match action.kind {
            ActionKind::Refresh => {
                self.refresh()?;
                self.status_line = "Capability page refreshed.".to_owned();
                Ok(())
            }
            ActionKind::InitHelloApi => {
                let dir = self.project_root.join("hello-api");
                let message = init::initialize(&dir)?;
                self.selected_fixture_id = CapabilityFixture::InitBootstrap
                    .default_fixture_id()
                    .to_owned();
                self.set_detail("Init Result".to_owned(), message);
                self.status_line = format!("Initialized sample project at {}", dir.display());
                Ok(())
            }
            ActionKind::ValidateDemoFixture => {
                self.render_validation_for_scenario(DemoScenario::HelloApi, false)
            }
            ActionKind::ValidateInvalidFixture => {
                self.render_validation_for_scenario(DemoScenario::InvalidParse, true)
            }
            ActionKind::ValidateCurrentProject => {
                let Some(dot_path) = self.dot_path.clone() else {
                    return Err(
                        "cannot validate without `app.dot` in the current project root".to_owned(),
                    );
                };
                self.render_validation_result(
                    "Current Project Validation",
                    "current-project",
                    dot_path.as_path(),
                    false,
                )
            }
            ActionKind::SmokeRun => self.run_demo_fixture(DemoScenario::HelloApi, "/hello"),
            ActionKind::RunCapabilityDenial => {
                self.render_validation_for_scenario(DemoScenario::CapabilityDenial, true)
            }
            ActionKind::RunCurrentProject => {
                let Some(dot_path) = self.dot_path.clone() else {
                    return Err(
                        "cannot run without `app.dot` in the current project root".to_owned()
                    );
                };
                self.job_state = JobState::Running("running");
                run::run_resolved_with_announcement(
                    dot_path,
                    self.project_root.clone(),
                    DeterminismMode::Default,
                    Some(0),
                    run::RunAnnouncement::Silent,
                )?;
                self.refresh()?;
                self.promote_latest_run();
                self.status_line = "Run completed and capability lab refreshed.".to_owned();
                Ok(())
            }
            ActionKind::InspectSelected => {
                self.show_selected_inspect()?;
                self.status_line = selected_status("Inspection loaded", self.selected_run());
                Ok(())
            }
            ActionKind::ViewLogs => {
                self.show_selected_logs()?;
                self.status_line = selected_status("Logs loaded", self.selected_run());
                Ok(())
            }
            ActionKind::ReplaySelected => {
                let run_id = self
                    .selected_run()
                    .map(|run| run.run_id.clone())
                    .ok_or_else(|| "select a run before replaying".to_owned())?;
                replay::run_in(
                    &self.project_root,
                    Some(&run_id),
                    None,
                    run::RunAnnouncement::Silent,
                )?;
                self.refresh()?;
                self.promote_latest_run();
                self.status_line = format!("Replay completed for {run_id}.");
                Ok(())
            }
            ActionKind::ExportSelected => {
                let run_id = self
                    .selected_run()
                    .map(|run| run.run_id.clone())
                    .ok_or_else(|| "select a run before exporting".to_owned())?;
                let export_dir = next_export_dir(
                    self.demo_workspace()
                        .export_root(&format!("{}-{run_id}", self.selected_fixture_id)),
                );
                let message =
                    export_artifacts::export_in(&self.project_root, &run_id, &export_dir)?;
                self.selected_export_dir = Some(export_dir);
                self.fixture_manager().update_export_metadata(
                    &self.selected_fixture_id,
                    self.selected_export_dir
                        .as_deref()
                        .expect("export dir must exist"),
                )?;
                self.set_detail("Export Result".to_owned(), message);
                self.status_line = format!("Exported artifacts for {run_id}.");
                Ok(())
            }
            ActionKind::ShowSecuritySummary => self.show_security_summary(),
        };

        self.job_state = JobState::Idle;
        result
    }

    pub(crate) fn set_detail(&mut self, title: String, body: String) {
        self.detail_title = title;
        self.detail_lines = if body.is_empty() {
            vec!["(no output)".to_owned()]
        } else {
            body.lines().map(str::to_owned).collect()
        };
        self.output_scroll = 0;
    }

    pub(crate) fn present_error(&mut self, title: &str, prefix: &str, error: String) {
        self.set_detail(title.to_owned(), error.clone());
        self.status_line = format!("{prefix}: {error}");
        self.job_state = JobState::Idle;
    }

    fn render_validation_for_scenario(
        &mut self,
        scenario: DemoScenario,
        expect_failure: bool,
    ) -> Result<(), String> {
        let dot_path = self.fixture_manager().refresh_fixture(scenario)?;
        self.selected_fixture_id = scenario.fixture_id().to_owned();
        self.render_validation_result(
            "Fixture Validation",
            scenario.fixture_id(),
            dot_path.as_path(),
            expect_failure,
        )
    }

    fn render_validation_result(
        &mut self,
        title: &str,
        fixture_id: &str,
        dot_path: &Path,
        expect_failure: bool,
    ) -> Result<(), String> {
        match dot_dsl::load_and_validate(dot_path) {
            Ok(document) => {
                if expect_failure {
                    return Err(format!(
                        "fixture `{fixture_id}` unexpectedly validated successfully"
                    ));
                }

                let runtime = RuntimeContext::from_dot_dsl(&document)
                    .map_err(|error| format!("failed to derive runtime context: {error}"))?;
                let declared = runtime
                    .declared_capabilities()
                    .iter()
                    .map(|capability| capability.capability().to_string())
                    .collect::<Vec<_>>();
                let body = format!(
                    "Fixture: {}\nPath: {}\nStatus: validation succeeded\nAPIs: {}\nRoutes: {}\nRuntime capabilities: {}\n",
                    fixture_id,
                    dot_path.display(),
                    document.apis.len(),
                    document
                        .apis
                        .iter()
                        .map(|api| api.routes.len())
                        .sum::<usize>(),
                    if declared.is_empty() {
                        "none".to_owned()
                    } else {
                        declared.join(", ")
                    },
                );
                self.set_detail(title.to_owned(), body);
                self.status_line = format!("Validated {fixture_id} successfully.");
                Ok(())
            }
            Err(error) => {
                let message = error.to_string();
                if !expect_failure {
                    return Err(message);
                }

                let body = format!(
                    "Fixture: {}\nPath: {}\nStatus: validation failed as expected\nDiagnostic: {}\n",
                    fixture_id,
                    dot_path.display(),
                    message,
                );
                self.set_detail(title.to_owned(), body);
                self.status_line = format!("Validation failure captured for {fixture_id}.");
                Ok(())
            }
        }
    }

    fn run_demo_fixture(
        &mut self,
        scenario: DemoScenario,
        request_path: &str,
    ) -> Result<(), String> {
        self.run_demo_fixture_with_port_source(scenario, request_path, reserve_local_port)
    }

    fn show_security_summary(&mut self) -> Result<(), String> {
        let run_id = self
            .selected_run()
            .map(|run| run.run_id.clone())
            .ok_or_else(|| "select a run before loading capability evidence".to_owned())?;
        let bundle_dir = self.resolve_selected_bundle_dir()?;
        let report_path = bundle_dir.join(CAPABILITY_REPORT_FILE);
        let report: JsonValue =
            serde_json::from_slice(&fs::read(&report_path).map_err(|error| {
                format!(
                    "failed to read capability report `{}`: {error}",
                    report_path.display()
                )
            })?)
            .map_err(|error| {
                format!(
                    "failed to parse capability report `{}`: {error}",
                    report_path.display()
                )
            })?;

        let declared = capability_names(&report, "declared");
        let used = capability_usage(&report);
        let denied = capability_denials(&report);
        let body = format!(
            concat!(
                "Run: {}\n",
                "Bundle: {}\n",
                "Declared: {}\n",
                "Used: {}\n",
                "Denied: {}\n"
            ),
            run_id,
            self.selected_bundle_label(),
            join_or_none(&declared),
            join_or_none(&used),
            join_or_none(&denied),
        );
        self.set_detail("Capability Evidence".to_owned(), body);
        self.status_line = "Capability evidence loaded.".to_owned();
        Ok(())
    }

    fn persist_selected_run_metadata(&self) -> Result<(), String> {
        let Some(run) = self.selected_run() else {
            return Ok(());
        };
        let Some(bundle_ref) = self.selected_bundle_ref.as_deref() else {
            return Ok(());
        };
        self.fixture_manager().update_run_metadata(
            &self.selected_fixture_id,
            &run.run_id,
            bundle_ref,
        )
    }

    fn sync_registry(&mut self) {
        self.actions = registry::actions_for(self.mode, self.capability);
        if self.action_index >= self.actions.len() {
            self.action_index = self.actions.len().saturating_sub(1);
        }
        self.status_line = format!("{} / {}", self.mode.label(), self.capability.label());
    }

    fn promote_selected_run_context(&mut self) -> Result<(), String> {
        let Some(run_id) = self.selected_run().map(|run| run.run_id.clone()) else {
            self.selected_bundle_ref = None;
            return Ok(());
        };

        if !self.db_available {
            self.selected_bundle_ref = None;
            return Ok(());
        }

        let db_path = DotDb::default_path_in(&self.project_root);
        let mut db =
            DotDb::open(&db_path).map_err(|error| format!("failed to open DotDB: {error}"))?;
        self.selected_bundle_ref = db
            .artifact_bundle(&run_id)
            .ok()
            .map(|bundle| bundle.bundle_ref);
        Ok(())
    }

    fn show_selected_default_view(&mut self) -> Result<(), String> {
        match self.capability {
            Capability::StateDb => self.show_selected_logs(),
            Capability::ArtifactsInspection => self.show_selected_inspect(),
            _ => self.show_selected_inspect().or_else(|_| {
                self.show_welcome();
                Ok(())
            }),
        }
    }

    fn show_selected_inspect(&mut self) -> Result<(), String> {
        let run_id = self
            .selected_run()
            .map(|run| run.run_id.clone())
            .ok_or_else(|| "select a run to inspect".to_owned())?;
        let summary = inspect::render_summary_in(&self.project_root, &run_id)?;
        self.set_detail(format!("Inspect {run_id}"), summary);
        Ok(())
    }

    fn show_selected_logs(&mut self) -> Result<(), String> {
        let run_id = self
            .selected_run()
            .map(|run| run.run_id.clone())
            .ok_or_else(|| "select a run to read logs".to_owned())?;
        let rendered = logs::render_logs_in(&self.project_root, &run_id)?;
        self.set_detail(format!("Logs {run_id}"), rendered);
        Ok(())
    }

    fn show_welcome(&mut self) {
        let body = format!(
            concat!(
                "No recorded runs yet.\n",
                "\n",
                "Capability Lab bootstrap path:\n",
                "1. Use Demo mode to initialize or validate a fixture.\n",
                "2. Run a real bounded project request to create DotDB and bundles.\n",
                "3. Use Replay, State, and Artifact pages once run state exists.\n",
                "\n",
                "Current mode: {}\n",
                "Current capability: {}\n",
                "Current fixture: {}\n",
                "Demo metadata: {}\n",
                "Demo tmp root: {}\n",
                "Current project root: {}\n",
                "Current dot file: {}\n"
            ),
            self.mode.label(),
            self.capability.label(),
            self.selected_fixture_id,
            self.demo_workspace().metadata_path().display(),
            self.demo_workspace().tmp_root().display(),
            self.project_root.display(),
            self.dot_path_label(),
        );
        self.set_detail("Capability Lab".to_owned(), body);
    }

    #[cfg(test)]
    pub(crate) fn test_fixture() -> Self {
        let mode = Mode::Demo;
        let capability = Capability::ParseValidate;
        Self {
            project_root: PathBuf::from("/tmp/dotlanth"),
            mode,
            capability,
            focus: Focus::Modes,
            actions: registry::actions_for(mode, capability),
            action_index: 0,
            recent_runs: vec![
                StoredRun {
                    run_id: "run_newest".to_owned(),
                    status: RunStatus::Succeeded,
                    created_at_ms: 30,
                    finalized_at_ms: Some(40),
                    determinism_mode: "default".to_owned(),
                },
                StoredRun {
                    run_id: "run_failed".to_owned(),
                    status: RunStatus::Failed,
                    created_at_ms: 20,
                    finalized_at_ms: Some(25),
                    determinism_mode: "default".to_owned(),
                },
            ],
            run_index: 0,
            detail_title: "Capability Lab".to_owned(),
            detail_lines: vec![
                "run_id: run_newest".to_owned(),
                "status: succeeded".to_owned(),
                "trace: events=4".to_owned(),
            ],
            status_line: "Capability lab ready".to_owned(),
            output_scroll: 0,
            db_available: true,
            bundle_count: 2,
            dot_path: Some(PathBuf::from("/tmp/dotlanth/app.dot")),
            selected_bundle_ref: Some(".dotlanth/bundles/run_newest".to_owned()),
            selected_export_dir: Some(PathBuf::from(
                "/tmp/dotlanth/.dotlanth/demo/exports/hello-api-run_newest",
            )),
            selected_fixture_id: "hello-api".to_owned(),
            job_state: JobState::Idle,
        }
    }

    fn demo_workspace(&self) -> DemoWorkspace {
        DemoWorkspace::new(&self.project_root)
    }

    fn fixture_manager(&self) -> FixtureManager {
        FixtureManager::new(&self.project_root)
    }

    fn resolve_selected_bundle_dir(&self) -> Result<PathBuf, String> {
        let bundle_ref = self.selected_bundle_ref.as_deref().ok_or_else(|| {
            "select a run with a canonical bundle before loading evidence".to_owned()
        })?;
        let path = Path::new(bundle_ref);
        Ok(if path.is_absolute() {
            path.to_path_buf()
        } else {
            self.project_root.join(path)
        })
    }

    fn promote_latest_run(&mut self) {
        if !self.recent_runs.is_empty() {
            self.run_index = 0;
            let _ = self.promote_selected_run_context();
        }
    }

    fn run_demo_fixture_with_port_source<F>(
        &mut self,
        scenario: DemoScenario,
        request_path: &str,
        mut next_port: F,
    ) -> Result<(), String>
    where
        F: FnMut() -> Result<u16, String>,
    {
        let mut last_bind_error = None;

        for _attempt in 0..3 {
            let port = next_port()?;
            let dot_path = self
                .fixture_manager()
                .refresh_fixture_with_port(scenario, Some(port))?;
            self.selected_fixture_id = scenario.fixture_id().to_owned();

            let probe_path = request_path.to_owned();
            let cancel = Arc::new(AtomicBool::new(false));
            let probe_cancel = Arc::clone(&cancel);
            let probe = thread::spawn(move || probe_http_get(port, &probe_path, probe_cancel));

            self.job_state = JobState::Running("running demo");
            let run_result = run::run_resolved_with_announcement(
                dot_path,
                self.project_root.clone(),
                DeterminismMode::Default,
                Some(1),
                run::RunAnnouncement::Silent,
            );
            self.job_state = JobState::Idle;

            match run_result {
                Ok(()) => {
                    let response = join_probe(probe)?;
                    self.refresh()?;
                    self.promote_latest_run();
                    self.persist_selected_run_metadata()?;

                    let run_id = self.selected_run_label();
                    let bundle_ref = self.selected_bundle_label();
                    let body = format!(
                        concat!(
                            "Fixture: {}\n",
                            "Run: {}\n",
                            "Bundle: {}\n",
                            "Probe Target: http://127.0.0.1:{}{}\n",
                            "HTTP Response:\n",
                            "{}"
                        ),
                        scenario.fixture_id(),
                        run_id,
                        bundle_ref,
                        port,
                        request_path,
                        response.trim_end(),
                    );
                    self.set_detail("Run Demo Fixture".to_owned(), body);
                    self.status_line = format!("Demo run completed for {}.", scenario.fixture_id());
                    return Ok(());
                }
                Err(error) => {
                    cancel.store(true, Ordering::Relaxed);
                    let _ = join_probe(probe);
                    if is_bind_conflict_error(&error) {
                        last_bind_error = Some(error);
                        continue;
                    }
                    return Err(error);
                }
            }
        }

        Err(last_bind_error
            .unwrap_or_else(|| "demo run failed before binding a local port".to_owned()))
    }
}

fn detect_dot_path(project_root: &Path) -> Option<PathBuf> {
    let dot_path = project_root.join("app.dot");
    dot_path.is_file().then_some(dot_path)
}

fn count_bundles(project_root: &Path) -> usize {
    let bundles_dir = project_root.join(".dotlanth").join("bundles");
    fs::read_dir(bundles_dir)
        .map(|entries| entries.filter_map(Result::ok).count())
        .unwrap_or(0)
}

fn selected_status(prefix: &str, run: Option<&StoredRun>) -> String {
    run.map(|run| format!("{prefix}: {}", run.run_id))
        .unwrap_or_else(|| prefix.to_owned())
}

fn join_or_none(values: &[String]) -> String {
    if values.is_empty() {
        "none".to_owned()
    } else {
        values.join(", ")
    }
}

fn capability_names(report: &JsonValue, field: &str) -> Vec<String> {
    report[field]
        .as_array()
        .into_iter()
        .flatten()
        .filter_map(|entry| entry.get("capability").and_then(JsonValue::as_str))
        .map(str::to_owned)
        .collect()
}

fn capability_usage(report: &JsonValue) -> Vec<String> {
    report["used"]
        .as_array()
        .into_iter()
        .flatten()
        .filter_map(|entry| {
            let capability = entry.get("capability")?.as_str()?;
            let count = entry.get("count")?.as_u64()?;
            Some(format!("{capability} x{count}"))
        })
        .collect()
}

fn capability_denials(report: &JsonValue) -> Vec<String> {
    report["denied"]
        .as_array()
        .into_iter()
        .flatten()
        .filter_map(|entry| entry.get("capability").and_then(JsonValue::as_str))
        .map(str::to_owned)
        .collect()
}

fn reserve_local_port() -> Result<u16, String> {
    let listener = TcpListener::bind("127.0.0.1:0")
        .map_err(|error| format!("failed to reserve local demo port: {error}"))?;
    listener
        .local_addr()
        .map(|addr| addr.port())
        .map_err(|error| format!("failed to read reserved local demo port: {error}"))
}

fn probe_http_get(port: u16, path: &str, cancel: Arc<AtomicBool>) -> Result<String, String> {
    let deadline = Instant::now() + Duration::from_secs(3);

    loop {
        if cancel.load(Ordering::Relaxed) {
            return Err("http probe cancelled".to_owned());
        }

        match TcpStream::connect(("127.0.0.1", port)) {
            Ok(mut stream) => {
                stream
                    .set_read_timeout(Some(Duration::from_secs(2)))
                    .map_err(|error| format!("failed to set probe read timeout: {error}"))?;
                stream
                    .set_write_timeout(Some(Duration::from_secs(2)))
                    .map_err(|error| format!("failed to set probe write timeout: {error}"))?;
                let request =
                    format!("GET {path} HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n");
                stream
                    .write_all(request.as_bytes())
                    .map_err(|error| format!("failed to write HTTP probe request: {error}"))?;

                let mut response = String::new();
                stream
                    .read_to_string(&mut response)
                    .map_err(|error| format!("failed to read HTTP probe response: {error}"))?;
                return Ok(response);
            }
            Err(error) if Instant::now() < deadline => {
                let _ = error;
                thread::sleep(Duration::from_millis(25));
            }
            Err(error) => {
                return Err(format!(
                    "failed to connect HTTP probe to 127.0.0.1:{port}: {error}"
                ));
            }
        }
    }
}

fn join_probe(probe: thread::JoinHandle<Result<String, String>>) -> Result<String, String> {
    probe
        .join()
        .map_err(|_| "http probe thread panicked".to_owned())?
}

fn is_bind_conflict_error(error: &str) -> bool {
    error.contains("failed to bind http listener")
}

fn next_export_dir(base: PathBuf) -> PathBuf {
    if !base.exists() {
        return base;
    }

    let parent = base
        .parent()
        .map(Path::to_path_buf)
        .unwrap_or_else(|| PathBuf::from("."));
    let stem = base
        .file_name()
        .and_then(|name| name.to_str())
        .unwrap_or("export");

    for suffix in 2.. {
        let candidate = parent.join(format!("{stem}-{suffix}"));
        if !candidate.exists() {
            return candidate;
        }
    }

    unreachable!("loop should always find a free export directory suffix")
}

fn default_fixture_id_for(capability: Capability) -> &'static str {
    match capability {
        Capability::ParseValidate => CapabilityFixture::ParseValidate.default_fixture_id(),
        Capability::RunServe => CapabilityFixture::RunServe.default_fixture_id(),
        Capability::ReplayRecovery => CapabilityFixture::ReplayRecovery.default_fixture_id(),
        Capability::SecurityCapabilities => {
            CapabilityFixture::SecurityCapabilities.default_fixture_id()
        }
        Capability::StateDb => CapabilityFixture::StateDb.default_fixture_id(),
        Capability::ArtifactsInspection => {
            CapabilityFixture::ArtifactsInspection.default_fixture_id()
        }
    }
}

#[cfg(test)]
mod tests {
    use super::{App, default_fixture_id_for, reserve_local_port};
    use crate::tui::fixtures::DemoScenario;
    use crate::tui::model::ActionKind;
    use crate::tui::registry;
    use crate::tui::state::{Capability, Focus, Mode};
    use std::fs;
    use std::net::TcpListener;
    use std::time::{Duration, Instant};
    use tempfile::TempDir;

    #[test]
    fn capability_actions_validate_demo_fixture_with_real_parser() {
        let temp = TempDir::new().expect("temp dir must create");
        let mut app = App::load(temp.path().to_path_buf()).expect("app must load");

        select_action(&mut app, Mode::Demo, Capability::ParseValidate, 0);
        app.execute_current_action()
            .expect("demo validation action must succeed");

        assert_eq!(app.selected_fixture_id, "hello-api");
        assert!(app.detail_title.contains("Validation"));
        assert!(
            app.detail_lines
                .iter()
                .any(|line| line.contains("Runtime capabilities")),
            "detail should show runtime capability context"
        );
        assert!(
            temp.path()
                .join(".dotlanth/demo/fixtures/hello-api/app.dot")
                .is_file()
        );
    }

    #[test]
    fn capability_actions_validate_invalid_fixture_surfaces_real_diagnostic() {
        let temp = TempDir::new().expect("temp dir must create");
        let mut app = App::load(temp.path().to_path_buf()).expect("app must load");

        select_action(&mut app, Mode::Demo, Capability::ParseValidate, 1);
        app.execute_current_action()
            .expect("invalid fixture action should render diagnostics");

        assert_eq!(app.selected_fixture_id, "invalid-parse");
        assert!(
            app.detail_lines
                .iter()
                .any(|line| line.contains("missing required `dot` version directive")),
            "detail should show the real parser diagnostic"
        );
    }

    #[test]
    fn capability_actions_smoke_run_records_demo_run_and_promotes_selection() {
        let temp = TempDir::new().expect("temp dir must create");
        let mut app = App::load(temp.path().to_path_buf()).expect("app must load");

        select_action(&mut app, Mode::Demo, Capability::RunServe, 1);
        app.execute_current_action()
            .expect("smoke run action must succeed");

        assert_eq!(app.selected_fixture_id, "hello-api");
        assert!(
            app.selected_run().is_some(),
            "run should be selected after demo run"
        );
        assert!(
            app.selected_bundle_ref.is_some(),
            "bundle ref should be promoted after demo run"
        );
        assert!(
            app.detail_lines
                .iter()
                .any(|line| line.contains("HTTP/1.1 200 OK")),
            "detail should include the real HTTP probe response"
        );
    }

    #[test]
    fn capability_actions_registry_covers_all_capability_pages() {
        let parse_demo = registry::actions_for(Mode::Demo, Capability::ParseValidate);
        assert!(
            parse_demo
                .iter()
                .any(|action| action.kind == ActionKind::ValidateDemoFixture)
        );

        let run_demo = registry::actions_for(Mode::Demo, Capability::RunServe);
        assert!(
            run_demo
                .iter()
                .any(|action| action.kind == ActionKind::SmokeRun)
        );

        let replay_demo = registry::actions_for(Mode::Demo, Capability::ReplayRecovery);
        assert!(
            replay_demo
                .iter()
                .any(|action| action.kind == ActionKind::ReplaySelected)
        );

        let security_demo = registry::actions_for(Mode::Demo, Capability::SecurityCapabilities);
        assert!(
            security_demo
                .iter()
                .any(|action| action.kind == ActionKind::RunCapabilityDenial)
        );
        assert!(
            security_demo
                .iter()
                .any(|action| action.kind == ActionKind::ShowSecuritySummary)
        );

        let state_demo = registry::actions_for(Mode::Demo, Capability::StateDb);
        assert!(
            state_demo
                .iter()
                .any(|action| action.kind == ActionKind::ViewLogs)
        );

        let artifacts_demo = registry::actions_for(Mode::Demo, Capability::ArtifactsInspection);
        assert!(
            artifacts_demo
                .iter()
                .any(|action| action.kind == ActionKind::InspectSelected)
        );
        assert!(
            artifacts_demo
                .iter()
                .any(|action| action.kind == ActionKind::ExportSelected)
        );
    }

    #[test]
    fn capability_actions_export_selected_is_repeatable() {
        let temp = TempDir::new().expect("temp dir must create");
        let mut app = App::load(temp.path().to_path_buf()).expect("app must load");

        select_action(&mut app, Mode::Demo, Capability::RunServe, 1);
        app.execute_current_action()
            .expect("demo run action must succeed");

        select_action(&mut app, Mode::Demo, Capability::ArtifactsInspection, 1);
        app.execute_current_action()
            .expect("first export should succeed");
        let first_export = app
            .selected_export_dir
            .clone()
            .expect("first export dir must exist");

        app.execute_current_action()
            .expect("second export should also succeed");
        let second_export = app
            .selected_export_dir
            .clone()
            .expect("second export dir must exist");

        assert!(first_export.is_dir());
        assert!(second_export.is_dir());
        assert_ne!(
            first_export, second_export,
            "repeat exports should avoid reusing the same non-empty directory"
        );
    }

    fn select_action(app: &mut App, mode: Mode, capability: Capability, action_index: usize) {
        app.mode = mode;
        app.capability = capability;
        app.actions = registry::actions_for(mode, capability);
        app.action_index = action_index;
        app.focus = Focus::Actions;
        app.selected_fixture_id = default_fixture_id_for(capability).to_owned();
    }

    #[test]
    fn integrated_capability_lab_demo_chain_covers_validate_run_export_and_replay() {
        let temp = TempDir::new().expect("temp dir must create");
        let mut app = App::load(temp.path().to_path_buf()).expect("app must load");

        select_action(&mut app, Mode::Demo, Capability::ParseValidate, 0);
        app.execute_current_action()
            .expect("demo validation action must succeed");

        select_action(&mut app, Mode::Demo, Capability::RunServe, 1);
        app.execute_current_action()
            .expect("demo run action must succeed");
        let first_run_id = app.selected_run_label();
        assert_ne!(first_run_id, "none");

        select_action(&mut app, Mode::Demo, Capability::SecurityCapabilities, 1);
        app.execute_current_action()
            .expect("security summary action must succeed");
        assert!(
            app.detail_lines
                .iter()
                .any(|line| line.contains("Declared:") && line.contains("net.http.listen")),
            "security summary should read capability report evidence"
        );

        select_action(&mut app, Mode::Demo, Capability::ArtifactsInspection, 0);
        app.execute_current_action()
            .expect("inspect action must succeed");
        assert!(
            app.detail_lines
                .iter()
                .any(|line| line.contains(&format!("run_id: {first_run_id}"))),
            "inspect view should render the selected run"
        );

        select_action(&mut app, Mode::Demo, Capability::ArtifactsInspection, 1);
        app.execute_current_action()
            .expect("export action must succeed");
        assert!(
            app.selected_export_dir
                .as_ref()
                .is_some_and(|path| path.is_dir()),
            "export action should materialize a directory"
        );

        select_action(&mut app, Mode::Demo, Capability::ReplayRecovery, 0);
        app.execute_current_action()
            .expect("replay action must succeed");
        assert_ne!(
            app.selected_run_label(),
            first_run_id,
            "replay should promote the newly recorded run"
        );
    }

    #[test]
    fn capability_denial_demo_returns_quickly_on_failure() {
        let temp = TempDir::new().expect("temp dir must create");
        let mut app = App::load(temp.path().to_path_buf()).expect("app must load");

        let start = Instant::now();
        let error = app
            .run_demo_fixture(DemoScenario::CapabilityDenial, "/hello")
            .expect_err("capability denial fixture should fail before serving");
        let elapsed = start.elapsed();

        assert!(
            error.contains("missing required capability `net.http.listen`"),
            "failure should come from the real validator/runtime path"
        );
        assert!(
            elapsed < Duration::from_millis(500),
            "failure should not wait for the full probe timeout: {:?}",
            elapsed
        );
    }

    #[test]
    fn app_load_tolerates_unopenable_dotdb() {
        let temp = TempDir::new().expect("temp dir must create");
        let dotlanth_dir = temp.path().join(".dotlanth");
        fs::create_dir_all(&dotlanth_dir).expect("dotlanth dir must create");
        fs::write(dotlanth_dir.join("dotdb.sqlite"), "not-a-sqlite-db")
            .expect("corrupt db bytes must write");

        let app = App::load(temp.path().to_path_buf()).expect("app should still load");

        assert!(!app.db_available, "corrupt db should degrade gracefully");
        assert!(
            app.status_line.contains("DotDB present but unavailable"),
            "status line should explain the degraded db state"
        );
    }

    #[test]
    fn demo_run_retries_after_bind_conflict() {
        let temp = TempDir::new().expect("temp dir must create");
        let mut app = App::load(temp.path().to_path_buf()).expect("app must load");

        let busy = TcpListener::bind("127.0.0.1:0").expect("busy listener must bind");
        let busy_port = busy.local_addr().expect("busy addr must read").port();
        let retry_port = reserve_local_port().expect("retry port must reserve");
        let mut ports = vec![busy_port, retry_port].into_iter();

        app.run_demo_fixture_with_port_source(DemoScenario::HelloApi, "/hello", || {
            ports
                .next()
                .ok_or_else(|| "expected another demo port".to_owned())
        })
        .expect("demo run should retry after the initial bind conflict");

        assert!(app.selected_run().is_some());
        assert!(
            app.detail_lines
                .iter()
                .any(|line| line.contains("HTTP/1.1 200 OK")),
            "successful retry should still probe the live route"
        );
    }
}
