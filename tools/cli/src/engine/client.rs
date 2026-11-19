use std::ffi::{CStr, CString};
use std::ptr;
use std::time::Instant;
use crate::engine::ffi::{CeebaseHandle, CeebaseLib, CeebaseStmtHandle};
use crate::engine::types::{QueryResult, TableSummary};

pub struct CeebaseClient {
    lib: Option<CeebaseLib>,
    handle: *mut CeebaseHandle,
    pub target: String,
    pub is_remote: bool,
}

unsafe impl Send for CeebaseClient {}
unsafe impl Sync for CeebaseClient {}

impl CeebaseClient {
    pub fn open(target: &str) -> Result<Self, String> {
        if target.starts_with("http://") || target.starts_with("https://") {
            return Ok(Self {
                lib: None,
                handle: ptr::null_mut(),
                target: target.to_string(),
                is_remote: true,
            });
        }

        let lib = CeebaseLib::load()?;
        let c_path = CString::new(target).map_err(|e| e.to_string())?;
        let mut handle: *mut CeebaseHandle = ptr::null_mut();

        let rc = unsafe { (lib.open)(c_path.as_ptr(), &mut handle) };
        if rc != 0 || handle.is_null() {
            return Err(format!("Failed to open database '{}' (error code: {})", target, rc));
        }

        Ok(Self {
            lib: Some(lib),
            handle,
            target: target.to_string(),
            is_remote: false,
        })
    }

    pub fn get_version(&self) -> String {
        if self.is_remote {
            return "1.0.0-remote".to_string();
        }
        if let Some(ref lib) = self.lib {
            unsafe {
                let v = (lib.version)();
                if !v.is_null() {
                    return CStr::from_ptr(v).to_string_lossy().to_string();
                }
            }
        }
        "1.0.0".to_string()
    }

    pub fn query(&self, sql: &str) -> QueryResult {
        let start = Instant::now();
        if self.is_remote {
            return self.query_remote(sql, start);
        }

        let lib = match self.lib.as_ref() {
            Some(l) => l,
            None => return QueryResult::error("CeeBase library not loaded", 0.0),
        };

        let c_sql = match CString::new(sql) {
            Ok(s) => s,
            Err(e) => return QueryResult::error(format!("Invalid SQL string: {}", e), 0.0),
        };

        let mut stmt: *mut CeebaseStmtHandle = ptr::null_mut();
        let prep_rc = unsafe { (lib.prepare)(self.handle, c_sql.as_ptr(), &mut stmt) };
        if prep_rc != 0 || stmt.is_null() {
            let err_msg = unsafe {
                let msg_ptr = (lib.errmsg)(self.handle);
                if !msg_ptr.is_null() {
                    CStr::from_ptr(msg_ptr).to_string_lossy().to_string()
                } else {
                    format!("Prepare failed with code {}", prep_rc)
                }
            };
            return QueryResult::error(err_msg, start.elapsed().as_secs_f64() * 1000.0);
        }

        let mut columns = Vec::new();
        let mut rows = Vec::new();
        let mut step_rc;
        loop {
            step_rc = unsafe { (lib.step)(stmt) };
            if step_rc == 100 {
                // Populate columns on first row if not yet populated
                if columns.is_empty() {
                    let col_count = unsafe { (lib.column_count)(stmt) } as usize;
                    for i in 0..col_count {
                        let col_name_ptr = unsafe { (lib.column_name)(stmt, i as i32) };
                        let col_name = if !col_name_ptr.is_null() {
                            unsafe { CStr::from_ptr(col_name_ptr).to_string_lossy().to_string() }
                        } else {
                            format!("col_{}", i)
                        };
                        columns.push(col_name);
                    }
                }

                // ROW AVAILABLE
                let col_count = columns.len();
                let mut row = Vec::with_capacity(col_count);
                for i in 0..col_count {
                    let col_type = unsafe { (lib.column_type)(stmt, i as i32) };
                    let val_str = match col_type {
                        1 => {
                            // INTEGER
                            let v = unsafe { (lib.column_int)(stmt, i as i32) };
                            v.to_string()
                        }
                        2 => {
                            // BIGINT
                            let v = unsafe { (lib.column_int64)(stmt, i as i32) };
                            v.to_string()
                        }
                        3 | 4 => {
                            // FLOAT | DOUBLE
                            let v = unsafe { (lib.column_double)(stmt, i as i32) };
                            format!("{:.2}", v)
                        }
                        5 => {
                            // BOOLEAN
                            let v = unsafe { (lib.column_boolean)(stmt, i as i32) };
                            if v != 0 { "true".to_string() } else { "false".to_string() }
                        }
                        6 => {
                            // VARCHAR / TEXT
                            let txt_ptr = unsafe { (lib.column_text)(stmt, i as i32) };
                            if !txt_ptr.is_null() {
                                unsafe { CStr::from_ptr(txt_ptr).to_string_lossy().to_string() }
                            } else {
                                "NULL".to_string()
                            }
                        }
                        7 => "NULL".to_string(),
                        _ => {
                            let txt_ptr = unsafe { (lib.column_text)(stmt, i as i32) };
                            if !txt_ptr.is_null() {
                                unsafe { CStr::from_ptr(txt_ptr).to_string_lossy().to_string() }
                            } else {
                                "NULL".to_string()
                            }
                        }
                    };
                    row.push(val_str);
                }
                rows.push(row);
            } else if step_rc == 101 {
                // DONE
                if columns.is_empty() {
                    let col_count = unsafe { (lib.column_count)(stmt) } as usize;
                    for i in 0..col_count {
                        let col_name_ptr = unsafe { (lib.column_name)(stmt, i as i32) };
                        let col_name = if !col_name_ptr.is_null() {
                            unsafe { CStr::from_ptr(col_name_ptr).to_string_lossy().to_string() }
                        } else {
                            format!("col_{}", i)
                        };
                        columns.push(col_name);
                    }
                }
                break;
            } else {
                // ERROR
                let err_msg = unsafe {
                    let msg_ptr = (lib.errmsg)(self.handle);
                    if !msg_ptr.is_null() {
                        CStr::from_ptr(msg_ptr).to_string_lossy().to_string()
                    } else {
                        format!("Step failed with code {}", step_rc)
                    }
                };
                unsafe { (lib.finalize)(stmt) };
                return QueryResult::error(err_msg, start.elapsed().as_secs_f64() * 1000.0);
            }
        }

        unsafe { (lib.finalize)(stmt) };
        let elapsed_ms = start.elapsed().as_secs_f64() * 1000.0;
        let rows_affected = rows.len();

        QueryResult {
            success: true,
            columns,
            rows,
            rows_affected,
            execution_time_ms: (elapsed_ms * 1000.0).round() / 1000.0,
            error: None,
        }
    }

