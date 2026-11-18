import ctypes
import os
from pathlib import Path
from typing import Any, List, Optional, Tuple


class Error(Exception):
    """Base exception for CeeBase errors."""
    pass


def _find_library() -> str:
    """Locate the compiled CeeBase shared library."""
    current_dir = Path(__file__).resolve().parent
    repo_root = current_dir.parent.parent.parent

    candidates = [
        repo_root / "build" / "libceebase.dll",
        repo_root / "build" / "ceebase.dll",
        repo_root / "build" / "Release" / "ceebase.dll",
        repo_root / "build" / "Debug" / "ceebase.dll",
        repo_root / "build" / "libceebase.so",
        repo_root / "build" / "libceebase.dylib",
    ]

    for p in candidates:
        if p.exists():
            return str(p)

    # Fallback to system search
    return "libceebase.dll" if os.name == "nt" else "libceebase.so"


class _CeebaseLib:
    _instance = None

    @classmethod
    def get(cls):
        if cls._instance is None:
            lib_path = _find_library()

            # On Windows, ensure DLL search directory and MinGW/UCRT runtimes are registered
            if os.name == "nt" and hasattr(os, "add_dll_directory"):
                dll_dir = Path(lib_path).resolve().parent
                if dll_dir.exists():
                    try:
                        os.add_dll_directory(str(dll_dir))
                    except Exception:
                        pass
                for mingw_path in ["C:/msys64/ucrt64/bin", "C:/msys64/mingw64/bin"]:
                    if Path(mingw_path).exists():
                        try:
                            os.add_dll_directory(mingw_path)
                        except Exception:
                            pass

            try:
                lib = ctypes.CDLL(lib_path)
            except OSError as e:
                raise Error(f"Failed to load CeeBase shared library from '{lib_path}': {e}") from e

            # Setup prototypes
            lib.ceebase_version.restype = ctypes.c_char_p
            lib.ceebase_version.argtypes = []

            lib.ceebase_open.restype = ctypes.c_int
            lib.ceebase_open.argtypes = [ctypes.c_char_p, ctypes.POINTER(ctypes.c_void_p)]

            lib.ceebase_close.restype = ctypes.c_int
            lib.ceebase_close.argtypes = [ctypes.c_void_p]

            lib.ceebase_errmsg.restype = ctypes.c_char_p
            lib.ceebase_errmsg.argtypes = [ctypes.c_void_p]

            lib.ceebase_exec.restype = ctypes.c_int
            lib.ceebase_exec.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.POINTER(ctypes.c_char_p)]

            lib.ceebase_free_errmsg.restype = None
            lib.ceebase_free_errmsg.argtypes = [ctypes.c_char_p]

            lib.ceebase_prepare.restype = ctypes.c_int
            lib.ceebase_prepare.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.POINTER(ctypes.c_void_p)]

            lib.ceebase_step.restype = ctypes.c_int
            lib.ceebase_step.argtypes = [ctypes.c_void_p]

            lib.ceebase_finalize.restype = ctypes.c_int
            lib.ceebase_finalize.argtypes = [ctypes.c_void_p]

            lib.ceebase_column_count.restype = ctypes.c_int
            lib.ceebase_column_count.argtypes = [ctypes.c_void_p]

            lib.ceebase_column_name.restype = ctypes.c_char_p
            lib.ceebase_column_name.argtypes = [ctypes.c_void_p, ctypes.c_int]

            lib.ceebase_column_text.restype = ctypes.c_char_p
            lib.ceebase_column_text.argtypes = [ctypes.c_void_p, ctypes.c_int]

            lib.ceebase_changes.restype = ctypes.c_int
            lib.ceebase_changes.argtypes = [ctypes.c_void_p]

            cls._instance = lib
        return cls._instance


class Cursor:
    def __init__(self, conn: "Connection"):
        self.conn = conn
        self.lib = conn.lib
        self._stmt = None
        self._next_rc = None
        self.description: List[Tuple[str]] = []
        self.rowcount: int = -1

    def execute(self, sql: str) -> "Cursor":
        self.close()
        stmt_ptr = ctypes.c_void_p()
        rc = self.lib.ceebase_prepare(self.conn._db, sql.encode("utf-8"), ctypes.byref(stmt_ptr))
        if rc != 0:
            err = self.lib.ceebase_errmsg(self.conn._db).decode("utf-8", errors="replace")
            raise Error(f"Prepare failed: {err}")

        self._stmt = stmt_ptr
        self._next_rc = self.lib.ceebase_step(self._stmt)
        if self._next_rc == 1:  # CEEBASE_ERROR
            err = self.lib.ceebase_errmsg(self.conn._db).decode("utf-8", errors="replace")
            raise Error(f"Execution failed: {err}")

        self.rowcount = self.lib.ceebase_changes(self.conn._db)
        return self

    def fetchone(self) -> Optional[Tuple[Any, ...]]:
        if not self._stmt:
            return None

        if self._next_rc is not None:
            rc = self._next_rc
            self._next_rc = None
        else:
            rc = self.lib.ceebase_step(self._stmt)

        if rc == 100:  # CEEBASE_ROW
            col_count = self.lib.ceebase_column_count(self._stmt)
            if not self.description:
                self.description = [
                    (self.lib.ceebase_column_name(self._stmt, i).decode("utf-8", errors="replace"),)
                    for i in range(col_count)
                ]
            row = []
            for i in range(col_count):
                txt = self.lib.ceebase_column_text(self._stmt, i)
                row.append(txt.decode("utf-8", errors="replace") if txt else None)
            return tuple(row)
        elif rc == 101:  # CEEBASE_DONE
            return None
        else:
            err = self.lib.ceebase_errmsg(self.conn._db).decode("utf-8", errors="replace")
            raise Error(f"Step failed: {err}")

    def fetchall(self) -> List[Tuple[Any, ...]]:
        rows = []
        while True:
            row = self.fetchone()
            if row is None:
                break
            rows.append(row)
        return rows

    def close(self):
        if self._stmt:
            self.lib.ceebase_finalize(self._stmt)
            self._stmt = None

    def __del__(self):
        self.close()


class Connection:
    def __init__(self, filename: str):
        self.filename = filename
        self._db = None
        self.lib = _CeebaseLib.get()

        db_handle = ctypes.c_void_p()
        rc = self.lib.ceebase_open(filename.encode("utf-8"), ctypes.byref(db_handle))
        if rc != 0 or not db_handle:
            err = self.lib.ceebase_errmsg(db_handle).decode("utf-8", errors="replace") if db_handle else "Failed to open"
            raise Error(f"Failed to open database '{filename}': {err}")
        self._db = db_handle

    def cursor(self) -> Cursor:
        return Cursor(self)

    def execute(self, sql: str) -> Cursor:
        cur = self.cursor()
        return cur.execute(sql)

    def commit(self):
        self.execute("COMMIT;")

    def rollback(self):
        self.execute("ROLLBACK;")

    def close(self):
        if self._db:
            self.lib.ceebase_close(self._db)
            self._db = None

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.close()

    def __del__(self):
        self.close()


def connect(filename: str) -> Connection:
    """Connect to a CeeBase database file."""
    return Connection(filename)
