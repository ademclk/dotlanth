#![forbid(unsafe_code)]

use dot_db::{DotDb, RunId, RunLogEntry, RunStatus, StateKvEntry};
use dot_dsl::Document;
use dot_sec::{CapabilitySet, Syscall};
use std::future::Future;
use std::io::Write;
use std::mem;
use std::net::{SocketAddr, TcpListener};
use std::pin::Pin;
use std::sync::Arc;
use std::time::Duration;
use tokio::io::{AsyncRead, AsyncReadExt, AsyncWrite, AsyncWriteExt};
use tokio::task::JoinSet;
use tokio::time::{Instant, timeout, timeout_at};

#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub struct SyscallId(pub u32);

pub const SYSCALL_LOG_EMIT: SyscallId = SyscallId(1);
pub const SYSCALL_NET_HTTP_SERVE: SyscallId = SyscallId(2);

const MAX_HTTP_REQUEST_LINE_BYTES: usize = 8 * 1024;
const HTTP_READ_TIMEOUT: Duration = Duration::from_secs(5);
const HTTP_WRITE_TIMEOUT: Duration = Duration::from_secs(5);
const MAX_IN_FLIGHT_HTTP_REQUESTS: usize = 256;

#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub struct SourceSpan {
    pub line: usize,
    pub column: usize,
    pub length: usize,
}

impl SourceSpan {
    pub const fn new(line: usize, column: usize, length: usize) -> Self {
        Self {
            line,
            column,
            length,
        }
    }
}

impl From<dot_dsl::Span> for SourceSpan {
    fn from(value: dot_dsl::Span) -> Self {
        Self::new(value.line, value.column, value.length)
    }
}

#[derive(Clone, Debug, Default, PartialEq, Eq, Hash)]
pub struct SourceRef {
    pub span: Option<SourceSpan>,
    pub semantic_path: Option<String>,
}

impl SourceRef {
    pub fn with_span_and_path(span: SourceSpan, semantic_path: impl Into<String>) -> Self {
        Self {
            span: Some(span),
            semantic_path: Some(semantic_path.into()),
        }
    }

    pub fn is_empty(&self) -> bool {
        self.span.is_none() && self.semantic_path.is_none()
    }
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct HostError {
    message: String,
}

impl HostError {
    pub fn new(message: impl Into<String>) -> Self {
        Self {
            message: message.into(),
        }
    }

    pub fn message(&self) -> &str {
        &self.message
    }
}

impl std::fmt::Display for HostError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.write_str(&self.message)
    }
}

impl std::error::Error for HostError {}

#[derive(Clone, Debug, PartialEq, Eq)]
pub enum RuntimeEvent {
    Log {
        message: String,
        source: Option<SourceRef>,
    },
    HttpServerStart {
        addr: SocketAddr,
        source: Option<SourceRef>,
    },
    HttpRequest {
        method: String,
        path: String,
        source: Option<SourceRef>,
    },
    HttpResponse {
        status: u16,
        source: Option<SourceRef>,
    },
}

/// Host interface used by the VM to perform capability-gated side effects.
///
/// The VM remains deterministic: nondeterminism may only enter via this interface.
pub trait Host<V> {
    fn syscall<'a>(
        &'a mut self,
        id: SyscallId,
        args: &'a [V],
    ) -> Pin<Box<dyn Future<Output = Result<Vec<V>, HostError>> + 'a>>;

    fn take_runtime_events(&mut self) -> Vec<RuntimeEvent> {
        Vec::new()
    }
}

/// Minimal value conversions needed by dot_ops syscall implementations.
pub trait OpValue: Clone {
    fn as_i64(&self) -> Option<i64>;
    fn as_str(&self) -> Option<&str>;
}

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub enum RecordMode {
    #[default]
    Record,
    Passthrough,
}

/// A capability-gated syscall host that records structured events into DotDB.
pub struct OpsHost {
    capabilities: CapabilitySet,
    db: DotDb,
    run_row_id: RunId,
    run_id: String,
    pending_runtime_events: Vec<RuntimeEvent>,
    runtime_event_forwarder: Option<Box<dyn FnMut(RuntimeEvent)>>,
    record_mode: RecordMode,
    stdout: Box<dyn Write + Send>,
    http: Option<HttpConfig>,
}

impl OpsHost {
    pub fn new(capabilities: CapabilitySet, mut db: DotDb) -> Result<Self, HostError> {
        let created_run = db
            .create_run()
            .map_err(|error| HostError::new(format!("failed to create run in DotDB: {error}")))?;
        let (run_row_id, run_id) = created_run.into_parts();
        Ok(Self {
            capabilities,
            db,
            run_row_id,
            run_id,
            pending_runtime_events: Vec::new(),
            runtime_event_forwarder: None,
            record_mode: RecordMode::default(),
            stdout: Box::new(std::io::stdout()),
            http: None,
        })
    }

    pub fn with_stdout(mut self, stdout: Box<dyn Write + Send>) -> Self {
        self.stdout = stdout;
        self
    }

    pub fn set_record_mode(&mut self, record_mode: RecordMode) {
        self.record_mode = record_mode;
    }

    pub fn set_runtime_event_forwarder<F>(&mut self, forwarder: F)
    where
        F: FnMut(RuntimeEvent) + 'static,
    {
        self.runtime_event_forwarder = Some(Box::new(forwarder));
    }

