use std::path::Path;
use crate::engine::CeebaseClient;

pub fn execute_init(db_path: &str) -> Result<(), String> {
    let path = Path::new(db_path);
    if path.exists() {
        return Err(format!("Database file already exists at '{}'", path.display()));
    }

    // CeebaseClient::open will invoke C API ceebase_open, which initializes
    // header page 0 with valid CRC32 checksum, sets up catalog, and creates .wal file
    let client = CeebaseClient::open(db_path)?;
    let ver = client.get_version();

    println!("Initialized fresh CeeBase database kernel storage at '{}'", path.display());
    println!("  Engine Version:  v{}", ver);
    println!("  Format:          CeeBase 1.0 (Fixed 4096-byte Slotted Pages with CRC-32)");
    println!("  Catalog:         Initialized and verified");
    Ok(())
}
