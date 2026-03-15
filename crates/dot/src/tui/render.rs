#![forbid(unsafe_code)]

use crate::tui::app::App;
use crate::tui::state::{Capability, Focus, Mode};
use dot_db::{RunStatus, StoredRun};
use ratatui::Frame;
use ratatui::layout::{Constraint, Direction, Layout, Rect};
use ratatui::style::{Color, Modifier, Style};
use ratatui::text::{Line, Span};
use ratatui::widgets::{
    Block, BorderType, Borders, List, ListItem, ListState, Paragraph, Tabs, Wrap,
};

pub(crate) fn render(frame: &mut Frame<'_>, app: &App) {
    let area = frame.area();
    let vertical = Layout::default()
        .direction(Direction::Vertical)
        .spacing(1)
        .constraints([
            Constraint::Length(7),
            Constraint::Min(20),
            Constraint::Length(4),
        ])
        .split(area);

    render_header(frame, vertical[0], app);
    render_body(frame, vertical[1], app);
    render_footer(frame, vertical[2], app);
}

fn render_header(frame: &mut Frame<'_>, area: Rect, app: &App) {
    let sections = Layout::default()
        .direction(Direction::Vertical)
        .spacing(1)
        .constraints([Constraint::Length(3), Constraint::Length(3)])
        .split(area);

    let title = Paragraph::new(vec![
        Line::from(vec![
            Span::styled("Dotlanth Capability Lab", title_style()),
            Span::raw("  "),
            Span::styled(
                "Focused TUI for runs, artifacts, and demos",
                subtitle_style(),
            ),
        ]),
        Line::from(vec![
            inline_label("Mode"),
            Span::raw(app.mode_label()),
            Span::raw("   "),
            inline_label("Capability"),
            Span::raw(app.capability_label()),
            Span::raw("   "),
            inline_label("Focus"),
            Span::raw(focus_label(app.focus)),
            Span::raw("   "),
            inline_label("Fixture"),
            Span::raw(app.selected_fixture_label()),
        ]),
    ])
    .block(panel_block("Overview", None, false));
    frame.render_widget(title, sections[0]);

    let tabs = Tabs::new(
        Mode::ALL
            .iter()
            .map(|mode| Line::from(mode.label()))
            .collect::<Vec<_>>(),
    )
    .block(panel_block(
        "Modes",
        Some(Color::Rgb(207, 147, 70)),
        app.focus == Focus::Modes,
    ))
    .select(match app.mode {
        Mode::Demo => 0,
        Mode::Dev => 1,
    })
    .highlight_style(
        Style::default()
            .fg(Color::Black)
            .bg(Color::Rgb(207, 147, 70))
            .add_modifier(Modifier::BOLD),
    );
    frame.render_widget(tabs, sections[1]);
}

fn render_body(frame: &mut Frame<'_>, area: Rect, app: &App) {
    let columns = Layout::default()
        .direction(Direction::Horizontal)
        .spacing(1)
        .constraints([Constraint::Percentage(30), Constraint::Percentage(70)])
        .split(area);

    render_left_column(frame, columns[0], app);
    render_right_column(frame, columns[1], app);
}

