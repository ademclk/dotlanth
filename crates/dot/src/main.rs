#![forbid(unsafe_code)]

use std::path::PathBuf;
use std::process::ExitCode;

use dot_db::DotDb;
use dot_dsl::LoadError;
use dot_ops::{Host, HostError, OpValue, OpsHost, SYSCALL_NET_HTTP_SERVE};
use dot_rt::RuntimeContext;
use dot_sec::UnknownCapabilityError;

fn main() -> ExitCode {
    match try_main() {
        Ok(()) => ExitCode::SUCCESS,
        Err(error) => {
            eprintln!("{error}");
            ExitCode::FAILURE
        }
    }
}

fn try_main() -> Result<(), CliError> {
    let mut args = std::env::args().skip(1);
    let Some(command) = args.next() else {
        return Err(CliError::Usage);
    };

    match command.as_str() {
        "run" => {
            let Some(dot_path) = args.next() else {
                return Err(CliError::MissingDotPath);
            };
            if args.next().is_some() {
                return Err(CliError::Usage);
            }
            run(PathBuf::from(dot_path))
        }
        "help" | "--help" | "-h" => {
            print_usage();
            Ok(())
        }
        "version" | "--version" | "-V" => {
            println!("dot {}", env!("CARGO_PKG_VERSION"));
            Ok(())
        }
        other => Err(CliError::UnknownCommand(other.to_owned())),
    }
}

fn run(dot_path: PathBuf) -> Result<(), CliError> {
    let document = dot_dsl::load_and_validate(&dot_path).map_err(CliError::Dot)?;
    let context = RuntimeContext::from_dot_dsl(&document).map_err(CliError::Capability)?;

    let db = DotDb::open_default().map_err(CliError::Db)?;
    let mut host = OpsHost::new(context.capabilities().clone(), db).map_err(CliError::Host)?;

    host.configure_http_from_document(&document)
        .map_err(CliError::Host)?;

    let addr = host
        .http_addr()
        .ok_or_else(|| CliError::Host(HostError::new("missing http listener")))?;

    println!("Listening on http://{addr} (Ctrl+C to stop)");

    host.syscall(SYSCALL_NET_HTTP_SERVE, &[] as &[CliValue])
        .map_err(CliError::Host)?;

    Ok(())
}

#[derive(Clone, Debug, PartialEq)]
enum CliValue {
    #[allow(dead_code)]
    I64(i64),
    #[allow(dead_code)]
    Str(String),
}

impl OpValue for CliValue {
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

#[derive(Debug)]
enum CliError {
    Usage,
    MissingDotPath,
    UnknownCommand(String),
    Dot(LoadError),
    Capability(UnknownCapabilityError),
    Db(dot_db::DotDbError),
    Host(dot_ops::HostError),
}

impl std::fmt::Display for CliError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::Usage => write!(f, "usage:\n\n{}", usage()),
            Self::MissingDotPath => write!(f, "missing dot file path\n\n{}", usage()),
            Self::UnknownCommand(command) => {
                write!(f, "unknown command `{command}`\n\n{}", usage())
            }
            Self::Dot(error) => write!(f, "{error}"),
            Self::Capability(error) => write!(f, "{error}"),
            Self::Db(error) => write!(f, "{error}"),
            Self::Host(error) => write!(f, "{error}"),
        }
    }
}

impl std::error::Error for CliError {}

fn usage() -> &'static str {
    concat!(
        "  dot run <file.dot>\n",
        "  dot --help\n",
        "  dot --version\n",
    )
}

fn print_usage() {
    print!("{}", usage());
}

#[cfg(test)]
mod tests {
    use super::usage;

    #[test]
    fn usage_mentions_run_subcommand() {
        assert!(usage().contains("dot run <file.dot>"));
    }
}
