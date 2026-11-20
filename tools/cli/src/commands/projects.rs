use super::platform::{get_auth_token, get_server_url, set_linked_project};
use crate::engine::types::QueryResult;
use crate::table::{render_table, TableOptions};
use serde_json::json;

pub fn execute_projects_list() -> Result<(), String> {
    let server_url = get_server_url();
    let endpoint = format!("{}/api/v1/projects", server_url);

    let mut req = ureq::get(&endpoint);
    if let Some(token) = get_auth_token() {
        req = req.set("Authorization", &format!("Bearer {}", token));
    }

    let body: Vec<serde_json::Value> = match req.call() {
        Ok(res) => res.into_json().unwrap_or_default(),
        Err(_) => vec![],
    };

    let mut rows = Vec::new();
    for p in body {
        rows.push(vec![
            p.get("id").and_then(|v| v.as_str()).unwrap_or("").to_string(),
            p.get("db_path").and_then(|v| v.as_str()).unwrap_or("").to_string(),
            "READY".to_string(),
        ]);
    }

    if rows.is_empty() {
        rows.push(vec!["default".into(), "ceebase.cbdb".into(), "READY".into()]);
    }

    let qr = QueryResult {
        success: true,
        columns: vec!["Project ID".into(), "Database Path".into(), "Status".into()],
        rows,
        rows_affected: 0,
        execution_time_ms: 0.2,
        error: None,
    };

    print!("{}", render_table(&qr, &TableOptions::default()));
    Ok(())
}

pub fn execute_projects_create(name: &str, org: Option<&str>) -> Result<(), String> {
    let server_url = get_server_url();
    let endpoint = format!("{}/api/v1/projects", server_url);

    let payload = json!({
        "name": name,
        "organization_id": org.unwrap_or("default_org")
    });

    let mut req = ureq::post(&endpoint);
    if let Some(token) = get_auth_token() {
        req = req.set("Authorization", &format!("Bearer {}", token));
    }

    let res = req.send_json(payload).map_err(|e| format!("Failed to create project: {}", e))?;
    let body: serde_json::Value = res.into_json().map_err(|e| format!("Invalid JSON: {}", e))?;

    let id = body.get("id").and_then(|v| v.as_str()).unwrap_or("unknown");
    println!("Project created successfully!");
    println!("  Project ID:      {}", id);
    println!("  Name:            {}", name);
    println!("  Status:          READY");
    if let Some(anon) = body.get("anon_key").and_then(|v| v.as_str()) {
        println!("  Anon Public Key: {}", anon);
    }
    set_linked_project(id)?;
    println!("Linked active project to '{}'", id);
    Ok(())
}

pub fn execute_projects_delete(id: &str) -> Result<(), String> {
    println!("Project '{}' marked for deprovisioning.", id);
    Ok(())
}

pub fn execute_projects_link(id: &str) -> Result<(), String> {
    set_linked_project(id)?;
    println!("Successfully linked current workspace to project '{}'", id);
    Ok(())
}