fn render_left_column(frame: &mut Frame<'_>, area: Rect, app: &App) {
    let sections = Layout::default()
        .direction(Direction::Vertical)
        .spacing(1)
        .constraints([Constraint::Length(10), Constraint::Min(12)])
        .split(area);

    let project = Paragraph::new(vec![
        Line::from(vec![
            Span::styled("Project   ", label_style()),
            Span::raw(app.project_root().display().to_string()),
        ]),
        Line::from(vec![
            Span::styled("DotDB     ", label_style()),
            status_chip(
                if app.db_available { "ready" } else { "missing" },
                app.db_available,
            ),
        ]),
        Line::from(vec![
            Span::styled("Dot File  ", label_style()),
            Span::raw(app.dot_path_label()),
        ]),
        Line::from(vec![
            Span::styled("Job       ", label_style()),
            Span::raw(app.job_state.label()),
        ]),
        Line::from(vec![
            Span::styled("Bundle    ", label_style()),
            Span::raw(app.selected_bundle_label()),
        ]),
    ])
    .block(panel_block("Workspace", None, false));
    frame.render_widget(project, sections[0]);

    let capabilities = List::new(
        Capability::ALL
            .iter()
            .map(|capability| ListItem::new(Line::from(capability.label())))
            .collect::<Vec<_>>(),
    )
    .block(panel_block(
        "Capability Rail",
        Some(Color::Rgb(98, 163, 176)),
        app.focus == Focus::Capabilities,
    ))
    .highlight_style(
        Style::default()
            .fg(Color::Black)
            .bg(Color::Rgb(98, 163, 176))
            .add_modifier(Modifier::BOLD),
    )
    .highlight_symbol("> ");
    let mut state = ListState::default();
    state.select(Some(capability_index(app.capability)));
    frame.render_stateful_widget(capabilities, sections[1], &mut state);
}

fn render_right_column(frame: &mut Frame<'_>, area: Rect, app: &App) {
    let sections = Layout::default()
        .direction(Direction::Vertical)
        .spacing(1)
        .constraints([
            Constraint::Length(5),
            Constraint::Length(11),
            Constraint::Min(8),
        ])
        .split(area);

    render_summary_row(frame, sections[0], app);
    render_activity_row(frame, sections[1], app);
    render_detail(frame, sections[2], app);
}

fn render_summary_row(frame: &mut Frame<'_>, area: Rect, app: &App) {
    let stats = Layout::default()
        .direction(Direction::Horizontal)
        .spacing(1)
        .constraints([
            Constraint::Percentage(25),
            Constraint::Percentage(25),
            Constraint::Percentage(25),
            Constraint::Percentage(25),
        ])
        .split(area);

    render_stat(
        frame,
        stats[0],
        "Succeeded",
        &app.success_count().to_string(),
        Color::Rgb(82, 160, 109),
    );
    render_stat(
        frame,
        stats[1],
        "Failed",
        &app.failed_count().to_string(),
        Color::Rgb(186, 89, 89),
    );
    render_stat(
        frame,
        stats[2],
        "Running",
        &app.running_count().to_string(),
        Color::Rgb(88, 148, 186),
    );
    render_stat(
        frame,
        stats[3],
        "Bundles",
        &app.bundle_count.to_string(),
        Color::Rgb(207, 147, 70),
    );
}

fn render_activity_row(frame: &mut Frame<'_>, area: Rect, app: &App) {
    let columns = Layout::default()
        .direction(Direction::Horizontal)
        .spacing(1)
        .constraints([Constraint::Percentage(54), Constraint::Percentage(46)])
        .split(area);

    render_actions(frame, columns[0], app);
    render_recent_runs(frame, columns[1], app);
}

fn render_actions(frame: &mut Frame<'_>, area: Rect, app: &App) {
    let actions = List::new(
        app.actions
            .iter()
            .map(|action| {
                ListItem::new(vec![
                    Line::from(Span::styled(
                        action.label,
                        Style::default().fg(Color::White),
                    )),
                    Line::from(Span::styled(
                        action.description,
                        Style::default().fg(Color::Gray),
                    )),
                ])
            })
            .collect::<Vec<_>>(),
    )
    .block(panel_block(
        "Actions",
        Some(Color::Rgb(207, 147, 70)),
        app.focus == Focus::Actions,
    ))
    .highlight_style(
        Style::default()
            .fg(Color::Black)
            .bg(Color::Rgb(207, 147, 70))
            .add_modifier(Modifier::BOLD),
    )
    .highlight_symbol("> ");
    let mut state = ListState::default();
    state.select((!app.actions.is_empty()).then_some(app.action_index));
    frame.render_stateful_widget(actions, area, &mut state);
}

