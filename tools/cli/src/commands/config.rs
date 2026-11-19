use std::fs;
use std::path::PathBuf;
use serde_json::{json, Value};

fn get_config_path() -> PathBuf {
    let home = std::env::var("USERPROFILE")
        .or_else(|_| std::env::var("HOME"))
        .unwrap_or_else(|_| ".".to_string());
    let dir = PathBuf::from(home).join(".ceebase");
    let _ = fs::create_dir_all(&dir);
    dir.join("config.json")
}

pub fn execute_config(get_key: Option<&str>, set_kv: Option<&str>) -> Result<(), String> {
    let path = get_config_path();
    let mut config: Value = if path.exists() {
        let content = fs::read_to_string(&path).unwrap_or_else(|_| "{}".to_string());
        serde_json::from_str(&content).unwrap_or(json!({}))
    } else {
        json!({
            "default_database": "ceebase.cbdb",
            "server_host": "127.0.0.1",
            "server_port": 8080,
            "theme": "powder_blue",
            "auto_commit": true
        })
    };

    if let Some(kv) = set_kv {
        let parts: Vec<&str> = kv.splitn(2, '=').collect();
        if parts.len() != 2 {
            return Err("Format must be key=value".to_string());
        }
        let key = parts[0].trim();
        let val = parts[1].trim();
        config[key] = json!(val);
        fs::write(&path, serde_json::to_string_pretty(&config).unwrap())
            .map_err(|e| format!("Failed to write config: {}", e))?;
        println!("Set config '{}' = '{}'", key, val);
        return Ok(());
    }

    if let Some(key) = get_key {
        if let Some(val) = config.get(key) {
            println!("{}", val);
        } else {
            println!("(null)");
        }
        return Ok(());
    }

    println!("CeeBase Configuration ({}):", path.display());
    println!("{}", serde_json::to_string_pretty(&config).unwrap_or_default());
    Ok(())
}
