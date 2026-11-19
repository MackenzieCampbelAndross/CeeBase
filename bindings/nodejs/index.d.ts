export interface ColumnMetadata {
    name: string;
    type: number;
    typeName: string;
    isNullable: boolean;
}

export interface QueryResult<T = Record<string, any>> {
    columns: ColumnMetadata[];
    rows: T[];
    changes?: number;
    elapsedMs?: number;
}

export interface PoolConfig {
    database: string;
    maxConnections?: number;
}

export class CeebaseError extends Error {
    constructor(message: string);
}

export class OperationalError extends CeebaseError {}
export class DatabaseError extends CeebaseError {}

export class PreparedStatement {
    bind(params: any[]): void;
    step(): boolean;
    reset(): void;
    finalize(): void;
    query<T = Record<string, any>>(params?: any[]): Promise<QueryResult<T>>;
    execute(params?: any[]): Promise<QueryResult>;
}

export class Connection {
    constructor(filename: string);
    execute(sql: string, params?: any[]): Promise<QueryResult>;
    query<T = Record<string, any>>(sql: string, params?: any[]): Promise<QueryResult<T>>;
    prepare(sql: string): PreparedStatement;
    beginTransaction(): Promise<void>;
    commit(): Promise<void>;
    rollback(): Promise<void>;
    transaction<T>(fn: (conn: Connection) => Promise<T>): Promise<T>;
    close(): void;
}

export class ConnectionPool {
    constructor(config: PoolConfig);
    acquire(): Promise<Connection>;
    release(conn: Connection): void;
    withConnection<T>(fn: (conn: Connection) => Promise<T>): Promise<T>;
    closeAll(): void;
}

export function connect(filename: string): Connection;
export function version(): string;
