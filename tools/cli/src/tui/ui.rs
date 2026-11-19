use ratatui::layout::{Constraint, Direction, Layout, Rect};
use ratatui::style::{Color, Modifier, Style};
use ratatui::text::{Line, Span};
use ratatui::widgets::{Block, Borders, Paragraph};
use ratatui::Frame;

use crate::tui::app::App;
use crate::tui::theme::PASTEL_THEME;

pub fn render_ui(frame: &mut Frame, app: &App) {
    let chunks = Layout::default()
        .direction(Direction::Vertical)
        .constraints([
            Constraint::Length(3),  // Header bar
            Constraint::Min(5),     // Output / History view
            Constraint::Length(3),  // Persistent composer input
        ])
        .split(frame.area());

    render_header(frame, app, chunks[0]);
    render_output(frame, app, chunks[1]);
    render_composer(frame, app, chunks[2]);
}

fn render_header(frame: &mut Frame, app: &App, area: Rect) {
    let header_line = Line::from(vec![
        Span::styled(" CeeBase ", Style::default().fg(Color::Black).bg(PASTEL_THEME.powder_blue).add_modifier(Modifier::BOLD)),
        Span::raw("  "),
        Span::styled(&app.database_name, Style::default().fg(PASTEL_THEME.lavender).add_modifier(Modifier::BOLD)),
        Span::raw("  │  "),
        Span::styled(format!("v{}", app.engine_version), Style::default().fg(PASTEL_THEME.muted_text)),
        Span::raw("  │  "),
        Span::styled("● CONNECTED", Style::default().fg(PASTEL_THEME.mint)),
        Span::raw("   "),
        Span::styled("Tab: Complete  ↑/↓: History  /help: Commands  Ctrl+Q: Quit", Style::default().fg(PASTEL_THEME.muted_text)),
    ]);

    let block = Block::default()
        .borders(Borders::BOTTOM)
        .border_style(Style::default().fg(PASTEL_THEME.border_subtle));

    let paragraph = Paragraph::new(header_line).block(block);
    frame.render_widget(paragraph, area);
}

fn render_output(frame: &mut Frame, app: &App, area: Rect) {
    let visible_height = area.height as usize;
    let total_lines = app.output_lines.len();

    let start_idx = if total_lines > visible_height {
        app.scroll_offset.min(total_lines.saturating_sub(visible_height))
    } else {
        0
    };

    let slice = if start_idx < total_lines {
        &app.output_lines[start_idx..total_lines.min(start_idx + visible_height)]
    } else {
        &[]
    };

    let styled_lines: Vec<Line> = slice.iter().map(|s| {
        if s.starts_with("ceebase>") {
            Line::from(vec![
                Span::styled("ceebase> ", Style::default().fg(PASTEL_THEME.powder_blue).add_modifier(Modifier::BOLD)),
                Span::styled(&s[9..], Style::default().fg(PASTEL_THEME.lavender)),
            ])
        } else if s.starts_with("Error") {
            Line::from(Span::styled(s, Style::default().fg(PASTEL_THEME.rose_error)))
        } else if s.starts_with("Query OK") || s.contains("in set") {
            Line::from(Span::styled(s, Style::default().fg(PASTEL_THEME.mint)))
        } else {
            Line::from(Span::raw(s))
        }
    }).collect();

    let block = Block::default()
        .borders(Borders::NONE);

    let paragraph = Paragraph::new(styled_lines).block(block);
    frame.render_widget(paragraph, area);
}

fn render_composer(frame: &mut Frame, app: &App, area: Rect) {
    let input_line = Line::from(vec![
        Span::styled("sql> ", Style::default().fg(PASTEL_THEME.powder_blue).add_modifier(Modifier::BOLD)),
        Span::styled(&app.input, Style::default().fg(Color::White)),
    ]);

    let block = Block::default()
        .borders(Borders::ALL)
        .border_style(Style::default().fg(PASTEL_THEME.powder_blue))
        .title(Span::styled(" Composer ", Style::default().fg(PASTEL_THEME.powder_blue)));

    let paragraph = Paragraph::new(input_line).block(block);
    frame.render_widget(paragraph, area);

    // Set terminal cursor position
    frame.set_cursor_position((
        area.x + 6 + app.cursor_pos as u16,
        area.y + 1,
    ));
}