    pub fn run_id(&self) -> &str {
        &self.run_id
    }

    pub fn finalize_run(&mut self, status: RunStatus) -> Result<(), HostError> {
        self.db
            .finalize_run(self.run_row_id, status)
            .map_err(|error| HostError::new(format!("failed to finalize run: {error}")))
    }

    pub fn run_logs(&mut self) -> Result<Vec<RunLogEntry>, HostError> {
        self.db
            .run_logs(&self.run_id)
            .map_err(|error| HostError::new(format!("failed to read run logs: {error}")))
    }

    pub fn state_snapshot(&mut self) -> Result<Vec<StateKvEntry>, HostError> {
        self.db
            .state_snapshot()
            .map_err(|error| HostError::new(format!("failed to snapshot state: {error}")))
    }

    pub fn index_artifact_bundle(
        &mut self,
        bundle_ref: &str,
        manifest_sha256: &str,
        manifest_bytes: u64,
    ) -> Result<(), HostError> {
        self.db
            .set_artifact_bundle(&self.run_id, bundle_ref, manifest_sha256, manifest_bytes)
            .map_err(|error| HostError::new(format!("failed to index artifact bundle: {error}")))?;
        Ok(())
    }

    pub fn configure_http(&mut self, listener: TcpListener, routes: RouteTable) {
        self.configure_http_with_source(listener, routes, None);
    }

    pub fn configure_http_from_document(&mut self, document: &Document) -> Result<(), HostError> {
        let Some(server) = &document.server else {
            return Err(HostError::new("missing required `server listen`"));
        };

        let addr = SocketAddr::from(([127, 0, 0, 1], server.port.value));
        let listener = TcpListener::bind(addr).map_err(|error| {
            HostError::new(format!("failed to bind http listener on {addr}: {error}"))
        })?;
        self.configure_http_with_source(
            listener,
            RouteTable::from_document(document),
            Some(SourceRef::with_span_and_path(server.span.into(), "server")),
        );
        Ok(())
    }

    pub fn http_addr(&self) -> Option<SocketAddr> {
        self.http
            .as_ref()
            .and_then(|http| http.listener.local_addr().ok())
    }

    fn configure_http_with_source(
        &mut self,
        listener: TcpListener,
        routes: RouteTable,
        server_source: Option<SourceRef>,
    ) {
        self.http = Some(HttpConfig {
            listener,
            routes,
            server_source,
        });
    }

    fn record_runtime_event(&mut self, event: RuntimeEvent) -> Result<(), HostError> {
        self.record_runtime_events([event])
    }

    fn record_runtime_events<I>(&mut self, events: I) -> Result<(), HostError>
    where
        I: IntoIterator<Item = RuntimeEvent>,
    {
        let events = events.into_iter().collect::<Vec<_>>();
        if events.is_empty() {
            return Ok(());
        }

        if self.record_mode == RecordMode::Record {
            let lines = events
                .iter()
                .map(RuntimeEvent::to_json_line)
                .collect::<Vec<_>>();
            self.db
                .append_run_logs(self.run_row_id, lines.iter().map(String::as_str))
                .map_err(|error| HostError::new(format!("failed to append run log: {error}")))?;
        }

        for event in events {
            self.dispatch_runtime_event(event);
        }
        Ok(())
    }

    fn dispatch_runtime_event(&mut self, event: RuntimeEvent) {
        if let Some(forwarder) = self.runtime_event_forwarder.as_mut() {
            forwarder(event);
        } else {
            self.pending_runtime_events.push(event);
        }
    }

    fn syscall_log_emit<V: OpValue>(&mut self, args: &[V]) -> Result<Vec<V>, HostError> {
        self.capabilities
            .enforce(Syscall::LogEmit)
            .map_err(|error| HostError::new(error.to_string()))?;

        let Some(message) = args.first().and_then(OpValue::as_str) else {
            return Err(HostError::new("log.emit expects 1 string argument"));
        };

        writeln!(self.stdout, "{message}")
            .map_err(|error| HostError::new(format!("failed to write log to stdout: {error}")))?;

        self.record_runtime_event(RuntimeEvent::Log {
            message: message.to_owned(),
            source: None,
        })?;

        Ok(vec![])
    }

