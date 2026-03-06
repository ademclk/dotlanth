#![forbid(unsafe_code)]

use dot_artifacts::BundleWriter;
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
use std::collections::BTreeSet;
use std::path::{Path, PathBuf};

const DOTLANTH_DIR: &str = ".dotlanth";
const BUNDLES_DIR: &str = "bundles";

#[derive(Debug, Default)]
pub(crate) struct RunOptions {
    pub(crate) file: Option<PathBuf>,
    pub(crate) max_requests: Option<u64>,
}

pub(crate) fn run(options: RunOptions) -> Result<(), String> {
    let dot_path = resolve_dot_file(options.file)?;
    let document = dot_dsl::load_and_validate(&dot_path).map_err(|error| error.to_string())?;
    let ctx = RuntimeContext::from_dot_dsl(&document).map_err(|error| error.to_string())?;

    let db = DotDb::open_default().map_err(|error| format!("failed to open DotDB: {error}"))?;
    let mut host = OpsHost::new(ctx.capabilities().clone(), db)
        .map_err(|error| format!("failed to initialize runtime host: {error}"))?;
    host.set_record_mode(RecordMode::Record);

    let run_id = host.run_id().to_owned();
    let bundle_dir = bundle_dir_for_run(&run_id)?;
    let mut bundle = match BundleWriter::new(&bundle_dir, &run_id) {
        Ok(bundle) => bundle,
        Err(error) => {
            let _ = host.finalize_run(dot_db::RunStatus::Failed);
            return Err(format!(
                "failed to initialize artifact bundle `{}`: {error}",
                bundle_dir.display()
            ));
        }
    };
    bundle
        .snapshot_entry_dot_from_path(&dot_path)
        .map_err(|error| {
            format!(
                "failed to snapshot `{}` into bundle: {error}",
                dot_path.display()
            )
        })?;

    let mut trace = TraceRecorder::new(&run_id);
    trace.record_run_start(&dot_path);

    let execution_result = run_vm_execution(
        &mut host,
        &document,
        options.max_requests,
        &run_id,
        &mut trace,
    );
    let run_status = if execution_result.is_ok() {
        dot_db::RunStatus::Succeeded
    } else {
        dot_db::RunStatus::Failed
    };
    trace.record_run_finish(
        run_status,
        execution_result.as_ref().err().map(String::as_str),
    );

    let bundle_result = write_bundle(&mut bundle, &trace, &document);
    let finalize_result = host
        .finalize_run(run_status)
        .map_err(|error| error.to_string());

    match execution_result {
        Ok(()) => {
            bundle_result?;
            finalize_result?;
            Ok(())
        }
        Err(error) => Err(combine_errors(
            error,
            [bundle_result.err(), finalize_result.err()],
        )),
    }
}

fn run_vm_execution(
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

fn write_bundle(
    bundle: &mut BundleWriter,
    trace: &TraceRecorder,
    document: &dot_dsl::Document,
) -> Result<(), String> {
    bundle
        .write_trace_jsonl(trace.lines.iter().map(String::as_str))
        .map_err(|error| format!("failed to write trace.jsonl: {error}"))?;
    bundle
        .write_capability_report_json(&json!({
            "granted": granted_capabilities(document),
        }))
        .map_err(|error| format!("failed to write capability_report.json: {error}"))?;
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

fn bundle_dir_for_run(run_id: &str) -> Result<PathBuf, String> {
    let cwd = std::env::current_dir()
        .map_err(|error| format!("failed to read current directory: {error}"))?;
    Ok(cwd.join(DOTLANTH_DIR).join(BUNDLES_DIR).join(run_id))
}

fn combine_errors(primary: String, extras: impl IntoIterator<Item = Option<String>>) -> String {
    let extras = extras.into_iter().flatten().collect::<Vec<_>>();
    if extras.is_empty() {
        primary
    } else {
        format!("{primary}; additionally: {}", extras.join("; "))
    }
}

#[derive(Debug)]
struct TraceRecorder {
    run_id: String,
    next_seq: u64,
    lines: Vec<String>,
}

impl TraceRecorder {
    fn new(run_id: &str) -> Self {
        Self {
            run_id: run_id.to_owned(),
            next_seq: 0,
            lines: Vec::new(),
        }
    }

    fn record_run_start(&mut self, entry_dot: &Path) {
        let mut payload = Map::new();
        payload.insert(
            "entry_dot".to_owned(),
            json!(entry_dot.as_os_str().to_string_lossy().to_string()),
        );
        self.push("run.start", payload, None);
    }

    fn record_run_finish(&mut self, status: dot_db::RunStatus, error: Option<&str>) {
        let mut payload = Map::new();
        payload.insert("status".to_owned(), json!(status.to_string()));
        if let Some(error) = error {
            payload.insert("error".to_owned(), json!(error));
        }
        self.push("run.finish", payload, None);
    }

    fn record_syscall_attempt(&mut self, event: SyscallAttemptEvent) {
        let mut payload = Map::new();
        payload.insert("syscall_id".to_owned(), json!(event.id.0));
        payload.insert("syscall".to_owned(), json!(syscall_name(event.id)));
        payload.insert(
            "args".to_owned(),
            JsonValue::Array(event.args.iter().map(vm_value_to_json).collect()),
        );
        self.push("syscall.attempt", payload, event.source.as_ref());
    }

    fn record_syscall_result(&mut self, event: SyscallResultEvent) {
        let mut payload = Map::new();
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
        self.push("syscall.result", payload, event.source.as_ref());
    }

    fn record_runtime_event(&mut self, event: RuntimeEvent) {
        match event {
            RuntimeEvent::Log { message, source } => {
                let mut payload = Map::new();
                payload.insert("message".to_owned(), json!(message));
                self.push("log", payload, source.as_ref());
            }
            RuntimeEvent::HttpServerStart { addr, source } => {
                let mut payload = Map::new();
                payload.insert("addr".to_owned(), json!(addr.to_string()));
                self.push("http.server_start", payload, source.as_ref());
            }
            RuntimeEvent::HttpRequest {
                method,
                path,
                source,
            } => {
                let mut payload = Map::new();
                payload.insert("method".to_owned(), json!(method));
                payload.insert("path".to_owned(), json!(path));
                self.push("http.request", payload, source.as_ref());
            }
            RuntimeEvent::HttpResponse { status, source } => {
                let mut payload = Map::new();
                payload.insert("status".to_owned(), json!(status));
                self.push("http.response", payload, source.as_ref());
            }
        }
    }

    fn push(
        &mut self,
        event: &str,
        mut payload: Map<String, JsonValue>,
        source: Option<&SourceRef>,
    ) {
        payload.insert("seq".to_owned(), json!(self.next_seq));
        payload.insert("run_id".to_owned(), json!(self.run_id.as_str()));
        payload.insert("event".to_owned(), json!(event));
        if let Some(source) = source.and_then(source_to_json) {
            payload.insert("source".to_owned(), source);
        }
        self.lines.push(
            serde_json::to_string(&JsonValue::Object(payload))
                .expect("trace events should serialize"),
        );
        self.next_seq += 1;
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
}