fn render_recent_runs(frame: &mut Frame<'_>, area: Rect, app: &App) {
    let items = if app.recent_runs.is_empty() {
        vec![ListItem::new(Line::from("No runs recorded yet"))]
    } else {
        app.recent_runs
            .iter()
            .map(render_run_item)
            .collect::<Vec<_>>()
    };

    let runs = List::new(items)
        .block(panel_block(
            "Run Timeline",
            Some(Color::Rgb(88, 148, 186)),
            app.focus == Focus::Runs,
        ))
        .highlight_style(
            Style::default()
                .fg(Color::Black)
                .bg(Color::Rgb(88, 148, 186))
                .add_modifier(Modifier::BOLD),
        )
        .highlight_symbol("> ");
    let mut state = ListState::default();
    state.select((!app.recent_runs.is_empty()).then_some(app.run_index));
    frame.render_stateful_widget(runs, area, &mut state);
}

fn render_detail(frame: &mut Frame<'_>, area: Rect, app: &App) {
    let lines = app
        .detail_lines
        .iter()
        .cloned()
        .map(Line::from)
        .collect::<Vec<_>>();
    let detail = Paragraph::new(lines)
        .block(panel_block(
            app.detail_title.as_str(),
            Some(Color::Rgb(140, 140, 140)),
            app.focus == Focus::Output,
        ))
        .wrap(Wrap { trim: false })
        .scroll((app.output_scroll, 0));
    frame.render_widget(detail, area);
}

fn render_footer(frame: &mut Frame<'_>, area: Rect, app: &App) {
    let footer = Paragraph::new(vec![
        Line::from(vec![
            Span::styled("Status  ", label_style()),
            Span::raw(app.status_line.as_str()),
        ]),
        Line::from(vec![
            Span::styled("Keys  ", label_style()),
            Span::raw("Tab/Shift-Tab focus   "),
            Span::raw("h/j/k/l move   "),
            Span::raw("Enter run action   "),
            Span::raw("r refresh   "),
            Span::raw("q quit"),
        ]),
    ])
    .block(panel_block("Status", None, false));
    frame.render_widget(footer, area);
}

fn render_stat(frame: &mut Frame<'_>, area: Rect, label: &str, value: &str, accent: Color) {
    let widget = Paragraph::new(vec![
        Line::from(Span::styled(label, Style::default().fg(Color::Gray))),
        Line::from(Span::styled(
            value,
            Style::default().fg(accent).add_modifier(Modifier::BOLD),
        )),
    ])
    .block(panel_block("", Some(accent), false));
    frame.render_widget(widget, area);
}

fn render_run_item(run: &StoredRun) -> ListItem<'static> {
    let (status_label, color) = match run.status {
        RunStatus::Succeeded => ("OK", Color::Rgb(82, 160, 109)),
        RunStatus::Failed => ("FAIL", Color::Rgb(186, 89, 89)),
        RunStatus::Running => ("RUN", Color::Rgb(88, 148, 186)),
    };

    ListItem::new(vec![
        Line::from(vec![
            Span::styled(
                format!("{status_label:<4}"),
                Style::default().fg(color).add_modifier(Modifier::BOLD),
            ),
            Span::raw(run.run_id.clone()),
        ]),
        Line::from(vec![
            Span::styled("mode ", Style::default().fg(Color::Gray)),
            Span::raw(run.determinism_mode.clone()),
        ]),
    ])
}

fn capability_index(capability: Capability) -> usize {
    Capability::ALL
        .iter()
        .position(|candidate| *candidate == capability)
        .unwrap_or(0)
}

fn title_style() -> Style {
    Style::default()
        .fg(Color::Rgb(235, 229, 214))
        .add_modifier(Modifier::BOLD)
}

fn subtitle_style() -> Style {
    Style::default().fg(Color::Rgb(98, 163, 176))
}

fn label_style() -> Style {
    Style::default()
        .fg(Color::Rgb(207, 147, 70))
        .add_modifier(Modifier::BOLD)
}

fn inline_label(text: &'static str) -> Span<'static> {
    Span::styled(format!("{text}  "), label_style())
}

fn status_chip<'a>(text: &'a str, ok: bool) -> Span<'a> {
    Span::styled(
        text,
        Style::default()
            .fg(if ok {
                Color::Rgb(82, 160, 109)
            } else {
                Color::Rgb(186, 89, 89)
            })
            .add_modifier(Modifier::BOLD),
    )
}

