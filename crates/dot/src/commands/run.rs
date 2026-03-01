#![forbid(unsafe_code)]

use dot_db::DotDb;
use dot_ops::{OpsHost, RecordMode, SYSCALL_NET_HTTP_SERVE};
use dot_rt::RuntimeContext;
use dot_vm::{Instruction, Reg, Value, Vm, VmError};
use std::path::PathBuf;

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

    host.configure_http_from_document(&document)
        .map_err(|error| error.to_string())?;

    let run_id = host.run_id();
    let addr = host
        .http_addr()
        .ok_or_else(|| "http listener did not report a local address".to_owned())?;

    println!("run {run_id} listening on http://{addr}");

    let program = build_program(options.max_requests)?;
    let mut vm = Vm::new(program);
    vm.run_with_host(&mut host).map_err(format_vm_error)?;

    Ok(())
}

fn build_program(max_requests: Option<u64>) -> Result<Vec<Instruction>, String> {
    let mut program = Vec::new();

    let args = if let Some(max_requests) = max_requests {
        let max_i64 = i64::try_from(max_requests)
            .map_err(|_| "max_requests must fit within a signed 64-bit integer".to_owned())?;
        program.push(Instruction::LoadConst {
            dst: Reg(0),
            value: Value::I64(max_i64),
        });
        vec![Reg(0)]
    } else {
        Vec::new()
    };

    program.push(Instruction::Syscall {
        id: SYSCALL_NET_HTTP_SERVE,
        args,
        results: Vec::new(),
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
    use dot_ops::SYSCALL_NET_HTTP_SERVE;
    use dot_vm::{Instruction, Reg, Value};
    use std::path::PathBuf;
    use std::sync::Mutex;
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

    #[test]
    fn build_program_uses_http_serve_syscall() {
        let program = build_program(None).expect("program should build");
        assert_eq!(
            program,
            vec![
                Instruction::Syscall {
                    id: SYSCALL_NET_HTTP_SERVE,
                    args: vec![],
                    results: vec![],
                },
                Instruction::Halt
            ]
        );
    }

    #[test]
    fn build_program_passes_max_requests_when_set() {
        let program = build_program(Some(3)).expect("program should build");
        assert_eq!(
            program,
            vec![
                Instruction::LoadConst {
                    dst: Reg(0),
                    value: Value::I64(3),
                },
                Instruction::Syscall {
                    id: SYSCALL_NET_HTTP_SERVE,
                    args: vec![Reg(0)],
                    results: vec![],
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
}
