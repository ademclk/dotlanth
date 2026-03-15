#![forbid(unsafe_code)]

use dot_artifacts::{
    BundleSection, BundleWriter, CAPABILITY_REPORT_FILE, STATE_DIFF_FILE, SectionStatus, TRACE_FILE,
};
use dot_db::{DotDb, StateKvEntry};
use dot_ops::{
    OpsHost, RecordMode, RuntimeEvent, SYSCALL_NET_HTTP_SERVE, SourceRef, SourceSpan, SyscallId,
};
use dot_rt::{DeterminismMode, RuntimeContext};
use dot_sec::{Capability, CapabilitySet};
use dot_vm::{
    EventSink, Instruction, Reg, SyscallAttemptEvent, SyscallResultEvent, Value as VmValue, Vm,
    VmError, VmEvent, validate_strict_determinism,
};
use serde_json::{Map, Value as JsonValue, json};
use sha2::{Digest, Sha256};
use std::cell::RefCell;
use std::collections::{BTreeMap, BTreeSet};
use std::fs::OpenOptions;
use std::io::{BufWriter, Write};
use std::path::{Path, PathBuf};
use std::rc::Rc;

const DOTLANTH_DIR: &str = ".dotlanth";
const BUNDLES_DIR: &str = "bundles";
const DETERMINISM_BUDGET_STATE_NAMESPACE: &str = "runtime";
const DETERMINISM_BUDGET_STATE_KEY: &str = "determinism_budget.v26_3";

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub(crate) enum RunAnnouncement {
    #[default]
    Print,
    Silent,
}

#[derive(Debug, Default)]
pub(crate) struct RunOptions {
    pub(crate) file: Option<PathBuf>,
    pub(crate) determinism: DeterminismMode,
    pub(crate) max_requests: Option<u64>,
    pub(crate) announcement: RunAnnouncement,
}

pub(crate) fn run(options: RunOptions) -> Result<(), String> {
    let resolved = match options.file {
        Some(file) => Some((file.clone(), project_root_for_dot_path(&file)?)),
        None => None,
    };
    run_resolved_options(
        resolved,
        options.determinism,
        options.max_requests,
        options.announcement,
    )
}

pub(crate) fn run_resolved_with_announcement(
    dot_path: PathBuf,
    project_root: PathBuf,
    determinism: DeterminismMode,
    max_requests: Option<u64>,
    announcement: RunAnnouncement,
) -> Result<(), String> {
    run_resolved_options(
        Some((dot_path, project_root)),
        determinism,
        max_requests,
        announcement,
    )
}

fn run_resolved_options(
    resolved: Option<(PathBuf, PathBuf)>,
    determinism: DeterminismMode,
    max_requests: Option<u64>,
    announcement: RunAnnouncement,
) -> Result<(), String> {
    let runtime = tokio::runtime::Builder::new_current_thread()
        .enable_all()
        .build()
        .map_err(|error| format!("failed to initialize tokio runtime: {error}"))?;
    runtime.block_on(run_async(resolved, determinism, max_requests, announcement))
}

async fn run_async(
    resolved: Option<(PathBuf, PathBuf)>,
    determinism: DeterminismMode,
    max_requests: Option<u64>,
    announcement: RunAnnouncement,
) -> Result<(), String> {
    let (dot_path, project_root) = match resolved {
        Some((dot_path, project_root)) => (dot_path, project_root),
        None => {
            let dot_path = resolve_dot_file(None)?;
            let project_root = project_root_for_dot_path(&dot_path)?;
            (dot_path, project_root)
        }
    };
    let db =
        DotDb::open_in(&project_root).map_err(|error| format!("failed to open DotDB: {error}"))?;
    let mut host = OpsHost::new(CapabilitySet::empty(), db)
        .map_err(|error| format!("failed to initialize runtime host: {error}"))?;
    host.set_determinism_mode(determinism.as_str())
        .map_err(|error| error.to_string())?;
    host.set_record_mode(RecordMode::Record);

    let run_id = host.run_id().to_owned();
    let bundle_dir = bundle_dir_for_run(&project_root, &run_id);
    let mut bundle = match BundleWriter::new(&bundle_dir, &run_id) {
        Ok(bundle) => bundle,
        Err(error) => {
            let primary = format!(
                "failed to initialize artifact bundle `{}`: {error}",
                bundle_dir.display()
            );
            let finalize_error = host
                .finalize_run(dot_db::RunStatus::Failed)
                .err()
                .map(|error| error.to_string());
            return Err(combine_errors(primary, [finalize_error]));
        }
    };
    if let Err(error) = bundle.set_determinism_mode(determinism.as_str()) {
        let finalize_error = host
            .finalize_run(dot_db::RunStatus::Failed)
            .err()
            .map(|host_error| host_error.to_string());
        return Err(combine_errors(
            format!("failed to record determinism mode in artifact bundle: {error}"),
            [finalize_error],
        ));
    }

    let trace_path = bundle.staging_dir().join(TRACE_FILE);
    let mut trace = match TraceRecorder::new(&run_id, &trace_path) {
        Ok(trace) => trace,
        Err(error) => {
            let finalize_error = host
                .finalize_run(dot_db::RunStatus::Failed)
                .err()
                .map(|error| error.to_string());
            return Err(combine_errors(error, [finalize_error]));
        }
    };
    host.set_runtime_event_forwarder(trace.runtime_event_forwarder());

    trace.record_run_start(&dot_path);
    let dot_source = match read_dot_source(&dot_path) {
        Ok(dot_source) => dot_source,
        Err(error) => {
            return finish_pre_execution_failure(
                &mut host,
                &mut bundle,
                &mut trace,
                &run_id,
                error,
            );
        }
    };
    if let Err(error) = bundle.snapshot_entry_dot_bytes(dot_source.as_bytes()) {
        return finish_pre_execution_failure(
            &mut host,
            &mut bundle,
            &mut trace,
            &run_id,
            format!(
                "failed to snapshot `{}` into bundle: {error}",
                dot_path.display()
            ),
        );
    }
    let document = match dot_dsl::parse_and_validate(&dot_source, &dot_path) {
        Ok(document) => document,
        Err(error) => {
            return finish_pre_execution_failure(
                &mut host,
                &mut bundle,
                &mut trace,
                &run_id,
                error.to_string(),
            );
        }
    };
    let ctx = match build_runtime_context(&document, determinism) {
        Ok(ctx) => ctx,
        Err(error) => {
            return finish_pre_execution_failure(
                &mut host,
                &mut bundle,
                &mut trace,
                &run_id,
                error.to_string(),
            );
        }
    };
    host.set_capabilities(ctx.capabilities().clone());
    let before_state = host
        .state_snapshot()
        .map_err(|error| format!("failed to capture state snapshot before execution: {error}"));
    let execution_result = run_vm_execution(
        &mut host,
        &ctx,
        &document,
        max_requests,
        &run_id,
        &mut trace,
        announcement,
    )
    .await;

    let budget_state_result = persist_determinism_budget_state(&mut host, &trace);
    let after_state = host
        .state_snapshot()
        .map_err(|error| format!("failed to capture state snapshot after execution: {error}"))
        .and_then(|snapshot| {
            budget_state_result
                .as_ref()
                .map(|_| snapshot)
                .map_err(Clone::clone)
        });
    let state_diff_result = write_state_diff(&mut bundle, before_state, after_state);
    let capability_report_result = write_capability_report(&mut bundle, &ctx, &trace);
    let determinism_report_result =
        write_determinism_report(&mut bundle, determinism.as_str(), &trace);
    let pre_bundle_status = pre_bundle_status(
        &execution_result,
        &state_diff_result,
        &determinism_report_result,
        &capability_report_result,
    );
    let finalize_result = host
        .finalize_run(pre_bundle_status)
        .map_err(|error| error.to_string());
    let finalize_recovery_result = if finalize_result.is_err() {
        Some(
            host.finalize_run(dot_db::RunStatus::Failed)
                .map_err(|error| error.to_string()),
        )
    } else {
        None
    };

    let trace_status = trace_finish_status(
        &execution_result,
        &state_diff_result,
        &determinism_report_result,
        &capability_report_result,
        &finalize_result,
        finalize_recovery_result.as_ref(),
    );
    let trace_error = summarize_errors([
        execution_result.as_ref().err().cloned(),
        state_diff_result.as_ref().err().cloned(),
        determinism_report_result.as_ref().err().cloned(),
        capability_report_result.as_ref().err().cloned(),
        finalize_result.as_ref().err().cloned(),
        finalize_recovery_result
            .as_ref()
            .and_then(|result| result.as_ref().err())
            .cloned(),
    ]);
    trace.record_run_finish(trace_status, trace_error.as_deref());

    let determinism_metadata_result = write_bundle_determinism_metadata(&mut bundle, &trace);
    let bundle_result = finalize_bundle(&mut host, &mut bundle, &trace, &run_id);
    let mut errors = Vec::new();

    if let Err(error) = execution_result {
        errors.push(error);
    }
    if let Err(error) = state_diff_result {
        errors.push(error);
    }
    if let Err(error) = determinism_report_result {
        errors.push(error);
    }
    if let Err(error) = capability_report_result {
        errors.push(error);
    }
    if let Err(error) = determinism_metadata_result {
        errors.push(error);
    }
    if let Err(error) = bundle_result {
        errors.push(error);
    }
    if let Err(error) = finalize_result {
        errors.push(error);
    }
    if let Some(Err(error)) = finalize_recovery_result {
        errors.push(error);
    }

    match errors.split_first() {
        None => Ok(()),
        Some((primary, extras)) => Err(combine_errors(
            primary.clone(),
            extras.iter().cloned().map(Some),
        )),
    }
}

fn build_runtime_context(
    document: &dot_dsl::Document,
    determinism: DeterminismMode,
) -> Result<RuntimeContext, dot_sec::UnknownCapabilityError> {
    RuntimeContext::from_dot_dsl_with_mode(document, determinism)
}

fn finish_pre_execution_failure(
    host: &mut OpsHost,
    bundle: &mut BundleWriter,
    trace: &mut TraceRecorder,
    run_id: &str,
    execution_error: String,
) -> Result<(), String> {
    let execution_result = Err(execution_error.clone());
    let state_diff_result = bundle
        .mark_section_unavailable(
            BundleSection::StateDiff,
            "execution_not_started",
            "state diff unavailable because execution did not start",
        )
        .map_err(|error| format!("failed to mark state_diff.json as unavailable: {error}"));
    let capability_report_result = bundle
        .mark_section_unavailable(
            BundleSection::CapabilityReport,
            "execution_not_started",
            "capability report unavailable because execution did not start",
        )
        .map_err(|error| format!("failed to mark capability_report.json as unavailable: {error}"));
    let determinism_mode = bundle.manifest().determinism_mode.clone();
    let determinism_report_result = write_determinism_report(bundle, &determinism_mode, trace);
    let finalize_result = host
        .finalize_run(dot_db::RunStatus::Failed)
        .map_err(|error| error.to_string());
    let trace_status = trace_finish_status(
        &execution_result,
        &state_diff_result,
        &determinism_report_result,
        &capability_report_result,
        &finalize_result,
        None,
    );
    let trace_error = summarize_errors([
        Some(execution_error.clone()),
        state_diff_result.as_ref().err().cloned(),
        determinism_report_result.as_ref().err().cloned(),
        capability_report_result.as_ref().err().cloned(),
        finalize_result.as_ref().err().cloned(),
    ]);
    trace.record_run_finish(trace_status, trace_error.as_deref());

    let determinism_metadata_result = write_bundle_determinism_metadata(bundle, trace);
    let bundle_result = finalize_bundle(host, bundle, trace, run_id);
    let mut errors = vec![execution_error];
    if let Err(error) = state_diff_result {
        errors.push(error);
    }
    if let Err(error) = determinism_report_result {
        errors.push(error);
    }
    if let Err(error) = capability_report_result {
        errors.push(error);
    }
    if let Err(error) = determinism_metadata_result {
        errors.push(error);
    }
    if let Err(error) = bundle_result {
        errors.push(error);
    }
    if let Err(error) = finalize_result {
        errors.push(error);
    }

    let (primary, extras) = errors
        .split_first()
        .expect("pre-execution failure should always include a primary error");
    Err(combine_errors(
        primary.clone(),
        extras.iter().cloned().map(Some),
    ))
}

