#![forbid(unsafe_code)]

mod commands;

use clap::{Parser, Subcommand};
use std::path::PathBuf;
use std::process::ExitCode;

#[derive(Parser)]
#[command(name = "dot", version, about = "Dotlanth CLI")]
struct Cli {
    #[command(subcommand)]
    command: Command,
}

#[derive(Subcommand)]
enum Command {
    /// Scaffold a runnable hello-api project.
    Init {
        /// Target directory to create.
        dir: PathBuf,
    },
    /// Run the current project (record-first).
    Run {
        /// Path to the `.dot` file (defaults to `./app.dot` when present).
        #[arg(long, short = 'f')]
        file: Option<PathBuf>,

        /// Stop after serving N HTTP requests (useful for tests).
        #[arg(long)]
        max_requests: Option<u64>,
    },
    /// Print persisted logs for a run id.
    Logs {
        /// Run id printed by `dot run`.
        run_id: String,
    },
}

fn main() -> ExitCode {
    let cli = Cli::parse();

    let result = match cli.command {
        Command::Init { dir } => commands::init::run(&dir),
        Command::Run { file, max_requests } => {
            commands::run::run(commands::run::RunOptions { file, max_requests })
        }
        Command::Logs { run_id } => commands::logs::run(&run_id),
    };

    match result {
        Ok(()) => ExitCode::SUCCESS,
        Err(message) => {
            eprintln!("{message}");
            ExitCode::from(1)
        }
    }
}
