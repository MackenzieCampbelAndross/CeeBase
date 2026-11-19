use std::ffi::{c_char, c_double, c_int};
use std::path::{Path, PathBuf};
use libloading::{Library, Symbol};

pub enum CeebaseHandle {}
pub enum CeebaseStmtHandle {}

#[allow(dead_code)]
pub struct CeebaseLib {
    _lib: Library,
    pub open: Symbol<'static, unsafe extern "C" fn(*const c_char, *mut *mut CeebaseHandle) -> c_int>,
    pub close: Symbol<'static, unsafe extern "C" fn(*mut CeebaseHandle) -> c_int>,
    pub exec: Symbol<'static, unsafe extern "C" fn(*mut CeebaseHandle, *const c_char) -> c_int>,
    pub prepare: Symbol<'static, unsafe extern "C" fn(*mut CeebaseHandle, *const c_char, *mut *mut CeebaseStmtHandle) -> c_int>,
    pub step: Symbol<'static, unsafe extern "C" fn(*mut CeebaseStmtHandle) -> c_int>,
    pub reset: Symbol<'static, unsafe extern "C" fn(*mut CeebaseStmtHandle) -> c_int>,
    pub finalize: Symbol<'static, unsafe extern "C" fn(*mut CeebaseStmtHandle) -> c_int>,
    pub column_count: Symbol<'static, unsafe extern "C" fn(*mut CeebaseStmtHandle) -> c_int>,
    pub column_name: Symbol<'static, unsafe extern "C" fn(*mut CeebaseStmtHandle, c_int) -> *const c_char>,
    pub column_type: Symbol<'static, unsafe extern "C" fn(*mut CeebaseStmtHandle, c_int) -> c_int>,
    pub column_text: Symbol<'static, unsafe extern "C" fn(*mut CeebaseStmtHandle, c_int) -> *const c_char>,
    pub column_int: Symbol<'static, unsafe extern "C" fn(*mut CeebaseStmtHandle, c_int) -> c_int>,
    pub column_int64: Symbol<'static, unsafe extern "C" fn(*mut CeebaseStmtHandle, c_int) -> i64>,
    pub column_double: Symbol<'static, unsafe extern "C" fn(*mut CeebaseStmtHandle, c_int) -> c_double>,
    pub column_boolean: Symbol<'static, unsafe extern "C" fn(*mut CeebaseStmtHandle, c_int) -> c_int>,
    pub errmsg: Symbol<'static, unsafe extern "C" fn(*mut CeebaseHandle) -> *const c_char>,
    pub version: Symbol<'static, unsafe extern "C" fn() -> *const c_char>,
}