async fn run_vm_execution(
    host: &mut OpsHost,
    ctx: &RuntimeContext,
    document: &dot_dsl::Document,
    max_requests: Option<u64>,
    run_id: &str,
    trace: &mut TraceRecorder,
    announcement: RunAnnouncement,
) -> Result<(), String> {
    let program = build_program(document, max_requests)?;
    validate_program_determinism(ctx, &program, trace)?;

    host.configure_http_from_document(document)
        .map_err(|error| error.to_string())?;

    let addr = host
        .http_addr()
        .ok_or_else(|| "http listener did not report a local address".to_owned())?;
    if matches!(announcement, RunAnnouncement::Print) {
        println!("run {run_id} listening on http://{addr}");
    }

    let mut vm = Vm::new(program);
    vm.run_with_policy_host_and_events(
        ctx.capabilities(),
        ctx.determinism_mode() == DeterminismMode::Strict,
        host,
        trace,
    )
    .await
    .map_err(format_vm_error)
}

fn build_program(
    document: &dot_dsl::Document,
    max_requests: Option<u64>,
) -> Result<Vec<Instruction>, String> {
    let mut program = Vec::new();

    let args = if let Some(max_requests) = max_requests {
        let max_i64 = i64::try_from(max_requests)
            .map_err(|_| "max_requests must fit within a signed 64-bit integer".to_owned())?;
        program.push(Instruction::LoadConst {
            dst: Reg(0),
            value: VmValue::I64(max_i64),
        });
        vec![Reg(0)]
    } else {
        Vec::new()
    };

    let source = document
        .server
        .as_ref()
        .map(|server| SourceRef::with_span_and_path(SourceSpan::from(server.span), "server"));

    program.push(Instruction::Syscall {
        id: SYSCALL_NET_HTTP_SERVE,
        args,
        results: Vec::new(),
        source,
    });
    program.push(Instruction::Halt);

    Ok(program)
}

fn format_vm_error(error: VmError) -> String {
    match error {
        VmError::SyscallFailed { id, error } => {
            format!("syscall {} failed: {}", id.0, error.message())
        }
        other => format!("runtime error: {other:?}"),
    }
}

fn validate_program_determinism(
    ctx: &RuntimeContext,
    program: &[Instruction],
    trace: &mut TraceRecorder,
) -> Result<(), String> {
    if ctx.determinism_mode() != DeterminismMode::Strict {
        return Ok(());
    }

    match validate_strict_determinism(program) {
        Ok(()) => Ok(()),
        Err(error) => {
            trace.record_strict_validation_denial(program, &error);
            Err(error.to_string())
        }
    }
}

fn write_determinism_report(
    bundle: &mut BundleWriter,
    determinism_mode: &str,
    trace: &TraceRecorder,
) -> Result<(), String> {
    let eligibility = build_replay_eligibility_json(bundle);
    match bundle.write_determinism_report_json(&build_determinism_report_json(
        determinism_mode,
        eligibility,
        trace.determinism_budget(),
        &trace.determinism_violations(),
    )) {
        Ok(()) => Ok(()),
        Err(error) => {
            let message = format!("failed to write determinism_report.json: {error}");
            match bundle.mark_section_error(
                BundleSection::DeterminismReport,
                "artifact_write_failed",
                &message,
            ) {
                Ok(()) => Err(message),
                Err(marker_error) => Err(combine_errors(
                    message,
                    [Some(format!(
                        "failed to mark determinism_report.json as errored: {marker_error}"
                    ))],
                )),
            }
        }
    }
}

fn write_capability_report(
    bundle: &mut BundleWriter,
    ctx: &RuntimeContext,
    trace: &TraceRecorder,
) -> Result<(), String> {
    match bundle
        .write_capability_report_json(&build_capability_report_json(ctx, trace.capability_usage()))
    {
        Ok(()) => Ok(()),
        Err(error) => {
            let message = format!("failed to write capability_report.json: {error}");
            match bundle.mark_section_error(
                BundleSection::CapabilityReport,
                "artifact_write_failed",
                &message,
            ) {
                Ok(()) => Err(message),
                Err(marker_error) => Err(combine_errors(
                    message,
                    [Some(format!(
                        "failed to mark capability_report.json as errored: {marker_error}"
                    ))],
                )),
            }
        }
    }
}

fn write_state_diff(
    bundle: &mut BundleWriter,
    before_state: Result<Vec<StateKvEntry>, String>,
    after_state: Result<Vec<StateKvEntry>, String>,
) -> Result<(), String> {
    let state_diff = match (before_state, after_state) {
        (Ok(before_state), Ok(after_state)) => build_state_diff(&before_state, &after_state),
        (Err(error), _) | (_, Err(error)) => {
            return mark_state_diff_error(bundle, &error);
        }
    };

    match bundle.write_state_diff_json(&state_diff) {
        Ok(()) => Ok(()),
        Err(error) => {
            mark_state_diff_error(bundle, &format!("failed to write state_diff.json: {error}"))
        }
    }
}

fn mark_state_diff_error(bundle: &mut BundleWriter, message: &str) -> Result<(), String> {
    match bundle.mark_section_error(BundleSection::StateDiff, "state_capture_failed", message) {
        Ok(()) => Err(message.to_owned()),
        Err(marker_error) => Err(combine_errors(
            message.to_owned(),
            [Some(format!(
                "failed to mark state_diff.json as errored: {marker_error}"
            ))],
        )),
    }
}

fn build_state_diff(before: &[StateKvEntry], after: &[StateKvEntry]) -> JsonValue {
    let before_map = before
        .iter()
        .map(|entry| {
            (
                (entry.namespace.clone(), entry.key.clone()),
                entry.value.clone(),
            )
        })
        .collect::<BTreeMap<_, _>>();
    let after_map = after
        .iter()
        .map(|entry| {
            (
                (entry.namespace.clone(), entry.key.clone()),
                entry.value.clone(),
            )
        })
        .collect::<BTreeMap<_, _>>();
    let mut keys = BTreeSet::new();
    keys.extend(before_map.keys().cloned());
    keys.extend(after_map.keys().cloned());

    let mut changes = Vec::new();
    for (namespace, key) in keys {
        match (
            before_map.get(&(namespace.clone(), key.clone())),
            after_map.get(&(namespace.clone(), key.clone())),
        ) {
            (Some(previous_value), Some(value)) if previous_value == value => {}
            (Some(previous_value), Some(value)) => changes.push(json!({
                "namespace": namespace,
                "key": key,
                "change": "updated",
                "previous_value": previous_value,
                "value": value,
            })),
            (Some(previous_value), None) => changes.push(json!({
                "namespace": namespace,
                "key": key,
                "change": "removed",
                "previous_value": previous_value,
            })),
            (None, Some(value)) => changes.push(json!({
                "namespace": namespace,
                "key": key,
                "change": "added",
                "value": value,
            })),
            (None, None) => {}
        }
    }

    json!({
        "schema_version": "1",
        "scope": "state_kv",
        "changes": changes,
    })
}

fn finalize_bundle(
    host: &mut OpsHost,
    bundle: &mut BundleWriter,
    trace: &TraceRecorder,
    run_id: &str,
) -> Result<(), String> {
    trace.flush()?;
    bundle
        .commit_existing_trace_jsonl()
        .map_err(|error| format!("failed to finalize trace.jsonl: {error}"))?;
    bundle
        .finalize()
        .map_err(|error| format!("failed to finalize artifact bundle: {error}"))?;
    index_artifact_bundle(host, bundle.bundle_dir(), run_id)
}

fn bundle_dir_for_run(project_root: &Path, run_id: &str) -> PathBuf {
    project_root
        .join(DOTLANTH_DIR)
        .join(BUNDLES_DIR)
        .join(run_id)
}

fn bundle_ref_for_run(run_id: &str) -> String {
    format!("{DOTLANTH_DIR}/{BUNDLES_DIR}/{run_id}")
}

fn index_artifact_bundle(
    host: &mut OpsHost,
    bundle_dir: &Path,
    run_id: &str,
) -> Result<(), String> {
    let manifest_path = bundle_dir.join("manifest.json");
    let manifest_bytes = std::fs::read(&manifest_path).map_err(|error| {
        format!(
            "failed to read `{}` for indexing: {error}",
            manifest_path.display()
        )
    })?;
    host.index_artifact_bundle(
        &bundle_ref_for_run(run_id),
        &sha256_hex(&manifest_bytes),
        manifest_bytes.len() as u64,
    )
    .map_err(|error| error.to_string())
}

fn project_root_for_dot_path(path: &Path) -> Result<PathBuf, String> {
    if let Some(parent) = path
        .parent()
        .filter(|parent| !parent.as_os_str().is_empty())
    {
        if parent.is_absolute() {
            return Ok(parent.to_path_buf());
        }

        let cwd = std::env::current_dir()
            .map_err(|error| format!("failed to read current directory: {error}"))?;
        return Ok(cwd.join(parent));
    }

    std::env::current_dir().map_err(|error| format!("failed to read current directory: {error}"))
}

fn read_dot_source(path: &Path) -> Result<String, String> {
    std::fs::read_to_string(path)
        .map_err(|error| format!("failed to read `{}`: {error}", path.display()))
}

fn combine_errors(primary: String, extras: impl IntoIterator<Item = Option<String>>) -> String {
    let extras = extras.into_iter().flatten().collect::<Vec<_>>();
    if extras.is_empty() {
        primary
    } else {
        format!("{primary}; additionally: {}", extras.join("; "))
    }
}

fn summarize_errors(errors: impl IntoIterator<Item = Option<String>>) -> Option<String> {
    let errors = errors.into_iter().flatten().collect::<Vec<_>>();
    let (primary, extras) = errors.split_first()?;
    Some(combine_errors(
        primary.clone(),
        extras.iter().cloned().map(Some),
    ))
}

fn pre_bundle_status(
    execution_result: &Result<(), String>,
    state_diff_result: &Result<(), String>,
    determinism_report_result: &Result<(), String>,
    capability_report_result: &Result<(), String>,
) -> dot_db::RunStatus {
    status_from_success(
        execution_result.is_ok()
            && state_diff_result.is_ok()
            && determinism_report_result.is_ok()
            && capability_report_result.is_ok(),
    )
}

fn trace_finish_status(
    execution_result: &Result<(), String>,
    state_diff_result: &Result<(), String>,
    determinism_report_result: &Result<(), String>,
    capability_report_result: &Result<(), String>,
    finalize_result: &Result<(), String>,
    finalize_recovery_result: Option<&Result<(), String>>,
) -> dot_db::RunStatus {
    status_from_success(
        execution_result.is_ok()
            && state_diff_result.is_ok()
            && determinism_report_result.is_ok()
            && capability_report_result.is_ok()
            && finalize_result.is_ok()
            && finalize_recovery_result.is_none_or(Result::is_ok),
    )
}

fn status_from_success(success: bool) -> dot_db::RunStatus {
    if success {
        dot_db::RunStatus::Succeeded
    } else {
        dot_db::RunStatus::Failed
    }
}

fn sha256_hex(bytes: &[u8]) -> String {
    let mut hasher = Sha256::new();
    hasher.update(bytes);
    format!("{:x}", hasher.finalize())
}

#[derive(Clone, Debug)]
struct TraceRecorder {
    inner: Rc<RefCell<TraceRecorderState>>,
}

#[derive(Debug)]
struct TraceRecorderState {
    run_id: String,
    next_seq: u64,
    writer: BufWriter<std::fs::File>,
    error: Option<String>,
    determinism_budget: DeterminismBudgetCounters,
    determinism_violations: DeterminismViolationAccumulator,
    capability_usage: CapabilityUsageAccumulator,
}

