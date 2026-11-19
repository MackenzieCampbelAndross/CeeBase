"""
CeeBase DB-API 2.0 Compatible Python Driver
"""

import ctypes
import os
from pathlib import Path
from typing import Any, List, Optional, Sequence, Tuple

# DB-API 2.0 Module Constants
apilevel = "2.0"
threadsafety = 1
paramstyle = "qmark"

# CeeBase C API Type Constants
CEEBASE_TYPE_INTEGER = 1
CEEBASE_TYPE_BIGINT = 2
CEEBASE_TYPE_FLOAT = 3
CEEBASE_TYPE_DOUBLE = 4
CEEBASE_TYPE_BOOLEAN = 5
CEEBASE_TYPE_TEXT = 6
CEEBASE_TYPE_NULL = 7

# DB-API 2.0 Exception Hierarchy
class Error(Exception):
    """Base exception for all CeeBase errors."""
    pass

class DatabaseError(Error):
    pass

class OperationalError(DatabaseError):
    pass

class IntegrityError(DatabaseError):
    pass

class ProgrammingError(DatabaseError):
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

    return "libceebase.dll" if os.name == "nt" else "libceebase.so"


class _CeebaseLib:
    _instance = None

    @classmethod
    def get(cls):
        if cls._instance is None:
            lib_path = _find_library()

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
                raise OperationalError(f"Failed to load CeeBase shared library from '{lib_path}': {e}") from e

            # Version & Lifecycle
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

            # Prepared Statement
            lib.ceebase_prepare.restype = ctypes.c_int
            lib.ceebase_prepare.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.POINTER(ctypes.c_void_p)]

            lib.ceebase_step.restype = ctypes.c_int
            lib.ceebase_step.argtypes = [ctypes.c_void_p]

            lib.ceebase_reset.restype = ctypes.c_int
            lib.ceebase_reset.argtypes = [ctypes.c_void_p]

            lib.ceebase_finalize.restype = ctypes.c_int
            lib.ceebase_finalize.argtypes = [ctypes.c_void_p]

            # Bindings
            lib.ceebase_bind_int.restype = ctypes.c_int
            lib.ceebase_bind_int.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int32]

            lib.ceebase_bind_int64.restype = ctypes.c_int
            lib.ceebase_bind_int64.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int64]

            lib.ceebase_bind_double.restype = ctypes.c_int
            lib.ceebase_bind_double.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_double]

            lib.ceebase_bind_text.restype = ctypes.c_int
            lib.ceebase_bind_text.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_char_p, ctypes.c_int]

            lib.ceebase_bind_null.restype = ctypes.c_int
            lib.ceebase_bind_null.argtypes = [ctypes.c_void_p, ctypes.c_int]

            lib.ceebase_clear_bindings.restype = ctypes.c_int
            lib.ceebase_clear_bindings.argtypes = [ctypes.c_void_p]

            # Column Accessors
            lib.ceebase_column_count.restype = ctypes.c_int
            lib.ceebase_column_count.argtypes = [ctypes.c_void_p]

            lib.ceebase_column_name.restype = ctypes.c_char_p
            lib.ceebase_column_name.argtypes = [ctypes.c_void_p, ctypes.c_int]

            lib.ceebase_column_type.restype = ctypes.c_int
            lib.ceebase_column_type.argtypes = [ctypes.c_void_p, ctypes.c_int]

            lib.ceebase_column_is_null.restype = ctypes.c_int
            lib.ceebase_column_is_null.argtypes = [ctypes.c_void_p, ctypes.c_int]

            lib.ceebase_column_int.restype = ctypes.c_int32
            lib.ceebase_column_int.argtypes = [ctypes.c_void_p, ctypes.c_int]

            lib.ceebase_column_int64.restype = ctypes.c_int64
            lib.ceebase_column_int64.argtypes = [ctypes.c_void_p, ctypes.c_int]

            lib.ceebase_column_double.restype = ctypes.c_double
            lib.ceebase_column_double.argtypes = [ctypes.c_void_p, ctypes.c_int]

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
        self.description: List[Tuple[str, int, None, None, None, None, bool]] = []
        self.rowcount: int = -1

    def execute(self, sql: str, parameters: Optional[Sequence[Any]] = None) -> "Cursor":
        self.close()
        stmt_ptr = ctypes.c_void_p()
        rc = self.lib.ceebase_prepare(self.conn._db, sql.encode("utf-8"), ctypes.byref(stmt_ptr))
        if rc != 0:
            err = self.lib.ceebase_errmsg(self.conn._db).decode("utf-8", errors="replace")
            raise ProgrammingError(f"Prepare failed: {err}")

        self._stmt = stmt_ptr

        # Bind parameters if supplied
        if parameters:
            for idx, val in enumerate(parameters, start=1):
                if val is None:
                    self.lib.ceebase_bind_null(self._stmt, idx)
                elif isinstance(val, bool):
                    self.lib.ceebase_bind_int(self._stmt, idx, 1 if val else 0)
                elif isinstance(val, int):
                    self.lib.ceebase_bind_int64(self._stmt, idx, val)
                elif isinstance(val, float):
                    self.lib.ceebase_bind_double(self._stmt, idx, val)
                elif isinstance(val, (str, bytes)):
                    val_str = val if isinstance(val, str) else val.decode("utf-8", errors="replace")
                    self.lib.ceebase_bind_text(self._stmt, idx, val_str.encode("utf-8"), len(val_str.encode("utf-8")))
                else:
                    val_str = str(val)
                    self.lib.ceebase_bind_text(self._stmt, idx, val_str.encode("utf-8"), len(val_str.encode("utf-8")))

        self._next_rc = self.lib.ceebase_step(self._stmt)
        if self._next_rc == 1:  # CEEBASE_ERROR
            err = self.lib.ceebase_errmsg(self.conn._db).decode("utf-8", errors="replace")
            raise DatabaseError(f"Execution failed: {err}")

        col_count = self.lib.ceebase_column_count(self._stmt)
        if col_count > 0:
            self.description = [
                (
                    self.lib.ceebase_column_name(self._stmt, i).decode("utf-8", errors="replace"),
                    self.lib.ceebase_column_type(self._stmt, i),
                    None, None, None, None,
                    bool(self.lib.ceebase_column_is_null(self._stmt, i))
                )
                for i in range(col_count)
            ]

        self.rowcount = self.lib.ceebase_changes(self.conn._db)
        return self

    def executemany(self, sql: str, seq_of_parameters: Sequence[Sequence[Any]]) -> "Cursor":
        count = 0
        for params in seq_of_parameters:
            self.execute(sql, params)
            count += max(0, self.rowcount)
        self.rowcount = count
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
            row = []
            for i in range(col_count):
                if self.lib.ceebase_column_is_null(self._stmt, i):
                    row.append(None)
                    continue

                col_type = self.lib.ceebase_column_type(self._stmt, i)
                if col_type == CEEBASE_TYPE_INTEGER:
                    row.append(int(self.lib.ceebase_column_int(self._stmt, i)))
                elif col_type == CEEBASE_TYPE_BIGINT:
                    row.append(int(self.lib.ceebase_column_int64(self._stmt, i)))
                elif col_type in (CEEBASE_TYPE_FLOAT, CEEBASE_TYPE_DOUBLE):
                    row.append(float(self.lib.ceebase_column_double(self._stmt, i)))
                elif col_type == CEEBASE_TYPE_BOOLEAN:
                    row.append(bool(self.lib.ceebase_column_int(self._stmt, i)))
                elif col_type == CEEBASE_TYPE_TEXT:
                    txt = self.lib.ceebase_column_text(self._stmt, i)
                    row.append(txt.decode("utf-8", errors="replace") if txt else "")
                else:
                    txt = self.lib.ceebase_column_text(self._stmt, i)
                    row.append(txt.decode("utf-8", errors="replace") if txt else None)
            return tuple(row)
        elif rc == 101:  # CEEBASE_DONE
            return None
        else:
            err = self.lib.ceebase_errmsg(self.conn._db).decode("utf-8", errors="replace")
            raise DatabaseError(f"Step failed: {err}")

    def fetchall(self) -> List[Tuple[Any, ...]]:
        rows = []
        while True:
            row = self.fetchone()
            if row is None:
                break
            rows.append(row)
        return rows

    def fetchmany(self, size: int = 1) -> List[Tuple[Any, ...]]:
        rows = []
        for _ in range(size):
            row = self.fetchone()
            if row is None:
                break
            rows.append(row)
        return rows

    def close(self):
        if self._stmt:
            self.lib.ceebase_finalize(self._stmt)
            self._stmt = None

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.close()

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
            raise OperationalError(f"Failed to open database '{filename}': {err}")
        self._db = db_handle

    def cursor(self) -> Cursor:
        return Cursor(self)

    def execute(self, sql: str, parameters: Optional[Sequence[Any]] = None) -> Cursor:
        cur = self.cursor()
        return cur.execute(sql, parameters)

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
        if exc_type is not None:
            try:
                self.rollback()
            except Exception:
                pass
        else:
            try:
                self.commit()
            except Exception:
                pass
        self.close()

    def __del__(self):
        self.close()


def connect(filename: str) -> Connection:
    """Connect to a CeeBase database file."""
    return Connection(filename)
