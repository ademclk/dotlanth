#![forbid(unsafe_code)]

use crate::tui::model::{ScenarioCatalog, ScenarioRecord};
use std::fs;
use std::path::{Path, PathBuf};

pub(crate) const DOTLANTH_DIR_NAME: &str = ".dotlanth";
pub(crate) const DEMO_DIR_NAME: &str = "demo";
pub(crate) const FIXTURES_DIR_NAME: &str = "fixtures";
pub(crate) const EXPORTS_DIR_NAME: &str = "exports";
pub(crate) const METADATA_DIR_NAME: &str = "metadata";
pub(crate) const TMP_DIR_NAME: &str = "tmp";
pub(crate) const SCENARIOS_FILE_NAME: &str = "scenarios.json";

const DOT_FILE_NAME: &str = "app.dot";
const GITIGNORE_FILE_NAME: &str = ".gitignore";
const DOTLANTH_GITIGNORE: &str = ".dotlanth/\n";

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub(crate) enum CapabilityFixture {
    InitBootstrap,
    ParseValidate,
    RunServe,
    ReplayRecovery,
    SecurityCapabilities,
    StateDb,
    ArtifactsInspection,
}

impl CapabilityFixture {
    pub(crate) const fn default_fixture_id(self) -> &'static str {
        match self {
            Self::InitBootstrap => "hello-api",
            Self::ParseValidate => "hello-api",
            Self::RunServe => "hello-api",
            Self::ReplayRecovery => "hello-api",
            Self::SecurityCapabilities => "capability-denial",
            Self::StateDb => "hello-api",
            Self::ArtifactsInspection => "hello-api",
        }
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub(crate) enum DemoScenario {
    HelloApi,
    InvalidParse,
    CapabilityDenial,
}

impl DemoScenario {
    pub(crate) const fn fixture_id(self) -> &'static str {
        match self {
            Self::HelloApi => "hello-api",
            Self::InvalidParse => "invalid-parse",
            Self::CapabilityDenial => "capability-denial",
        }
    }

    fn app_dot_contents(self, port: Option<u16>) -> String {
        let port = port.unwrap_or(8080);
        match self {
            Self::HelloApi => format!(
                r#"dot 0.1

app "hello-api"

allow log
allow net.http.listen

server listen {port}

api "public"
  route GET "/hello"
    respond 200 "Hello from Dotlanth"
  end
end
"#
            ),
            Self::InvalidParse => r#"app "invalid-parse"

allow net.http.listen

api "public"
  route GET "/hello"
    respond 200 "ok"
  end
end
"#
            .to_owned(),
            Self::CapabilityDenial => format!(
                r#"dot 0.1

app "capability-denial"

allow log

server listen {port}

api "public"
  route GET "/hello"
    respond 200 "This should fail validation"
  end
end
"#
            ),
        }
    }
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub(crate) struct DemoWorkspace {
    project_root: PathBuf,
}

impl DemoWorkspace {
    pub(crate) fn new(project_root: &Path) -> Self {
        Self {
            project_root: project_root.to_path_buf(),
        }
    }

    pub(crate) fn dotlanth_root(&self) -> PathBuf {
        self.project_root.join(DOTLANTH_DIR_NAME)
    }

    pub(crate) fn demo_root(&self) -> PathBuf {
        self.dotlanth_root().join(DEMO_DIR_NAME)
    }

    pub(crate) fn fixtures_root(&self) -> PathBuf {
        self.demo_root().join(FIXTURES_DIR_NAME)
    }

    pub(crate) fn exports_root(&self) -> PathBuf {
        self.demo_root().join(EXPORTS_DIR_NAME)
    }

    pub(crate) fn metadata_root(&self) -> PathBuf {
        self.demo_root().join(METADATA_DIR_NAME)
    }

    pub(crate) fn tmp_root(&self) -> PathBuf {
        self.demo_root().join(TMP_DIR_NAME)
    }

    pub(crate) fn metadata_path(&self) -> PathBuf {
        self.metadata_root().join(SCENARIOS_FILE_NAME)
    }

    pub(crate) fn fixture_root(&self, fixture_id: &str) -> PathBuf {
        self.fixtures_root().join(fixture_id)
    }

