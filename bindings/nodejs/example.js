const ceebase = require('./index.js');
const fs = require('fs');

console.log(`CeeBase Node.js Engine Version: ${ceebase.version()}`);

const dbFile = 'node_demo.cbdb';
const db = ceebase.open(dbFile);

console.log(`Connected to: ${dbFile}`);

db.execute("CREATE TABLE services (id INTEGER PRIMARY KEY, name TEXT, port INTEGER);");
db.execute("INSERT INTO services VALUES (1, 'Auth Service', 5001);");
db.execute("INSERT INTO services VALUES (2, 'Payment Gateway', 5002);");
db.execute("INSERT INTO services VALUES (3, 'Notification Engine', 5003);");

const res = db.query("SELECT id, name, port FROM services ORDER BY id;");

console.log("\nNode.js Query Results:");
console.table(res.rows);

db.close();

// Cleanup
if (fs.existsSync(dbFile)) fs.unlinkSync(dbFile);
if (fs.existsSync('node_demo.wal')) fs.unlinkSync('node_demo.wal');

console.log("[SUCCESS] Node.js CeeBase workflow completed successfully!");
