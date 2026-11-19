# CeeBase CLI & Interactive Terminal Guide

The CeeBase Command Line Interface (`ceebase`) is a high-performance developer tool and interactive terminal written in Rust, built on top of the CeeBase C++20 database engine kernel (`libceebase`).

---

## 1. Design Direction: Calm Pastel & OpenCode Aesthetic

The CeeBase terminal follows a calm, minimalist aesthetic:
- **Palette**: Soft powder blue (`#93C5FD`), calm lavender (`#C4B5FD`), soft mint (`#6EE7B7`), and muted slate borders (`#475569`).
- **Typography & Layout**: Clear header with connected database, engine version, live status pill, and shortcut hints.
- **Scrollable Output**: Structured result tables, query timing (in ms), and syntax-highlighted alerts.
- **Persistent Composer**: Fixed input bar at the bottom with real-time autocompletion and multiline support.
- **Graceful Fallback**: Automatic terminal capability detection with `--plain` mode for headless or ASCII-only environments.

---

## 2. Installation & Compilation

```bash
# Build release binary using Cargo
cargo build --release --manifest-path tools/cli/Cargo.toml

# The optimized binary will be produced at:
tools/cli/target/release/ceebase.exe
```

---

## 3. Command Reference

### Database Initialization & Kernel Storage
```bash
# Initialize a new CeeBase 4 KB slotted page database file with catalog page
ceebase init my_app.cbdb

# Deep physical inspection of storage pages, CRC-32 checksums, and catalog tables
ceebase inspect my_app.cbdb
```

### SQL Execution (Non-Interactive)
```bash
# Execute DDL
ceebase query "CREATE TABLE users (id INT PRIMARY KEY, name VARCHAR, balance DOUBLE, active BOOLEAN);" -d my_app.cbdb

# Execute DML
ceebase query "INSERT INTO users VALUES (1, 'Alice Smith', 4500.0, true);" -d my_app.cbdb

# Execute Query with Unicode table borders
ceebase query "SELECT * FROM users;" -d my_app.cbdb

# Execute Query in plain ASCII mode
ceebase query "SELECT * FROM users;" -d my_app.cbdb --plain
```

### Interactive TUI
```bash
# Launch interactive TUI on local database file
ceebase connect my_app.cbdb

# Or connect to a running remote CeeBase server
ceebase connect http://localhost:8080
```

#### TUI Keyboard Shortcuts
- `Enter`: Submit SQL query or slash command.
- `Tab`: Autocomplete SQL keywords (`SELECT`, `INSERT`, `CREATE TABLE`) and slash commands.
- `Up` / `Down`: Navigate command execution history.
- `PageUp` / `PageDown`: Scroll query output.
- `Ctrl+L`: Clear output screen.
- `Ctrl+C` / `Ctrl+Q`: Gracefully quit terminal.

#### TUI Slash Commands
- `/help`: Display interactive command overview.
- `/tables` or `/dt`: List database tables with live row counts.
- `/desc <table>` or `/d <table>`: Describe columns and types for a table.
- `/databases`: List all CeeBase database files in the current workspace.
- `/status`: Show active connection, engine version, and health metrics.
- `/clear`: Clear terminal history.
- `/quit`: Exit terminal.

### Server Lifecycle Management
```bash
# Start background server
ceebase start --port 8080 --daemon

# Check server or database status
ceebase status http://localhost:8080
ceebase status my_app.cbdb

# Stop server
ceebase stop --port 8080
```

### Multi-Tenancy & Administration
```bash
# Database management
ceebase database create tenant_alpha
ceebase database list
ceebase database delete tenant_alpha

# User management
ceebase user create engineer@acme.com --role developer

# Organization management
ceebase organization create "Acme Corp"
```

### Backup & Disaster Recovery
```bash
# Create an atomic snapshot backup
ceebase backup -d my_app.cbdb --out backups/my_app_snapshot.cbdb

# Restore database from snapshot
ceebase restore backups/my_app_snapshot.cbdb -d my_app_restored.cbdb
```

### CLI Configuration
```bash
# Inspect active CLI configuration
ceebase config

# Set custom defaults
ceebase config --set theme=powder_blue_pastel
ceebase config --set default_database=production.cbdb
```