impl TraceRecorder {
    fn new(run_id: &str, path: &Path) -> Result<Self, String> {
        if let Some(parent) = path.parent() {
            std::fs::create_dir_all(parent).map_err(|error| {
                format!(
                    "failed to create trace directory `{}`: {error}",
                    parent.display()
                )
            })?;
        }
        let file = OpenOptions::new()
            .create(true)
            .truncate(true)
            .write(true)
            .open(path)
            .map_err(|error| format!("failed to open trace file `{}`: {error}", path.display()))?;
        Ok(Self {
            inner: Rc::new(RefCell::new(TraceRecorderState {
                run_id: run_id.to_owned(),
                next_seq: 0,
                writer: BufWriter::new(file),
                error: None,
                determinism_budget: DeterminismBudgetCounters::default(),
                determinism_violations: DeterminismViolationAccumulator::default(),
                capability_usage: CapabilityUsageAccumulator::default(),
            })),
        })
    }

    fn runtime_event_forwarder(&self) -> impl FnMut(RuntimeEvent) + 'static {
        let mut recorder = self.clone();
        move |event| recorder.record_runtime_event(event)
    }

    fn flush(&self) -> Result<(), String> {
        self.inner.borrow_mut().flush()
    }

    fn capability_usage(&self) -> CapabilityUsageAccumulator {
        self.inner.borrow().capability_usage.clone()
    }

    fn determinism_budget(&self) -> DeterminismBudgetCounters {
        self.inner.borrow().determinism_budget
    }

    fn determinism_violations(&self) -> DeterminismViolationAccumulator {
        self.inner.borrow().determinism_violations.clone()
    }

    fn with_state(&mut self, op: impl FnOnce(&mut TraceRecorderState)) {
        op(&mut self.inner.borrow_mut());
    }

    fn new_payload(&self) -> Map<String, JsonValue> {
        Map::new()
    }

    fn record_run_start(&mut self, entry_dot: &Path) {
        let mut payload = self.new_payload();
        payload.insert(
            "entry_dot".to_owned(),
            json!(entry_dot.as_os_str().to_string_lossy().to_string()),
        );
        self.with_state(|state| state.push("run.start", payload, None));
    }

    fn record_run_finish(&mut self, status: dot_db::RunStatus, error: Option<&str>) {
        let mut payload = self.new_payload();
        payload.insert("status".to_owned(), json!(status.to_string()));
        if let Some(error) = error {
            payload.insert("error".to_owned(), json!(error));
        }
        self.with_state(|state| state.push("run.finish", payload, None));
    }

    fn record_syscall_attempt(&mut self, event: SyscallAttemptEvent) {
        let mut payload = self.new_payload();
        payload.insert("syscall_id".to_owned(), json!(event.id.0));
        payload.insert("syscall".to_owned(), json!(syscall_name(event.id)));
        insert_syscall_trace_facts(&mut payload, event.id);
        payload.insert(
            "args".to_owned(),
            JsonValue::Array(event.args.iter().map(vm_value_to_json).collect()),
        );
        self.with_state(|state| state.push("syscall.attempt", payload, event.source.as_ref()));
    }

    fn record_syscall_result(&mut self, event: SyscallResultEvent) {
        let gated_capability = capability_for_syscall(event.id);
        let result = event.result.clone();
        let mut payload = self.new_payload();
        payload.insert("syscall_id".to_owned(), json!(event.id.0));
        payload.insert("syscall".to_owned(), json!(syscall_name(event.id)));
        insert_syscall_trace_facts(&mut payload, event.id);
        match event.result {
            Ok(values) => {
                payload.insert("status".to_owned(), json!("ok"));
                payload.insert(
                    "result".to_owned(),
                    JsonValue::Array(values.iter().map(vm_value_to_json).collect()),
                );
            }
            Err(error) => {
                payload.insert("status".to_owned(), json!("error"));
                payload.insert("error".to_owned(), json!(error.message()));
            }
        }
        self.with_state(|state| {
            let seq = state.next_seq;
            state.push("syscall.result", payload, event.source.as_ref());
            state
                .determinism_budget
                .record_syscall_result(event.id, &result);
            if state.error.is_none()
                && let Some(capability) = gated_capability
            {
                state
                    .capability_usage
                    .record_result(capability, &result, seq);
            }
            if let Some(spec) = dot_ops::syscall_spec(event.id) {
                match &result {
                    Err(error) if is_determinism_denial(error.message()) => {
                        let mut payload = Map::new();
                        payload.insert("phase".to_owned(), json!("boundary"));
                        payload.insert("message".to_owned(), json!(error.message()));
                        payload.insert("syscall_id".to_owned(), json!(event.id.0));
                        payload.insert("syscall".to_owned(), json!(spec.name()));
                        insert_syscall_trace_facts(&mut payload, event.id);
                        let audit_seq = state.next_seq;
                        state.push("audit.determinism_denial", payload, event.source.as_ref());
                        state.determinism_violations.record(
                            spec.name(),
                            spec.classification().as_str(),
                            spec.required_capability()
                                .map(|capability| capability.as_str()),
                            error.message(),
                            audit_seq,
                        );
                    }
                    Err(error) if is_capability_denial(error.message()) => {}
                    Ok(_) | Err(_)
                        if spec.classification()
                            == dot_ops::DeterminismClass::ControlledSideEffect =>
                    {
                        let mut payload = Map::new();
                        payload.insert(
                            "result_status".to_owned(),
                            json!(match &result {
                                Ok(_) => "ok",
                                Err(_) => "error",
                            }),
                        );
                        payload.insert("syscall_id".to_owned(), json!(event.id.0));
                        payload.insert("syscall".to_owned(), json!(spec.name()));
                        insert_syscall_trace_facts(&mut payload, event.id);
                        state.push(
                            "audit.controlled_side_effect",
                            payload,
                            event.source.as_ref(),
                        );
                    }
                    _ => {}
                }
            }
        });
    }

    fn record_runtime_event(&mut self, event: RuntimeEvent) {
        match event {
            RuntimeEvent::Log { message, source } => {
                let mut payload = self.new_payload();
                payload.insert("message".to_owned(), json!(message));
                self.with_state(|state| state.push("log", payload, source.as_ref()));
            }
            RuntimeEvent::HttpServerStart { addr, source } => {
                let mut payload = self.new_payload();
                payload.insert("addr".to_owned(), json!(addr.to_string()));
                self.with_state(|state| state.push("http.server_start", payload, source.as_ref()));
            }
            RuntimeEvent::HttpRequest {
                method,
                path,
                source,
            } => {
                let mut payload = self.new_payload();
                payload.insert("method".to_owned(), json!(method));
                payload.insert("path".to_owned(), json!(path));
                self.with_state(|state| state.push("http.request", payload, source.as_ref()));
            }
            RuntimeEvent::HttpResponse { status, source } => {
                let mut payload = self.new_payload();
                payload.insert("status".to_owned(), json!(status));
                self.with_state(|state| state.push("http.response", payload, source.as_ref()));
            }
        }
    }

    fn record_strict_validation_denial(
        &mut self,
        program: &[Instruction],
        error: &dot_vm::StrictDeterminismError,
    ) {
        let source = strict_determinism_source(program, error);
        let syscall_id = strict_determinism_syscall_id(error);
        self.with_state(|state| {
            state.determinism_budget.record_validation_denial(error);
            let mut payload = Map::new();
            payload.insert("phase".to_owned(), json!("validation"));
            payload.insert("message".to_owned(), json!(error.to_string()));
            if let Some(id) = syscall_id {
                payload.insert("syscall_id".to_owned(), json!(id.0));
                payload.insert("syscall".to_owned(), json!(syscall_name(id)));
                insert_syscall_trace_facts(&mut payload, id);
                let seq = state.next_seq;
                state.push("audit.determinism_denial", payload, source.as_ref());
                if let Some(spec) = dot_ops::syscall_spec(id) {
                    state.determinism_violations.record(
                        spec.name(),
                        spec.classification().as_str(),
                        spec.required_capability()
                            .map(|capability| capability.as_str()),
                        &error.to_string(),
                        seq,
                    );
                }
            } else {
                payload.insert("opcode".to_owned(), json!(strict_determinism_opcode(error)));
                state.push("audit.determinism_denial", payload, source.as_ref());
            }
        });
    }
}

impl TraceRecorderState {
    fn push(
        &mut self,
        event: &str,
        mut payload: Map<String, JsonValue>,
        source: Option<&SourceRef>,
    ) {
        if self.error.is_some() {
            return;
        }

        payload.insert("seq".to_owned(), json!(self.next_seq));
        payload.insert("run_id".to_owned(), json!(self.run_id.as_str()));
        payload.insert("event".to_owned(), json!(event));
        if let Some(source) = source.and_then(source_to_json) {
            payload.insert("source".to_owned(), source);
        }

        match serde_json::to_writer(&mut self.writer, &JsonValue::Object(payload)) {
            Ok(()) => {
                if let Err(error) = self.writer.write_all(b"\n") {
                    self.error = Some(format!("failed to write trace event: {error}"));
                    return;
                }
                self.next_seq += 1;
            }
            Err(error) => {
                self.error = Some(format!("failed to serialize trace event: {error}"));
            }
        }
    }

    fn flush(&mut self) -> Result<(), String> {
        if let Some(error) = self.error.take() {
            return Err(error);
        }
        self.writer
            .flush()
            .map_err(|error| format!("failed to flush trace file: {error}"))
    }
}

impl EventSink for TraceRecorder {
    fn emit(&mut self, event: VmEvent) {
        match event {
            VmEvent::SyscallAttempt(event) => self.record_syscall_attempt(event),
            VmEvent::Runtime(event) => self.record_runtime_event(event),
            VmEvent::SyscallResult(event) => self.record_syscall_result(event),
        }
    }
}

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
struct DeterminismBudgetCounters {
    gated_total: u64,
    allowed_total: u64,
    denied_total: u64,
    controlled_side_effect_total: u64,
    non_deterministic_total: u64,
}

impl DeterminismBudgetCounters {
    fn record_syscall_result(
        &mut self,
        id: SyscallId,
        result: &Result<Vec<VmValue>, dot_ops::HostError>,
    ) {
        let Some(spec) = dot_ops::syscall_spec(id) else {
            return;
        };

        self.gated_total += 1;
        match spec.classification() {
            dot_ops::DeterminismClass::ControlledSideEffect => {
                self.controlled_side_effect_total += 1;
            }
            dot_ops::DeterminismClass::NonDeterministic => {
                self.non_deterministic_total += 1;
            }
            dot_ops::DeterminismClass::Pure => {}
        }

        match result {
            Err(error) if is_determinism_denial(error.message()) => self.denied_total += 1,
            _ => self.allowed_total += 1,
        }
    }

    fn record_validation_denial(&mut self, error: &dot_vm::StrictDeterminismError) {
        self.gated_total += 1;
        self.denied_total += 1;
        match error {
            dot_vm::StrictDeterminismError::UnsupportedInStrictMode {
                classification: dot_ops::DeterminismClass::ControlledSideEffect,
                ..
            } => {
                self.controlled_side_effect_total += 1;
            }
            dot_vm::StrictDeterminismError::UnsupportedInStrictMode {
                classification: dot_ops::DeterminismClass::NonDeterministic,
                ..
            } => {
                self.non_deterministic_total += 1;
            }
            _ => {}
        }
    }
}

fn build_determinism_report_json(
    determinism_mode: &str,
    eligibility: JsonValue,
    counters: DeterminismBudgetCounters,
    violations: &DeterminismViolationAccumulator,
) -> JsonValue {
    let audit_summary = build_determinism_audit_summary_json(counters, violations);
    json!({
        "schema_version": "1",
        "informational": true,
        "mode": determinism_mode,
        "eligibility": eligibility,
        "audit_summary": audit_summary,
        "counters": {
            "gated_total": counters.gated_total,
            "allowed_total": counters.allowed_total,
            "denied_total": counters.denied_total,
            "controlled_side_effect_total": counters.controlled_side_effect_total,
            "non_deterministic_total": counters.non_deterministic_total,
        },
        "violations": violations.as_json()
    })
}

