use std::fs::File;
use std::io::Read;
use std::path::Path;
use crate::engine::CeebaseClient;
use crate::table::{render_table, TableOptions};
use crate::engine::types::QueryResult;

pub fn execute_inspect(db_path: &str) -> Result<(), String> {
    let path = Path::new(db_path);
    if !path.exists() {
        return Err(format!("Database file '{}' does not exist", db_path));
    }

    let mut file = File::open(path).map_err(|e| format!("Failed to open file: {}", e))?;
    let file_len = file.metadata().map(|m| m.len()).unwrap_or(0);
    let total_pages = file_len / 4096;

    let mut header_buf = [0u8; 4096];
    file.read_exact(&mut header_buf).map_err(|e| format!("Failed to read header page: {}", e))?;

    let magic = u32::from_le_bytes(header_buf[0..4].try_into().unwrap());
    let v_maj = u16::from_le_bytes(header_buf[4..6].try_into().unwrap());
    let v_min = u16::from_le_bytes(header_buf[6..8].try_into().unwrap());
    let page_size = u32::from_le_bytes(header_buf[8..12].try_into().unwrap());
    let pages_recorded = u32::from_le_bytes(header_buf[12..16].try_into().unwrap());
    let free_head = u32::from_le_bytes(header_buf[16..20].try_into().unwrap());
    let cat_root = u32::from_le_bytes(header_buf[20..24].try_into().unwrap());

    println!("============================================================");
    println!("           CeeBase Storage Kernel Inspection                ");
    println!("============================================================");
    println!("Database File:         {}", path.display());
    println!("File Size:             {} bytes ({:.2} KB)", file_len, file_len as f64 / 1024.0);
    println!("Header Magic:          0x{:08X} ({})", magic, if magic == 0x43424442 { "CBDB - Valid" } else { "Unknown" });
    println!("Format Version:        v{}.{}", v_maj, v_min);
    println!("Fixed Page Size:       {} bytes", page_size);
    println!("Total Pages Allocated: {} (Disk size matches: {})", pages_recorded, pages_recorded as u64 == total_pages);
    println!("Free Page List Head:   Page {}", free_head);
    println!("Catalog Root Page:     Page {}", cat_root);
    println!("------------------------------------------------------------");

    // Attempt to inspect tables via client
    if let Ok(client) = CeebaseClient::open(db_path) {
        if let Ok(tables) = client.list_tables() {
            println!("Catalog Tables ({}):", tables.len());
            let mut rows = Vec::new();
            for t in tables {
                rows.push(vec![t.name, t.row_count.to_string(), "SlottedPage".to_string()]);
            }
            let qr = QueryResult {
                success: true,
                columns: vec!["Table Name".to_string(), "Estimated Rows".to_string(), "Storage Type".to_string()],
                rows,
                rows_affected: 0,
                execution_time_ms: 0.2,
                error: None,
            };
            print!("{}", render_table(&qr, &TableOptions::default()));
        }
    }

    Ok(())
}
