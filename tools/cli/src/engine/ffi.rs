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
        let mut candidates: Vec<PathBuf> = Vec::new();

        // 1. Explicit environment variable override
        if let Ok(env_path) = std::env::var("CEEBASE_LIB_PATH") {
            if !env_path.is_empty() {
                candidates.push(PathBuf::from(env_path));
            }
        }

        // 2. In the same directory as the current executable (crucial for standalone install ~/.ceebase/bin)
        if let Ok(exe_path) = std::env::current_exe() {
            if let Some(parent) = exe_path.parent() {
                candidates.push(parent.join("libceebase.dylib"));
                candidates.push(parent.join("ceebase.dylib"));
                candidates.push(parent.join("libceebase.so"));
                candidates.push(parent.join("ceebase.so"));
                candidates.push(parent.join("libceebase.dll"));
                candidates.push(parent.join("ceebase.dll"));

                candidates.push(parent.join("../lib/libceebase.dylib"));
                candidates.push(parent.join("../lib/libceebase.so"));
                candidates.push(parent.join("../lib/libceebase.dll"));
            }
        }

        // 3. User directory ~/.ceebase/bin and ~/.ceebase/lib
        if let Ok(home) = std::env::var("HOME").or_else(|_| std::env::var("USERPROFILE")) {
            let base = Path::new(&home).join(".ceebase");
            candidates.push(base.join("bin/libceebase.dylib"));
            candidates.push(base.join("bin/libceebase.so"));
            candidates.push(base.join("bin/libceebase.dll"));
            candidates.push(base.join("lib/libceebase.dylib"));
            candidates.push(base.join("lib/libceebase.so"));
            candidates.push(base.join("lib/libceebase.dll"));
        }

        // 4. Local repository and build directory candidates
        let local_names = [
            "build/libceebase.dll",
            "build/libceebase.dylib",
            "build/libceebase.so",
            "libceebase.dll",
            "libceebase.dylib",
            "libceebase.so",
            "../build/libceebase.dll",
            "../build/libceebase.dylib",
            "../build/libceebase.so",
            "../../build/libceebase.dll",
            "../../build/libceebase.dylib",
            "../../build/libceebase.so",
            "build/bin/libceebase.dll",
            "build/Release/libceebase.dll",
            "build/Debug/libceebase.dll",
            "/usr/local/lib/libceebase.dylib",
            "/usr/local/lib/libceebase.so",
            "/opt/homebrew/lib/libceebase.dylib",
        ];
        for name in local_names {
            candidates.push(PathBuf::from(name));
        }

        let mut found_path: Option<PathBuf> = None;
        for p in &candidates {
            if p.exists() {
                found_path = Some(p.clone());
                break;
            }
        }

        let path = found_path.ok_or_else(|| {
            "Could not locate native library (libceebase.dylib / libceebase.so / libceebase.dll).".to_string()
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
