# CeeBase Model Context Protocol (MCP) Server Specification

The CeeBase MCP Server connects AI developer agents (Claude Code, Antigravity, Cursor) to CeeBase databases using the official Model Context Protocol (JSON-RPC 2.0 stdio transport).

---

## 1. Architecture & Security Model

The MCP server acts as a **strictly restricted client** of the CeeBase database engine:
- **Authentication**: All agent invocations require cryptographic agent tokens passed via initialization options, environment variables (`CEEBASE_AGENT_TOKEN`), or per-tool arguments.
- **Permission Scopes**:
  - `read-only`: Grants access to inspection, schema browsing, and `execute_read_query`. Destructive statements (`INSERT`, `UPDATE`, `DELETE`, `DROP`, `ALTER`, `CREATE`, `TRUNCATE`) are strictly rejected.
  - `read-write`: Allows `execute_write_query` in addition to read tools.
  - `admin`: Full administrative access.
- **Tenant Isolation**: Agents are bound to an `org_id` and assigned database. Any attempt to query across tenant boundaries is rejected with an authorization error.
- **Safe Identifiers**: Table, column, and database names are validated against strict regex (`^[a-zA-Z_][a-zA-Z0-9_]*$`) preventing SQL injection in schema exploration.
- **Parameterized SQL**: Values are passed through bound parameters rather than string concatenation.
- **Structured Audit Logging**: Every invocation is appended to `mcp/mcp_audit.log` recording timestamp, agent ID, organization, tool name, sanitized parameters, outcome, execution duration, and errors.

---

## 2. Tool Reference

### `list_databases`
Lists databases accessible to the agent within their authorized organization scope.
- **Input Schema**: Optional `organization_id`.
- **Output**: Array of database objects with names, file paths, organization, and access mode.

### `list_tables`
Lists all tables in the active database alongside real-time row counts.
- **Input Schema**: None.
- **Output**: Array of `{ table_name, row_count }`.

### `describe_table`
Returns column metadata, data types, nullability, and total row count.
- **Input Schema**: `{ table_name: string }`.
- **Security**: Validates `table_name` identifier format.

### `execute_read_query`
Executes read-only queries (`SELECT`, `SHOW`, `EXPLAIN`).
- **Input Schema**: `{ query: string, parameters?: any[] }`.
- **Security**: Verifies absence of write keywords (`INSERT`, `UPDATE`, `DELETE`, `DROP`, `CREATE`, `ALTER`).

### `execute_write_query`
Executes data modifications (`INSERT`, `UPDATE`, `DELETE`, `CREATE`).
- **Input Schema**: `{ query: string, parameters?: any[] }`.
- **Security**: Requires explicit `read-write` or `admin` scope. Fails closed for `read-only` agent credentials.

### `explain_query`
Analyzes query execution plans, B+ Tree index scans, and estimated costs.
- **Input Schema**: `{ query: string }`.

### `inspect_database`
Inspects physical 4 KB storage pages, header magic (`0x43424442`), CRC-32 checksums, and slotted page distribution.
- **Input Schema**: None.

### `get_database_health`
Returns database integrity status (`HEALTHY` / `DEGRADED`), header verification, WAL presence, and file sizes.
- **Input Schema**: None.

---

## 3. Configuration & Client Integration

### MCP Client Config (`ceebase_mcp_config.json`)
```json
{
  "mcpServers": {
    "ceebase": {
      "command": "python",
      "args": ["mcp/server.py", "ceebase.cbdb"],
      "env": {
        "CEEBASE_AGENT_TOKEN": "cb_agent_readwrite_master_token"
      }
    }
  }
}
```

### Running the Server
```bash
# Start MCP server over stdio
python mcp/server.py ceebase.cbdb
```
