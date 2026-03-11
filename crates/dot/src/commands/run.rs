#![forbid(unsafe_code)]

use dot_artifacts::{BundleSection, BundleWriter, TRACE_FILE};
use dot_db::{DotDb, StateKvEntry};
use dot_ops::{
    OpsHost, RecordMode, RuntimeEvent, SYSCALL_LOG_EMIT, SYSCALL_NET_HTTP_SERVE, SourceRef,
    SourceSpan, SyscallId,
};
use dot_rt::RuntimeContext;
use dot_sec::Capability;
use dot_vm::{
    EventSink, Instruction, Reg, SyscallAttemptEvent, SyscallResultEvent, Value as VmValue, Vm,
    VmError, VmEvent,
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

#[derive(Debug, Default)]
pub(crate) struct RunOptions {
    pub(crate) file: Option<PathBuf>,
    pub(crate) max_requests: Option<u64>,
}

pub(crate) fn run(options: RunOptions) -> Result<(), String> {
    let runtime = tokio::runtime::Builder::new_current_thread()
        .enable_all()
        .build()
        .map_err(|error| format!("failed to initialize tokio runtime: {error}"))?;
    runtime.block_on(run_async(options))
}

async fn run_async(options: RunOptions) -> Result<(), String> {
    let dot_path = resolve_dot_file(options.file)?;
    let project_root = project_root_for_dot_path(&dot_path)?;
    let dot_source = read_dot_source(&dot_path)?;
    let document =
        dot_dsl::parse_and_validate(&dot_source, &dot_path).map_err(|error| error.to_string())?;
    let ctx = RuntimeContext::from_dot_dsl(&document).map_err(|error| error.to_string())?;

    let db =
        DotDb::open_in(&project_root).map_err(|error| format!("failed to open DotDB: {error}"))?;
    let mut host = OpsHost::new(ctx.capabilities().clone(), db)
        .map_err(|error| format!("failed to initialize runtime host: {error}"))?;
    host.set_record_mode(RecordMode::Record);
    let before_state = host
        .state_snapshot()
        .map_err(|error| format!("failed to capture state snapshot before execution: {error}"));

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
    let execution_result = match bundle.snapshot_entry_dot_bytes(dot_source.as_bytes()) {
        Ok(()) => {
            run_vm_execution(
                &mut host,
                &ctx,
                &document,
                options.max_requests,
                &run_id,
                &mut trace,
            )
            .await
        }
        Err(error) => Err(format!(
            "failed to snapshot `{}` into bundle: {error}",
            dot_path.display()
        )),
    };

    let after_state = host
        .state_snapshot()
        .map_err(|error| format!("failed to capture state snapshot after execution: {error}"));
    let state_diff_result = write_state_diff(&mut bundle, before_state, after_state);
    let capability_report_result = write_capability_report(&mut bundle, &ctx, &trace);
    let pre_bundle_status = if execution_result.is_ok()
        && state_diff_result.is_ok()
        && capability_report_result.is_ok()
    {
        dot_db::RunStatus::Succeeded
    } else {
        dot_db::RunStatus::Failed
    };
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

    let trace_status = if execution_result.is_ok()
        && capability_report_result.is_ok()
        && finalize_result.is_ok()
    {
        dot_db::RunStatus::Succeeded
    } else {
        dot_db::RunStatus::Failed
    };
    let trace_error = summarize_errors([
        execution_result.as_ref().err().cloned(),
        state_diff_result.as_ref().err().cloned(),
        capability_report_result.as_ref().err().cloned(),
        finalize_result.as_ref().err().cloned(),
        finalize_recovery_result
            .as_ref()
            .and_then(|result| result.as_ref().err())
            .cloned(),
    ]);
    trace.record_run_finish(trace_status, trace_error.as_deref());

    let bundle_result = finalize_bundle(&mut host, &mut bundle, &trace, &run_id);
    let bundle_recovery_finalize_result = if bundle_result.is_err()
        && pre_bundle_status == dot_db::RunStatus::Succeeded
        && finalize_result.is_ok()
    {
        Some(
            host.finalize_run(dot_db::RunStatus::Failed)
                .map_err(|error| error.to_string()),
        )
    } else {
        None
    };
    let mut errors = Vec::new();

    if let Err(error) = execution_result {
        errors.push(error);
    }
    if let Err(error) = state_diff_result {
        errors.push(error);
    }
    if let Err(error) = capability_report_result {
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
    if let Some(Err(error)) = bundle_recovery_finalize_result {
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

async fn run_vm_execution(
    host: &mut OpsHost,
    ctx: &RuntimeContext,
    document: &dot_dsl::Document,
    max_requests: Option<u64>,
    run_id: &str,
    trace: &mut TraceRecorder,
) -> Result<(), String> {
    host.configure_http_from_document(document)
        .map_err(|error| error.to_string())?;

    let addr = host
        .http_addr()
        .ok_or_else(|| "http listener did not report a local address".to_owned())?;
    println!("run {run_id} listening on http://{addr}");

    let program = build_program(document, max_requests)?;
    let mut vm = Vm::new(program);
    vm.run_with_capabilities_host_and_events(ctx.capabilities(), host, trace)
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
            if state.error.is_none()
                && let Some(capability) = gated_capability
            {
                state
                    .capability_usage
                    .record_result(capability, &result, seq);
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
    match id {
        SYSCALL_LOG_EMIT => Some(Capability::Log),
        SYSCALL_NET_HTTP_SERVE => Some(Capability::NetHttpListen),
        _ => None,
    }
}

fn is_capability_denial(message: &str) -> bool {
    message.starts_with("capability denied: syscall `")
}

fn syscall_name(id: SyscallId) -> &'static str {
    match id {
        SYSCALL_LOG_EMIT => "log.emit",
        SYSCALL_NET_HTTP_SERVE => "net.http.serve",
        _ => "unknown",
    }
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
        RunOptions, TraceRecorder, build_capability_report_json, build_program, build_state_diff,
        resolve_dot_file, run, sha256_hex,
    };
    use dot_db::{DotDb, StateKvEntry};
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
    use std::path::PathBuf;
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
    fn run_errors_when_no_dot_file_is_present() {
        let _cwd_lock = CWD_LOCK.lock().expect("cwd lock");
        let temp = TempDir::new().expect("temp dir must create");
        let _cwd = CwdGuard::set(temp.path());

        let err = run(RunOptions::default()).expect_err("run must fail");
        assert!(err.contains("no `.dot` file found"));
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
            max_requests: Some(1),
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
        assert_eq!(trace[2]["source"]["semantic_path"], "server");
        assert_eq!(trace[3]["source"]["semantic_path"], "apis[0].routes[0]");
        assert_eq!(
            trace[4]["source"]["semantic_path"],
            "apis[0].routes[0].response"
        );
        assert_eq!(trace[5]["status"], "ok");
        assert_eq!(trace[6]["status"], "succeeded");

        assert_eq!(manifest["sections"]["trace"]["status"], "ok");
        assert_eq!(
            manifest["sections"]["trace"]["bytes"]
                .as_u64()
                .expect("trace bytes must exist") as usize,
            trace_raw.len()
        );
        assert_eq!(manifest["sections"]["state_diff"]["status"], "ok");
        assert_eq!(manifest["sections"]["capability_report"]["status"], "ok");

        let state_diff: JsonValue = serde_json::from_slice(
            &std::fs::read(bundle_path.join("state_diff.json")).expect("state diff must read"),
        )
        .expect("state diff must parse");
        assert_eq!(
            state_diff,
            json!({
                "schema_version": "1",
                "scope": "state_kv",
                "changes": []
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
            max_requests: Some(1),
        })
        .expect("run should succeed");

        let response = client.join().expect("client must join");
        assert!(response.contains("Hello from Dotlanth"));
        assert!(project_dir.join(".dotlanth").join("dotdb.sqlite").is_file());
        assert!(project_dir.join(".dotlanth").join("bundles").is_dir());
        assert!(!caller_dir.join(".dotlanth").exists());
    }
}
