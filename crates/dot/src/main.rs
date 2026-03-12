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
    /// Print a stable summary for a recorded run bundle.
    Inspect {
        /// Run id printed by `dot run`.
        run_id: String,
    },
    /// Export a recorded run bundle to a directory.
    ExportArtifacts {
        /// Run id printed by `dot run`.
        run_id: String,

        /// Output directory for the exported bundle.
        #[arg(long)]
        out: PathBuf,
    },
    /// Replay a recorded run's input snapshot into a new run.
    Replay {
        /// Run id printed by `dot run`.
        run_id: Option<String>,

        /// Path to an exported bundle directory.
        #[arg(long, conflicts_with = "run_id")]
        bundle: Option<PathBuf>,
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
        Command::Inspect { run_id } => commands::inspect::run(&run_id),
        Command::ExportArtifacts { run_id, out } => commands::export_artifacts::run(&run_id, &out),
        Command::Replay { run_id, bundle } => {
            commands::replay::run(run_id.as_deref(), bundle.as_deref())
        }
    };

    match result {
        Ok(()) => ExitCode::SUCCESS,
        Err(message) => {
            eprintln!("{message}");
            ExitCode::from(1)
        }
    }
}
