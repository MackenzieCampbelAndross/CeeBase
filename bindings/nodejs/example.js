const ceebase = require('./index.js');
const fs = require('fs');

async function main() {
    console.log('CeeBase Node.js Engine Version:', ceebase.version());

    const dbFile = 'node_demo.cbdb';
    const conn = ceebase.connect(dbFile);
    console.log('Connected to:', dbFile);

    // Create table
    await conn.execute('CREATE TABLE services (id INTEGER PRIMARY KEY, name TEXT, port INTEGER, active BOOLEAN, latency DOUBLE);');

    // Parameterized inserts with native types
    await conn.execute('INSERT INTO services VALUES (?, ?, ?, ?, ?);', [1, 'Auth Service', 5001, true, 1.25]);
    await conn.execute('INSERT INTO services VALUES (?, ?, ?, ?, ?);', [2, 'Payment Gateway', 5002, true, 3.80]);
    await conn.execute('INSERT INTO services VALUES (?, ?, ?, ?, ?);', [3, 'Notification Engine', 5003, false, 8.40]);

    // Query records with typed return values
    const result = await conn.query('SELECT id, name, port, active, latency FROM services ORDER BY id;');

    console.log('\nNode.js Query Results:');
    console.table(result.rows);

    conn.close();

    // Clean up demo files
    for (const f of [dbFile, 'node_demo.wal']) {
        if (fs.existsSync(f)) fs.unlinkSync(f);
    }
    console.log('[SUCCESS] Node.js CeeBase workflow completed successfully!');
}

main().catch(console.error);