impl CeebaseLib {
    pub fn load() -> Result<Self, String> {
        let candidates = [
            std::env::var("CEEBASE_LIB_PATH").unwrap_or_default(),
            "build/libceebase.dll".to_string(),
            "libceebase.dll".to_string(),
            "../build/libceebase.dll".to_string(),
            "../../build/libceebase.dll".to_string(),
            "build/bin/libceebase.dll".to_string(),
            "bin/libceebase.dll".to_string(),
            "lib/libceebase.dll".to_string(),
            "build/libceebase.so".to_string(),
            "libceebase.so".to_string(),
            "build/libceebase.dylib".to_string(),
            "libceebase.dylib".to_string(),
        ];

        let mut found_path: Option<PathBuf> = None;
        for c in &candidates {
            if !c.is_empty() {
                let p = Path::new(c);
                if p.exists() {
                    found_path = Some(p.to_path_buf());
                    break;
                }
            }
        }

        let path = found_path.ok_or_else(|| {
            "Could not locate libceebase.dll / libceebase.so. Please set CEEBASE_LIB_PATH or ensure build/libceebase.dll exists.".to_string()
        })?;

        unsafe {
            let lib = Library::new(&path).map_err(|e| format!("Failed to load library {}: {}", path.display(), e))?;
            // Transmute symbols to 'static because they live as long as CeebaseLib
            let open = std::mem::transmute(lib.get::<unsafe extern "C" fn(*const c_char, *mut *mut CeebaseHandle) -> c_int>(b"ceebase_open\0")
                .map_err(|e| e.to_string())?);
            let close = std::mem::transmute(lib.get::<unsafe extern "C" fn(*mut CeebaseHandle) -> c_int>(b"ceebase_close\0")
                .map_err(|e| e.to_string())?);
            let exec = std::mem::transmute(lib.get::<unsafe extern "C" fn(*mut CeebaseHandle, *const c_char) -> c_int>(b"ceebase_exec\0")
                .map_err(|e| e.to_string())?);
            let prepare = std::mem::transmute(lib.get::<unsafe extern "C" fn(*mut CeebaseHandle, *const c_char, *mut *mut CeebaseStmtHandle) -> c_int>(b"ceebase_prepare\0")
                .map_err(|e| e.to_string())?);
            let step = std::mem::transmute(lib.get::<unsafe extern "C" fn(*mut CeebaseStmtHandle) -> c_int>(b"ceebase_step\0")
                .map_err(|e| e.to_string())?);
            let reset = std::mem::transmute(lib.get::<unsafe extern "C" fn(*mut CeebaseStmtHandle) -> c_int>(b"ceebase_reset\0")
                .map_err(|e| e.to_string())?);
            let finalize = std::mem::transmute(lib.get::<unsafe extern "C" fn(*mut CeebaseStmtHandle) -> c_int>(b"ceebase_finalize\0")
                .map_err(|e| e.to_string())?);
            let column_count = std::mem::transmute(lib.get::<unsafe extern "C" fn(*mut CeebaseStmtHandle) -> c_int>(b"ceebase_column_count\0")
                .map_err(|e| e.to_string())?);
            let column_name = std::mem::transmute(lib.get::<unsafe extern "C" fn(*mut CeebaseStmtHandle, c_int) -> *const c_char>(b"ceebase_column_name\0")
                .map_err(|e| e.to_string())?);
            let column_type = std::mem::transmute(lib.get::<unsafe extern "C" fn(*mut CeebaseStmtHandle, c_int) -> c_int>(b"ceebase_column_type\0")
                .map_err(|e| e.to_string())?);
            let column_text = std::mem::transmute(lib.get::<unsafe extern "C" fn(*mut CeebaseStmtHandle, c_int) -> *const c_char>(b"ceebase_column_text\0")
                .map_err(|e| e.to_string())?);
            let column_int = std::mem::transmute(lib.get::<unsafe extern "C" fn(*mut CeebaseStmtHandle, c_int) -> c_int>(b"ceebase_column_int\0")
                .map_err(|e| e.to_string())?);
            let column_int64 = std::mem::transmute(lib.get::<unsafe extern "C" fn(*mut CeebaseStmtHandle, c_int) -> i64>(b"ceebase_column_int64\0")
                .map_err(|e| e.to_string())?);
            let column_double = std::mem::transmute(lib.get::<unsafe extern "C" fn(*mut CeebaseStmtHandle, c_int) -> c_double>(b"ceebase_column_double\0")
                .map_err(|e| e.to_string())?);
            let column_boolean = std::mem::transmute(lib.get::<unsafe extern "C" fn(*mut CeebaseStmtHandle, c_int) -> c_int>(b"ceebase_column_boolean\0")
                .map_err(|e| e.to_string())?);
            let errmsg = std::mem::transmute(lib.get::<unsafe extern "C" fn(*mut CeebaseHandle) -> *const c_char>(b"ceebase_errmsg\0")
                .map_err(|e| e.to_string())?);
            let version = std::mem::transmute(lib.get::<unsafe extern "C" fn() -> *const c_char>(b"ceebase_version\0")
                .map_err(|e| e.to_string())?);

            Ok(Self {
                _lib: lib,
                open,
                close,
                exec,
                prepare,
                step,
                reset,
                finalize,
                column_count,
                column_name,
                column_type,
                column_text,
                column_int,
                column_int64,
                column_double,
                column_boolean,
                errmsg,
                version,
            })
        }
    }
}