    async fn syscall_http_serve<V: OpValue>(&mut self, args: &[V]) -> Result<Vec<V>, HostError> {
        self.capabilities
            .enforce(Syscall::NetHttpServe)
            .map_err(|error| HostError::new(error.to_string()))?;

        let max_requests = match args.len() {
            0 => None,
            1 => {
                let Some(value) = args[0].as_i64() else {
                    return Err(HostError::new(
                        "net.http.serve expects 0 or 1 integer argument (max_requests)",
                    ));
                };
                let Ok(value) = usize::try_from(value) else {
                    return Err(HostError::new(
                        "net.http.serve expects max_requests to be a non-negative integer",
                    ));
                };
                Some(value)
            }
            _ => {
                return Err(HostError::new(
                    "net.http.serve expects 0 or 1 argument (max_requests)",
                ));
            }
        };

        let Some(http) = self.http.take() else {
            return Err(HostError::new(
                "net.http.serve requires an HTTP listener to be configured",
            ));
        };

        if let Ok(addr) = http.listener.local_addr() {
            self.record_runtime_event(RuntimeEvent::HttpServerStart {
                addr,
                source: http.server_source.clone(),
            })?;
        }

        http.listener.set_nonblocking(true).map_err(|error| {
            HostError::new(format!("failed to make listener nonblocking: {error}"))
        })?;
        let listener = tokio::net::TcpListener::from_std(http.listener).map_err(|error| {
            HostError::new(format!(
                "failed to attach listener to tokio runtime: {error}"
            ))
        })?;

        let routes = Arc::new(http.routes.clone());
        let server_source = http.server_source.clone();
        let mut tasks = JoinSet::new();
        let mut accepted = 0_usize;

        let result = loop {
            if let Some(limit) = max_requests
                && accepted >= limit
                && tasks.is_empty()
            {
                break Ok(());
            }

            let can_accept_more = max_requests.is_none_or(|limit| accepted < limit)
                && tasks.len() < MAX_IN_FLIGHT_HTTP_REQUESTS;

            tokio::select! {
                accept_result = listener.accept(), if can_accept_more => {
                    let (stream, _) = match accept_result {
                        Ok(value) => value,
                        Err(error) => {
                            break Err(HostError::new(format!(
                                "failed to accept incoming http connection: {error}"
                            )));
                        }
                    };
                    accepted += 1;
                    let routes = Arc::clone(&routes);
                    tasks.spawn(async move { handle_http_connection_async(stream, routes).await });
                }
                join_result = tasks.join_next(), if !tasks.is_empty() => {
                    let joined = match join_result.expect("join_next must yield while tasks are pending") {
                        Ok(outcome) => outcome,
                        Err(error) => {
                            break Err(HostError::new(format!("http connection task failed: {error}")));
                        }
                    };
                    let outcome = match joined {
                        Ok(outcome) => outcome,
                        Err(error) => break Err(error),
                    };
                    if let Err(error) = self.record_runtime_events(outcome.runtime_events) {
                        break Err(error);
                    }
                }
            }
        };

        let listener = listener.into_std().map_err(|error| {
            HostError::new(format!(
                "failed to detach listener from tokio runtime: {error}"
            ))
        })?;
        self.http = Some(HttpConfig {
            listener,
            routes: Arc::unwrap_or_clone(routes),
            server_source,
        });

        result?;
        Ok(vec![])
    }
}

impl<V: OpValue> Host<V> for OpsHost {
    fn syscall<'a>(
        &'a mut self,
        id: SyscallId,
        args: &'a [V],
    ) -> Pin<Box<dyn Future<Output = Result<Vec<V>, HostError>> + 'a>> {
        Box::pin(async move {
            match id {
                SYSCALL_LOG_EMIT => self.syscall_log_emit(args),
                SYSCALL_NET_HTTP_SERVE => self.syscall_http_serve(args).await,
                _ => Err(HostError::new(format!("unknown syscall id: {}", id.0))),
            }
        })
    }

    fn take_runtime_events(&mut self) -> Vec<RuntimeEvent> {
        mem::take(&mut self.pending_runtime_events)
    }
}

impl RuntimeEvent {
    pub fn to_json_line(&self) -> String {
        match self {
            Self::Log { message, source } => {
                let mut out = format!("{{\"type\":\"log\",\"message\":{}}}", json_string(message));
                insert_source_json(&mut out, source);
                out
            }
            Self::HttpServerStart { addr, source } => {
                let mut out = format!(
                    "{{\"type\":\"http.server_start\",\"addr\":{}}}",
                    json_string(&addr.to_string())
                );
                insert_source_json(&mut out, source);
                out
            }
            Self::HttpRequest {
                method,
                path,
                source,
            } => {
                let mut out = format!(
                    "{{\"type\":\"http.request\",\"method\":{},\"path\":{}}}",
                    json_string(method),
                    json_string(path)
                );
                insert_source_json(&mut out, source);
                out
            }
            Self::HttpResponse { status, source } => {
                let mut out = format!("{{\"type\":\"http.response\",\"status\":{status}}}");
                insert_source_json(&mut out, source);
                out
            }
        }
    }
}

struct ConnectionOutcome {
    runtime_events: Vec<RuntimeEvent>,
}

fn json_string(value: &str) -> String {
    let mut out = String::with_capacity(value.len() + 2);
    out.push('"');
    for ch in value.chars() {
        match ch {
            '"' => out.push_str("\\\""),
            '\\' => out.push_str("\\\\"),
            '\n' => out.push_str("\\n"),
            '\r' => out.push_str("\\r"),
            '\t' => out.push_str("\\t"),
            c if c < '\u{20}' => {
                out.push_str(&format!("\\u{:04x}", c as u32));
            }
            c => out.push(c),
        }
    }
    out.push('"');
    out
}

