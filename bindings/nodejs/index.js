const path = require('path');
const fs = require('fs');
const koffi = require('koffi');

function findLibrary() {
    const candidates = [
        path.resolve(__dirname, '../../build/libceebase.dll'),
        path.resolve(__dirname, '../../build/ceebase.dll'),
        path.resolve(__dirname, '../../build/Release/ceebase.dll'),
        path.resolve(__dirname, '../../build/libceebase.so'),
        path.resolve(__dirname, '../../build/libceebase.dylib'),
    ];
    for (const p of candidates) {
        if (fs.existsSync(p)) return p;
    }
    return 'libceebase.dll';
}

const libPath = findLibrary();
const lib = koffi.load(libPath);

// Define C ABI opaque structs & function pointers
const ceebase_db = koffi.opaque('ceebase_db');
const ceebase_stmt = koffi.opaque('ceebase_stmt');

const ceebase_version = lib.func('const char* ceebase_version()');
const ceebase_open = lib.func('int ceebase_open(const char* filename, _Out_ ceebase_db** db)');
const ceebase_close = lib.func('int ceebase_close(ceebase_db* db)');
const ceebase_errmsg = lib.func('const char* ceebase_errmsg(ceebase_db* db)');
const ceebase_prepare = lib.func('int ceebase_prepare(ceebase_db* db, const char* sql, _Out_ ceebase_stmt** stmt)');
const ceebase_step = lib.func('int ceebase_step(ceebase_stmt* stmt)');
const ceebase_finalize = lib.func('int ceebase_finalize(ceebase_stmt* stmt)');
const ceebase_column_count = lib.func('int ceebase_column_count(ceebase_stmt* stmt)');
const ceebase_column_name = lib.func('const char* ceebase_column_name(ceebase_stmt* stmt, int col_idx)');
const ceebase_column_text = lib.func('const char* ceebase_column_text(ceebase_stmt* stmt, int col_idx)');
const ceebase_changes = lib.func('int ceebase_changes(ceebase_db* db)');

class Database {
    constructor(filename) {
        this.filename = filename;
        const dbPtrPtr = [null];
        const rc = ceebase_open(filename, dbPtrPtr);
        if (rc !== 0 || !dbPtrPtr[0]) {
            const err = dbPtrPtr[0] ? ceebase_errmsg(dbPtrPtr[0]) : 'Failed to allocate database';
            throw new Error(`Failed to open CeeBase database '${filename}': ${err}`);
        }
        this._db = dbPtrPtr[0];
    }

    execute(sql) {
        const stmtPtrPtr = [null];
        const rc = ceebase_prepare(this._db, sql, stmtPtrPtr);
        if (rc !== 0 || !stmtPtrPtr[0]) {
            throw new Error(`SQL prepare failed: ${ceebase_errmsg(this._db)}`);
        }
        const stmt = stmtPtrPtr[0];
        try {
            ceebase_step(stmt);
            return { changes: ceebase_changes(this._db) };
        } finally {
            ceebase_finalize(stmt);
        }
    }

    query(sql) {
        const stmtPtrPtr = [null];
        const rc = ceebase_prepare(this._db, sql, stmtPtrPtr);
        if (rc !== 0 || !stmtPtrPtr[0]) {
            throw new Error(`SQL query prepare failed: ${ceebase_errmsg(this._db)}`);
        }
        const stmt = stmtPtrPtr[0];
        try {
            let stepRc = ceebase_step(stmt);
            const colCount = ceebase_column_count(stmt);
            const columns = [];
            for (let i = 0; i < colCount; i++) {
                columns.push(ceebase_column_name(stmt, i));
            }

            const rows = [];
            while (stepRc === 100) { // CEEBASE_ROW
                const row = {};
                for (let i = 0; i < colCount; i++) {
                    const colName = columns[i];
                    row[colName] = ceebase_column_text(stmt, i);
                }
                rows.push(row);
                stepRc = ceebase_step(stmt);
            }

            return { columns, rows };
        } finally {
            ceebase_finalize(stmt);
        }
    }

    close() {
        if (this._db) {
            ceebase_close(this._db);
            this._db = null;
        }
    }
}

module.exports = {
    version: () => ceebase_version(),
    open: (filename) => new Database(filename),
    Database
};
