#![forbid(unsafe_code)]

use crate::tui::app::App;
use crate::tui::state::{Capability, Focus, Mode};
use dot_db::{RunStatus, StoredRun};
use ratatui::Frame;
use ratatui::layout::{Constraint, Direction, Layout, Rect};
use ratatui::style::{Color, Modifier, Style};
use ratatui::text::{Line, Span};
use ratatui::widgets::{Block, Borders, List, ListItem, ListState, Paragraph, Tabs, Wrap};

pub(crate) fn render(frame: &mut Frame<'_>, app: &App) {
    let area = frame.area();
    let vertical = Layout::default()
        .direction(Direction::Vertical)
        .constraints([
            Constraint::Length(5),
            Constraint::Min(22),
            Constraint::Length(3),
        ])
        .split(area);

    render_header(frame, vertical[0], app);
    render_body(frame, vertical[1], app);
    render_footer(frame, vertical[2], app);
}

fn render_header(frame: &mut Frame<'_>, area: Rect, app: &App) {
    let sections = Layout::default()
        .direction(Direction::Vertical)
        .constraints([Constraint::Length(2), Constraint::Length(3)])
        .split(area);

    let title = Paragraph::new(vec![Line::from(vec![
        Span::styled("Dotlanth Capability Lab", title_style()),
        Span::raw("  "),
        Span::styled("Unified Demo/Dev ecosystem testbench", subtitle_style()),
    ])])
    .block(Block::default().borders(Borders::ALL).title("Overview"));
    frame.render_widget(title, sections[0]);

    let tabs = Tabs::new(
        Mode::ALL
            .iter()
            .map(|mode| Line::from(mode.label()))
            .collect::<Vec<_>>(),
    )
    .block(
        Block::default()
            .borders(Borders::ALL)
            .title("Modes")
            .border_style(if app.focus == Focus::Modes {
                Style::default().fg(Color::Rgb(207, 147, 70))
            } else {
                Style::default().fg(Color::DarkGray)
            }),
    )
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
        .constraints([Constraint::Percentage(28), Constraint::Percentage(72)])
        .split(area);

    render_left_column(frame, columns[0], app);
    render_right_column(frame, columns[1], app);
}

fn render_left_column(frame: &mut Frame<'_>, area: Rect, app: &App) {
    let sections = Layout::default()
        .direction(Direction::Vertical)
        .constraints([Constraint::Length(8), Constraint::Min(14)])
        .split(area);

    let project = Paragraph::new(vec![
        Line::from(vec![
            Span::styled("Project  ", label_style()),
            Span::raw(app.project_root().display().to_string()),
        ]),
        Line::from(vec![
            Span::styled("DotDB  ", label_style()),
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
            Span::styled("Job  ", label_style()),
            Span::raw(app.job_state.label()),
        ]),
    ])
    .block(
        Block::default()
            .borders(Borders::ALL)
            .title("Mission Summary"),
    );
    frame.render_widget(project, sections[0]);

    let capabilities = List::new(
        Capability::ALL
            .iter()
            .map(|capability| ListItem::new(Line::from(capability.label())))
            .collect::<Vec<_>>(),
    )
    .block(
        Block::default()
            .borders(Borders::ALL)
            .title("Capability Rail")
            .border_style(if app.focus == Focus::Capabilities {
                Style::default().fg(Color::Rgb(98, 163, 176))
            } else {
                Style::default().fg(Color::DarkGray)
            }),
    )
    .highlight_style(
        Style::default()
            .fg(Color::Black)
            .bg(Color::Rgb(98, 163, 176))
            .add_modifier(Modifier::BOLD),
    )
    .highlight_symbol(">> ");
    let mut state = ListState::default();
    state.select(Some(capability_index(app.capability)));
    frame.render_stateful_widget(capabilities, sections[1], &mut state);
}

fn render_right_column(frame: &mut Frame<'_>, area: Rect, app: &App) {
    let sections = Layout::default()
        .direction(Direction::Vertical)
        .constraints([
            Constraint::Length(5),
            Constraint::Length(9),
            Constraint::Length(8),
            Constraint::Min(10),
        ])
        .split(area);

    render_summary_row(frame, sections[0], app);
    render_actions(frame, sections[1], app);
    render_recent_runs(frame, sections[2], app);
    render_detail(frame, sections[3], app);
}

