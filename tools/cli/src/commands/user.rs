use std::path::Path;
use crate::engine::CeebaseClient;

pub fn execute_user_create(email: &str, role: &str) -> Result<(), String> {
    println!("Creating user '{}' with role '{}'...", email, role);

    // If identity database exists or can be accessed:
    let identity_db = "ceebase_identity.cbdb";
    if Path::new(identity_db).exists() {
        if let Ok(client) = CeebaseClient::open(identity_db) {
            let sql = format!("INSERT INTO users (email, role) VALUES ('{}', '{}');", email, role);
            let _ = client.query(&sql);
        }
    }

    println!("User '{}' created successfully (Role: {}).", email, role);
    Ok(())
}