fn persist_determinism_budget_state(
    host: &mut OpsHost,
    trace: &TraceRecorder,
) -> Result<(), String> {
    let bytes = serde_json::to_vec(&build_determinism_report_json(
        "default",
        json!({ "status": "unknown" }),
        trace.determinism_budget(),
        &trace.determinism_violations(),
    ))
    .map_err(|error| format!("failed to serialize determinism budget state: {error}"))?;
    host.state_set(
        DETERMINISM_BUDGET_STATE_NAMESPACE,
        DETERMINISM_BUDGET_STATE_KEY,
        &bytes,
    )
    .map_err(|error| error.to_string())
}

#[derive(Clone, Debug, Default)]
struct DeterminismViolationAccumulator {
    entries: BTreeMap<String, DeterminismViolationEntry>,
}

#[derive(Clone, Debug)]
struct DeterminismViolationEntry {
    classification: String,
    required_capability: Option<String>,
    count: u64,
    first_seq: u64,
    message: String,
}

impl DeterminismViolationAccumulator {
    fn record(
        &mut self,
        syscall: &str,
        classification: &str,
        required_capability: Option<&str>,
        message: &str,
        seq: u64,
    ) {
        let entry =
            self.entries
                .entry(syscall.to_owned())
                .or_insert_with(|| DeterminismViolationEntry {
                    classification: classification.to_owned(),
                    required_capability: required_capability.map(str::to_owned),
                    count: 0,
                    first_seq: seq,
                    message: message.to_owned(),
                });
        entry.count += 1;
    }

    fn as_json(&self) -> Vec<JsonValue> {
        self.entries
            .iter()
            .map(|(syscall, entry)| {
                let mut value = json!({
                    "syscall": syscall,
                    "classification": entry.classification,
                    "count": entry.count,
                    "first_seq": entry.first_seq,
                    "message": entry.message,
                });
                if let Some(required_capability) = entry.required_capability.as_deref() {
                    value["required_capability"] = json!(required_capability);
                }
                value
            })
            .collect()
    }

    fn total_count(&self) -> u64 {
        self.entries.values().map(|entry| entry.count).sum()
    }

    fn first_seq(&self) -> Option<u64> {
        self.entries.values().map(|entry| entry.first_seq).min()
    }
}

fn build_determinism_audit_summary_json(
    counters: DeterminismBudgetCounters,
    violations: &DeterminismViolationAccumulator,
) -> JsonValue {
    json!({
        "budget": {
            "gated_total": counters.gated_total,
            "allowed_total": counters.allowed_total,
            "denied_total": counters.denied_total,
            "controlled_side_effect_total": counters.controlled_side_effect_total,
            "non_deterministic_total": counters.non_deterministic_total,
        },
        "violations": {
            "count": violations.total_count(),
            "first_seq": violations.first_seq(),
        }
    })
}

fn build_replay_eligibility_json(bundle: &BundleWriter) -> JsonValue {
    let Some(state_diff) = bundle
        .manifest()
        .sections
        .get(BundleSection::StateDiff.key())
    else {
        return json!({
            "status": "unsupported",
            "reason": "manifest_incomplete"
        });
    };
    let Some(capability_report) = bundle
        .manifest()
        .sections
        .get(BundleSection::CapabilityReport.key())
    else {
        return json!({
            "status": "unsupported",
            "reason": "manifest_incomplete"
        });
    };

    for section in [state_diff, capability_report] {
        if section.status != SectionStatus::Ok {
            return json!({
                "status": "unsupported",
                "reason": section
                    .error
                    .as_ref()
                    .map(|error| error.code.clone())
                    .unwrap_or_else(|| "artifact_unavailable".to_owned()),
            });
        }
    }

    json!({
        "status": "eligible"
    })
}

fn write_bundle_determinism_metadata(
    bundle: &mut BundleWriter,
    trace: &TraceRecorder,
) -> Result<(), String> {
    trace.flush()?;

    let eligibility = build_replay_eligibility_json(bundle);
    let audit_summary = build_determinism_audit_summary_json(
        trace.determinism_budget(),
        &trace.determinism_violations(),
    );
    let determinism_mode = bundle.manifest().determinism_mode.clone();
    let replay_proof = build_replay_proof_json(bundle, &determinism_mode, &eligibility, trace)?;

    bundle
        .set_determinism_eligibility_json(eligibility)
        .map_err(|error| format!("failed to update manifest determinism eligibility: {error}"))?;
    bundle
        .set_determinism_audit_summary_json(audit_summary)
        .map_err(|error| format!("failed to update manifest determinism audit summary: {error}"))?;
    bundle
        .set_replay_proof_json(replay_proof)
        .map_err(|error| format!("failed to update manifest replay proof: {error}"))?;
    Ok(())
}

fn build_replay_proof_json(
    bundle: &BundleWriter,
    determinism_mode: &str,
    eligibility: &JsonValue,
    trace: &TraceRecorder,
) -> Result<JsonValue, String> {
    let trace_surface = build_trace_replay_surface(&bundle.staging_dir().join(TRACE_FILE))?;
    let state_diff_surface = build_artifact_replay_surface(
        bundle,
        BundleSection::StateDiff,
        &bundle.staging_dir().join(STATE_DIFF_FILE),
    )?;
    let capability_surface = build_artifact_replay_surface(
        bundle,
        BundleSection::CapabilityReport,
        &bundle.staging_dir().join(CAPABILITY_REPORT_FILE),
    )?;
    let determinism_surface = build_determinism_replay_surface(
        determinism_mode,
        trace.determinism_budget(),
        &trace.determinism_violations(),
    );
    let canonical_surface = json!({
        "trace": trace_surface,
        "state_diff": state_diff_surface,
        "capability_report": capability_surface,
        "determinism": determinism_surface,
    });
    let comparison_fingerprint = sha256_hex(
        canonical_json_string(&json!({
            "eligibility": eligibility,
            "canonical_surface": canonical_surface,
        }))
        .as_bytes(),
    );

    Ok(json!({
        "status": "ready",
        "schema_version": "1",
        "eligibility": eligibility,
        "canonical_surface": canonical_surface,
        "comparison_fingerprint": comparison_fingerprint,
    }))
}

fn build_trace_replay_surface(path: &Path) -> Result<JsonValue, String> {
    let raw = std::fs::read_to_string(path).map_err(|error| {
        format!(
            "failed to read `{}` for replay proof: {error}",
            path.display()
        )
    })?;
    let events = raw
        .lines()
        .map(normalize_trace_line_for_proof)
        .collect::<Result<Vec<_>, _>>()?;
    Ok(json!({
        "event_count": events.len(),
        "fingerprint": sha256_hex(canonical_json_string(&JsonValue::Array(events)).as_bytes()),
    }))
}

fn normalize_trace_line_for_proof(line: &str) -> Result<JsonValue, String> {
    let mut value = serde_json::from_str::<JsonValue>(line)
        .map_err(|error| format!("failed to parse trace line for replay proof: {error}"))?;
    let JsonValue::Object(ref mut map) = value else {
        return Err("trace line for replay proof must be a JSON object".to_owned());
    };
    map.remove("seq");
    map.remove("run_id");
    if map.get("event").and_then(JsonValue::as_str) == Some("run.start") {
        map.remove("entry_dot");
    }
    Ok(value)
}

fn build_artifact_replay_surface(
    bundle: &BundleWriter,
    section: BundleSection,
    path: &Path,
) -> Result<JsonValue, String> {
    let section_manifest = bundle
        .manifest()
        .sections
        .get(section.key())
        .ok_or_else(|| format!("bundle manifest missing `{}` section", section.key()))?;

    if section_manifest.status != SectionStatus::Ok {
        return Ok(json!({
            "status": match section_manifest.status {
                SectionStatus::Ok => "ok",
                SectionStatus::Unavailable => "unavailable",
                SectionStatus::Error => "error",
            },
            "reason": section_manifest
                .error
                .as_ref()
                .map(|error| error.code.as_str())
                .unwrap_or("artifact_unavailable"),
        }));
    }

    let value = read_json_file(path)?;
    match section {
        BundleSection::StateDiff => Ok(json!({
            "status": "ok",
            "change_count": normalized_state_diff_for_proof(&value)?
                .get("changes")
                .and_then(JsonValue::as_array)
                .map(|changes| changes.len() as u64)
                .ok_or_else(|| "state_diff replay proof requires a `changes` array".to_owned())?,
            "fingerprint": sha256_hex(
                canonical_json_string(&normalized_state_diff_for_proof(&value)?).as_bytes()
            ),
        })),
        BundleSection::CapabilityReport => Ok(json!({
            "status": "ok",
            "declared_count": value
                .get("declared")
                .and_then(JsonValue::as_array)
                .map(|items| items.len() as u64)
                .ok_or_else(|| "capability_report replay proof requires a `declared` array".to_owned())?,
            "used_count": value
                .get("used")
                .and_then(JsonValue::as_array)
                .map(|items| items.len() as u64)
                .ok_or_else(|| "capability_report replay proof requires a `used` array".to_owned())?,
            "denied_count": value
                .get("denied")
                .and_then(JsonValue::as_array)
                .map(|items| items.len() as u64)
                .ok_or_else(|| "capability_report replay proof requires a `denied` array".to_owned())?,
            "fingerprint": sha256_hex(canonical_json_string(&value).as_bytes()),
        })),
        BundleSection::Trace | BundleSection::DeterminismReport => Err(format!(
            "replay proof surface helper does not support `{}`",
            section.key()
        )),
    }
}

fn build_determinism_replay_surface(
    determinism_mode: &str,
    counters: DeterminismBudgetCounters,
    violations: &DeterminismViolationAccumulator,
) -> JsonValue {
    let mut surface = json!({
        "mode": determinism_mode,
        "budget": {
            "gated_total": counters.gated_total,
            "allowed_total": counters.allowed_total,
            "denied_total": counters.denied_total,
            "controlled_side_effect_total": counters.controlled_side_effect_total,
            "non_deterministic_total": counters.non_deterministic_total,
        },
        "violations": {
            "count": violations.total_count(),
            "first_seq": violations.first_seq(),
        }
    });
    let fingerprint = sha256_hex(canonical_json_string(&surface).as_bytes());
    surface["fingerprint"] = json!(fingerprint);
    surface
}

fn normalized_state_diff_for_proof(value: &JsonValue) -> Result<JsonValue, String> {
    let Some(changes) = value.get("changes").and_then(JsonValue::as_array) else {
        return Err("state_diff replay proof requires a `changes` array".to_owned());
    };
    let filtered_changes = changes
        .iter()
        .filter(|change| {
            change.get("namespace").and_then(JsonValue::as_str) != Some("runtime")
                || change.get("key").and_then(JsonValue::as_str)
                    != Some(DETERMINISM_BUDGET_STATE_KEY)
        })
        .cloned()
        .collect::<Vec<_>>();

    Ok(json!({
        "schema_version": value
            .get("schema_version")
            .and_then(JsonValue::as_str)
            .unwrap_or("1"),
        "scope": value
            .get("scope")
            .and_then(JsonValue::as_str)
            .unwrap_or("state_kv"),
        "changes": filtered_changes,
    }))
}

fn read_json_file(path: &Path) -> Result<JsonValue, String> {
    serde_json::from_slice(&std::fs::read(path).map_err(|error| {
        format!(
            "failed to read `{}` for replay proof: {error}",
            path.display()
        )
    })?)
    .map_err(|error| {
        format!(
            "failed to parse `{}` for replay proof: {error}",
            path.display()
        )
    })
}

fn canonical_json_string(value: &JsonValue) -> String {
    match value {
        JsonValue::Null => "null".to_owned(),
        JsonValue::Bool(value) => value.to_string(),
        JsonValue::Number(value) => value.to_string(),
        JsonValue::String(value) => {
            serde_json::to_string(value).expect("json string must serialize")
        }
        JsonValue::Array(values) => format!(
            "[{}]",
            values
                .iter()
                .map(canonical_json_string)
                .collect::<Vec<_>>()
                .join(",")
        ),
        JsonValue::Object(values) => {
            let mut keys = values.keys().collect::<Vec<_>>();
            keys.sort_unstable();
            let mut entries = Vec::with_capacity(keys.len());
            for key in keys {
                entries.push(format!(
                    "{}:{}",
                    serde_json::to_string(key).expect("json key must serialize"),
                    canonical_json_string(&values[key]),
                ));
            }
            format!("{{{}}}", entries.join(","))
        }
    }
}

