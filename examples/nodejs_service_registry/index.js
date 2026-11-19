/**
 * Real-world Microservice Discovery & Registry Application using @ceebase/client.
 * Demonstrates:
 * 1. Asynchronous connection & connection pooling
 * 2. Parameterized SQL queries
 * 3. Typed column return values
 * 4. ACID transactions
 * 5. Complete engine restart and disk persistence verification
 */

const path = require('path');
const fs = require('fs');
const { connect, ConnectionPool, version } = require('../../bindings/nodejs/index.js');

const DB_PATH = path.resolve(__dirname, 'service_registry.cbdb');
const WAL_PATH = path.resolve(__dirname, 'service_registry.wal');

function cleanup() {
    try {
        if (fs.existsSync(DB_PATH)) fs.unlinkSync(DB_PATH);
        if (fs.existsSync(WAL_PATH)) fs.unlinkSync(WAL_PATH);
    } catch (_) {}
}

async function stageOnePopulate() {
    console.log('\n--- STAGE 1: Provision Registry & Register Microservices ---');
    console.log('CeeBase Driver Version:', version());

    const conn = connect(DB_PATH);

    // 1. Create table
    await conn.execute('CREATE TABLE services (service_id INTEGER PRIMARY KEY, name TEXT, host TEXT, port INTEGER, healthy BOOLEAN, latency_ms DOUBLE);');
    console.log('[OK] Table "services" created.');

    // 2. Register services
    const services = [
        [1, 'auth-service', '10.0.1.10', 8001, true, 2.45],
        [2, 'billing-service', '10.0.1.11', 8002, true, 5.12],
        [3, 'notification-service', '10.0.1.12', 8003, true, 8.90],
        [4, 'analytics-pipeline', '10.0.1.13', 8004, false, 45.30]
    ];

    for (const svc of services) {
        await conn.execute('INSERT INTO services VALUES (?, ?, ?, ?, ?, ?);', svc);
    }
    console.log(`[OK] Registered ${services.length} microservices via parameterized bindings.`);

    // 3. Transaction commit
    await conn.transaction(async (tx) => {
        await tx.execute('INSERT INTO services VALUES (?, ?, ?, ?, ?, ?);', [5, 'api-gateway', '10.0.1.1', 443, true, 0.85]);
    });
    console.log('[OK] Committed Gateway registration via ACID transaction.');

    conn.close();
    console.log('[OK] Connection closed. Engine unmounted.');
}

async function stageTwoRestartAndVerify() {
    console.log('\n--- STAGE 2: Reopen Registry and Verify Persistence ---');

    // Reopen using connection pool
    const pool = new ConnectionPool({ database: DB_PATH, maxConnections: 3 });

    await pool.withConnection(async (conn) => {
        const result = await conn.query('SELECT service_id, name, host, port, healthy, latency_ms FROM services ORDER BY service_id;');
        console.log(`[OK] Reloaded ${result.rows.length} microservices across restart:`);
        console.table(result.rows);

        // Verify native types
        const gateway = result.rows.find(r => r.service_id === 5);
        if (!gateway || gateway.name !== 'api-gateway' || typeof gateway.port !== 'number' || typeof gateway.healthy !== 'boolean') {
            throw new Error('Verification failure: types or rows corrupted across restart');
        }
        console.log('[OK] Verified strict native type preservation across engine restart.');
    });

    pool.closeAll();
}

async function main() {
    cleanup();
    try {
        await stageOnePopulate();
        await stageTwoRestartAndVerify();
        console.log('\n[SUCCESS] Node.js CeeBase integration demonstration passed with 100% integrity!');
    } finally {
        cleanup();
    }
}

main().catch(console.error);
