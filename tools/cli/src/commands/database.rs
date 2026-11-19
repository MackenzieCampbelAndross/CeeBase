use std::fs;
use std::path::Path;
use crate::commands::init::execute_init;
use crate::table::{render_table, TableOptions};
use crate::engine::types::QueryResult;

pub fn execute_database_create(name: &str) -> Result<(), String> {
    let filename = if name.ends_with(".cbdb") {
        name.to_string()
    } else {
        format!("{}.cbdb", name)
    };
    execute_init(&filename)?;
    println!("Database '{}' successfully created.", name);
    Ok(())
}

pub fn execute_database_list() -> Result<(), String> {
    let mut dbs = Vec::new();
    let current_dir = Path::new(".");
    if let Ok(entries) = fs::read_dir(current_dir) {
        for entry in entries.flatten() {
            let path = entry.path();
            if let Some(ext) = path.extension() {
                if ext == "cbdb" || ext == "db" {
                    if let Ok(meta) = entry.metadata() {
                        let pages = meta.len() / 4096;
                        dbs.push(vec![
                            path.file_name().unwrap().to_string_lossy().to_string(),
                            format!("{} B", meta.len()),
                            pages.to_string(),
                            "ONLINE".to_string(),
                        ]);
                    }
                }
            }
        }
    }

    let qr = QueryResult {
        success: true,
        columns: vec![
            "Database".to_string(),
            "Size".to_string(),
            "Pages".to_string(),
            "Status".to_string(),
        ],
        rows: dbs,
        rows_affected: 0,
        execution_time_ms: 0.15,
        error: None,
    };

    print!("{}", render_table(&qr, &TableOptions::default()));
    Ok(())
}

pub fn execute_database_delete(name: &str) -> Result<(), String> {
    let filename = if name.ends_with(".cbdb") {
        name.to_string()
    } else {
        format!("{}.cbdb", name)
    };
    let path = Path::new(&filename);
    if !path.exists() {
        return Err(format!("Database '{}' does not exist.", filename));
    }

    fs::remove_file(path).map_err(|e| format!("Failed to delete database file: {}", e))?;
    let wal = format!("{}.wal", filename);
    if Path::new(&wal).exists() {
        let _ = fs::remove_file(&wal);
    }

    println!("Database '{}' deleted successfully.", name);
    Ok(())
}
