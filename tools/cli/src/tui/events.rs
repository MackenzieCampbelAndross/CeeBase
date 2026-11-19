use crossterm::event::{self, Event, KeyCode, KeyModifiers};
use std::time::Duration;
use crate::tui::app::App;

pub fn handle_events(app: &mut App) -> Result<(), Box<dyn std::error::Error>> {
    if event::poll(Duration::from_millis(100))? {
        if let Event::Key(key) = event::read()? {
            if key.modifiers.contains(KeyModifiers::CONTROL) {
                match key.code {
                    KeyCode::Char('c') | KeyCode::Char('q') => {
                        app.should_quit = true;
                        return Ok(());
                    }
                    KeyCode::Char('l') => {
                        app.output_lines.clear();
                        app.scroll_offset = 0;
                        return Ok(());
                    }
                    KeyCode::Char('u') => {
                        app.input.clear();
                        app.cursor_pos = 0;
                        return Ok(());
                    }
                    _ => {}
                }
            }

            match key.code {
                KeyCode::Char(c) => app.handle_char(c),
                KeyCode::Backspace => app.handle_backspace(),
                KeyCode::Delete => app.handle_delete(),
                KeyCode::Left => app.move_cursor_left(),
                KeyCode::Right => app.move_cursor_right(),
                KeyCode::Up => app.history_prev(),
                KeyCode::Down => app.history_next(),
                KeyCode::PageUp => app.scroll_up(),
                KeyCode::PageDown => app.scroll_down(),
                KeyCode::Tab => app.autocomplete(),
                KeyCode::Esc => {
                    app.input.clear();
                    app.cursor_pos = 0;
                }
                KeyCode::Enter => app.submit_input(),
                _ => {}
            }
        }
    }
    Ok(())
}