fn strict_determinism_syscall_id(error: &dot_vm::StrictDeterminismError) -> Option<SyscallId> {
    match error {
        dot_vm::StrictDeterminismError::MissingClassification {
            opcode: dot_vm::Opcode::Syscall(id),
        }
        | dot_vm::StrictDeterminismError::UnsupportedInStrictMode {
            opcode: dot_vm::Opcode::Syscall(id),
            ..
        } => Some(*id),
        _ => None,
    }
}

fn strict_determinism_opcode(error: &dot_vm::StrictDeterminismError) -> String {
    match error {
        dot_vm::StrictDeterminismError::MissingClassification { opcode }
        | dot_vm::StrictDeterminismError::UnsupportedInStrictMode { opcode, .. } => {
            opcode.to_string()
        }
    }
}

fn strict_determinism_source(
    program: &[Instruction],
    error: &dot_vm::StrictDeterminismError,
) -> Option<SourceRef> {
    let syscall_id = strict_determinism_syscall_id(error)?;
    program.iter().find_map(|instruction| match instruction {
        Instruction::Syscall { id, source, .. } if *id == syscall_id => source.clone(),
        _ => None,
    })
}

fn source_to_json(source: &SourceRef) -> Option<JsonValue> {
    if source.is_empty() {
        return None;
    }

    let mut value = Map::new();
    if let Some(span) = source.span {
        value.insert(
            "span".to_owned(),
            json!({
                "line": span.line,
                "column": span.column,
                "length": span.length,
            }),
        );
    }
    if let Some(semantic_path) = source.semantic_path.as_deref() {
        value.insert("semantic_path".to_owned(), json!(semantic_path));
    }

    Some(JsonValue::Object(value))
}

#[derive(Clone, Debug, Default)]
struct CapabilityUsageAccumulator {
    entries: BTreeMap<String, CapabilityUsageEntry>,
}

#[derive(Clone, Debug, Default)]
struct CapabilityUsageEntry {
    used: u64,
    denied: u64,
    representative_message: Option<String>,
    representative_seq: Option<u64>,
}

impl CapabilityUsageAccumulator {
    fn record_result(
        &mut self,
        capability: Capability,
        result: &Result<Vec<VmValue>, dot_ops::HostError>,
        seq: u64,
    ) {
        let entry = self
            .entries
            .entry(capability.as_str().to_owned())
            .or_default();
        match result {
            Ok(_) => entry.used += 1,
            Err(error) if is_capability_denial(error.message()) => {
                entry.denied += 1;
                if entry.representative_message.is_none() {
                    entry.representative_message = Some(error.message().to_owned());
                }
                if entry.representative_seq.is_none() {
                    entry.representative_seq = Some(seq);
                }
            }
            Err(_) => entry.used += 1,
        }
    }
}

fn build_capability_report_json(
    ctx: &RuntimeContext,
    usage: CapabilityUsageAccumulator,
) -> JsonValue {
    let mut declared = ctx
        .declared_capabilities()
        .iter()
        .map(|decl| {
            json!({
                "capability": decl.capability().as_str(),
                "source": source_to_json(decl.source()).unwrap_or_else(|| json!({})),
            })
        })
        .collect::<Vec<_>>();
    declared.sort_by(|left, right| {
        left["capability"]
            .as_str()
            .cmp(&right["capability"].as_str())
    });

    let mut used = Vec::new();
    let mut denied = Vec::new();
    for (capability, entry) in usage.entries {
        if entry.used > 0 {
            used.push(json!({
                "capability": capability,
                "count": entry.used,
            }));
        }
        if entry.denied > 0 {
            let mut value = json!({
                "capability": capability,
                "count": entry.denied,
                "message": entry.representative_message,
            });
            if let Some(seq) = entry.representative_seq {
                value["seq"] = json!(seq);
            }
            denied.push(value);
        }
    }

    json!({
        "schema_version": "1",
        "declared": declared,
        "used": used,
        "denied": denied,
    })
}

fn capability_for_syscall(id: SyscallId) -> Option<Capability> {
    dot_ops::required_capability_for_syscall(id)
}

fn is_capability_denial(message: &str) -> bool {
    message.starts_with("capability denied: syscall `")
}

fn is_determinism_denial(message: &str) -> bool {
    message.starts_with("determinism violation: ")
}

fn syscall_name(id: SyscallId) -> &'static str {
    dot_ops::syscall_name(id)
}

fn vm_value_to_json(value: &VmValue) -> JsonValue {
    match value {
        VmValue::Unit => JsonValue::Null,
        VmValue::Bool(value) => json!(value),
        VmValue::I64(value) => json!(value),
        VmValue::Str(value) => json!(value),
        VmValue::Bytes(value) => json!({ "bytes": value }),
    }
}

fn insert_syscall_trace_facts(payload: &mut Map<String, JsonValue>, id: SyscallId) {
    let Some(spec) = dot_ops::syscall_spec(id) else {
        return;
    };

    payload.insert(
        "classification".to_owned(),
        json!(spec.classification().as_str()),
    );
    if let Some(capability) = spec.required_capability() {
        payload.insert("required_capability".to_owned(), json!(capability.as_str()));
    }
}

fn resolve_dot_file(explicit: Option<PathBuf>) -> Result<PathBuf, String> {
    if let Some(path) = explicit {
        return Ok(path);
    }

    let cwd = std::env::current_dir()
        .map_err(|error| format!("failed to read current directory: {error}"))?;

    let default = cwd.join("app.dot");
    if default.is_file() {
        return Ok(default);
    }

    let mut dot_files = Vec::new();
    let entries = std::fs::read_dir(&cwd)
        .map_err(|error| format!("failed to read directory `{}`: {error}", cwd.display()))?;
    for entry in entries {
        let entry = entry.map_err(|error| format!("failed to read directory entry: {error}"))?;
        let path = entry.path();
        if path.extension().and_then(|ext| ext.to_str()) != Some("dot") {
            continue;
        }
        if path.is_file() {
            dot_files.push(path);
        }
    }

    match dot_files.len() {
        0 => Err(format!(
            "no `.dot` file found in `{}` (expected `app.dot`, or pass `--file <path>`)",
            cwd.display()
        )),
        1 => Ok(dot_files.remove(0)),
        _ => {
            dot_files.sort();
            let listing = dot_files
                .iter()
                .filter_map(|path| path.file_name().and_then(|name| name.to_str()))
                .collect::<Vec<_>>()
                .join(", ");
            Err(format!(
                "multiple `.dot` files found in `{}` ({listing}); pass `--file <path>`",
                cwd.display()
            ))
        }
    }
}

#[cfg(test)]
mod tests {
    use super::{
        RunAnnouncement, RunOptions, TraceRecorder, build_capability_report_json, build_program,
        build_runtime_context, build_state_diff, pre_bundle_status, resolve_dot_file, run,
        sha256_hex, trace_finish_status,
    };
    use dot_artifacts::{
        CAPABILITY_REPORT_FILE, DETERMINISM_REPORT_FILE, ENTRY_DOT_FILE, MANIFEST_FILE,
        STATE_DIFF_FILE, TRACE_FILE,
    };
    use dot_db::{DotDb, RunStatus, StateKvEntry};
    use dot_ops::{
        HostError, SYSCALL_LOG_EMIT, SYSCALL_NET_HTTP_SERVE, SourceRef, SourceSpan, SyscallId,
    };
    use dot_rt::RuntimeContext;
    use dot_vm::{
        EventSink, Instruction, Reg, SyscallAttemptEvent, SyscallResultEvent, Value as VmValue,
        VmEvent,
    };
    use serde_json::{Value as JsonValue, json};
    use std::io::{Read, Write};
    use std::net::{TcpListener, TcpStream};
    use std::path::{Path, PathBuf};
    use std::sync::Mutex;
    use std::time::Duration;
    use tempfile::TempDir;

    static CWD_LOCK: Mutex<()> = Mutex::new(());

    struct CwdGuard {
        prev: PathBuf,
    }

    impl CwdGuard {
        fn set(path: &std::path::Path) -> Self {
            let prev = std::env::current_dir().expect("cwd should read");
            std::env::set_current_dir(path).expect("cwd should set");
            Self { prev }
        }
    }

    impl Drop for CwdGuard {
        fn drop(&mut self) {
            let _ = std::env::set_current_dir(&self.prev);
        }
    }

    fn sample_document(port: u16) -> dot_dsl::Document {
        dot_dsl::parse_and_validate(
            &format!(
                r#"
dot 0.1
app "x"
allow net.http.listen

server listen {port}

api "public"
  route GET "/hello"
    respond 200 "Hello from Dotlanth"
  end
end
"#
            ),
            "inline.dot",
        )
        .expect("document must validate")
    }

    fn reserve_port() -> u16 {
        let listener = TcpListener::bind("127.0.0.1:0").expect("port reservation must bind");
        listener.local_addr().expect("port must read").port()
    }

    fn single_bundle_dir(root: &Path) -> PathBuf {
        let bundles_dir = root.join(".dotlanth").join("bundles");
        let entries = std::fs::read_dir(&bundles_dir)
            .expect("bundles dir must exist")
            .collect::<Result<Vec<_>, _>>()
            .expect("bundles dir entries must read");
        assert_eq!(entries.len(), 1, "expected exactly one bundle");
        entries[0].path()
    }

    fn read_json(path: &Path) -> JsonValue {
        serde_json::from_slice(&std::fs::read(path).expect("json file must read"))
            .expect("json file must parse")
    }

    fn read_trace(bundle_dir: &Path) -> Vec<JsonValue> {
        std::fs::read_to_string(bundle_dir.join(TRACE_FILE))
            .expect("trace file must read")
            .lines()
            .map(|line| serde_json::from_str::<JsonValue>(line).expect("trace line must parse"))
            .collect()
    }

    #[test]
    fn build_program_uses_http_serve_syscall() {
        let document = sample_document(8080);
        let program = build_program(&document, None).expect("program should build");
        let expected_source = Some(SourceRef::with_span_and_path(
            SourceSpan::from(document.server.as_ref().expect("server").span),
            "server",
        ));
        assert_eq!(
            program,
            vec![
                Instruction::Syscall {
                    id: SYSCALL_NET_HTTP_SERVE,
                    args: vec![],
                    results: vec![],
                    source: expected_source,
                },
                Instruction::Halt
            ]
        );
    }

    #[test]
    fn build_program_passes_max_requests_when_set() {
        let document = sample_document(8080);
        let program = build_program(&document, Some(3)).expect("program should build");
        let expected_source = Some(SourceRef::with_span_and_path(
            SourceSpan::from(document.server.as_ref().expect("server").span),
            "server",
        ));
        assert_eq!(
            program,
            vec![
                Instruction::LoadConst {
                    dst: Reg(0),
                    value: VmValue::I64(3),
                },
                Instruction::Syscall {
                    id: SYSCALL_NET_HTTP_SERVE,
                    args: vec![Reg(0)],
                    results: vec![],
                    source: expected_source,
                },
                Instruction::Halt
            ]
        );
    }

    #[test]
    fn build_runtime_context_uses_selected_determinism_mode() {
        let document = sample_document(8080);
        let ctx = build_runtime_context(&document, dot_rt::DeterminismMode::Strict)
            .expect("runtime context should build");

        assert_eq!(ctx.determinism_mode(), dot_rt::DeterminismMode::Strict);
    }

