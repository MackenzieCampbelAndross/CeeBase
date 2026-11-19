pub mod app;
pub mod events;
pub mod theme;
pub mod ui;

use crossterm::{
    event::{DisableBracketedPaste, EnableBracketedPaste},
    execute,
    terminal::{disable_raw_mode, enable_raw_mode, EnterAlternateScreen, LeaveAlternateScreen},
};
use ratatui::backend::CrosstermBackend;
use ratatui::Terminal;
use std::io;

use crate::engine::CeebaseClient;
use crate::tui::app::App;
use crate::tui::events::handle_events;
use crate::tui::ui::render_ui;

pub fn run_tui(client: CeebaseClient) -> Result<(), Box<dyn std::error::Error>> {
    enable_raw_mode()?;
    let mut stdout = io::stdout();
    let _ = execute!(stdout, EnterAlternateScreen, EnableBracketedPaste);
    let backend = CrosstermBackend::new(stdout);
    let mut terminal = Terminal::new(backend)?;

    let mut app = App::new(client);

    while !app.should_quit {
        terminal.draw(|f| render_ui(f, &app))?;
        handle_events(&mut app)?;
    }

    disable_raw_mode()?;
    let _ = execute!(terminal.backend_mut(), DisableBracketedPaste, LeaveAlternateScreen);
    terminal.show_cursor()?;
    Ok(())
}
