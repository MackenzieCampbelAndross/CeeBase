use super::platform::{get_auth_token, get_linked_project, get_server_url};
use crate::engine::types::QueryResult;
use crate::table::{render_table, TableOptions};
use std::fs;
use std::path::Path;

pub fn execute_storage_buckets() -> Result<(), String> {
    let server_url = get_server_url();
    let proj = get_linked_project();
    let endpoint = format!("{}/storage/v1/bucket", server_url);

    let mut req = ureq::get(&endpoint).set("X-Project-Id", &proj);
    if let Some(token) = get_auth_token() {
        req = req.set("Authorization", &format!("Bearer {}", token));
    }

    let buckets: Vec<serde_json::Value> = match req.call() {
        Ok(res) => res.into_json().unwrap_or_default(),
        Err(_) => vec![],
    };

    let mut rows = Vec::new();
    for b in buckets {
        rows.push(vec![
            b.get("id").and_then(|v| v.as_str()).unwrap_or("").to_string(),
            b.get("name").and_then(|v| v.as_str()).unwrap_or("").to_string(),
            b.get("public").and_then(|v| v.as_bool()).unwrap_or(false).to_string(),
        ]);
    }

    if rows.is_empty() {
        println!("No buckets found for project '{}'", proj);
        return Ok(());
    }

    let qr = QueryResult {
        success: true,
        columns: vec!["Bucket ID".into(), "Name".into(), "Public".into()],
        rows,
        rows_affected: 0,
        execution_time_ms: 0.1,
        error: None,
    };

    print!("{}", render_table(&qr, &TableOptions::default()));
    Ok(())
}

pub fn execute_storage_upload(bucket: &str, file_path: &str, dest_path: Option<&str>) -> Result<(), String> {
    let server_url = get_server_url();
    let proj = get_linked_project();
    let path_obj = Path::new(file_path);
    if !path_obj.exists() {
        return Err(format!("File '{}' does not exist", file_path));
    }

    let file_bytes = fs::read(path_obj).map_err(|e| format!("Failed to read file: {}", e))?;
    let target_dest = dest_path.unwrap_or_else(|| path_obj.file_name().and_then(|s| s.to_str()).unwrap_or("file"));
    let endpoint = format!("{}/storage/v1/object/{}/{}", server_url, bucket, target_dest);

    let mut req = ureq::post(&endpoint)
        .set("X-Project-Id", &proj)
        .set("Content-Type", "application/octet-stream");

    if let Some(token) = get_auth_token() {
        req = req.set("Authorization", &format!("Bearer {}", token));
    }

    let _res = req.send_bytes(&file_bytes).map_err(|e| format!("Upload failed: {}", e))?;
    println!("File '{}' uploaded successfully to bucket '{}' at '{}'", file_path, bucket, target_dest);
    Ok(())
}

pub fn execute_storage_download(bucket: &str, remote_path: &str, local_path: Option<&str>) -> Result<(), String> {
    let server_url = get_server_url();
    let proj = get_linked_project();
    let endpoint = format!("{}/storage/v1/object/{}/{}", server_url, bucket, remote_path);

    let mut req = ureq::get(&endpoint).set("X-Project-Id", &proj);
    if let Some(token) = get_auth_token() {
        req = req.set("Authorization", &format!("Bearer {}", token));
    }

    let response = req.call().map_err(|e| format!("Download failed: {}", e))?;
    let mut reader = response.into_reader();
    let mut buf = Vec::new();
    std::io::copy(&mut reader, &mut buf).map_err(|e| format!("Failed to read payload: {}", e))?;

    let dest = local_path.unwrap_or(remote_path);
    fs::write(dest, buf).map_err(|e| format!("Failed to save file: {}", e))?;
    println!("Downloaded object '{}' from bucket '{}' to '{}'", remote_path, bucket, dest);
    Ok(())
}
