use super::platform::{clear_auth_token, get_auth_token, get_server_url, save_auth_token};
use serde_json::json;

pub fn execute_login(email: Option<&str>, password: Option<&str>) -> Result<(), String> {
    let server_url = get_server_url();
    let email_val = email.map(|s| s.to_string()).unwrap_or_else(|| {
        print!("Enter developer email: ");
        let mut input = String::new();
        std::io::stdin().read_line(&mut input).unwrap();
        input.trim().to_string()
    });

    let password_val = password.map(|s| s.to_string()).unwrap_or_else(|| {
        print!("Enter password: ");
        let mut input = String::new();
        std::io::stdin().read_line(&mut input).unwrap();
        input.trim().to_string()
    });

    let endpoint = format!("{}/api/v1/auth/login", server_url);
    let payload = json!({
        "email": email_val,
        "password": password_val
    });

    let res = ureq::post(&endpoint)
        .send_json(payload)
        .map_err(|e| format!("Login failed: {}", e))?;

    let body: serde_json::Value = res.into_json()
        .map_err(|e| format!("Failed to parse response: {}", e))?;

    if let Some(token) = body.get("token").and_then(|t| t.as_str()) {
        save_auth_token(&email_val, token)?;
        println!("Successfully authenticated as '{}'", email_val);
        println!("Session token stored securely in ~/.ceebase/credentials.json");
        Ok(())
    } else {
        Err("Login response did not contain a valid session token".to_string())
    }
}

pub fn execute_logout() -> Result<(), String> {
    clear_auth_token()?;
    println!("Successfully logged out. Stored credentials removed.");
    Ok(())
}
