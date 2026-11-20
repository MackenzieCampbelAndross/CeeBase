use super::platform::{get_auth_token, get_linked_project, get_server_url};
use serde_json::json;
use std::fs;
use std::path::Path;

pub fn execute_functions_deploy(name: &str, file_path: &str) -> Result<(), String> {
    let server_url = get_server_url();
    let proj = get_linked_project();
    let code = fs::read_to_string(file_path).map_err(|e| format!("Failed to read function code: {}", e))?;

    let endpoint = format!("{}/functions/v1", server_url);
    let payload = json!({
        "name": name,
        "code": code
    });

    let mut req = ureq::post(&endpoint).set("X-Project-Id", &proj);
    if let Some(token) = get_auth_token() {
        req = req.set("Authorization", &format!("Bearer {}", token));
    }

    let res = req.send_json(payload).map_err(|e| format!("Deploy failed: {}", e))?;
    println!("Function '{}' deployed successfully to project '{}'!", name, proj);
    Ok(())
}

pub fn execute_functions_invoke(name: &str, payload_json: Option<&str>) -> Result<(), String> {
    let server_url = get_server_url();
    let proj = get_linked_project();
    let endpoint = format!("{}/functions/v1/{}/invoke", server_url, name);

    let parsed_payload: serde_json::Value = if let Some(p) = payload_json {
        serde_json::from_str(p).map_err(|e| format!("Invalid JSON payload: {}", e))?
    } else {
        json!({})
    };

    let mut req = ureq::post(&endpoint).set("X-Project-Id", &proj);
    if let Some(token) = get_auth_token() {
        req = req.set("Authorization", &format!("Bearer {}", token));
    }

    let res = req.send_json(parsed_payload).map_err(|e| format!("Invocation failed: {}", e))?;
    let body: serde_json::Value = res.into_json().map_err(|e| format!("Invalid response: {}", e))?;
    println!("{}", serde_json::to_string_pretty(&body).unwrap());
    Ok(())
}

pub fn execute_functions_logs(name: &str) -> Result<(), String> {
    let server_url = get_server_url();
    let proj = get_linked_project();
    let endpoint = format!("{}/functions/v1/{}/logs", server_url, name);

    let mut req = ureq::get(&endpoint).set("X-Project-Id", &proj);
    if let Some(token) = get_auth_token() {
        req = req.set("Authorization", &format!("Bearer {}", token));
    }

    match req.call() {
        Ok(res) => {
            let logs: Vec<serde_json::Value> = res.into_json().unwrap_or_default();
            for l in logs {
                println!("{}", serde_json::to_string_pretty(&l).unwrap());
            }
            Ok(())
        }
        Err(e) => {
            println!("No remote logs found for function '{}'", name);
            Ok(())
        }
    }
}
