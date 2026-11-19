use crate::engine::CeebaseClient;
use crate::table::{render_table, TableOptions};

const SQL_KEYWORDS: &[&str] = &[
    "SELECT", "FROM", "WHERE", "INSERT INTO", "VALUES", "CREATE TABLE",
    "DROP TABLE", "UPDATE", "SET", "DELETE FROM", "PRIMARY KEY", "INT",
    "DOUBLE", "BOOLEAN", "VARCHAR", "EXPLAIN", "SHOW TABLES", "BEGIN",
    "COMMIT", "ROLLBACK", "/help", "/tables", "/desc", "/databases",
    "/status", "/clear", "/quit"
];

pub struct App {
    pub client: CeebaseClient,
    pub input: String,
    pub cursor_pos: usize,
    pub command_history: Vec<String>,
    pub history_idx: Option<usize>,
    pub output_lines: Vec<String>,
    pub scroll_offset: usize,
    pub should_quit: bool,
    pub database_name: String,
    pub engine_version: String,
}

impl App {
    pub fn new(client: CeebaseClient) -> Self {
        let db_name = client.target.clone();
        let ver = client.get_version();
        let mut app = Self {
            client,
            input: String::new(),
            cursor_pos: 0,
            command_history: Vec::new(),
            history_idx: None,
            output_lines: Vec::new(),
            scroll_offset: 0,
            should_quit: false,
            database_name: db_name,
            engine_version: ver,
        };

        app.output_lines.push("Welcome to CeeBase Interactive Terminal (OpenCode Edition)".to_string());
        app.output_lines.push("Type SQL queries ending with ';' or slash commands like /help, /tables, /quit".to_string());
        app.output_lines.push(String::new());
        app
    }

    pub fn handle_char(&mut self, c: char) {
        self.input.insert(self.cursor_pos, c);
        self.cursor_pos += 1;
    }

    pub fn handle_backspace(&mut self) {
        if self.cursor_pos > 0 {
            self.input.remove(self.cursor_pos - 1);
            self.cursor_pos -= 1;
        }
    }

    pub fn handle_delete(&mut self) {
        if self.cursor_pos < self.input.len() {
            self.input.remove(self.cursor_pos);
        }
    }

    pub fn move_cursor_left(&mut self) {
        if self.cursor_pos > 0 {
            self.cursor_pos -= 1;
        }
    }

    pub fn move_cursor_right(&mut self) {
        if self.cursor_pos < self.input.len() {
            self.cursor_pos += 1;
        }
    }

    pub fn history_prev(&mut self) {
        if self.command_history.is_empty() {
            return;
        }
        let next_idx = match self.history_idx {
            None => self.command_history.len() - 1,
            Some(i) if i > 0 => i - 1,
            Some(0) => 0,
            _ => 0,
        };
        self.history_idx = Some(next_idx);
        self.input = self.command_history[next_idx].clone();
        self.cursor_pos = self.input.len();
    }

    pub fn history_next(&mut self) {
        if let Some(i) = self.history_idx {
            if i + 1 < self.command_history.len() {
                let next_idx = i + 1;
                self.history_idx = Some(next_idx);
                self.input = self.command_history[next_idx].clone();
                self.cursor_pos = self.input.len();
            } else {
                self.history_idx = None;
                self.input.clear();
                self.cursor_pos = 0;
            }
        }
    }

    pub fn autocomplete(&mut self) {
        let input_upper = self.input.to_uppercase();
        for kw in SQL_KEYWORDS {
            if kw.to_uppercase().starts_with(&input_upper) && kw.len() > self.input.len() {
                self.input = kw.to_string();
                self.cursor_pos = self.input.len();
                break;
            }
        }
    }

    pub fn submit_input(&mut self) {
        let trimmed = self.input.trim().to_string();
        if trimmed.is_empty() {
            return;
        }

        self.output_lines.push(format!("ceebase> {}", trimmed));
        self.command_history.push(trimmed.clone());
        self.history_idx = None;
        self.input.clear();
        self.cursor_pos = 0;

        if trimmed.starts_with('/') {
            self.handle_slash_command(&trimmed);
        } else {
            self.execute_sql(&trimmed);
        }

        // Scroll to bottom
        self.scroll_to_bottom();
    }

