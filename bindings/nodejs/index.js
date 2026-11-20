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

// C API types & functions
const ceebase_db = koffi.opaque('ceebase_db');
const ceebase_stmt = koffi.opaque('ceebase_stmt');

const ceebase_version = lib.func('const char* ceebase_version()');
const ceebase_open = lib.func('int ceebase_open(const char* filename, _Out_ ceebase_db** db)');
const ceebase_close = lib.func('int ceebase_close(ceebase_db* db)');
const ceebase_errmsg = lib.func('const char* ceebase_errmsg(ceebase_db* db)');
const ceebase_prepare = lib.func('int ceebase_prepare(ceebase_db* db, const char* sql, _Out_ ceebase_stmt** stmt)');
const ceebase_step = lib.func('int ceebase_step(ceebase_stmt* stmt)');
const ceebase_reset = lib.func('int ceebase_reset(ceebase_stmt* stmt)');
const ceebase_finalize = lib.func('int ceebase_finalize(ceebase_stmt* stmt)');

const ceebase_bind_int = lib.func('int ceebase_bind_int(ceebase_stmt* stmt, int param_idx, int32 val)');
const ceebase_bind_int64 = lib.func('int ceebase_bind_int64(ceebase_stmt* stmt, int param_idx, int64 val)');
const ceebase_bind_double = lib.func('int ceebase_bind_double(ceebase_stmt* stmt, int param_idx, double val)');
const ceebase_bind_text = lib.func('int ceebase_bind_text(ceebase_stmt* stmt, int param_idx, const char* val, int len)');
const ceebase_bind_null = lib.func('int ceebase_bind_null(ceebase_stmt* stmt, int param_idx)');
const ceebase_clear_bindings = lib.func('int ceebase_clear_bindings(ceebase_stmt* stmt)');

const ceebase_column_count = lib.func('int ceebase_column_count(ceebase_stmt* stmt)');
const ceebase_column_name = lib.func('const char* ceebase_column_name(ceebase_stmt* stmt, int col_idx)');
const ceebase_column_type = lib.func('int ceebase_column_type(ceebase_stmt* stmt, int col_idx)');
const ceebase_column_is_null = lib.func('int ceebase_column_is_null(ceebase_stmt* stmt, int col_idx)');
const ceebase_column_int = lib.func('int32 ceebase_column_int(ceebase_stmt* stmt, int col_idx)');
const ceebase_column_int64 = lib.func('int64 ceebase_column_int64(ceebase_stmt* stmt, int col_idx)');
const ceebase_column_double = lib.func('double ceebase_column_double(ceebase_stmt* stmt, int col_idx)');
const ceebase_column_boolean = lib.func('int ceebase_column_boolean(ceebase_stmt* stmt, int col_idx)');
const ceebase_column_text = lib.func('const char* ceebase_column_text(ceebase_stmt* stmt, int col_idx)');
const ceebase_changes = lib.func('int ceebase_changes(ceebase_db* db)');

// Type Constants
const TYPE_INTEGER = 1;
const TYPE_BIGINT = 2;
const TYPE_FLOAT = 3;
const TYPE_DOUBLE = 4;
const TYPE_BOOLEAN = 5;
const TYPE_TEXT = 6;
const TYPE_NULL = 7;

function typeToString(type) {
    switch (type) {
        case TYPE_INTEGER: return 'INTEGER';
        case TYPE_BIGINT: return 'BIGINT';
        case TYPE_FLOAT: return 'FLOAT';
        case TYPE_DOUBLE: return 'DOUBLE';
        case TYPE_BOOLEAN: return 'BOOLEAN';
        case TYPE_TEXT: return 'TEXT';
        default: return 'NULL';
    }
}

class CeebaseError extends Error {
    constructor(message) {
        super(message);
        this.name = 'CeebaseError';
    }
}

class OperationalError extends CeebaseError {
    constructor(message) {
        super(message);
        this.name = 'OperationalError';
    }
}

class DatabaseError extends CeebaseError {
    constructor(message) {
        super(message);
        this.name = 'DatabaseError';
    }
}

class PreparedStatement {
    constructor(dbHandle, sql) {
        this._db = dbHandle;
        this.sql = sql;
        const stmtPtrPtr = [null];
        const rc = ceebase_prepare(this._db, sql, stmtPtrPtr);
        if (rc !== 0 || !stmtPtrPtr[0]) {
            throw new DatabaseError(`SQL prepare failed: ${ceebase_errmsg(this._db)}`);
        }
        this._stmt = stmtPtrPtr[0];
    }

    bind(params) {
        if (!this._stmt) throw new OperationalError('Statement already finalized');
        if (!params || !Array.isArray(params)) return;

        for (let i = 0; i < params.length; i++) {
            const idx = i + 1;
            const val = params[i];
            if (val === null || val === undefined) {
                ceebase_bind_null(this._stmt, idx);
            } else if (typeof val === 'boolean') {
                ceebase_bind_int(this._stmt, idx, val ? 1 : 0);
            } else if (typeof val === 'number') {
                if (Number.isInteger(val)) {
                    ceebase_bind_int64(this._stmt, idx, BigInt(val));
                } else {
                    ceebase_bind_double(this._stmt, idx, val);
                }
            } else if (typeof val === 'bigint') {
                ceebase_bind_int64(this._stmt, idx, val);
            } else {
                const s = String(val);
                ceebase_bind_text(this._stmt, idx, s, Buffer.byteLength(s, 'utf8'));
            }
        }
    }