    pub(crate) fn export_root(&self, export_id: &str) -> PathBuf {
        self.exports_root().join(export_id)
    }

    pub(crate) fn relative_display_path(&self, path: &Path) -> String {
        path.strip_prefix(&self.project_root)
            .map(|relative| relative.display().to_string())
            .unwrap_or_else(|_| path.display().to_string())
    }
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub(crate) struct FixtureManager {
    workspace: DemoWorkspace,
}

impl FixtureManager {
    pub(crate) fn new(project_root: &Path) -> Self {
        Self {
            workspace: DemoWorkspace::new(project_root),
        }
    }

    pub(crate) fn refresh_fixture(&self, scenario: DemoScenario) -> Result<PathBuf, String> {
        self.refresh_fixture_with_port(scenario, None)
    }

    pub(crate) fn refresh_fixture_with_port(
        &self,
        scenario: DemoScenario,
        port: Option<u16>,
    ) -> Result<PathBuf, String> {
        self.ensure_workspace_layout()?;

        let fixture_root = self.workspace.fixture_root(scenario.fixture_id());
        if fixture_root.exists() {
            fs::remove_dir_all(&fixture_root).map_err(|error| {
                format!(
                    "failed to refresh fixture `{}`: {error}",
                    fixture_root.display()
                )
            })?;
        }
        fs::create_dir_all(&fixture_root).map_err(|error| {
            format!(
                "failed to create fixture directory `{}`: {error}",
                fixture_root.display()
            )
        })?;
        let dot_path = fixture_root.join(DOT_FILE_NAME);
        fs::write(&dot_path, scenario.app_dot_contents(port)).map_err(|error| {
            format!(
                "failed to write fixture dot file `{}`: {error}",
                dot_path.display()
            )
        })?;
        fs::write(fixture_root.join(GITIGNORE_FILE_NAME), DOTLANTH_GITIGNORE).map_err(|error| {
            format!(
                "failed to write fixture gitignore `{}`: {error}",
                fixture_root.join(GITIGNORE_FILE_NAME).display()
            )
        })?;

        let mut catalog = self.load_catalog()?;
        let record = scenario_record_mut(&mut catalog, scenario.fixture_id(), &self.workspace);
        record.fixture_root = self.workspace.relative_display_path(&fixture_root);
        self.save_catalog(&catalog)?;
        Ok(dot_path)
    }

    pub(crate) fn update_run_metadata(
        &self,
        fixture_id: &str,
        run_id: &str,
        bundle_ref: &str,
    ) -> Result<(), String> {
        self.ensure_workspace_layout()?;

        let mut catalog = self.load_catalog()?;
        let record = scenario_record_mut(&mut catalog, fixture_id, &self.workspace);
        record.latest_run_id = Some(run_id.to_owned());
        record.latest_bundle_ref = Some(bundle_ref.to_owned());
        self.save_catalog(&catalog)
    }

    pub(crate) fn update_export_metadata(
        &self,
        fixture_id: &str,
        export_dir: &Path,
    ) -> Result<(), String> {
        self.ensure_workspace_layout()?;

        let mut catalog = self.load_catalog()?;
        let record = scenario_record_mut(&mut catalog, fixture_id, &self.workspace);
        record.latest_export_dir = Some(self.workspace.relative_display_path(export_dir));
        self.save_catalog(&catalog)
    }

    pub(crate) fn load_catalog(&self) -> Result<ScenarioCatalog, String> {
        if !self.workspace.metadata_path().is_file() {
            return Ok(ScenarioCatalog::default());
        }

        let raw = fs::read(self.workspace.metadata_path()).map_err(|error| {
            format!(
                "failed to read scenario metadata `{}`: {error}",
                self.workspace.metadata_path().display()
            )
        })?;

        match serde_json::from_slice(&raw) {
            Ok(catalog) => Ok(catalog),
            Err(_) => Ok(ScenarioCatalog::default()),
        }
    }

    fn ensure_workspace_layout(&self) -> Result<(), String> {
        for dir in [
            self.workspace.demo_root(),
            self.workspace.fixtures_root(),
            self.workspace.exports_root(),
            self.workspace.metadata_root(),
            self.workspace.tmp_root(),
        ] {
            fs::create_dir_all(&dir).map_err(|error| {
                format!(
                    "failed to create demo workspace directory `{}`: {error}",
                    dir.display()
                )
            })?;
        }
        Ok(())
    }

