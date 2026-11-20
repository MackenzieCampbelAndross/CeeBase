use ratatui::style::Color;

#[allow(dead_code)]
pub struct Theme {
    pub dark_azure: Color,
    pub deep_azure: Color,
    pub light_azure: Color,
    pub primary_soft: Color,
    pub paper_white: Color,
    pub onyx: Color,
    pub onyx_raised: Color,
    pub charcoal: Color,
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
    dark_azure: Color::Rgb(0, 99, 142),     // #00638E Dark Azure (Primary)
    deep_azure: Color::Rgb(0, 74, 107),     // #004A6B Deep Azure (Pressed/Stop)
    light_azure: Color::Rgb(191, 216, 227), // #BFD8E3 Light Azure (Soft accent)
    primary_soft: Color::Rgb(140, 185, 204),// #8CB9CC Soft Azure
    paper_white: Color::Rgb(255, 255, 255), // #FFFFFF Paper White
    onyx: Color::Rgb(0, 0, 0),              // #000000 Onyx Deep
    onyx_raised: Color::Rgb(20, 20, 20),    // #141414 Onyx Elevated
    charcoal: Color::Rgb(43, 43, 43),       // #2B2B2B Charcoal
    powder_blue: Color::Rgb(0, 99, 142),    // Dark Azure for Brand badge
    lavender: Color::Rgb(191, 216, 227),    // Light Azure
    mint: Color::Rgb(140, 185, 204),        // Soft Azure for status indicator
    peach: Color::Rgb(254, 215, 170),       // Calm Gold
    rose_error: Color::Rgb(253, 164, 175),  // Rose Error
    border_subtle: Color::Rgb(43, 43, 43),  // Charcoal #2B2B2B
    muted_text: Color::Rgb(140, 185, 204),  // Muted Azure #8CB9CC
    background: Color::Rgb(0, 0, 0),        // Onyx #000000
};
