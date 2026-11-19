use serde::{Deserialize, Serialize};

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct QueryResult {
    pub success: bool,
    pub columns: Vec<String>,
    pub rows: Vec<Vec<String>>,
    pub rows_affected: usize,
    pub execution_time_ms: f64,
    pub error: Option<String>,
}

impl QueryResult {
    pub fn error(msg: impl Into<String>, elapsed_ms: f64) -> Self {
        Self {
            success: false,
            columns: Vec::new(),
            rows: Vec::new(),
            rows_affected: 0,
            execution_time_ms: elapsed_ms,
            error: Some(msg.into()),
        }
    }
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct TableSummary {
    pub name: String,
    pub row_count: usize,
}

#[allow(dead_code)]
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ColumnMetadata {
    pub name: String,
    pub data_type: String,
    pub nullable: bool,
}

#[allow(dead_code)]
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct DatabaseDiagnostics {
    pub database_path: String,
    pub file_size_bytes: u64,
    pub total_pages: u32,
    pub page_size: u32,
    pub magic: String,
    pub crc32_valid: bool,
}