    #[test]
    fn capability_report_records_denied_attempt_with_trace_seq() {
        let temp = TempDir::new().expect("temp dir must create");
        let mut trace = TraceRecorder::new("run-test", &temp.path().join("trace.jsonl"))
            .expect("trace should initialize");
        let document = dot_dsl::parse_and_validate(
            r#"dot 0.1
app "x"
allow log

api "public"
  route GET "/hello"
    respond 200 "ok"
  end
end
"#,
            "inline.dot",
        )
        .expect("document must validate");
        let ctx = RuntimeContext::from_dot_dsl(&document).expect("context must build");
        let denied_message = "capability denied: syscall `log.emit` requires capability `log`. Hint: add `allow log`. Declare it in your `.dot` file with an `allow ...` statement.";

        trace.emit(VmEvent::SyscallAttempt(SyscallAttemptEvent {
            ip: 0,
            id: SYSCALL_LOG_EMIT,
            args: vec![VmValue::from("hello")],
            source: None,
        }));
        trace.emit(VmEvent::SyscallResult(SyscallResultEvent {
            ip: 0,
            id: SYSCALL_LOG_EMIT,
            result: Err(HostError::new(denied_message)),
            source: None,
        }));

        let report = build_capability_report_json(&ctx, trace.capability_usage());
        assert_eq!(
            report["denied"],
            json!([
                {
                    "capability": "log",
                    "count": 1,
                    "message": denied_message,
                    "seq": 1
                }
            ])
        );
        assert_eq!(report["used"], json!([]));
    }

    #[test]
    fn capability_report_counts_allowed_failures_as_used() {
        let temp = TempDir::new().expect("temp dir must create");
        let mut trace = TraceRecorder::new("run-test", &temp.path().join("trace.jsonl"))
            .expect("trace should initialize");
        let document = dot_dsl::parse_and_validate(
            r#"dot 0.1
app "x"
allow log

api "public"
  route GET "/hello"
    respond 200 "ok"
  end
end
"#,
            "inline.dot",
        )
        .expect("document must validate");
        let ctx = RuntimeContext::from_dot_dsl(&document).expect("context must build");

        trace.emit(VmEvent::SyscallResult(SyscallResultEvent {
            ip: 0,
            id: SYSCALL_LOG_EMIT,
            result: Err(HostError::new("stdout write failed")),
            source: None,
        }));

        let report = build_capability_report_json(&ctx, trace.capability_usage());
        assert_eq!(
            report["used"],
            json!([
                {
                    "capability": "log",
                    "count": 1
                }
            ])
        );
        assert_eq!(report["denied"], json!([]));
    }

    #[test]
    fn capability_report_tracks_mixed_usage_and_preserves_first_denial() {
        let temp = TempDir::new().expect("temp dir must create");
        let mut trace = TraceRecorder::new("run-test", &temp.path().join("trace.jsonl"))
            .expect("trace should initialize");
        let document = dot_dsl::parse_and_validate(
            r#"dot 0.1
app "x"
allow log

api "public"
  route GET "/hello"
    respond 200 "ok"
  end
end
"#,
            "inline.dot",
        )
        .expect("document must validate");
        let ctx = RuntimeContext::from_dot_dsl(&document).expect("context must build");
        let first_denial = "capability denied: syscall `log.emit` requires capability `log`. Hint: add `allow log`. Declare it in your `.dot` file with an `allow ...` statement.";

        trace.emit(VmEvent::SyscallResult(SyscallResultEvent {
            ip: 0,
            id: SYSCALL_LOG_EMIT,
            result: Err(HostError::new(first_denial)),
            source: None,
        }));
        trace.emit(VmEvent::SyscallResult(SyscallResultEvent {
            ip: 1,
            id: SYSCALL_LOG_EMIT,
            result: Ok(vec![]),
            source: None,
        }));
        trace.emit(VmEvent::SyscallResult(SyscallResultEvent {
            ip: 2,
            id: SYSCALL_LOG_EMIT,
            result: Err(HostError::new(
                "capability denied: syscall `log.emit` requires capability `log`. Hint: add `allow log`. Declare it in your `.dot` file with an `allow ...` statement. later",
            )),
            source: None,
        }));

        let report = build_capability_report_json(&ctx, trace.capability_usage());

        assert_eq!(
            report["used"],
            json!([
                {
                    "capability": "log",
                    "count": 1
                }
            ])
        );
        assert_eq!(
            report["denied"],
            json!([
                {
                    "capability": "log",
                    "count": 2,
                    "message": first_denial,
                    "seq": 0
                }
            ])
        );
    }

    #[test]
    fn capability_report_ignores_unknown_syscalls_even_if_error_looks_like_denial() {
        let temp = TempDir::new().expect("temp dir must create");
        let mut trace = TraceRecorder::new("run-test", &temp.path().join("trace.jsonl"))
            .expect("trace should initialize");
        let document = dot_dsl::parse_and_validate(
            r#"dot 0.1
app "x"
allow log

api "public"
  route GET "/hello"
    respond 200 "ok"
  end
end
"#,
            "inline.dot",
        )
        .expect("document must validate");
        let ctx = RuntimeContext::from_dot_dsl(&document).expect("context must build");

        trace.emit(VmEvent::SyscallResult(SyscallResultEvent {
            ip: 0,
            id: SyscallId(999),
            result: Err(HostError::new(
                "capability denied: syscall `made.up` requires capability `made.up`. Hint: add `allow made.up`. Declare it in your `.dot` file with an `allow ...` statement.",
            )),
            source: None,
        }));

        let report = build_capability_report_json(&ctx, trace.capability_usage());

        assert_eq!(report["used"], json!([]));
        assert_eq!(report["denied"], json!([]));
    }

    #[test]
    fn capability_report_is_stably_sorted_and_matches_trace_denials() {
        let temp = TempDir::new().expect("temp dir must create");
        let trace_path = temp.path().join("trace.jsonl");
        let mut trace =
            TraceRecorder::new("run-test", &trace_path).expect("trace should initialize");
        let document = dot_dsl::parse_and_validate(
            r#"dot 0.1
app "x"
allow net.http.listen
allow log

server listen 8080

api "public"
  route GET "/hello"
    respond 200 "ok"
  end
end
"#,
            "inline.dot",
        )
        .expect("document must validate");
        let ctx = RuntimeContext::from_dot_dsl(&document).expect("context must build");
        let denied_message = "capability denied: syscall `log.emit` requires capability `log`. Hint: add `allow log`. Declare it in your `.dot` file with an `allow ...` statement.";

        trace.emit(VmEvent::SyscallResult(SyscallResultEvent {
            ip: 0,
            id: SYSCALL_NET_HTTP_SERVE,
            result: Ok(vec![]),
            source: None,
        }));
        trace.emit(VmEvent::SyscallResult(SyscallResultEvent {
            ip: 1,
            id: SYSCALL_LOG_EMIT,
            result: Err(HostError::new(denied_message)),
            source: None,
        }));
        trace.emit(VmEvent::SyscallResult(SyscallResultEvent {
            ip: 2,
            id: SYSCALL_LOG_EMIT,
            result: Err(HostError::new("capability denied: syscall `log.emit` requires capability `log`. Hint: add `allow log`. Declare it in your `.dot` file with an `allow ...` statement. later repeat")),
            source: None,
        }));
        trace.flush().expect("trace should flush");

        let report = build_capability_report_json(&ctx, trace.capability_usage());
        assert_eq!(
            report["declared"],
            json!([
                {
                    "capability": "log",
                    "source": {
                        "semantic_path": "capabilities[1]",
                        "span": {
                            "line": 4,
                            "column": 7,
                            "length": 3
                        }
                    }
                },
                {
                    "capability": "net.http.listen",
                    "source": {
                        "semantic_path": "capabilities[0]",
                        "span": {
                            "line": 3,
                            "column": 7,
                            "length": 15
                        }
                    }
                }
            ])
        );
        assert_eq!(
            report["used"],
            json!([
                {
                    "capability": "net.http.listen",
                    "count": 1
                }
            ])
        );
        assert_eq!(
            report["denied"],
            json!([
                {
                    "capability": "log",
                    "count": 2,
                    "message": denied_message,
                    "seq": 1
                }
            ])
        );

        let trace_entries = std::fs::read_to_string(&trace_path)
            .expect("trace file must read")
            .lines()
            .map(|line| serde_json::from_str::<JsonValue>(line).expect("trace line must parse"))
            .collect::<Vec<_>>();
        let denied_seq = report["denied"][0]["seq"]
            .as_u64()
            .expect("denied seq must exist") as usize;
        assert_eq!(trace_entries[denied_seq]["event"], "syscall.result");
        assert_eq!(trace_entries[denied_seq]["syscall"], "log.emit");
        assert_eq!(trace_entries[denied_seq]["error"], denied_message);
    }

    #[test]
    fn build_state_diff_is_stably_sorted_and_omits_unchanged_values() {
        let before = vec![
            StateKvEntry {
                namespace: "beta".to_owned(),
                key: "gone".to_owned(),
                value: b"before".to_vec(),
                updated_at_ms: 1,
            },
            StateKvEntry {
                namespace: "alpha".to_owned(),
                key: "same".to_owned(),
                value: b"steady".to_vec(),
                updated_at_ms: 1,
            },
            StateKvEntry {
                namespace: "alpha".to_owned(),
                key: "flip".to_owned(),
                value: b"a".to_vec(),
                updated_at_ms: 1,
            },
        ];
        let after = vec![
            StateKvEntry {
                namespace: "gamma".to_owned(),
                key: "new".to_owned(),
                value: b"after".to_vec(),
                updated_at_ms: 2,
            },
            StateKvEntry {
                namespace: "alpha".to_owned(),
                key: "flip".to_owned(),
                value: b"b".to_vec(),
                updated_at_ms: 2,
            },
            StateKvEntry {
                namespace: "alpha".to_owned(),
                key: "same".to_owned(),
                value: b"steady".to_vec(),
                updated_at_ms: 99,
            },
        ];

        let diff = build_state_diff(&before, &after);

        assert_eq!(
            diff,
            json!({
                "schema_version": "1",
                "scope": "state_kv",
                "changes": [
                    {
                        "namespace": "alpha",
                        "key": "flip",
                        "change": "updated",
                        "previous_value": [97],
                        "value": [98]
                    },
                    {
                        "namespace": "beta",
                        "key": "gone",
                        "change": "removed",
                        "previous_value": [98, 101, 102, 111, 114, 101]
                    },
                    {
                        "namespace": "gamma",
                        "key": "new",
                        "change": "added",
                        "value": [97, 102, 116, 101, 114]
                    }
                ]
            })
        );
    }

    #[test]
    fn trace_finish_status_matches_failed_state_diff_runs() {
        let ok = Ok(());
        let err = Err("state diff failed".to_owned());

        assert_eq!(pre_bundle_status(&ok, &err, &ok, &ok), RunStatus::Failed);
        assert_eq!(
            trace_finish_status(&ok, &err, &ok, &ok, &ok, None),
            RunStatus::Failed
        );
    }

    #[test]
    fn run_errors_when_no_dot_file_is_present() {
        let _cwd_lock = CWD_LOCK.lock().expect("cwd lock");
        let temp = TempDir::new().expect("temp dir must create");
        let _cwd = CwdGuard::set(temp.path());

        let err = run(RunOptions::default()).expect_err("run must fail");
        assert!(err.contains("no `.dot` file found"));
    }