    fn save_catalog(&self, catalog: &ScenarioCatalog) -> Result<(), String> {
        let raw = serde_json::to_vec_pretty(catalog)
            .map_err(|error| format!("failed to serialize scenario metadata: {error}"))?;
        let metadata_path = self.workspace.metadata_path();
        let temp_path = metadata_path.with_extension("json.tmp");
        fs::write(&temp_path, raw).map_err(|error| {
            format!(
                "failed to write scenario metadata `{}`: {error}",
                temp_path.display()
            )
        })?;
        #[cfg(windows)]
        if metadata_path.exists() {
            fs::remove_file(&metadata_path).map_err(|error| {
                format!(
                    "failed to replace scenario metadata `{}`: {error}",
                    metadata_path.display()
                )
            })?;
        }
        fs::rename(&temp_path, &metadata_path).map_err(|error| {
            format!(
                "failed to finalize scenario metadata `{}`: {error}",
                metadata_path.display()
            )
        })
    }
}

fn scenario_record_mut<'a>(
    catalog: &'a mut ScenarioCatalog,
    fixture_id: &str,
    workspace: &DemoWorkspace,
) -> &'a mut ScenarioRecord {
    catalog
        .scenarios
        .entry(fixture_id.to_owned())
        .or_insert_with(|| ScenarioRecord {
            fixture_root: workspace.relative_display_path(&workspace.fixture_root(fixture_id)),
            ..ScenarioRecord::default()
        })
}

#[cfg(test)]
mod tests {
    use super::{
        CapabilityFixture, DEMO_DIR_NAME, DemoScenario, DemoWorkspace, EXPORTS_DIR_NAME,
        FIXTURES_DIR_NAME, FixtureManager, METADATA_DIR_NAME, TMP_DIR_NAME,
    };
    use serde_json::Value as JsonValue;
    use std::fs;
    use std::path::Path;
    use tempfile::TempDir;

    #[test]
    fn workspace_paths_are_stable() {
        let workspace = DemoWorkspace::new(Path::new("/tmp/dotlanth"));

        assert_eq!(DEMO_DIR_NAME, "demo");
        assert_eq!(FIXTURES_DIR_NAME, "fixtures");
        assert_eq!(EXPORTS_DIR_NAME, "exports");
        assert_eq!(METADATA_DIR_NAME, "metadata");
        assert_eq!(
            workspace.demo_root(),
            Path::new("/tmp/dotlanth/.dotlanth/demo")
        );
        assert_eq!(
            workspace.metadata_path(),
            Path::new("/tmp/dotlanth/.dotlanth/demo/metadata/scenarios.json")
        );
        assert_eq!(
            workspace.fixture_root("hello-api"),
            Path::new("/tmp/dotlanth/.dotlanth/demo/fixtures/hello-api")
        );
        assert_eq!(
            workspace.export_root("hello-api-run_123"),
            Path::new("/tmp/dotlanth/.dotlanth/demo/exports/hello-api-run_123")
        );
        assert_eq!(TMP_DIR_NAME, "tmp");
    }

    #[test]
    fn capabilities_map_to_default_fixture_ids() {
        assert_eq!(
            CapabilityFixture::InitBootstrap.default_fixture_id(),
            "hello-api"
        );
        assert_eq!(
            CapabilityFixture::ParseValidate.default_fixture_id(),
            "hello-api"
        );
        assert_eq!(
            CapabilityFixture::RunServe.default_fixture_id(),
            "hello-api"
        );
        assert_eq!(
            CapabilityFixture::ReplayRecovery.default_fixture_id(),
            "hello-api"
        );
        assert_eq!(
            CapabilityFixture::SecurityCapabilities.default_fixture_id(),
            "capability-denial"
        );
        assert_eq!(CapabilityFixture::StateDb.default_fixture_id(), "hello-api");
        assert_eq!(
            CapabilityFixture::ArtifactsInspection.default_fixture_id(),
            "hello-api"
        );
    }

