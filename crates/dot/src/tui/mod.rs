#![forbid(unsafe_code)]

mod app;
mod fixtures;
mod jobs;
mod model;
mod registry;
mod render;
mod state;

use crate::tui::app::App;
use crossterm::event::{self, Event, KeyCode, KeyEvent, KeyEventKind, KeyModifiers};
use crossterm::execute;
use crossterm::terminal::{
    EnterAlternateScreen, LeaveAlternateScreen, disable_raw_mode, enable_raw_mode,
};
use ratatui::Terminal;
use ratatui::backend::CrosstermBackend;
use std::io::IsTerminal;
use std::io::{self, Stdout};
use std::time::Duration;

pub(crate) fn launch(entrypoint: &'static str) -> Result<(), String> {
    let stdin_is_terminal = io::stdin().is_terminal();
    let stdout_is_terminal = io::stdout().is_terminal();
    let term = std::env::var("TERM").ok();
    validate_launch_environment(
        entrypoint,
        stdin_is_terminal,
        stdout_is_terminal,
        term.as_deref(),
    )?;
    run()
}

fn run() -> Result<(), String> {
    let project_root = std::env::current_dir()
        .map_err(|error| format!("failed to read current directory: {error}"))?;
    let mut app = App::load(project_root)?;
    let mut terminal = TerminalSession::enter()?;

    loop {
        terminal
            .terminal_mut()
            .draw(|frame| render::render(frame, &app))
            .map_err(|error| format!("failed to draw TUI frame: {error}"))?;

        if !event::poll(Duration::from_millis(250))
            .map_err(|error| format!("failed to poll terminal events: {error}"))?
        {
            continue;
        }

        let Event::Key(key) =
            event::read().map_err(|error| format!("failed to read terminal event: {error}"))?
        else {
            continue;
        };

        if key.kind == KeyEventKind::Press && handle_key(&mut app, key)? {
            break;
        }
    }

    Ok(())
}

fn handle_key(app: &mut App, key: KeyEvent) -> Result<bool, String> {
    if key.modifiers.contains(KeyModifiers::CONTROL) && key.code == KeyCode::Char('c') {
        return Ok(true);
    }

    match key.code {
        KeyCode::Char('q') => Ok(true),
        KeyCode::Tab => {
            app.next_focus();
            Ok(false)
        }
        KeyCode::BackTab => {
            app.previous_focus();
            Ok(false)
        }
        KeyCode::Down | KeyCode::Char('j') | KeyCode::Right | KeyCode::Char('l') => {
            app.move_next();
            Ok(false)
        }
        KeyCode::Up | KeyCode::Char('k') | KeyCode::Left | KeyCode::Char('h') => {
            app.move_previous();
            Ok(false)
        }
        KeyCode::Enter => {
            if let Err(error) = app.activate() {
                app.present_error("Action Error", "Action failed", error);
            }
            Ok(false)
        }
        KeyCode::Char('r') => {
            if let Err(error) = app.refresh() {
                app.present_error("Refresh Error", "Refresh failed", error);
            } else {
                app.status_line = "Dashboard refreshed.".to_owned();
            }
            Ok(false)
        }
        _ => Ok(false),
    }
}

struct TerminalSession {
    terminal: Terminal<CrosstermBackend<Stdout>>,
}

impl TerminalSession {
    fn enter() -> Result<Self, String> {
        enable_raw_mode().map_err(|error| format!("failed to enable raw mode: {error}"))?;
        let mut stdout = io::stdout();
        execute!(stdout, EnterAlternateScreen)
            .map_err(|error| format!("failed to enter alternate screen: {error}"))?;
        let backend = CrosstermBackend::new(stdout);
        let terminal = Terminal::new(backend)
            .map_err(|error| format!("failed to initialize ratatui terminal: {error}"))?;
        Ok(Self { terminal })
    }

    fn terminal_mut(&mut self) -> &mut Terminal<CrosstermBackend<Stdout>> {
        &mut self.terminal
    }
}

impl Drop for TerminalSession {
    fn drop(&mut self) {
        let _ = disable_raw_mode();
        let _ = execute!(self.terminal.backend_mut(), LeaveAlternateScreen);
        let _ = self.terminal.show_cursor();
    }
}

fn validate_launch_environment(
    entrypoint: &str,
    stdin_is_terminal: bool,
    stdout_is_terminal: bool,
    term: Option<&str>,
) -> Result<(), String> {
    if !stdin_is_terminal || !stdout_is_terminal || matches!(term, Some("dumb")) {
        return Err(format!(
            "{entrypoint} requires an interactive terminal. Run `{entrypoint}` in a TTY, or use an explicit scripted subcommand such as `dot run` or `dot inspect` for automation."
        ));
    }

    Ok(())
}

#[cfg(test)]
mod tests {
    use super::{handle_key, validate_launch_environment};
    use crate::tui::app::App;
    use crate::tui::registry;
    use crate::tui::state::{Capability, Focus, Mode};
    use crossterm::event::{KeyCode, KeyEvent, KeyModifiers};

    #[test]
    fn validate_launch_environment_rejects_non_interactive_terminals() {
        let error = validate_launch_environment("dot tui", false, false, Some("xterm-256color"))
            .expect_err("non-interactive launch must fail");
        assert!(error.contains("interactive terminal"));
        assert!(error.contains("dot tui"));
    }

    #[test]
    fn validate_launch_environment_rejects_dumb_term() {
        let error = validate_launch_environment("dot", true, true, Some("dumb"))
            .expect_err("TERM=dumb launch must fail");
        assert!(error.contains("dot requires an interactive terminal"));
    }

    #[test]
    fn validate_launch_environment_allows_real_terminal() {
        validate_launch_environment("dot", true, true, Some("xterm-256color"))
            .expect("interactive launch should succeed");
    }

    #[test]
    fn enter_key_captures_action_errors_without_exiting_tui() {
        let mut app = App::test_fixture();
        app.mode = Mode::Demo;
        app.capability = Capability::ArtifactsInspection;
        app.actions = registry::actions_for(Mode::Demo, Capability::ArtifactsInspection);
        app.action_index = 0;
        app.focus = Focus::Actions;
        app.recent_runs.clear();
        app.selected_bundle_ref = None;

        let should_quit = handle_key(&mut app, KeyEvent::new(KeyCode::Enter, KeyModifiers::NONE))
            .expect("enter handling should not bubble action errors");

        assert!(!should_quit);
        assert_eq!(app.detail_title, "Action Error");
        assert!(
            app.status_line.contains("Action failed"),
            "status line should capture the failure"
        );
    }
}
