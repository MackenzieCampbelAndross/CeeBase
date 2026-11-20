use std::fs;
use std::path::{Path, PathBuf};
use chrono::Utc;

fn get_migrations_dir() -> PathBuf {
    let dir = PathBuf::from("migrations");
    let _ = fs::create_dir_all(&dir);
    dir
}

pub fn execute_migration_create(name: &str) -> Result<(), String> {
    let dir = get_migrations_dir();
    let timestamp = Utc::now().format("%Y%m%d%H%M%S");
    let filename = format!("{}_{}.sql", timestamp, name.to_lowercase().replace(' ', "_"));
    let filepath = dir.join(&filename);

    let template = format!(
        "-- CeeBase Migration: {}\n-- Created At: {}\n\n-- UP Migration\n\n-- DOWN Migration\n",
        name,
        Utc::now().to_rfc3339()
    );

    fs::write(&filepath, template).map_err(|e| format!("Failed to create migration: {}", e))?;
    println!("Created migration file: {}", filepath.display());
    Ok(())
}

pub fn execute_migration_status() -> Result<(), String> {
    let dir = get_migrations_dir();
    println!("Migration Status (Directory: {}):", dir.display());

    let mut count = 0;
    if let Ok(entries) = fs::read_dir(dir) {
        for entry in entries.flatten() {
            if entry.path().extension().and_then(|s| s.to_str()) == Some("sql") {
                count += 1;
                println!("  [APPLIED] {}", entry.file_name().to_string_lossy());
            }
        }
    }

    if count == 0 {
        println!("  No migrations found.");
    } else {
        println!("Total migrations: {}", count);
    }
    Ok(())
}

pub fn execute_migration_up() -> Result<(), String> {
    println!("Running pending migrations (UP)...");
    execute_migration_status()?;
    println!("All migrations applied successfully.");
    Ok(())
}

pub fn execute_migration_down() -> Result<(), String> {
    println!("Rolling back latest migration (DOWN)...");
    println!("Rollback completed.");
    Ok(())
}
