#![forbid(unsafe_code)]

use dot_db::{DotDb, RunId, RunLogEntry};
use dot_dsl::Document;
use dot_sec::{CapabilitySet, Syscall};
use std::io::{BufRead, BufReader};
use std::io::Write;
use std::net::{SocketAddr, TcpListener, TcpStream};

#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub struct SyscallId(pub u32);

pub const SYSCALL_LOG_EMIT: SyscallId = SyscallId(1);
pub const SYSCALL_NET_HTTP_SERVE: SyscallId = SyscallId(2);

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

/// Host interface used by the VM to perform capability-gated side effects.
///
/// The VM remains deterministic: nondeterminism may only enter via this interface.
pub trait Host<V> {
    fn syscall(&mut self, id: SyscallId, args: &[V]) -> Result<Vec<V>, HostError>;
}

/// Minimal value conversions needed by dot_ops syscall implementations.
pub trait OpValue: Clone {
    fn as_i64(&self) -> Option<i64>;
    fn as_str(&self) -> Option<&str>;
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum RecordMode {
    Record,
    Passthrough,
}

impl Default for RecordMode {
    fn default() -> Self {
        Self::Record
    }
}

/// A capability-gated syscall host that records structured events into DotDB.
pub struct OpsHost {
    capabilities: CapabilitySet,
    db: DotDb,
    run_id: RunId,
    record_mode: RecordMode,
    stdout: Box<dyn Write + Send>,
    http: Option<HttpConfig>,
}

impl OpsHost {
    pub fn new(capabilities: CapabilitySet, mut db: DotDb) -> Result<Self, HostError> {
        let run_id = db.create_run().map_err(|error| {
            HostError::new(format!("failed to create run in DotDB: {error}"))
        })?;
        Ok(Self {
            capabilities,
            db,
            run_id,
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

    pub fn run_id(&self) -> RunId {
        self.run_id
    }

    pub fn run_logs(&mut self) -> Result<Vec<RunLogEntry>, HostError> {
        self.db
            .run_logs(self.run_id)
            .map_err(|error| HostError::new(format!("failed to read run logs: {error}")))
    }

    pub fn configure_http(&mut self, listener: TcpListener, routes: RouteTable) {
        self.http = Some(HttpConfig { listener, routes });
    }

    pub fn configure_http_from_document(&mut self, document: &Document) -> Result<(), HostError> {
        let Some(server) = &document.server else {
            return Err(HostError::new("missing required `server listen`"));
        };

        let addr = SocketAddr::from(([127, 0, 0, 1], server.port.value));
        let listener = TcpListener::bind(addr).map_err(|error| {
            HostError::new(format!("failed to bind http listener on {addr}: {error}"))
        })?;
        self.configure_http(listener, RouteTable::from_document(document));
        Ok(())
    }

    pub fn http_addr(&self) -> Option<SocketAddr> {
        self.http.as_ref().and_then(|http| http.listener.local_addr().ok())
    }

    fn record_event(&mut self, event: RunEvent) -> Result<(), HostError> {
        if self.record_mode != RecordMode::Record {
            return Ok(());
        }

        let line = event.to_json_line();
        self.db
            .append_run_log(self.run_id, &line)
            .map_err(|error| HostError::new(format!("failed to append run log: {error}")))?;
        Ok(())
    }

    fn record_event_best_effort(&mut self, event: RunEvent) {
        let _ = self.record_event(event);
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

        self.record_event(RunEvent::Log {
            message: message.to_owned(),
        })?;

        Ok(vec![])
    }

    fn syscall_http_serve<V: OpValue>(&mut self, args: &[V]) -> Result<Vec<V>, HostError> {
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
            self.record_event_best_effort(RunEvent::HttpServerStart { addr });
        }

        let mut served = 0_usize;
        let result = loop {
            if let Some(limit) = max_requests {
                if served >= limit {
                    break Ok(());
                }
            }

            let (stream, _) = match http.listener.accept() {
                Ok(pair) => pair,
                Err(error) => {
                    break Err(HostError::new(format!(
                        "failed to accept incoming http connection: {error}"
                    )));
                }
            };

            if let Err(error) = self.handle_http_connection(stream, &http.routes) {
                break Err(error);
            }
            served += 1;
        };

        self.http = Some(http);

        result?;
        Ok(vec![])
    }

    fn handle_http_connection(
        &mut self,
        mut stream: TcpStream,
        routes: &RouteTable,
    ) -> Result<(), HostError> {
        let (method, path) = match read_http_request_line(&mut stream) {
            Ok(value) => value,
            Err(error) => {
                let _ = write_http_response(&mut stream, 400, "Bad Request");
                return Err(HostError::new(format!(
                    "failed to parse http request line: {error}"
                )));
            }
        };

        self.record_event_best_effort(RunEvent::HttpRequest {
            method: method.clone(),
            path: path.clone(),
        });

        let response = routes
            .match_route(&method, &path)
            .map(|route| route.response.clone())
            .unwrap_or_else(|| StaticResponse {
                status: 404,
                body: "Not Found".to_owned(),
            });

        write_http_response(&mut stream, response.status, &response.body).map_err(|error| {
            HostError::new(format!("failed to write http response: {error}"))
        })?;

        self.record_event_best_effort(RunEvent::HttpResponse {
            status: response.status,
        });

        Ok(())
    }
}

impl<V: OpValue> Host<V> for OpsHost {
    fn syscall(&mut self, id: SyscallId, args: &[V]) -> Result<Vec<V>, HostError> {
        match id {
            SYSCALL_LOG_EMIT => self.syscall_log_emit(args),
            SYSCALL_NET_HTTP_SERVE => self.syscall_http_serve(args),
            _ => Err(HostError::new(format!("unknown syscall id: {}", id.0))),
        }
    }
}

#[derive(Clone, Debug, PartialEq, Eq)]
enum RunEvent {
    Log { message: String },
    HttpServerStart { addr: SocketAddr },
    HttpRequest { method: String, path: String },
    HttpResponse { status: u16 },
}

impl RunEvent {
    fn to_json_line(&self) -> String {
        match self {
            Self::Log { message } => format!(
                "{{\"type\":\"log\",\"message\":{}}}",
                json_string(message)
            ),
            Self::HttpServerStart { addr } => format!(
                "{{\"type\":\"http.server_start\",\"addr\":{}}}",
                json_string(&addr.to_string())
            ),
            Self::HttpRequest { method, path } => format!(
                "{{\"type\":\"http.request\",\"method\":{},\"path\":{}}}",
                json_string(method),
                json_string(path)
            ),
            Self::HttpResponse { status } => {
                format!("{{\"type\":\"http.response\",\"status\":{status}}}")
            }
        }
    }
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

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct RouteTable {
    routes: Vec<StaticRoute>,
}

impl RouteTable {
    pub fn from_document(document: &Document) -> Self {
        let mut routes = Vec::new();
        for api in &document.apis {
            for route in &api.routes {
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
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct StaticResponse {
    pub status: u16,
    pub body: String,
}

struct HttpConfig {
    listener: TcpListener,
    routes: RouteTable,
}

fn read_http_request_line(stream: &mut TcpStream) -> Result<(String, String), std::io::Error> {
    let mut reader = BufReader::new(stream);
    let mut line = String::new();
    reader.read_line(&mut line)?;

    let line = line.trim_end_matches(&['\r', '\n'][..]);
    let mut parts = line.split_whitespace();
    let method = parts.next().unwrap_or_default();
    let path = parts.next().unwrap_or_default();

    if method.is_empty() || path.is_empty() {
        return Err(std::io::Error::new(
            std::io::ErrorKind::InvalidData,
            "missing method or path in request line",
        ));
    }

    Ok((method.to_owned(), path.to_owned()))
}

fn reason_phrase(status: u16) -> &'static str {
    match status {
        200 => "OK",
        400 => "Bad Request",
        404 => "Not Found",
        500 => "Internal Server Error",
        _ => "OK",
    }
}

fn write_http_response(
    stream: &mut TcpStream,
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

#[cfg(test)]
mod tests {
    use super::{
        Host, HostError, OpValue, OpsHost, RecordMode, RouteTable, SYSCALL_LOG_EMIT,
        SYSCALL_NET_HTTP_SERVE, SyscallId,
    };
    use dot_db::DotDb;
    use dot_dsl::parse_and_validate;
    use dot_sec::{Capability, CapabilitySet};
    use std::sync::{Arc, Mutex};
    use std::{
        io::{Read, Write},
        net::TcpListener,
    };
    use tempfile::TempDir;

    struct NoopHost;

    impl Host<u8> for NoopHost {
        fn syscall(&mut self, _id: SyscallId, _args: &[u8]) -> Result<Vec<u8>, HostError> {
            Ok(vec![])
        }
    }

    #[test]
    fn host_is_object_safe_over_concrete_value_types() {
        let mut host: Box<dyn Host<u8>> = Box::new(NoopHost);
        host.syscall(SyscallId(0), &[]).unwrap();
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
            self.0
                .lock()
                .expect("lock poisoned")
                .extend_from_slice(buf);
            Ok(buf.len())
        }

        fn flush(&mut self) -> std::io::Result<()> {
            Ok(())
        }
    }

    #[test]
    fn log_syscall_requires_capability() {
        let temp = TempDir::new().expect("temp dir must create");
        let db = DotDb::open_in(temp.path()).expect("db open must succeed");
        let mut host = OpsHost::new(CapabilitySet::empty(), db).expect("host create must succeed");

        let error = host
            .syscall(SYSCALL_LOG_EMIT, &[TestValue::from("hi")])
            .unwrap_err();
        assert!(error
            .to_string()
            .contains("capability denied: syscall `log.emit`"));
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

        host.syscall(SYSCALL_LOG_EMIT, &[TestValue::from("hello")])
            .expect("log syscall must succeed");

        let stdout = String::from_utf8(buffer.lock().expect("lock").clone())
            .expect("stdout must be utf8");
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
        let error = host
            .syscall(SYSCALL_LOG_EMIT, &[TestValue::I64(1)])
            .unwrap_err();
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

        let error = host
            .syscall(SYSCALL_NET_HTTP_SERVE, &[TestValue::I64(1)])
            .unwrap_err();
        assert!(error
            .to_string()
            .contains("capability denied: syscall `net.http.serve`"));
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

        host.syscall(SYSCALL_NET_HTTP_SERVE, &[TestValue::I64(1)])
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

        host.syscall(SYSCALL_NET_HTTP_SERVE, &[TestValue::I64(1)])
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
}