    #[test]
    fn run_validation_failure_still_emits_bundle_and_failed_run_record() {
        let _cwd_lock = CWD_LOCK.lock().expect("cwd lock");
        let temp = TempDir::new().expect("temp dir must create");
        let source = r#"dot 0.1
app "x"

server listen 18080

api "public"
  route GET "/hello"
    respond 200 "Hello from Dotlanth"
  end
end
"#;
        std::fs::write(temp.path().join("app.dot"), source).expect("dot file write");
        let _cwd = CwdGuard::set(temp.path());

        let err = run(RunOptions::default()).expect_err("run must fail validation");
        assert!(err.contains("missing required capability `net.http.listen`"));

        let bundle_dir = single_bundle_dir(temp.path());
        for relative in [
            MANIFEST_FILE,
            ENTRY_DOT_FILE,
            TRACE_FILE,
            STATE_DIFF_FILE,
            CAPABILITY_REPORT_FILE,
        ] {
            assert!(
                bundle_dir.join(relative).is_file(),
                "required bundle file should exist: {relative}"
            );
        }

        let manifest = read_json(&bundle_dir.join(MANIFEST_FILE));
        let run_id = manifest["run_id"]
            .as_str()
            .expect("run id must be present in manifest");
        assert_eq!(
            bundle_dir.file_name().and_then(|name| name.to_str()),
            Some(run_id)
        );
        assert_eq!(
            std::fs::read_to_string(bundle_dir.join(ENTRY_DOT_FILE))
                .expect("entry snapshot must read"),
            source
        );
        assert_eq!(manifest["inputs"]["entry_dot"]["status"], "ok");
        assert_eq!(manifest["sections"]["trace"]["status"], "ok");
        assert_eq!(manifest["sections"]["state_diff"]["status"], "unavailable");
        assert_eq!(
            manifest["sections"]["capability_report"]["status"],
            "unavailable"
        );
        assert_eq!(manifest["determinism_mode"], "default");
        assert_eq!(manifest["determinism_eligibility"]["status"], "unsupported");
        assert_eq!(
            manifest["determinism_eligibility"]["reason"],
            "execution_not_started"
        );
        assert_eq!(
            manifest["determinism_audit_summary"]["budget"]["gated_total"],
            0
        );
        assert_eq!(
            manifest["determinism_audit_summary"]["violations"]["count"],
            0
        );
        assert_eq!(manifest["replay_proof"]["status"], "ready");
        assert_eq!(
            manifest["replay_proof"]["eligibility"]["status"],
            "unsupported"
        );
        assert_eq!(
            manifest["replay_proof"]["canonical_surface"]["trace"]["event_count"],
            2
        );
        assert!(
            manifest["replay_proof"]["comparison_fingerprint"]
                .as_str()
                .expect("proof fingerprint must exist")
                .len()
                > 8
        );

        let trace = read_trace(&bundle_dir);
        assert_eq!(trace.len(), 2);
        assert_eq!(trace[0]["event"], "run.start");
        assert_eq!(trace[1]["event"], "run.finish");
        assert_eq!(trace[1]["status"], "failed");
        assert!(
            trace[1]["error"]
                .as_str()
                .expect("run finish error must exist")
                .contains("missing required capability `net.http.listen`")
        );

        let mut db = DotDb::open_in(temp.path()).expect("db must reopen");
        let run = db.run_record(run_id).expect("run record must exist");
        assert_eq!(run.status, RunStatus::Failed);
        let bundle = db.artifact_bundle(run_id).expect("bundle ref must resolve");
        assert_eq!(bundle.bundle_ref, format!(".dotlanth/bundles/{run_id}"));
    }

    #[test]
    fn run_parse_failure_still_emits_bundle_and_failed_run_record() {
        let _cwd_lock = CWD_LOCK.lock().expect("cwd lock");
        let temp = TempDir::new().expect("temp dir must create");
        let source = r#"dot 0.1
app "x"
allow net.http.listen

server listen nope
"#;
        std::fs::write(temp.path().join("app.dot"), source).expect("dot file write");
        let _cwd = CwdGuard::set(temp.path());

        let err = run(RunOptions::default()).expect_err("run must fail parse validation");
        assert!(err.contains("invalid server port"));

        let bundle_dir = single_bundle_dir(temp.path());
        let manifest = read_json(&bundle_dir.join(MANIFEST_FILE));
        let run_id = manifest["run_id"]
            .as_str()
            .expect("run id must be present in manifest");
        assert_eq!(manifest["inputs"]["entry_dot"]["status"], "ok");
        assert_eq!(manifest["sections"]["trace"]["status"], "ok");
        assert_eq!(manifest["sections"]["state_diff"]["status"], "unavailable");
        assert_eq!(
            manifest["sections"]["capability_report"]["status"],
            "unavailable"
        );

        let trace = read_trace(&bundle_dir);
        assert_eq!(trace.len(), 2);
        assert_eq!(trace[0]["event"], "run.start");
        assert_eq!(trace[1]["event"], "run.finish");
        assert_eq!(trace[1]["status"], "failed");

        let db = DotDb::open_in(temp.path()).expect("db must reopen");
        let run = db.run_record(run_id).expect("run record must exist");
        assert_eq!(run.status, RunStatus::Failed);
    }

    #[test]
    fn run_runtime_failure_still_emits_bundle_and_failed_run_record() {
        let _cwd_lock = CWD_LOCK.lock().expect("cwd lock");
        let temp = TempDir::new().expect("temp dir must create");
        let listener = TcpListener::bind("127.0.0.1:0").expect("listener must bind");
        let port = listener
            .local_addr()
            .expect("listener addr must read")
            .port();
        std::fs::write(
            temp.path().join("app.dot"),
            format!(
                r#"dot 0.1
app "x"
allow net.http.listen

server listen {port}

api "public"
  route GET "/hello"
    respond 200 "Hello from Dotlanth"
  end
end
"#
            ),
        )
        .expect("dot file write");
        let _cwd = CwdGuard::set(temp.path());

        let err = run(RunOptions::default()).expect_err("run must fail at runtime");
        assert!(err.contains("failed to bind http listener"));

        let bundle_dir = single_bundle_dir(temp.path());
        let manifest = read_json(&bundle_dir.join(MANIFEST_FILE));
        let run_id = manifest["run_id"]
            .as_str()
            .expect("run id must be present in manifest");
        assert_eq!(manifest["sections"]["trace"]["status"], "ok");
        assert_eq!(manifest["sections"]["state_diff"]["status"], "ok");
        assert_eq!(manifest["sections"]["capability_report"]["status"], "ok");

        let trace = read_trace(&bundle_dir);
        assert_eq!(trace.len(), 2);
        assert_eq!(trace[0]["event"], "run.start");
        assert_eq!(trace[1]["event"], "run.finish");
        assert_eq!(trace[1]["status"], "failed");
        assert!(
            trace[1]["error"]
                .as_str()
                .expect("run finish error must exist")
                .contains("failed to bind http listener")
        );

        let mut db = DotDb::open_in(temp.path()).expect("db must reopen");
        let run = db.run_record(run_id).expect("run record must exist");
        assert_eq!(run.status, RunStatus::Failed);
        let bundle = db.artifact_bundle(run_id).expect("bundle ref must resolve");
        assert_eq!(bundle.bundle_ref, format!(".dotlanth/bundles/{run_id}"));
    }

    #[test]
    fn run_persists_selected_determinism_mode_in_run_record_and_manifest() {
        let _cwd_lock = CWD_LOCK.lock().expect("cwd lock");
        let temp = TempDir::new().expect("temp dir must create");
        let source = r#"dot 0.1
app "x"

server listen 18080

api "public"
  route GET "/hello"
    respond 200 "Hello from Dotlanth"
  end
end
"#;
        std::fs::write(temp.path().join("app.dot"), source).expect("dot file write");
        let _cwd = CwdGuard::set(temp.path());

        let err = run(RunOptions {
            file: None,
            determinism: dot_rt::DeterminismMode::Strict,
            max_requests: Some(1),
            announcement: RunAnnouncement::Print,
        })
        .expect_err("run should fail validation");
        assert!(err.contains("missing required capability `net.http.listen`"));

        let bundle_dir = single_bundle_dir(temp.path());
        let manifest = read_json(&bundle_dir.join(MANIFEST_FILE));
        let run_id = manifest["run_id"]
            .as_str()
            .expect("run id must be present in manifest");
        assert_eq!(manifest["determinism_mode"], "strict");

        let db = DotDb::open_in(temp.path()).expect("db must reopen");
        let run = db.run_record(run_id).expect("run record must exist");
        assert_eq!(run.determinism_mode, "strict");
    }

    #[test]
    fn strict_mode_denies_http_serve_before_binding_listener() {
        let _cwd_lock = CWD_LOCK.lock().expect("cwd lock");
        let temp = TempDir::new().expect("temp dir must create");
        let port = reserve_port();
        std::fs::write(
            temp.path().join("app.dot"),
            format!(
                r#"dot 0.1
app "x"
allow net.http.listen

server listen {port}

api "public"
  route GET "/hello"
    respond 200 "Hello from Dotlanth"
  end
end
"#
            ),
        )
        .expect("dot file write");
        let _cwd = CwdGuard::set(temp.path());

        let err = run(RunOptions {
            file: None,
            determinism: dot_rt::DeterminismMode::Strict,
            max_requests: Some(1),
            announcement: RunAnnouncement::Print,
        })
        .expect_err("strict mode should deny unsupported http serving");
        assert_eq!(
            err,
            "determinism violation: strict mode does not support syscall `net.http.serve`"
        );

        let bundle_dir = single_bundle_dir(temp.path());
        let manifest = read_json(&bundle_dir.join(MANIFEST_FILE));
        assert_eq!(manifest["sections"]["determinism_report"]["status"], "ok");
        assert_eq!(manifest["determinism_mode"], "strict");
        assert_eq!(manifest["determinism_eligibility"]["status"], "eligible");
        assert_eq!(
            manifest["determinism_audit_summary"]["budget"]["gated_total"],
            1
        );
        assert_eq!(
            manifest["determinism_audit_summary"]["budget"]["denied_total"],
            1
        );
        assert_eq!(
            manifest["determinism_audit_summary"]["violations"]["count"],
            1
        );
        assert_eq!(
            manifest["determinism_audit_summary"]["violations"]["first_seq"],
            1
        );
        assert_eq!(manifest["replay_proof"]["status"], "ready");
        assert_eq!(
            manifest["replay_proof"]["canonical_surface"]["trace"]["event_count"],
            3
        );
        assert_eq!(
            manifest["replay_proof"]["canonical_surface"]["determinism"]["mode"],
            "strict"
        );
        assert!(
            manifest["replay_proof"]["comparison_fingerprint"]
                .as_str()
                .expect("proof fingerprint must exist")
                .len()
                > 8
        );

        let determinism_report: JsonValue = serde_json::from_slice(
            &std::fs::read(bundle_dir.join(DETERMINISM_REPORT_FILE))
                .expect("determinism report must read"),
        )
        .expect("determinism report must parse");
        let trace = read_trace(&bundle_dir);
        assert_eq!(
            trace
                .iter()
                .map(|entry| entry["event"].as_str().unwrap())
                .collect::<Vec<_>>(),
            vec!["run.start", "audit.determinism_denial", "run.finish"]
        );
        assert_eq!(trace[1]["syscall"], "net.http.serve");
        assert_eq!(trace[1]["classification"], "non_deterministic");
        assert_eq!(trace[1]["required_capability"], "net.http.listen");
        assert_eq!(trace[1]["phase"], "validation");
        assert_eq!(
            determinism_report,
            json!({
                "schema_version": "1",
                "informational": true,
                "mode": "strict",
                "eligibility": {
                    "status": "eligible"
                },
                "audit_summary": {
                    "budget": {
                        "gated_total": 1,
                        "allowed_total": 0,
                        "denied_total": 1,
                        "controlled_side_effect_total": 0,
                        "non_deterministic_total": 1
                    },
                    "violations": {
                        "count": 1,
                        "first_seq": 1
                    }
                },
                "counters": {
                    "gated_total": 1,
                    "allowed_total": 0,
                    "denied_total": 1,
                    "controlled_side_effect_total": 0,
                    "non_deterministic_total": 1
                },
                "violations": [
                    {
                        "syscall": "net.http.serve",
                        "classification": "non_deterministic",
                        "required_capability": "net.http.listen",
                        "count": 1,
                        "first_seq": 1,
                        "message": "determinism violation: strict mode does not support syscall `net.http.serve`"
                    }
                ]
            })
        );

        let rebound =
            TcpListener::bind(("127.0.0.1", port)).expect("strict failure should not bind port");
        drop(rebound);
    }

    #[test]
    fn resolve_dot_file_prefers_app_dot() {
        let _cwd_lock = CWD_LOCK.lock().expect("cwd lock");
        let temp = TempDir::new().expect("temp dir must create");
        std::fs::write(
            temp.path().join("app.dot"),
            "dot 0.1\napp \"x\"\napi \"a\"\nend\n",
        )
        .expect("dot file write");
        let _cwd = CwdGuard::set(temp.path());

        let resolved = resolve_dot_file(None).expect("file should resolve");
        assert!(resolved.ends_with("app.dot"));
    }

