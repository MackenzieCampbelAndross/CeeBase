use std::fs;
use std::path::PathBuf;
use serde_json::{json, Value};

pub fn get_ceebase_dir() -> PathBuf {
    let home = std::env::var("USERPROFILE")
        .or_else(|_| std::env::var("HOME"))
        .unwrap_or_else(|_| ".".to_string());
    let dir = PathBuf::from(home).join(".ceebase");
    let _ = fs::create_dir_all(&dir);
    dir
}

pub fn get_server_url() -> String {
    let config_path = get_ceebase_dir().join("config.json");
    if config_path.exists() {
        if let Ok(content) = fs::read_to_string(config_path) {
            if let Ok(val) = serde_json::from_str::<Value>(&content) {
                if let Some(url) = val.get("server_url").and_then(|u| u.as_str()) {
                    return url.to_string();
                }
            }
        }
    }
    "http://127.0.0.1:8080".to_string()
}

pub fn get_auth_token() -> Option<String> {
    let creds_path = get_ceebase_dir().join("credentials.json");
    if creds_path.exists() {
        if let Ok(content) = fs::read_to_string(creds_path) {
            if let Ok(val) = serde_json::from_str::<Value>(&content) {
                return val.get("token").and_then(|t| t.as_str()).map(|s| s.to_string());
            }
        }
    }
    None
}

pub fn save_auth_token(email: &str, token: &str) -> Result<(), String> {
    let creds_path = get_ceebase_dir().join("credentials.json");
    let payload = json!({
        "email": email,
        "token": token,
        "updated_at": chrono::Utc::now().to_rfc3339()
    });
    fs::write(creds_path, serde_json::to_string_pretty(&payload).unwrap())
        .map_err(|e| format!("Failed to save credentials: {}", e))
}

pub fn clear_auth_token() -> Result<(), String> {
    let creds_path = get_ceebase_dir().join("credentials.json");
    if creds_path.exists() {
        let _ = fs::remove_file(creds_path);
    }
    Ok(())
}

pub fn get_linked_project() -> String {
    let proj_file = PathBuf::from(".ceebase_project");
    if proj_file.exists() {
        if let Ok(content) = fs::read_to_string(proj_file) {
            return content.trim().to_string();
        }
    }
    "default".to_string()
}

pub fn set_linked_project(proj_id: &str) -> Result<(), String> {
    fs::write(".ceebase_project", proj_id)
        .map_err(|e| format!("Failed to link project: {}", e))
}
