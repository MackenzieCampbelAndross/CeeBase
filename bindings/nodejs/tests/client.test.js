const { test, before, after } = require('node:test');
const assert = require('node:assert');
const fs = require('node:fs');
const path = require('node:path');
const { connect, version, ConnectionPool } = require('../index.js');

const DB_PATH = path.resolve(__dirname, 'test_node_suite.cbdb');
const WAL_PATH = path.resolve(__dirname, 'test_node_suite.wal');

function cleanup() {
    try {
        if (fs.existsSync(DB_PATH)) fs.unlinkSync(DB_PATH);
        if (fs.existsSync(WAL_PATH)) fs.unlinkSync(WAL_PATH);
    } catch (_) {}
}

before(() => {
    cleanup();
});

after(() => {
    cleanup();
});

test('CeeBase Node.js: version check', () => {
    assert.strictEqual(version(), '1.0.0');
});

test('CeeBase Node.js: CRUD and Typed Query Execution', async () => {
    const conn = connect(DB_PATH);

    // DDL
    await conn.execute('CREATE TABLE services (id INTEGER PRIMARY KEY, name TEXT, port INTEGER, active BOOLEAN, latency DOUBLE);');

    // Parameterized Insert
    await conn.execute('INSERT INTO services VALUES (?, ?, ?, ?, ?);', [1, 'Auth Service', 5001, true, 1.25]);
    await conn.execute('INSERT INTO services VALUES (?, ?, ?, ?, ?);', [2, 'Payment Gateway', 5002, true, 3.80]);
    await conn.execute('INSERT INTO services VALUES (?, ?, ?, ?, ?);', [3, 'Reporting Worker', 5003, false, 12.40]);

    // Typed Query
    const res = await conn.query('SELECT id, name, port, active, latency FROM services ORDER BY id;');
    assert.strictEqual(res.rows.length, 3);

    // Verify true types
    const r1 = res.rows[0];
    assert.strictEqual(r1.id, 1);
    assert.strictEqual(typeof r1.id, 'number');
    assert.strictEqual(r1.name, 'Auth Service');
    assert.strictEqual(typeof r1.name, 'string');
    assert.strictEqual(r1.port, 5001);
    assert.strictEqual(r1.active, true);
    assert.strictEqual(typeof r1.active, 'boolean');
    assert.strictEqual(r1.latency, 1.25);
    assert.strictEqual(typeof r1.latency, 'number');

    conn.close();
});

test('CeeBase Node.js: Prepared Statement Reuse', async () => {
    const conn = connect(DB_PATH);
    const stmt = conn.prepare('SELECT id, name FROM services WHERE id = ?;');

    const res1 = await stmt.query([1]);
    assert.strictEqual(res1.rows.length, 1);
    assert.strictEqual(res1.rows[0].name, 'Auth Service');

    const res2 = await stmt.query([2]);
    assert.strictEqual(res2.rows.length, 1);
    assert.strictEqual(res2.rows[0].name, 'Payment Gateway');

    stmt.finalize();
    conn.close();
});

test('CeeBase Node.js: Transaction Rollback Support', async () => {
    const conn = connect(DB_PATH);

    await assert.rejects(async () => {
        await conn.transaction(async (tx) => {
            await tx.execute('INSERT INTO services VALUES (?, ?, ?, ?, ?);', [99, 'Failed Service', 9999, false, 0.0]);
            throw new Error('Intentional transaction failure');
        });
    });

    // Check that ID 99 was rolled back and does not exist
    const check = await conn.query('SELECT * FROM services WHERE id = ?;', [99]);
    assert.strictEqual(check.rows.length, 0);

    conn.close();
});

test('CeeBase Node.js: Connection Pool Lifecycle', async () => {
    const pool = new ConnectionPool({ database: DB_PATH, maxConnections: 3 });

    const result = await pool.withConnection(async (conn) => {
        const q = await conn.query('SELECT COUNT(*) FROM services;');
        return q.rows.length;
    });

    assert.ok(result > 0);
    pool.closeAll();
});
