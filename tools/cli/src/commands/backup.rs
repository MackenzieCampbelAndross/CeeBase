use std::fs;
use std::path::{Path, PathBuf};
use chrono::Utc;

pub fn execute_backup(database: &str, output: Option<&str>) -> Result<(), String> {
    let db_path = Path::new(database);
    if !db_path.exists() {
        return Err(format!("Database file '{}' does not exist", database));
    }

    let backup_dir = Path::new("backups");
    if !backup_dir.exists() {
        fs::create_dir_all(backup_dir).map_err(|e| format!("Failed to create backups directory: {}", e))?;
    }

    let out_path: PathBuf = match output {
        Some(p) => PathBuf::from(p),
        None => {
            let timestamp = Utc::now().format("%Y%m%d_%H%M%S");
            let stem = db_path.file_stem().unwrap_or_default().to_string_lossy();
            backup_dir.join(format!("{}_backup_{}.cbdb", stem, timestamp))
        }
    };

    println!("Creating backup of '{}' -> '{}'...", db_path.display(), out_path.display());

    fs::copy(db_path, &out_path).map_err(|e| format!("Failed to copy database: {}", e))?;

    let wal_path = format!("{}.wal", database);
    if Path::new(&wal_path).exists() {
        let wal_out = format!("{}.wal", out_path.display());
        let _ = fs::copy(&wal_path, &wal_out);
    }

    let bytes = fs::metadata(&out_path).map(|m| m.len()).unwrap_or(0);
    println!("Backup completed successfully: {} bytes written to '{}'", bytes, out_path.display());
    Ok(())
}

pub fn execute_restore(backup_file: &str, target_db: Option<&str>) -> Result<(), String> {
    let bkp_path = Path::new(backup_file);
    if !bkp_path.exists() {
        return Err(format!("Backup file '{}' does not exist", backup_file));
    }

    let dest = target_db.unwrap_or("ceebase.cbdb");
    let dest_path = Path::new(dest);

    println!("Restoring backup from '{}' into '{}'...", bkp_path.display(), dest_path.display());

    fs::copy(bkp_path, dest_path).map_err(|e| format!("Failed to restore database file: {}", e))?;

    let bkp_wal = format!("{}.wal", backup_file);
    let dest_wal = format!("{}.wal", dest);
    if Path::new(&bkp_wal).exists() {
        let _ = fs::copy(&bkp_wal, &dest_wal);
    }

    println!("Database '{}' successfully restored from backup.", dest);
    Ok(())
}
