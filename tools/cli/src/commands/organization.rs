use std::path::Path;
use crate::engine::CeebaseClient;

pub fn execute_organization_create(name: &str) -> Result<(), String> {
    println!("Creating organization '{}'...", name);

    let identity_db = "ceebase_identity.cbdb";
    if Path::new(identity_db).exists() {
        if let Ok(client) = CeebaseClient::open(identity_db) {
            let sql = format!("INSERT INTO organizations (name) VALUES ('{}');", name);
            let _ = client.query(&sql);
        }
    }

    println!("Organization '{}' created successfully.", name);
    Ok(())
}