    async execute(params) {
        this.reset();
        if (params) this.bind(params);

        const rc = ceebase_step(this._stmt);
        if (rc === 1) { // ERROR
            throw new DatabaseError(`Step failed: ${ceebase_errmsg(this._db)}`);
        }
        const changes = ceebase_changes(this._db);
        return { changes };
    }

    async query(params) {
        this.reset();
        if (params) this.bind(params);

        let stepRc = ceebase_step(this._stmt);
        if (stepRc === 1) {
            throw new DatabaseError(`Query execution failed: ${ceebase_errmsg(this._db)}`);
        }

        const colCount = ceebase_column_count(this._stmt);
        const columns = [];
        for (let i = 0; i < colCount; i++) {
            const type = ceebase_column_type(this._stmt, i);
            columns.push({
                name: ceebase_column_name(this._stmt, i),
                type: type,
                typeName: typeToString(type),
                isNullable: Boolean(ceebase_column_is_null(this._stmt, i))
            });
        }

        const rows = [];
        while (stepRc === 100) { // CEEBASE_ROW
            const row = {};
            for (let i = 0; i < colCount; i++) {
                const col = columns[i];
                if (ceebase_column_is_null(this._stmt, i)) {
                    row[col.name] = null;
                } else if (col.type === TYPE_INTEGER || col.type === TYPE_BIGINT) {
                    row[col.name] = Number(ceebase_column_int64(this._stmt, i));
                } else if (col.type === TYPE_FLOAT || col.type === TYPE_DOUBLE) {
                    row[col.name] = ceebase_column_double(this._stmt, i);
                } else if (col.type === TYPE_BOOLEAN) {
                    row[col.name] = Boolean(ceebase_column_boolean(this._stmt, i));
                } else {
                    row[col.name] = ceebase_column_text(this._stmt, i);
                }
            }
            rows.push(row);
            stepRc = ceebase_step(this._stmt);
        }

        return { columns, rows };
    }

    reset() {
        if (this._stmt) {
            ceebase_reset(this._stmt);
        }
    }

    finalize() {
        if (this._stmt) {
            ceebase_finalize(this._stmt);
            this._stmt = null;
        }
    }
}

class Connection {
    constructor(filename) {
        this.filename = filename;
        const dbPtrPtr = [null];
        const rc = ceebase_open(filename, dbPtrPtr);
        if (rc !== 0 || !dbPtrPtr[0]) {
            const err = dbPtrPtr[0] ? ceebase_errmsg(dbPtrPtr[0]) : 'Failed to allocate database';
            throw new OperationalError(`Failed to open CeeBase database '${filename}': ${err}`);
        }
        this._db = dbPtrPtr[0];
    }

    prepare(sql) {
        return new PreparedStatement(this._db, sql);
    }

    async execute(sql, params) {
        const stmt = this.prepare(sql);
        try {
            return await stmt.execute(params);
        } finally {
            stmt.finalize();
        }
    }

    async query(sql, params) {
        const stmt = this.prepare(sql);
        try {
            return await stmt.query(params);
        } finally {
            stmt.finalize();
        }
    }

    async beginTransaction() {
        await this.execute('BEGIN;');
    }

    async commit() {
        await this.execute('COMMIT;');
    }

    async rollback() {
        await this.execute('ROLLBACK;');
    }

    async transaction(fn) {
        await this.beginTransaction();
        try {
            const res = await fn(this);
            await this.commit();
            return res;
        } catch (err) {
            try {
                await this.rollback();
            } catch (_) {}
            throw err;
        }
    }

    close() {
        if (this._db) {
            ceebase_close(this._db);
            this._db = null;
        }
    }
}

class ConnectionPool {
    constructor(config) {
        this.database = config.database;
        this.maxConnections = config.maxConnections || 5;
        this.pool = [];
        this.busy = new Set();
    }

    async acquire() {
        if (this.pool.length > 0) {
            const conn = this.pool.pop();
            this.busy.add(conn);
            return conn;
        }
        if (this.busy.size < this.maxConnections) {
            const conn = new Connection(this.database);
            this.busy.add(conn);
            return conn;
        }
        // Wait or create
        return new Promise((resolve) => {
            const check = () => {
                if (this.pool.length > 0) {
                    const conn = this.pool.pop();
                    this.busy.add(conn);
                    resolve(conn);
                } else {
                    setTimeout(check, 10);
                }
            };
            check();
        });
    }

    release(conn) {
        this.busy.delete(conn);
        if (this.pool.length < this.maxConnections) {
            this.pool.push(conn);
        } else {
            conn.close();
        }
    }

    async withConnection(fn) {
        const conn = await this.acquire();
        try {
            return await fn(conn);
        } finally {
            this.release(conn);
        }
    }

    closeAll() {
        for (const conn of this.pool) conn.close();
        for (const conn of this.busy) conn.close();
        this.pool = [];
        this.busy.clear();
    }
}

function connect(filename) {
    return new Connection(filename);
}

function version() {
    return ceebase_version();
}

const sdk = require('../../sdk/javascript/index.js');

module.exports = {
    connect,
    open: connect,
    version,
    createClient: sdk.createClient,
    CeeBaseClient: sdk.CeeBaseClient,
    Connection,
    PreparedStatement,
    ConnectionPool,
    CeebaseError,
    OperationalError,
    DatabaseError
};