fn render_summary_row(frame: &mut Frame<'_>, area: Rect, app: &App) {
    let stats = Layout::default()
        .direction(Direction::Horizontal)
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
    .block(
        Block::default()
            .borders(Borders::ALL)
            .title("Action List")
            .border_style(if app.focus == Focus::Actions {
                Style::default().fg(Color::Rgb(207, 147, 70))
            } else {
                Style::default().fg(Color::DarkGray)
            }),
    )
    .highlight_style(
        Style::default()
            .fg(Color::Black)
            .bg(Color::Rgb(207, 147, 70))
            .add_modifier(Modifier::BOLD),
    )
    .highlight_symbol(">> ");
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
        .block(
            Block::default()
                .borders(Borders::ALL)
                .title("Recent Runs")
                .border_style(if app.focus == Focus::Runs {
                    Style::default().fg(Color::Rgb(88, 148, 186))
                } else {
                    Style::default().fg(Color::DarkGray)
                }),
        )
        .highlight_style(
            Style::default()
                .fg(Color::Black)
                .bg(Color::Rgb(88, 148, 186))
                .add_modifier(Modifier::BOLD),
        )
        .highlight_symbol(">> ");
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
        .block(
            Block::default()
                .borders(Borders::ALL)
                .title(app.detail_title.as_str())
                .border_style(if app.focus == Focus::Output {
                    Style::default().fg(Color::Rgb(140, 140, 140))
                } else {
                    Style::default().fg(Color::DarkGray)
                }),
        )
        .wrap(Wrap { trim: false })
        .scroll((app.output_scroll, 0));
    frame.render_widget(detail, area);
}

fn render_footer(frame: &mut Frame<'_>, area: Rect, app: &App) {
    let footer = Paragraph::new(vec![
        Line::from(vec![
            Span::styled("Keys  ", label_style()),
            Span::raw("Tab/Shift-Tab focus  "),
            Span::raw("h/j/k/l move  "),
            Span::raw("Enter execute  "),
            Span::raw("q quit  "),
            Span::raw("r refresh"),
        ]),
        Line::from(vec![
            Span::styled("Mode  ", label_style()),
            Span::raw(app.mode_label()),
            Span::raw("   "),
            Span::styled("Capability  ", label_style()),
            Span::raw(app.capability_label()),
            Span::raw("   "),
            Span::styled("Action  ", label_style()),
            Span::raw(app.action_label()),
            Span::raw("   "),
            Span::styled("Run  ", label_style()),
            Span::raw(app.selected_run_label()),
            Span::raw("   "),
            Span::styled("Bundle  ", label_style()),
            Span::raw(app.selected_bundle_label()),
            Span::raw("   "),
            Span::styled("Export  ", label_style()),
            Span::raw(app.selected_export_label()),
            Span::raw("   "),
            Span::styled("Fixture  ", label_style()),
            Span::raw(app.selected_fixture_label()),
        ]),
    ])
    .block(Block::default().borders(Borders::ALL));
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
    .block(Block::default().borders(Borders::ALL));
    frame.render_widget(widget, area);
}

fn render_run_item(run: &StoredRun) -> ListItem<'static> {
    let (status_label, color) = match run.status {
        RunStatus::Succeeded => ("OK", Color::Rgb(82, 160, 109)),
        RunStatus::Failed => ("FAIL", Color::Rgb(186, 89, 89)),
        RunStatus::Running => ("RUN", Color::Rgb(88, 148, 186)),
    };

    ListItem::new(Line::from(vec![
        Span::styled(
            format!("{status_label:<4}"),
            Style::default().fg(color).add_modifier(Modifier::BOLD),
        ),
        Span::raw(run.run_id.clone()),
    ]))
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
        assert!(rendered.contains("Action List"));
        assert!(rendered.contains("Recent Runs"));
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
        assert!(rendered.contains("Mode"));
        assert!(rendered.contains("Capability"));
        assert!(rendered.contains("Action"));
        assert!(rendered.contains("Run"));
        assert!(rendered.contains("Fixture"));
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