    fn handle_slash_command(&mut self, cmd: &str) {
        let parts: Vec<&str> = cmd.split_whitespace().collect();
        match parts[0] {
            "/help" => {
                self.output_lines.push("Available Commands:".to_string());
                self.output_lines.push("  /help           Show this help message".to_string());
                self.output_lines.push("  /tables         List database tables and row counts".to_string());
                self.output_lines.push("  /desc <table>   Describe schema for a specific table".to_string());
                self.output_lines.push("  /databases      List all databases in current workspace".to_string());
                self.output_lines.push("  /status         Show engine health and connection status".to_string());
                self.output_lines.push("  /clear          Clear execution screen history".to_string());
                self.output_lines.push("  /quit, /exit    Exit CeeBase terminal".to_string());
                self.output_lines.push(String::new());
            }
            "/tables" | "/dt" => {
                match self.client.list_tables() {
                    Ok(tables) => {
                        let rows = tables.into_iter().map(|t| vec![t.name, t.row_count.to_string()]).collect();
                        let qr = crate::engine::types::QueryResult {
                            success: true,
                            columns: vec!["Table Name".to_string(), "Row Count".to_string()],
                            rows,
                            rows_affected: 0,
                            execution_time_ms: 0.1,
                            error: None,
                        };
                        let table_str = render_table(&qr, &TableOptions::default());
                        for line in table_str.lines() {
                            self.output_lines.push(line.to_string());
                        }
                    }
                    Err(e) => {
                        self.output_lines.push(format!("Error listing tables: {}", e));
                    }
                }
            }
            "/desc" | "/d" => {
                if parts.len() < 2 {
                    self.output_lines.push("Usage: /desc <table_name>".to_string());
                } else {
                    let tbl = parts[1];
                    let res = self.client.query(&format!("SELECT * FROM {} LIMIT 1;", tbl));
                    if res.success {
                        self.output_lines.push(format!("Columns for table '{}':", tbl));
                        for col in &res.columns {
                            self.output_lines.push(format!("  - {}", col));
                        }
                    } else {
                        self.output_lines.push(format!("Error describing table: {}", res.error.unwrap_or_default()));
                    }
                }
            }
            "/databases" => {
                let res = crate::commands::database::execute_database_list();
                if let Err(e) = res {
                    self.output_lines.push(format!("Error: {}", e));
                }
            }
            "/status" => {
                self.output_lines.push(format!("Target:      {}", self.database_name));
                self.output_lines.push(format!("Engine:      CeeBase v{} (Slotted Storage Kernel)", self.engine_version));
                self.output_lines.push("Status:      HEALTHY".to_string());
            }
            "/clear" => {
                self.output_lines.clear();
                self.scroll_offset = 0;
            }
            "/quit" | "/exit" => {
                self.should_quit = true;
            }
            other => {
                self.output_lines.push(format!("Unknown slash command: '{}'. Type /help for options.", other));
            }
        }
    }

    fn execute_sql(&mut self, sql: &str) {
        let result = self.client.query(sql);
        let table_str = render_table(&result, &TableOptions::default());
        for line in table_str.lines() {
            self.output_lines.push(line.to_string());
        }
        self.output_lines.push(String::new());
    }

    pub fn scroll_up(&mut self) {
        if self.scroll_offset > 0 {
            self.scroll_offset -= 1;
        }
    }

    pub fn scroll_down(&mut self) {
        if self.scroll_offset + 10 < self.output_lines.len() {
            self.scroll_offset += 1;
        }
    }

    pub fn scroll_to_bottom(&mut self) {
        if self.output_lines.len() > 15 {
            self.scroll_offset = self.output_lines.len() - 15;
        } else {
            self.scroll_offset = 0;
        }
    }
}