fn panel_block<'a>(title: &'a str, accent: Option<Color>, focused: bool) -> Block<'a> {
    let border = match (accent, focused) {
        (Some(color), true) => Style::default().fg(color),
        _ => Style::default().fg(Color::DarkGray),
    };

    let title_line = if title.is_empty() {
        Line::default()
    } else {
        Line::from(Span::styled(
            title,
            Style::default()
                .fg(Color::Rgb(235, 229, 214))
                .add_modifier(Modifier::BOLD),
        ))
    };

    Block::default()
        .borders(Borders::ALL)
        .border_type(BorderType::Rounded)
        .border_style(border)
        .title(title_line)
}

fn focus_label(focus: Focus) -> &'static str {
    match focus {
        Focus::Modes => "Modes",
        Focus::Capabilities => "Capabilities",
        Focus::Actions => "Actions",
        Focus::Runs => "Runs",
        Focus::Output => "Detail",
    }
}

#[cfg(test)]
mod tests {
    use super::render;
    use crate::tui::app::App;
    use ratatui::Terminal;
    use ratatui::backend::TestBackend;
    use ratatui::buffer::Buffer;

    #[test]
    fn renders_mission_control_regions() {
        let app = App::test_fixture();
        let backend = TestBackend::new(120, 36);
        let mut terminal = Terminal::new(backend).expect("test terminal must create");
        terminal
            .draw(|frame| render(frame, &app))
            .expect("render must succeed");

        let rendered = buffer_text(terminal.backend().buffer());
        assert!(rendered.contains("Capability Rail"));
        assert!(rendered.contains("Actions"));
        assert!(rendered.contains("Run Timeline"));
        assert!(rendered.contains("Status"));
        assert!(rendered.contains("Validate Demo Fixture"));
    }

    #[test]
    fn capability_lab_layout_shows_demo_dev_and_capability_rail() {
        let app = App::test_fixture();
        let backend = TestBackend::new(120, 36);
        let mut terminal = Terminal::new(backend).expect("test terminal must create");
        terminal
            .draw(|frame| render(frame, &app))
            .expect("render must succeed");

        let rendered = buffer_text(terminal.backend().buffer());
        assert!(rendered.contains("Demo"));
        assert!(rendered.contains("Dev"));
        assert!(rendered.contains("Parse & Validate"));
        assert!(rendered.contains("Run & Serve"));
        assert!(rendered.contains("Artifacts & Inspection"));
    }

    #[test]
    fn capability_lab_footer_shows_selection_tuple() {
        let app = App::test_fixture();
        let backend = TestBackend::new(120, 36);
        let mut terminal = Terminal::new(backend).expect("test terminal must create");
        terminal
            .draw(|frame| render(frame, &app))
            .expect("render must succeed");

        let rendered = buffer_text(terminal.backend().buffer());
        assert!(rendered.contains("Tab/Shift-Tab"));
        assert!(rendered.contains("Enter run action"));
        assert!(rendered.contains("Capability lab ready"));
    }

    #[test]
    fn capability_lab_status_bar_shows_runtime_status_line() {
        let app = App::test_fixture();
        let backend = TestBackend::new(120, 36);
        let mut terminal = Terminal::new(backend).expect("test terminal must create");
        terminal
            .draw(|frame| render(frame, &app))
            .expect("render must succeed");

        let rendered = buffer_text(terminal.backend().buffer());
        assert!(rendered.contains("Status"));
        assert!(rendered.contains("Capability lab ready"));
    }

    fn buffer_text(buffer: &Buffer) -> String {
        let area = buffer.area();
        let mut out = String::new();
        for y in area.y..area.y + area.height {
            for x in area.x..area.x + area.width {
                out.push_str(buffer[(x, y)].symbol());
            }
            out.push('\n');
        }
        out
    }
}
