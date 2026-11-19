use std::fs;
use std::path::Path;
use std::process::Command;

pub fn execute_start(port: u16, host: &str, background: bool) -> Result<(), String> {
    let script_path = Path::new("tools/studio/server.py");
    if !script_path.exists() {
        return Err("Could not find server entry point at tools/studio/server.py".to_string());
    }

    println!("Starting CeeBase Server on {}:{}...", host, port);

    if background {
        #[cfg(target_os = "windows")]
        {
            let _child = Command::new("powershell")
                .args(["-NoProfile", "-Command", &format!("Start-Process python -ArgumentList 'tools/studio/server.py --port {} --host {}' -WindowStyle Hidden", port, host)])
                .spawn()
                .map_err(|e| format!("Failed to spawn background server: {}", e))?;
            println!("CeeBase Server started in background. Listening on http://{}:{}", host, port);
        }
        #[cfg(not(target_os = "windows"))]
        {
            let _ = Command::new("python3")
                .args(["tools/studio/server.py", "--port", &port.to_string(), "--host", host])
                .spawn()
                .map_err(|e| format!("Failed to spawn background server: {}", e))?;
            println!("CeeBase Server started in background. Listening on http://{}:{}", host, port);
        }
    } else {
        println!("Running CeeBase Server in foreground (Press Ctrl+C to terminate)...");
        let mut child = Command::new("python")
            .args(["tools/studio/server.py", "--port", &port.to_string(), "--host", host])
            .spawn()
            .map_err(|e| format!("Failed to run server: {}", e))?;
        let _ = child.wait();
    }
    Ok(())
}

pub fn execute_stop(port: u16) -> Result<(), String> {
    println!("Stopping CeeBase Server on port {}...", port);
    #[cfg(target_os = "windows")]
    {
        // Find process listening on port
        let output = Command::new("powershell")
            .args(["-NoProfile", "-Command", &format!("Get-NetTCPConnection -LocalPort {} -ErrorAction SilentlyContinue | ForEach-Object {{ Stop-Process -Id $_.OwningProcess -Force }}", port)])
            .output()
            .map_err(|e| format!("Failed to execute stop command: {}", e))?;
        if output.status.success() {
            println!("CeeBase Server on port {} has been stopped.", port);
        } else {
            println!("No running CeeBase Server detected on port {}.", port);
        }
    }
    #[cfg(not(target_os = "windows"))]
    {
        let _ = Command::new("fuser")
            .args(["-k", &format!("{}/tcp", port)])
            .output();
        println!("CeeBase Server on port {} stopped.", port);
    }
    Ok(())
}

pub fn execute_status(target: &str) -> Result<(), String> {
    if target.starts_with("http://") || target.starts_with("https://") {
        let health_url = format!("{}/api/v1/health", target.trim_end_matches('/'));
        match ureq::get(&health_url).timeout(std::time::Duration::from_secs(3)).call() {
            Ok(resp) => {
                let body: serde_json::Value = resp.into_json().map_err(|e| e.to_string())?;
                println!("CeeBase Server Status: ONLINE (HTTP)");
                println!("  Target:      {}", target);
                println!("  Status:      {}", body["status"].as_str().unwrap_or("HEALTHY"));
                println!("  Version:     {}", body["version"].as_str().unwrap_or("1.0.0"));
                println!("  Uptime:      {} sec", body["uptime_seconds"].as_u64().unwrap_or(0));
            }
            Err(_) => {
                println!("CeeBase Server Status: OFFLINE");
                println!("  Target:      {}", target);
                println!("  Details:     Cannot connect to server at {}", health_url);
            }
        }
    } else {
        let p = Path::new(target);
        if p.exists() {
            let meta = fs::metadata(p).map_err(|e| e.to_string())?;
            println!("CeeBase Database File: ACTIVE");
            println!("  Path:        {}", p.display());
            println!("  Size:        {} bytes ({:.2} KB)", meta.len(), meta.len() as f64 / 1024.0);
            println!("  Pages:       {}", meta.len() / 4096);
            println!("  Engine:      Embedded Kernel (C++20 Slotted Storage)");
        } else {
            println!("CeeBase Database File: NOT FOUND");
            println!("  Path:        {}", p.display());
            println!("  Hint:        Run 'ceebase init {}' to initialize kernel storage.", p.display());
        }
    }
    Ok(())
}