fn insert_source_json(buffer: &mut String, source: &Option<SourceRef>) {
    let Some(source) = source.as_ref().filter(|source| !source.is_empty()) else {
        return;
    };

    let mut source_json = String::from("\"source\":{");
    let mut needs_comma = false;

    if let Some(span) = source.span {
        source_json.push_str(&format!(
            "\"span\":{{\"line\":{},\"column\":{},\"length\":{}}}",
            span.line, span.column, span.length
        ));
        needs_comma = true;
    }

    if let Some(semantic_path) = source.semantic_path.as_deref() {
        if needs_comma {
            source_json.push(',');
        }
        source_json.push_str("\"semantic_path\":");
        source_json.push_str(&json_string(semantic_path));
    }

    source_json.push('}');
    buffer.insert(buffer.len() - 1, ',');
    buffer.insert_str(buffer.len() - 1, &source_json);
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct RouteTable {
    routes: Vec<StaticRoute>,
}

impl RouteTable {
    pub fn from_document(document: &Document) -> Self {
        let mut routes = Vec::new();
        for (api_index, api) in document.apis.iter().enumerate() {
            for (route_index, route) in api.routes.iter().enumerate() {
                let Some(response) = &route.response else {
                    continue;
                };
                routes.push(StaticRoute {
                    method: route.verb.value.clone(),
                    path: route.path.value.clone(),
                    response: StaticResponse {
                        status: response.status.value,
                        body: response.body.value.clone(),
                    },
                    source: Some(SourceRef::with_span_and_path(
                        route.span.into(),
                        format!("apis[{api_index}].routes[{route_index}]"),
                    )),
                    response_source: Some(SourceRef::with_span_and_path(
                        response.span.into(),
                        format!("apis[{api_index}].routes[{route_index}].response"),
                    )),
                });
            }
        }
        Self { routes }
    }

    pub fn match_route(&self, method: &str, path: &str) -> Option<&StaticRoute> {
        self.routes
            .iter()
            .find(|route| route.method == method && route.path == path)
    }
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct StaticRoute {
    pub method: String,
    pub path: String,
    pub response: StaticResponse,
    pub source: Option<SourceRef>,
    pub response_source: Option<SourceRef>,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct StaticResponse {
    pub status: u16,
    pub body: String,
}

struct HttpConfig {
    listener: TcpListener,
    routes: RouteTable,
    server_source: Option<SourceRef>,
}

async fn handle_http_connection_async<S>(
    mut stream: S,
    routes: Arc<RouteTable>,
) -> Result<ConnectionOutcome, HostError>
where
    S: AsyncRead + AsyncWrite + Unpin,
{
    let (method, path) = match read_http_request_line_async(&mut stream).await {
        Ok(value) => value,
        Err(error) => {
            let status = match error.kind() {
                std::io::ErrorKind::TimedOut => 408,
                _ => 400,
            };
            let body = reason_phrase(status);
            let runtime_event = match write_http_response_async(&mut stream, status, body).await {
                Ok(()) => RuntimeEvent::HttpResponse {
                    status,
                    source: None,
                },
                Err(write_error) => RuntimeEvent::Log {
                    message: format!(
                        "failed to write malformed request response {status}: {write_error}"
                    ),
                    source: None,
                },
            };
            return Ok(ConnectionOutcome {
                runtime_events: vec![runtime_event],
            });
        }
    };

    let matched_route = routes.match_route(&method, &path);
    let (response, request_source, response_source) = if let Some(route) = matched_route {
        (
            route.response.clone(),
            route.source.clone(),
            route.response_source.clone(),
        )
    } else {
        (
            StaticResponse {
                status: 404,
                body: "Not Found".to_owned(),
            },
            None,
            None,
        )
    };

    let request_event = RuntimeEvent::HttpRequest {
        method: method.clone(),
        path: path.clone(),
        source: request_source.clone(),
    };
    let error_source = response_source.clone().or(request_source);

    match write_http_response_async(&mut stream, response.status, &response.body).await {
        Ok(()) => Ok(ConnectionOutcome {
            runtime_events: vec![
                request_event,
                RuntimeEvent::HttpResponse {
                    status: response.status,
                    source: response_source,
                },
            ],
        }),
        Err(error) => Ok(ConnectionOutcome {
            runtime_events: vec![
                request_event,
                RuntimeEvent::Log {
                    message: format!(
                        "failed to write http response {} for {} {}: {error}",
                        response.status, method, path
                    ),
                    source: error_source,
                },
            ],
        }),
    }
}

async fn read_http_request_line_async<S>(stream: &mut S) -> Result<(String, String), std::io::Error>
where
    S: AsyncRead + Unpin,
{
    let deadline = Instant::now() + HTTP_READ_TIMEOUT;
    let mut line = Vec::with_capacity(128);
    let mut chunk = [0_u8; 256];

    loop {
        let bytes_read = timeout_at(deadline, stream.read(&mut chunk))
            .await
            .map_err(|_| {
                std::io::Error::new(std::io::ErrorKind::TimedOut, "request line timed out")
            })??;
        if bytes_read == 0 {
            if line.is_empty() {
                return Err(std::io::Error::new(
                    std::io::ErrorKind::UnexpectedEof,
                    "missing request line",
                ));
            }
            break;
        }

        let bytes = &chunk[..bytes_read];
        if let Some(newline_index) = bytes.iter().position(|&byte| byte == b'\n') {
            let needed = newline_index + 1;
            if line.len() + needed > MAX_HTTP_REQUEST_LINE_BYTES {
                return Err(std::io::Error::new(
                    std::io::ErrorKind::InvalidData,
                    "request line exceeds maximum supported length",
                ));
            }
            line.extend_from_slice(&bytes[..needed]);
            break;
        }

        if line.len() + bytes.len() > MAX_HTTP_REQUEST_LINE_BYTES {
            return Err(std::io::Error::new(
                std::io::ErrorKind::InvalidData,
                "request line exceeds maximum supported length",
            ));
        }
        line.extend_from_slice(bytes);
    }

    let line = String::from_utf8(line).map_err(|_| {
        std::io::Error::new(
            std::io::ErrorKind::InvalidData,
            "request line is not valid utf-8",
        )
    })?;
    let line = line.trim_end_matches(&['\r', '\n'][..]);
    let mut parts = line.split_whitespace();
    let method = parts.next().unwrap_or_default();
    let path = parts.next().unwrap_or_default();
    let version = parts.next().unwrap_or_default();

    if method.is_empty() || path.is_empty() || version.is_empty() {
        return Err(std::io::Error::new(
            std::io::ErrorKind::InvalidData,
            "request line must include method, path, and HTTP version",
        ));
    }
    if parts.next().is_some() {
        return Err(std::io::Error::new(
            std::io::ErrorKind::InvalidData,
            "request line contains unexpected trailing content",
        ));
    }
    if !matches!(version, "HTTP/1.0" | "HTTP/1.1") {
        return Err(std::io::Error::new(
            std::io::ErrorKind::InvalidData,
            "unsupported HTTP version",
        ));
    }

    Ok((method.to_owned(), path.to_owned()))
}

fn reason_phrase(status: u16) -> &'static str {
    match status {
        100 => "Continue",
        101 => "Switching Protocols",
        200 => "OK",
        201 => "Created",
        202 => "Accepted",
        204 => "No Content",
        301 => "Moved Permanently",
        302 => "Found",
        304 => "Not Modified",
        400 => "Bad Request",
        401 => "Unauthorized",
        403 => "Forbidden",
        404 => "Not Found",
        405 => "Method Not Allowed",
        408 => "Request Timeout",
        409 => "Conflict",
        410 => "Gone",
        422 => "Unprocessable Entity",
        429 => "Too Many Requests",
        500 => "Internal Server Error",
        501 => "Not Implemented",
        502 => "Bad Gateway",
        503 => "Service Unavailable",
        504 => "Gateway Timeout",
        _ => status_class_reason_phrase(status),
    }
}

fn status_class_reason_phrase(status: u16) -> &'static str {
    match status {
        100..=199 => "Informational",
        200..=299 => "Success",
        300..=399 => "Redirection",
        400..=499 => "Client Error",
        500..=599 => "Server Error",
        _ => "Unknown Status",
    }
}

#[cfg(test)]
fn write_http_response(
    stream: &mut std::net::TcpStream,
    status: u16,
    body: &str,
) -> Result<(), std::io::Error> {
    let reason = reason_phrase(status);
    let bytes = body.as_bytes();
    let header = format!(
        "HTTP/1.1 {status} {reason}\r\nContent-Type: text/plain; charset=utf-8\r\nContent-Length: {}\r\nConnection: close\r\n\r\n",
        bytes.len()
    );

    stream.write_all(header.as_bytes())?;
    stream.write_all(bytes)?;
    stream.flush()?;
    Ok(())
}

async fn write_http_response_async<S>(
    stream: &mut S,
    status: u16,
    body: &str,
) -> Result<(), std::io::Error>
where
    S: AsyncWrite + Unpin,
{
    let reason = reason_phrase(status);
    let bytes = body.as_bytes();
    let header = format!(
        "HTTP/1.1 {status} {reason}\r\nContent-Type: text/plain; charset=utf-8\r\nContent-Length: {}\r\nConnection: close\r\n\r\n",
        bytes.len()
    );

    timeout(HTTP_WRITE_TIMEOUT, stream.write_all(header.as_bytes()))
        .await
        .map_err(|_| {
            std::io::Error::new(
                std::io::ErrorKind::TimedOut,
                "response header write timed out",
            )
        })??;
    timeout(HTTP_WRITE_TIMEOUT, stream.write_all(bytes))
        .await
        .map_err(|_| {
            std::io::Error::new(
                std::io::ErrorKind::TimedOut,
                "response body write timed out",
            )
        })??;
    timeout(HTTP_WRITE_TIMEOUT, stream.flush())
        .await
        .map_err(|_| {
            std::io::Error::new(std::io::ErrorKind::TimedOut, "response flush timed out")
        })??;
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::{
        Host, HostError, MAX_HTTP_REQUEST_LINE_BYTES, OpValue, OpsHost, RecordMode, RouteTable,
        RuntimeEvent, SYSCALL_LOG_EMIT, SYSCALL_NET_HTTP_SERVE, SyscallId,
        handle_http_connection_async, write_http_response,
    };
    use dot_db::DotDb;
    use dot_dsl::parse_and_validate;
    use dot_sec::{Capability, CapabilitySet};
    use std::future::Future;
    use std::pin::Pin;
    use std::sync::{Arc, Mutex};
    use std::task::{Context, Poll};
    use std::{
        io::{Read, Write},
        net::TcpListener,
    };
    use tempfile::TempDir;

    struct NoopHost;

    impl Host<u8> for NoopHost {
        fn syscall<'a>(
            &'a mut self,
            _id: SyscallId,
            _args: &'a [u8],
        ) -> Pin<Box<dyn Future<Output = Result<Vec<u8>, HostError>> + 'a>> {
            Box::pin(async { Ok(vec![]) })
        }
    }

    fn block_on<F: Future>(future: F) -> F::Output {
        tokio::runtime::Builder::new_current_thread()
            .enable_all()
            .build()
            .expect("runtime must build")
            .block_on(future)
    }

    #[test]
    fn host_is_object_safe_over_concrete_value_types() {
        let mut host: Box<dyn Host<u8>> = Box::new(NoopHost);
        block_on(host.syscall(SyscallId(0), &[])).unwrap();
    }

    #[derive(Clone, Debug, PartialEq)]
    enum TestValue {
        I64(i64),
        Str(String),
    }

    impl From<&str> for TestValue {
        fn from(value: &str) -> Self {
            Self::Str(value.to_owned())
        }
    }

    impl OpValue for TestValue {
        fn as_i64(&self) -> Option<i64> {
            match self {
                Self::I64(value) => Some(*value),
                Self::Str(_) => None,
            }
        }

        fn as_str(&self) -> Option<&str> {
            match self {
                Self::I64(_) => None,
                Self::Str(value) => Some(value),
            }
        }
    }

    #[derive(Clone, Default)]
    struct SharedWrite(Arc<Mutex<Vec<u8>>>);

    impl std::io::Write for SharedWrite {
        fn write(&mut self, buf: &[u8]) -> std::io::Result<usize> {
            self.0.lock().expect("lock poisoned").extend_from_slice(buf);
            Ok(buf.len())
        }

        fn flush(&mut self) -> std::io::Result<()> {
            Ok(())
        }
    }

    struct FailingWriteStream {
        request: Vec<u8>,
        read_offset: usize,
    }

    impl FailingWriteStream {
        fn new(request: &str) -> Self {
            Self {
                request: request.as_bytes().to_vec(),
                read_offset: 0,
            }
        }
    }

    impl tokio::io::AsyncRead for FailingWriteStream {
        fn poll_read(
            mut self: Pin<&mut Self>,
            _cx: &mut Context<'_>,
            buf: &mut tokio::io::ReadBuf<'_>,
        ) -> Poll<std::io::Result<()>> {
            if self.read_offset >= self.request.len() {
                return Poll::Ready(Ok(()));
            }

            let remaining = &self.request[self.read_offset..];
            let to_copy = remaining.len().min(buf.remaining());
            buf.put_slice(&remaining[..to_copy]);
            self.read_offset += to_copy;
            Poll::Ready(Ok(()))
        }
    }

    impl tokio::io::AsyncWrite for FailingWriteStream {
        fn poll_write(
            self: Pin<&mut Self>,
            _cx: &mut Context<'_>,
            _buf: &[u8],
        ) -> Poll<std::io::Result<usize>> {
            Poll::Ready(Err(std::io::Error::new(
                std::io::ErrorKind::BrokenPipe,
                "simulated write failure",
            )))
        }

        fn poll_flush(self: Pin<&mut Self>, _cx: &mut Context<'_>) -> Poll<std::io::Result<()>> {
            Poll::Ready(Ok(()))
        }

        fn poll_shutdown(self: Pin<&mut Self>, _cx: &mut Context<'_>) -> Poll<std::io::Result<()>> {
            Poll::Ready(Ok(()))
        }
    }

    #[test]
    fn log_syscall_requires_capability() {
        let temp = TempDir::new().expect("temp dir must create");
        let db = DotDb::open_in(temp.path()).expect("db open must succeed");
        let mut host = OpsHost::new(CapabilitySet::empty(), db).expect("host create must succeed");

        let error = block_on(host.syscall(SYSCALL_LOG_EMIT, &[TestValue::from("hi")])).unwrap_err();
        assert!(
            error
                .to_string()
                .contains("capability denied: syscall `log.emit`")
        );
        assert!(error.to_string().contains("allow log"));
    }

    #[test]
    fn log_syscall_writes_stdout_and_records_event() {
        let temp = TempDir::new().expect("temp dir must create");
        let db = DotDb::open_in(temp.path()).expect("db open must succeed");

        let mut capabilities = CapabilitySet::empty();
        capabilities.insert(Capability::Log);

        let shared = SharedWrite::default();
        let buffer = shared.0.clone();

        let mut host = OpsHost::new(capabilities, db)
            .expect("host create must succeed")
            .with_stdout(Box::new(shared));
        host.set_record_mode(RecordMode::Record);

        block_on(host.syscall(SYSCALL_LOG_EMIT, &[TestValue::from("hello")]))
            .expect("log syscall must succeed");

        let stdout =
            String::from_utf8(buffer.lock().expect("lock").clone()).expect("stdout must be utf8");
        assert_eq!(stdout, "hello\n");

        let logs = host.run_logs().expect("run logs must read");
        assert_eq!(logs.len(), 1);
        assert!(logs[0].line.contains("\"type\":\"log\""));
        assert!(logs[0].line.contains("\"message\":\"hello\""));
    }

    #[test]
    fn log_syscall_rejects_non_string_argument() {
        let temp = TempDir::new().expect("temp dir must create");
        let db = DotDb::open_in(temp.path()).expect("db open must succeed");

        let mut capabilities = CapabilitySet::empty();
        capabilities.insert(Capability::Log);

        let mut host = OpsHost::new(capabilities, db).expect("host create must succeed");
        let error = block_on(host.syscall(SYSCALL_LOG_EMIT, &[TestValue::I64(1)])).unwrap_err();
        assert_eq!(error.to_string(), "log.emit expects 1 string argument");
    }

    #[test]
    fn http_serve_requires_net_capability() {
        let temp = TempDir::new().expect("temp dir must create");
        let db = DotDb::open_in(temp.path()).expect("db open must succeed");

        let listener = TcpListener::bind("127.0.0.1:0").expect("bind must succeed");
        let document = parse_and_validate(
            r#"
dot 0.1
app "x"
allow net.http.listen

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

        let mut host = OpsHost::new(CapabilitySet::empty(), db).expect("host create must succeed");
        host.configure_http(listener, RouteTable::from_document(&document));

        let error =
            block_on(host.syscall(SYSCALL_NET_HTTP_SERVE, &[TestValue::I64(1)])).unwrap_err();
        assert!(
            error
                .to_string()
                .contains("capability denied: syscall `net.http.serve`")
        );
    }

    #[test]
    fn http_serve_routes_by_method_and_path() {
        let temp = TempDir::new().expect("temp dir must create");
        let db = DotDb::open_in(temp.path()).expect("db open must succeed");

        let mut capabilities = CapabilitySet::empty();
        capabilities.insert(Capability::NetHttpListen);

        let listener = TcpListener::bind("127.0.0.1:0").expect("bind must succeed");
        let addr = listener.local_addr().expect("listener addr");

        let document = parse_and_validate(
            r#"
dot 0.1
app "x"
allow net.http.listen

server listen 8080

api "public"
  route GET "/hello"
    respond 200 "Hello from Dotlanth"
  end
end
"#,
            "inline.dot",
        )
        .expect("document must validate");

        let mut host = OpsHost::new(capabilities, db).expect("host create must succeed");
        host.configure_http(listener, RouteTable::from_document(&document));

        let client = std::thread::spawn(move || {
            let mut stream =
                std::net::TcpStream::connect(addr).expect("client connect must succeed");
            stream
                .write_all(b"GET /hello HTTP/1.1\r\nHost: localhost\r\n\r\n")
                .expect("client write must succeed");

            let mut buf = String::new();
            stream
                .read_to_string(&mut buf)
                .expect("client read must succeed");
            buf
        });

        block_on(host.syscall(SYSCALL_NET_HTTP_SERVE, &[TestValue::I64(1)]))
            .expect("http serve must succeed");

        let response = client.join().expect("client must join");
        assert!(response.starts_with("HTTP/1.1 200 OK\r\n"));
        assert!(response.contains("\r\n\r\nHello from Dotlanth"));
    }

    #[test]
    fn http_serve_records_request_response_metadata() {
        let temp = TempDir::new().expect("temp dir must create");
        let db = DotDb::open_in(temp.path()).expect("db open must succeed");

        let mut capabilities = CapabilitySet::empty();
        capabilities.insert(Capability::NetHttpListen);

        let listener = TcpListener::bind("127.0.0.1:0").expect("bind must succeed");
        let addr = listener.local_addr().expect("listener addr");

        let document = parse_and_validate(
            r#"
dot 0.1
app "x"
allow net.http.listen

server listen 8080

api "public"
  route GET "/hello"
    respond 200 "Hello from Dotlanth"
  end
end
"#,
            "inline.dot",
        )
        .expect("document must validate");

        let mut host = OpsHost::new(capabilities, db).expect("host create must succeed");
        host.configure_http(listener, RouteTable::from_document(&document));

        let client = std::thread::spawn(move || {
            let mut stream =
                std::net::TcpStream::connect(addr).expect("client connect must succeed");
            stream
                .write_all(b"GET /hello HTTP/1.1\r\nHost: localhost\r\n\r\n")
                .expect("client write must succeed");

            let mut buf = String::new();
            stream
                .read_to_string(&mut buf)
                .expect("client read must succeed");
            buf
        });

        block_on(host.syscall(SYSCALL_NET_HTTP_SERVE, &[TestValue::I64(1)]))
            .expect("http serve must succeed");

        let response = client.join().expect("client must join");
        assert!(response.contains("\r\n\r\nHello from Dotlanth"));

        let logs = host.run_logs().expect("run logs must read");
        assert_eq!(logs.len(), 3);
        assert!(logs[0].line.contains("\"type\":\"http.server_start\""));
        assert!(logs[0].line.contains(&addr.to_string()));
        assert!(logs[1].line.contains("\"type\":\"http.request\""));
        assert!(logs[1].line.contains("\"method\":\"GET\""));
        assert!(logs[1].line.contains("\"path\":\"/hello\""));
        assert!(logs[2].line.contains("\"type\":\"http.response\""));
        assert!(logs[2].line.contains("\"status\":200"));

        assert!(!logs[1].line.contains("Hello from Dotlanth"));
        assert!(!logs[2].line.contains("Hello from Dotlanth"));
    }

    #[test]
    fn invalid_request_does_not_abort_the_server() {
        let temp = TempDir::new().expect("temp dir must create");
        let db = DotDb::open_in(temp.path()).expect("db open must succeed");

        let mut capabilities = CapabilitySet::empty();
        capabilities.insert(Capability::NetHttpListen);

        let listener = TcpListener::bind("127.0.0.1:0").expect("bind must succeed");
        let addr = listener.local_addr().expect("listener addr");

        let document = parse_and_validate(
            r#"
dot 0.1
app "x"
allow net.http.listen

server listen 8080

api "public"
  route GET "/hello"
    respond 200 "Hello from Dotlanth"
  end
end
"#,
            "inline.dot",
        )
        .expect("document must validate");

        let mut host = OpsHost::new(capabilities, db).expect("host create must succeed");
        host.configure_http(listener, RouteTable::from_document(&document));

        let invalid_client = std::thread::spawn(move || {
            let mut stream =
                std::net::TcpStream::connect(addr).expect("client connect must succeed");
            stream
                .write_all(b"THIS IS NOT HTTP\r\n\r\n")
                .expect("client write must succeed");

            let mut buf = String::new();
            stream
                .read_to_string(&mut buf)
                .expect("client read must succeed");
            buf
        });

        let valid_client = std::thread::spawn(move || {
            let mut stream =
                std::net::TcpStream::connect(addr).expect("client connect must succeed");
            stream
                .write_all(b"GET /hello HTTP/1.1\r\nHost: localhost\r\n\r\n")
                .expect("client write must succeed");

            let mut buf = String::new();
            stream
                .read_to_string(&mut buf)
                .expect("client read must succeed");
            buf
        });

        block_on(host.syscall(SYSCALL_NET_HTTP_SERVE, &[TestValue::I64(2)]))
            .expect("http serve must succeed");

        let invalid_response = invalid_client.join().expect("invalid client must join");
        let valid_response = valid_client.join().expect("valid client must join");

        assert!(invalid_response.starts_with("HTTP/1.1 400 Bad Request\r\n"));
        assert!(valid_response.starts_with("HTTP/1.1 200 OK\r\n"));
        assert!(valid_response.contains("\r\n\r\nHello from Dotlanth"));
    }

    #[test]
    fn oversized_request_line_is_rejected_and_server_continues() {
        let temp = TempDir::new().expect("temp dir must create");
        let db = DotDb::open_in(temp.path()).expect("db open must succeed");

        let mut capabilities = CapabilitySet::empty();
        capabilities.insert(Capability::NetHttpListen);

        let listener = TcpListener::bind("127.0.0.1:0").expect("bind must succeed");
        let addr = listener.local_addr().expect("listener addr");

        let document = parse_and_validate(
            r#"
dot 0.1
app "x"
allow net.http.listen

server listen 8080

api "public"
  route GET "/hello"
    respond 200 "Hello from Dotlanth"
  end
end
"#,
            "inline.dot",
        )
        .expect("document must validate");

        let mut host = OpsHost::new(capabilities, db).expect("host create must succeed");
        host.configure_http(listener, RouteTable::from_document(&document));

        let invalid_client = std::thread::spawn(move || {
            let mut stream =
                std::net::TcpStream::connect(addr).expect("client connect must succeed");
            let oversized_path = format!("/{}", "a".repeat(MAX_HTTP_REQUEST_LINE_BYTES));
            let request = format!("GET {oversized_path} HTTP/1.1\r\nHost: localhost\r\n\r\n");
            stream
                .write_all(request.as_bytes())
                .expect("client write must succeed");

            let mut buf = String::new();
            stream
                .read_to_string(&mut buf)
                .expect("client read must succeed");
            buf
        });

        let valid_client = std::thread::spawn(move || {
            let mut stream =
                std::net::TcpStream::connect(addr).expect("client connect must succeed");
            stream
                .write_all(b"GET /hello HTTP/1.1\r\nHost: localhost\r\n\r\n")
                .expect("client write must succeed");

            let mut buf = String::new();
            stream
                .read_to_string(&mut buf)
                .expect("client read must succeed");
            buf
        });

        block_on(host.syscall(SYSCALL_NET_HTTP_SERVE, &[TestValue::I64(2)]))
            .expect("http serve must succeed");

        let invalid_response = invalid_client.join().expect("invalid client must join");
        let valid_response = valid_client.join().expect("valid client must join");

        assert!(invalid_response.starts_with("HTTP/1.1 400 Bad Request\r\n"));
        assert!(valid_response.starts_with("HTTP/1.1 200 OK\r\n"));
        assert!(valid_response.contains("\r\n\r\nHello from Dotlanth"));
    }

    #[test]
    fn response_write_failures_become_runtime_log_events() {
        let document = parse_and_validate(
            r#"
dot 0.1
app "x"
allow net.http.listen

server listen 8080

api "public"
  route GET "/hello"
    respond 200 "Hello from Dotlanth"
  end
end
"#,
            "inline.dot",
        )
        .expect("document must validate");

        let outcome = block_on(handle_http_connection_async(
            FailingWriteStream::new("GET /hello HTTP/1.1\r\nHost: localhost\r\n\r\n"),
            Arc::new(RouteTable::from_document(&document)),
        ))
        .expect("connection failure should not abort the server");

        assert_eq!(outcome.runtime_events.len(), 2);
        assert!(matches!(
            &outcome.runtime_events[0],
            RuntimeEvent::HttpRequest { method, path, .. }
                if method == "GET" && path == "/hello"
        ));
        assert!(matches!(
            &outcome.runtime_events[1],
            RuntimeEvent::Log { message, .. }
                if message.contains("failed to write http response 200 for GET /hello")
        ));
    }

    #[test]
    fn write_http_response_uses_reasonable_reason_phrases() {
        let listener = TcpListener::bind("127.0.0.1:0").expect("bind must succeed");
        let addr = listener.local_addr().expect("listener addr");

        let handle = std::thread::spawn(move || {
            let (mut stream, _) = listener.accept().expect("accept must succeed");
            write_http_response(&mut stream, 201, "created").expect("response should write");
        });

        let mut client = std::net::TcpStream::connect(addr).expect("client connect must succeed");
        let mut response = String::new();
        client
            .read_to_string(&mut response)
            .expect("client read must succeed");
        handle.join().expect("server thread must join");

        assert!(response.starts_with("HTTP/1.1 201 Created\r\n"));
    }
}