    #[test]
    fn fixture_manager_creates_stable_roots_and_scenario_metadata() {
        let temp = TempDir::new().expect("temp dir must create");
        let manager = FixtureManager::new(temp.path());
        let workspace = DemoWorkspace::new(temp.path());

        manager
            .refresh_fixture(DemoScenario::HelloApi)
            .expect("hello fixture must refresh");
        manager
            .refresh_fixture(DemoScenario::InvalidParse)
            .expect("invalid fixture must refresh");
        manager
            .refresh_fixture(DemoScenario::CapabilityDenial)
            .expect("denial fixture must refresh");

        let export_dir = workspace.export_root("hello-api-run_hello");
        fs::create_dir_all(&export_dir).expect("export dir must create");
        manager
            .update_run_metadata("hello-api", "run_hello", ".dotlanth/bundles/run_hello")
            .expect("run metadata must persist");
        manager
            .update_export_metadata("hello-api", &export_dir)
            .expect("export metadata must persist");

        assert!(
            workspace
                .fixture_root("hello-api")
                .join("app.dot")
                .is_file()
        );
        assert!(
            workspace
                .fixture_root("invalid-parse")
                .join("app.dot")
                .is_file()
        );
        assert!(
            workspace
                .fixture_root("capability-denial")
                .join("app.dot")
                .is_file()
        );

        let raw: JsonValue = serde_json::from_slice(
            &fs::read(workspace.metadata_path()).expect("metadata file must exist"),
        )
        .expect("metadata json must parse");
        assert_eq!(raw["schema_version"], "1");
        assert_eq!(
            raw["scenarios"]["hello-api"]["fixture_root"],
            ".dotlanth/demo/fixtures/hello-api"
        );
        assert_eq!(raw["scenarios"]["hello-api"]["latest_run_id"], "run_hello");
        assert_eq!(
            raw["scenarios"]["hello-api"]["latest_bundle_ref"],
            ".dotlanth/bundles/run_hello"
        );
        assert_eq!(
            raw["scenarios"]["hello-api"]["latest_export_dir"],
            ".dotlanth/demo/exports/hello-api-run_hello"
        );
    }

    #[test]
    fn fixture_manager_refreshing_one_fixture_preserves_other_fixture_files() {
        let temp = TempDir::new().expect("temp dir must create");
        let manager = FixtureManager::new(temp.path());
        let workspace = DemoWorkspace::new(temp.path());

        manager
            .refresh_fixture(DemoScenario::HelloApi)
            .expect("hello fixture must refresh");
        manager
            .refresh_fixture(DemoScenario::CapabilityDenial)
            .expect("denial fixture must refresh");

        let hello_owned_file = workspace.fixture_root("hello-api").join("owned.txt");
        let denial_sentinel = workspace
            .fixture_root("capability-denial")
            .join("keep-me.txt");
        fs::write(&hello_owned_file, "hello").expect("hello sentinel must write");
        fs::write(&denial_sentinel, "deny").expect("denial sentinel must write");

        manager
            .refresh_fixture(DemoScenario::HelloApi)
            .expect("hello fixture must refresh again");

        assert!(
            !hello_owned_file.exists(),
            "refreshed fixture should replace its own directory"
        );
        assert!(
            denial_sentinel.is_file(),
            "refreshing hello fixture must not delete other fixture files"
        );
    }

    #[test]
    fn fixture_manager_recovers_from_corrupt_metadata_file() {
        let temp = TempDir::new().expect("temp dir must create");
        let manager = FixtureManager::new(temp.path());
        let workspace = DemoWorkspace::new(temp.path());

        fs::create_dir_all(workspace.metadata_root()).expect("metadata dir must create");
        fs::write(workspace.metadata_path(), "{not-json").expect("corrupt metadata must write");

        manager
            .refresh_fixture(DemoScenario::HelloApi)
            .expect("fixture refresh should repair corrupt metadata");

        let raw: JsonValue = serde_json::from_slice(
            &fs::read(workspace.metadata_path()).expect("metadata file must exist"),
        )
        .expect("repaired metadata json must parse");
        assert_eq!(raw["schema_version"], "1");
        assert_eq!(
            raw["scenarios"]["hello-api"]["fixture_root"],
            ".dotlanth/demo/fixtures/hello-api"
        );
    }
}
