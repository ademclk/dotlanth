#![forbid(unsafe_code)]

use dot_artifacts::{BundleSection, BundleWriter, TRACE_FILE};
use dot_db::DotDb;
use dot_ops::{
    OpsHost, RecordMode, RuntimeEvent, SYSCALL_LOG_EMIT, SYSCALL_NET_HTTP_SERVE, SourceRef,
    SourceSpan, SyscallId,
};
use dot_rt::RuntimeContext;
use dot_vm::{
    EventSink, Instruction, Reg, SyscallAttemptEvent, SyscallResultEvent, Value as VmValue, Vm,
    VmError, VmEvent,
};
use serde_json::{Map, Value as JsonValue, json};
use std::cell::RefCell;
use std::collections::BTreeSet;
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

    let capability_report_result = write_capability_report(&mut bundle, &document);
    let pre_bundle_status = if execution_result.is_ok() && capability_report_result.is_ok() {
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
        capability_report_result.as_ref().err().cloned(),
        finalize_result.as_ref().err().cloned(),
        finalize_recovery_result
            .as_ref()
            .and_then(|result| result.as_ref().err())
            .cloned(),
    ]);
    trace.record_run_finish(trace_status, trace_error.as_deref());

    let bundle_result = finalize_bundle(&mut bundle, &trace);
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
    vm.run_with_host_and_events(host, trace)
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
    document: &dot_dsl::Document,
) -> Result<(), String> {
    match bundle.write_capability_report_json(&json!({
        "granted": granted_capabilities(document),
    })) {
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

fn finalize_bundle(bundle: &mut BundleWriter, trace: &TraceRecorder) -> Result<(), String> {
    trace.flush()?;
    bundle
        .commit_existing_trace_jsonl()
        .map_err(|error| format!("failed to finalize trace.jsonl: {error}"))?;
    bundle
        .finalize()
        .map_err(|error| format!("failed to finalize artifact bundle: {error}"))
}

fn granted_capabilities(document: &dot_dsl::Document) -> Vec<String> {
    let mut capabilities = BTreeSet::new();
    for capability in &document.capabilities {
        capabilities.insert(capability.value.clone());
    }
    capabilities.into_iter().collect()
}

fn bundle_dir_for_run(project_root: &Path, run_id: &str) -> PathBuf {
    project_root
        .join(DOTLANTH_DIR)
        .join(BUNDLES_DIR)
        .join(run_id)
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
        self.with_state(|state| state.push("syscall.result", payload, event.source.as_ref()));
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
    use super::{RunOptions, build_program, resolve_dot_file, run};
    use dot_ops::{SYSCALL_NET_HTTP_SERVE, SourceRef, SourceSpan};
    use dot_vm::{Instruction, Reg, Value as VmValue};
    use serde_json::Value as JsonValue;
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
        assert_eq!(manifest["sections"]["capability_report"]["status"], "ok");
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