    fn query_remote(&self, sql: &str, start: Instant) -> QueryResult {
        let endpoint = format!("{}/api/v1/query", self.target.trim_end_matches('/'));
        let body = serde_json::json!({ "query": sql });
        let resp = match ureq::post(&endpoint).send_json(body) {
            Ok(r) => r,
            Err(e) => {
                return QueryResult::error(format!("HTTP request failed: {}", e), start.elapsed().as_secs_f64() * 1000.0);
            }
        };

        #[derive(serde::Deserialize)]
        struct RemoteResp {
            success: bool,
            columns: Option<Vec<String>>,
            rows: Option<Vec<Vec<serde_json::Value>>>,
            rows_affected: Option<usize>,
            error: Option<String>,
        }

        match resp.into_json::<RemoteResp>() {
            Ok(data) => {
                let elapsed_ms = start.elapsed().as_secs_f64() * 1000.0;
                if !data.success {
                    return QueryResult::error(data.error.unwrap_or_else(|| "Unknown error".to_string()), elapsed_ms);
                }
                let cols = data.columns.unwrap_or_default();
                let string_rows = data.rows.unwrap_or_default().into_iter().map(|row| {
                    row.into_iter().map(|v| match v {
                        serde_json::Value::Null => "NULL".to_string(),
                        serde_json::Value::Bool(b) => b.to_string(),
                        serde_json::Value::Number(n) => n.to_string(),
                        serde_json::Value::String(s) => s,
                        other => other.to_string(),
                    }).collect()
                }).collect();

                QueryResult {
                    success: true,
                    columns: cols,
                    rows: string_rows,
                    rows_affected: data.rows_affected.unwrap_or(0),
                    execution_time_ms: (elapsed_ms * 1000.0).round() / 1000.0,
                    error: None,
                }
            }
            Err(e) => QueryResult::error(format!("Failed to parse remote JSON: {}", e), start.elapsed().as_secs_f64() * 1000.0),
        }
    }

    pub fn list_tables(&self) -> Result<Vec<TableSummary>, String> {
        let res = self.query("SHOW TABLES;");
        if !res.success {
            return Err(res.error.unwrap_or_else(|| "Failed to list tables".to_string()));
        }

        let mut summaries = Vec::new();
        for r in res.rows {
            if let Some(tbl) = r.first() {
                let cnt_res = self.query(&format!("SELECT COUNT(*) FROM {};", tbl));
                let count = cnt_res.rows.first().and_then(|row| row.first()).and_then(|s| s.parse::<usize>().ok()).unwrap_or(0);
                summaries.push(TableSummary {
                    name: tbl.clone(),
                    row_count: count,
                });
            }
        }
        Ok(summaries)
    }
}

impl Drop for CeebaseClient {
    fn drop(&mut self) {
        if !self.handle.is_null() {
            if let Some(ref lib) = self.lib {
                unsafe {
                    (lib.close)(self.handle);
                }
            }
            self.handle = ptr::null_mut();
        }
    }
}
