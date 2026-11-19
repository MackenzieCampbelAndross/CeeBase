use crate::engine::types::QueryResult;

pub struct TableOptions {
    pub max_col_width: usize,
    pub max_rows: usize,
    pub plain: bool,
}

impl Default for TableOptions {
    fn default() -> Self {
        Self {
            max_col_width: 40,
            max_rows: 100,
            plain: false,
        }
    }
}

pub fn render_table(result: &QueryResult, options: &TableOptions) -> String {
    if !result.success {
        if let Some(ref err) = result.error {
            return format!("Error: {}\n", err);
        }
        return "Unknown error\n".to_string();
    }

    if result.columns.is_empty() {
        return format!("Query OK, {} rows affected ({:.2} ms)\n", result.rows_affected, result.execution_time_ms);
    }

    let col_count = result.columns.len();
    let mut widths = vec![0; col_count];

    // Compute column widths based on headers
    for (i, col) in result.columns.iter().enumerate() {
        widths[i] = col.len().max(widths[i]);
    }

    // Inspect rows up to max_rows
    let display_rows = result.rows.iter().take(options.max_rows).collect::<Vec<_>>();
    for row in &display_rows {
        for (i, cell) in row.iter().enumerate() {
            if i < col_count {
                let cell_len = cell.len().min(options.max_col_width);
                widths[i] = widths[i].max(cell_len);
            }
        }
    }

    // Cap at max_col_width
    for w in &mut widths {
        *w = (*w).min(options.max_col_width).max(3);
    }

    let mut out = String::new();

    let (tl, tm, tr, bl, bm, br, ml, mm, mr, h, v) = if options.plain {
        ('+', '+', '+', '+', '+', '+', '+', '+', '+', '-', '|')
    } else {
        ('┌', '┬', '┐', '└', '┴', '┘', '├', '┼', '┤', '─', '│')
    };

    // Top border
    out.push(tl);
    for (i, w) in widths.iter().enumerate() {
        out.push_str(&h.to_string().repeat(w + 2));
        if i < col_count - 1 {
            out.push(tm);
        }
    }
    out.push(tr);
    out.push('\n');

    // Header row
    out.push(v);
    for (i, col) in result.columns.iter().enumerate() {
        let truncated = truncate_cell(col, widths[i]);
        out.push_str(&format!(" {:<width$} ", truncated, width = widths[i]));
        out.push(v);
    }
    out.push('\n');

    // Middle divider
    out.push(ml);
    for (i, w) in widths.iter().enumerate() {
        out.push_str(&h.to_string().repeat(w + 2));
        if i < col_count - 1 {
            out.push(mm);
        }
    }
    out.push(mr);
    out.push('\n');

    // Rows
    for row in &display_rows {
        out.push(v);
        for i in 0..col_count {
            let val = row.get(i).map(|s| s.as_str()).unwrap_or("NULL");
            let truncated = truncate_cell(val, widths[i]);
            out.push_str(&format!(" {:<width$} ", truncated, width = widths[i]));
            out.push(v);
        }
        out.push('\n');
    }

    // Bottom border
    out.push(bl);
    for (i, w) in widths.iter().enumerate() {
        out.push_str(&h.to_string().repeat(w + 2));
        if i < col_count - 1 {
            out.push(bm);
        }
    }
    out.push(br);
    out.push('\n');

    // Footer summary
    let total = result.rows.len();
    if total > options.max_rows {
        out.push_str(&format!(
            "Showing {} of {} rows (truncated) ({:.2} ms)\n",
            options.max_rows, total, result.execution_time_ms
        ));
    } else {
        out.push_str(&format!(
            "{} {} in set ({:.2} ms)\n",
            total,
            if total == 1 { "row" } else { "rows" },
            result.execution_time_ms
        ));
    }

    out
}

fn truncate_cell(text: &str, max_len: usize) -> String {
    if text.len() <= max_len {
        text.to_string()
    } else if max_len > 3 {
        format!("{}...", &text[..max_len - 3])
    } else {
        text[..max_len].to_string()
    }
}
