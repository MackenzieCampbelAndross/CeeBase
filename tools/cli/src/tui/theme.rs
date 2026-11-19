use ratatui::style::Color;

#[allow(dead_code)]
pub struct Theme {
    pub powder_blue: Color,
    pub lavender: Color,
    pub mint: Color,
    pub peach: Color,
    pub rose_error: Color,
    pub border_subtle: Color,
    pub muted_text: Color,
    pub background: Color,
}

pub const PASTEL_THEME: Theme = Theme {
    powder_blue: Color::Rgb(147, 197, 253),   // Soft Powder Blue
    lavender: Color::Rgb(196, 181, 253),      // Calm Lavender
    mint: Color::Rgb(110, 231, 183),          // Soft Mint Green
    peach: Color::Rgb(254, 215, 170),         // Calm Peach / Gold
    rose_error: Color::Rgb(253, 164, 175),    // Calm Pastel Red / Rose
    border_subtle: Color::Rgb(71, 85, 105),   // Slate 600
    muted_text: Color::Rgb(148, 163, 184),    // Slate 400
    background: Color::Reset,
};