    #[test]
    fn run_exports_ordered_trace_bundle_with_source_mapping() {
        let _cwd_lock = CWD_LOCK.lock().expect("cwd lock");
        let temp = TempDir::new().expect("temp dir must create");
        let port = reserve_port();
        std::fs::write(
            temp.path().join("app.dot"),
            format!(
                r#"dot 0.1
app "x"
allow net.http.listen

server listen {port}

api "public"
  route GET "/hello"
    respond 200 "Hello from Dotlanth"
  end
end
"#
            ),
        )
        .expect("dot file write");
        let _cwd = CwdGuard::set(temp.path());

        let client = std::thread::spawn(move || {
            for _ in 0..50 {
                match TcpStream::connect(("127.0.0.1", port)) {
                    Ok(mut stream) => {
                        stream
                            .write_all(b"GET /hello HTTP/1.1\r\nHost: localhost\r\n\r\n")
                            .expect("client write must succeed");
                        let mut response = String::new();
                        stream
                            .read_to_string(&mut response)
                            .expect("client read must succeed");
                        return response;
                    }
                    Err(_) => std::thread::sleep(Duration::from_millis(20)),
                }
            }
            panic!("client failed to connect to runtime");
        });

        run(RunOptions {
            file: None,
            determinism: dot_rt::DeterminismMode::Default,
            max_requests: Some(1),
            announcement: RunAnnouncement::Print,
        })
        .expect("run should succeed");

        let response = client.join().expect("client must join");
        assert!(response.contains("Hello from Dotlanth"));

        let bundles_dir = temp.path().join(".dotlanth").join("bundles");
        let entries = std::fs::read_dir(&bundles_dir)
            .expect("bundles dir must exist")
            .collect::<Result<Vec<_>, _>>()
            .expect("bundles dir entries must read");
        assert_eq!(entries.len(), 1);

        let bundle_path = entries[0].path();
        let manifest: JsonValue = serde_json::from_slice(
            &std::fs::read(bundle_path.join("manifest.json")).expect("manifest must read"),
        )
        .expect("manifest must parse");
        let run_id = manifest["run_id"]
            .as_str()
            .expect("run id must be a string");
        assert_eq!(
            bundle_path
                .file_name()
                .and_then(|name| name.to_str())
                .expect("bundle dir must have name"),
            run_id
        );

        let trace_raw =
            std::fs::read_to_string(bundle_path.join("trace.jsonl")).expect("trace must read");
        let trace = trace_raw
            .lines()
            .map(|line| serde_json::from_str::<JsonValue>(line).expect("trace line must parse"))
            .collect::<Vec<_>>();
        let events = trace
            .iter()
            .map(|entry| entry["event"].as_str().expect("event must exist"))
            .collect::<Vec<_>>();
        assert_eq!(
            events,
            vec![
                "run.start",
                "syscall.attempt",
                "http.server_start",
                "http.request",
                "http.response",
                "syscall.result",
                "run.finish",
            ]
        );

        let seqs = trace
            .iter()
            .map(|entry| entry["seq"].as_u64().expect("seq must exist"))
            .collect::<Vec<_>>();
        assert_eq!(seqs, (0..trace.len() as u64).collect::<Vec<_>>());
        assert_eq!(trace[1]["source"]["semantic_path"], "server");
        assert_eq!(trace[1]["classification"], "non_deterministic");
        assert_eq!(trace[1]["required_capability"], "net.http.listen");
        assert_eq!(trace[2]["source"]["semantic_path"], "server");
        assert_eq!(trace[3]["source"]["semantic_path"], "apis[0].routes[0]");
        assert_eq!(
            trace[4]["source"]["semantic_path"],
            "apis[0].routes[0].response"
        );
        assert_eq!(trace[5]["status"], "ok");
        assert_eq!(trace[5]["classification"], "non_deterministic");
        assert_eq!(trace[5]["required_capability"], "net.http.listen");
        assert_eq!(trace[6]["status"], "succeeded");

        assert_eq!(manifest["sections"]["trace"]["status"], "ok");
        assert_eq!(
            manifest["sections"]["trace"]["bytes"]
                .as_u64()
                .expect("trace bytes must exist") as usize,
            trace_raw.len()
        );
        assert_eq!(manifest["sections"]["state_diff"]["status"], "ok");
        assert_eq!(manifest["sections"]["determinism_report"]["status"], "ok");
        assert_eq!(manifest["sections"]["capability_report"]["status"], "ok");
        assert_eq!(manifest["determinism_mode"], "default");
        assert_eq!(manifest["determinism_eligibility"]["status"], "eligible");
        assert_eq!(
            manifest["determinism_audit_summary"]["budget"]["gated_total"],
            1
        );
        assert_eq!(
            manifest["determinism_audit_summary"]["budget"]["allowed_total"],
            1
        );
        assert_eq!(
            manifest["determinism_audit_summary"]["violations"]["count"],
            0
        );
        assert_eq!(manifest["replay_proof"]["status"], "ready");
        assert_eq!(
            manifest["replay_proof"]["canonical_surface"]["trace"]["event_count"],
            7
        );
        assert_eq!(
            manifest["replay_proof"]["canonical_surface"]["state_diff"]["change_count"],
            0
        );
        assert_eq!(
            manifest["replay_proof"]["canonical_surface"]["capability_report"]["used_count"],
            1
        );
        assert!(
            manifest["replay_proof"]["comparison_fingerprint"]
                .as_str()
                .expect("proof fingerprint must exist")
                .len()
                > 8
        );

        let state_diff: JsonValue = serde_json::from_slice(
            &std::fs::read(bundle_path.join("state_diff.json")).expect("state diff must read"),
        )
        .expect("state diff must parse");
        assert_eq!(state_diff["schema_version"], "1");
        assert_eq!(state_diff["scope"], "state_kv");
        let changes = state_diff["changes"]
            .as_array()
            .expect("state diff changes must be an array");
        assert_eq!(changes.len(), 1);
        assert_eq!(changes[0]["namespace"], "runtime");
        assert_eq!(changes[0]["key"], "determinism_budget.v26_3");
        assert_eq!(changes[0]["change"], "added");

        let determinism_report: JsonValue = serde_json::from_slice(
            &std::fs::read(bundle_path.join(DETERMINISM_REPORT_FILE))
                .expect("determinism report must read"),
        )
        .expect("determinism report must parse");
        assert_eq!(
            determinism_report,
            json!({
                "schema_version": "1",
                "informational": true,
                "mode": "default",
                "eligibility": {
                    "status": "eligible"
                },
                "audit_summary": {
                    "budget": {
                        "gated_total": 1,
                        "allowed_total": 1,
                        "denied_total": 0,
                        "controlled_side_effect_total": 0,
                        "non_deterministic_total": 1
                    },
                    "violations": {
                        "count": 0,
                        "first_seq": null
                    }
                },
                "counters": {
                    "gated_total": 1,
                    "allowed_total": 1,
                    "denied_total": 0,
                    "controlled_side_effect_total": 0,
                    "non_deterministic_total": 1
                },
                "violations": []
            })
        );

        let capability_report: JsonValue = serde_json::from_slice(
            &std::fs::read(bundle_path.join("capability_report.json"))
                .expect("capability report must read"),
        )
        .expect("capability report must parse");
        assert_eq!(capability_report["schema_version"], "1");
        assert_eq!(
            capability_report["declared"],
            serde_json::json!([
                {
                    "capability": "net.http.listen",
                    "source": {
                        "semantic_path": "capabilities[0]",
                        "span": {
                            "line": 3,
                            "column": 7,
                            "length": 15
                        }
                    }
                }
            ])
        );
        assert_eq!(
            capability_report["used"],
            serde_json::json!([
                {
                    "capability": "net.http.listen",
                    "count": 1
                }
            ])
        );
        assert_eq!(capability_report["denied"], serde_json::json!([]));

        let manifest_raw =
            std::fs::read(bundle_path.join("manifest.json")).expect("manifest must read again");
        let mut db = DotDb::open_in(temp.path()).expect("db must reopen");
        let bundle = db.artifact_bundle(run_id).expect("bundle ref must resolve");
        assert_eq!(bundle.bundle_ref, format!(".dotlanth/bundles/{run_id}"));
        assert_eq!(bundle.manifest_bytes, manifest_raw.len() as u64);
        assert_eq!(bundle.manifest_sha256, sha256_hex(&manifest_raw));
    }

    #[test]
    fn run_with_explicit_file_writes_state_into_the_dot_file_project() {
        let _cwd_lock = CWD_LOCK.lock().expect("cwd lock");
        let temp = TempDir::new().expect("temp dir must create");
        let caller_dir = temp.path().join("caller");
        let project_dir = temp.path().join("project");
        std::fs::create_dir_all(&caller_dir).expect("caller dir must create");
        std::fs::create_dir_all(&project_dir).expect("project dir must create");

        let port = reserve_port();
        let dot_path = project_dir.join("app.dot");
        std::fs::write(
            &dot_path,
            format!(
                r#"dot 0.1
app "x"
allow net.http.listen

server listen {port}

api "public"
  route GET "/hello"
    respond 200 "Hello from Dotlanth"
  end
end
"#
            ),
        )
        .expect("dot file write");

        let _cwd = CwdGuard::set(&caller_dir);
        let client = std::thread::spawn(move || {
            for _ in 0..50 {
                match TcpStream::connect(("127.0.0.1", port)) {
                    Ok(mut stream) => {
                        stream
                            .write_all(b"GET /hello HTTP/1.1\r\nHost: localhost\r\n\r\n")
                            .expect("client write must succeed");
                        let mut response = String::new();
                        stream
                            .read_to_string(&mut response)
                            .expect("client read must succeed");
                        return response;
                    }
                    Err(_) => std::thread::sleep(Duration::from_millis(20)),
                }
            }
            panic!("client failed to connect to runtime");
        });

        run(RunOptions {
            file: Some(dot_path),
            determinism: dot_rt::DeterminismMode::Default,
            max_requests: Some(1),
            announcement: RunAnnouncement::Print,
        })
        .expect("run should succeed");

        let response = client.join().expect("client must join");
        assert!(response.contains("Hello from Dotlanth"));
        assert!(project_dir.join(".dotlanth").join("dotdb.sqlite").is_file());
        assert!(project_dir.join(".dotlanth").join("bundles").is_dir());
        assert!(!caller_dir.join(".dotlanth").exists());
    }

    #[test]
    fn controlled_side_effect_syscalls_emit_audit_events() {
        let temp = TempDir::new().expect("temp dir must create");
        let trace_path = temp.path().join("trace.jsonl");
        let mut trace = TraceRecorder::new("run-audit", &trace_path).expect("trace should init");

        trace.emit(VmEvent::SyscallAttempt(SyscallAttemptEvent {
            ip: 0,
            id: SYSCALL_LOG_EMIT,
            args: vec![],
            source: None,
        }));
        trace.emit(VmEvent::SyscallResult(SyscallResultEvent {
            ip: 0,
            id: SYSCALL_LOG_EMIT,
            result: Ok(vec![]),
            source: None,
        }));
        trace.flush().expect("trace should flush");

        let trace_entries = std::fs::read_to_string(&trace_path)
            .expect("trace file must read")
            .lines()
            .map(|line| serde_json::from_str::<JsonValue>(line).expect("trace line must parse"))
            .collect::<Vec<_>>();
        assert_eq!(
            trace_entries
                .iter()
                .map(|entry| entry["event"].as_str().expect("event must exist"))
                .collect::<Vec<_>>(),
            vec![
                "syscall.attempt",
                "syscall.result",
                "audit.controlled_side_effect"
            ]
        );
        assert_eq!(trace_entries[2]["syscall"], "log.emit");
        assert_eq!(trace_entries[2]["classification"], "controlled_side_effect");
        assert_eq!(trace_entries[2]["required_capability"], "log");
        assert_eq!(trace_entries[2]["result_status"], "ok");
    }
}
