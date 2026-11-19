package com.ceebase;

import java.io.File;
import java.lang.foreign.*;
import java.lang.invoke.MethodHandle;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.Queue;
import java.util.concurrent.ConcurrentLinkedQueue;

public class Ceebase {
    private static final Linker LINKER = Linker.nativeLinker();
    private static final SymbolLookup LOOKUP;

    static final MethodHandle MH_OPEN;
    static final MethodHandle MH_CLOSE;
    static final MethodHandle MH_EXEC;
    static final MethodHandle MH_PREPARE;
    static final MethodHandle MH_STEP;
    static final MethodHandle MH_RESET;
    static final MethodHandle MH_FINALIZE;

    static final MethodHandle MH_BIND_INT;
    static final MethodHandle MH_BIND_INT64;
    static final MethodHandle MH_BIND_DOUBLE;
    static final MethodHandle MH_BIND_TEXT;
    static final MethodHandle MH_BIND_NULL;
    static final MethodHandle MH_CLEAR_BINDINGS;

    static final MethodHandle MH_COLUMN_COUNT;
    static final MethodHandle MH_COLUMN_NAME;
    static final MethodHandle MH_COLUMN_TYPE;
    static final MethodHandle MH_COLUMN_IS_NULL;
    static final MethodHandle MH_COLUMN_INT;
    static final MethodHandle MH_COLUMN_INT64;
    static final MethodHandle MH_COLUMN_DOUBLE;
    static final MethodHandle MH_COLUMN_BOOLEAN;
    static final MethodHandle MH_COLUMN_TEXT;

    static final MethodHandle MH_CHANGES;
    static final MethodHandle MH_ERRMSG;
    static final MethodHandle MH_VERSION;

    static {
        for (String p : List.of("C:/msys64/ucrt64/bin/libstdc++-6.dll", "C:/msys64/mingw64/bin/libstdc++-6.dll")) {
            if (new File(p).exists()) {
                try { System.load(p); } catch (Throwable ignored) {}
            }
        }

        Path dllPath = findLibrary();
        System.load(dllPath.toAbsolutePath().toString());
        LOOKUP = SymbolLookup.loaderLookup();

        MH_VERSION = downcall("ceebase_version", FunctionDescriptor.of(ValueLayout.ADDRESS));
        MH_OPEN = downcall("ceebase_open", FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS));
        MH_CLOSE = downcall("ceebase_close", FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS));
        MH_EXEC = downcall("ceebase_exec", FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS));
        MH_PREPARE = downcall("ceebase_prepare", FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS));
        MH_STEP = downcall("ceebase_step", FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS));
        MH_RESET = downcall("ceebase_reset", FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS));
        MH_FINALIZE = downcall("ceebase_finalize", FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS));

        MH_BIND_INT = downcall("ceebase_bind_int", FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.JAVA_INT, ValueLayout.JAVA_INT));
        MH_BIND_INT64 = downcall("ceebase_bind_int64", FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.JAVA_INT, ValueLayout.JAVA_LONG));
        MH_BIND_DOUBLE = downcall("ceebase_bind_double", FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.JAVA_INT, ValueLayout.JAVA_DOUBLE));
        MH_BIND_TEXT = downcall("ceebase_bind_text", FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.JAVA_INT));
        MH_BIND_NULL = downcall("ceebase_bind_null", FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.JAVA_INT));
        MH_CLEAR_BINDINGS = downcall("ceebase_clear_bindings", FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS));

        MH_COLUMN_COUNT = downcall("ceebase_column_count", FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS));
        MH_COLUMN_NAME = downcall("ceebase_column_name", FunctionDescriptor.of(ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.JAVA_INT));
        MH_COLUMN_TYPE = downcall("ceebase_column_type", FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.JAVA_INT));
        MH_COLUMN_IS_NULL = downcall("ceebase_column_is_null", FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.JAVA_INT));
        MH_COLUMN_INT = downcall("ceebase_column_int", FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.JAVA_INT));
        MH_COLUMN_INT64 = downcall("ceebase_column_int64", FunctionDescriptor.of(ValueLayout.JAVA_LONG, ValueLayout.ADDRESS, ValueLayout.JAVA_INT));
        MH_COLUMN_DOUBLE = downcall("ceebase_column_double", FunctionDescriptor.of(ValueLayout.JAVA_DOUBLE, ValueLayout.ADDRESS, ValueLayout.JAVA_INT));
        MH_COLUMN_BOOLEAN = downcall("ceebase_column_boolean", FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.JAVA_INT));
        MH_COLUMN_TEXT = downcall("ceebase_column_text", FunctionDescriptor.of(ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.JAVA_INT));

        MH_CHANGES = downcall("ceebase_changes", FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS));
        MH_ERRMSG = downcall("ceebase_errmsg", FunctionDescriptor.of(ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    }

    private static MethodHandle downcall(String name, FunctionDescriptor descriptor) {
        return LINKER.downcallHandle(LOOKUP.find(name).orElseThrow(() -> new RuntimeException("Symbol not found: " + name)), descriptor);
    }

    private static Path findLibrary() {
        List<Path> candidates = List.of(
            Path.of("build/libceebase.dll"),
            Path.of("../build/libceebase.dll"),
            Path.of("../../build/libceebase.dll"),
            Path.of("build/Release/ceebase.dll"),
            Path.of("build/libceebase.so"),
            Path.of("build/libceebase.dylib")
        );
        for (Path p : candidates) {
            if (p.toFile().exists()) return p;
        }
        return Path.of("libceebase.dll");
    }

    public static String version() {
        try {
            MemorySegment res = (MemorySegment) MH_VERSION.invokeExact();
            return res.reinterpret(32).getString(0);
        } catch (Throwable e) {
            throw new RuntimeException(e);
        }
    }

    public static Connection connect(String filename) {
        Arena arena = Arena.ofShared();
        MemorySegment dbPtrPtr = arena.allocate(ValueLayout.ADDRESS);
        MemorySegment cFilename = arena.allocateFrom(filename);

        try {
            int rc = (int) MH_OPEN.invokeExact(cFilename, dbPtrPtr);
            if (rc != 0) {
                arena.close();
                throw new CeebaseException("Failed to open database: " + filename);
            }
            MemorySegment dbHandle = dbPtrPtr.get(ValueLayout.ADDRESS, 0);
            return new Connection(dbHandle, arena);
        } catch (Throwable e) {
            arena.close();
            throw new CeebaseException(e.getMessage());
        }
    }

    public static class CeebaseException extends RuntimeException {
        public CeebaseException(String message) { super(message); }
    }

    public static class Connection implements AutoCloseable {
        final MemorySegment dbHandle;
        final Arena arena;
        private boolean closed = false;

        Connection(MemorySegment dbHandle, Arena arena) {
            this.dbHandle = dbHandle;
            this.arena = arena;
        }

        public PreparedStatement prepareStatement(String sql) {
            checkOpen();
            try (Arena local = Arena.ofConfined()) {
                MemorySegment cSql = local.allocateFrom(sql);
                MemorySegment stmtPtrPtr = local.allocate(ValueLayout.ADDRESS);
                int rc = (int) MH_PREPARE.invokeExact(dbHandle, cSql, stmtPtrPtr);
                if (rc != 0) {
                    throw new CeebaseException("SQL prepare failed: " + getLastError());
                }
                MemorySegment stmt = stmtPtrPtr.get(ValueLayout.ADDRESS, 0);
                return new PreparedStatement(this, stmt);
            } catch (Throwable t) {
                throw new CeebaseException(t.getMessage());
            }
        }

        public void execute(String sql) {
            try (PreparedStatement stmt = prepareStatement(sql)) {
                stmt.executeUpdate();
            }
        }

        public void beginTransaction() { execute("BEGIN;"); }
        public void commit() { execute("COMMIT;"); }
        public void rollback() { execute("ROLLBACK;"); }

        String getLastError() {
            try {
                MemorySegment err = (MemorySegment) MH_ERRMSG.invokeExact(dbHandle);
                return err.address() == 0 ? "Unknown error" : err.reinterpret(1024).getString(0);
            } catch (Throwable t) {
                return "Internal error";
            }
        }

        private void checkOpen() {
            if (closed) throw new CeebaseException("Connection is closed");
        }

        @Override
        public void close() {
            if (!closed) {
                closed = true;
                try {
                    int ignored = (int) MH_CLOSE.invokeExact(dbHandle);
                } catch (Throwable ignored) {
                } finally {
                    arena.close();
                }
            }
        }
    }

    public static class PreparedStatement implements AutoCloseable {
        private final Connection conn;
        private MemorySegment stmt;
        private final List<Arena> boundArenas = new ArrayList<>();

        PreparedStatement(Connection conn, MemorySegment stmt) {
            this.conn = conn;
            this.stmt = stmt;
        }

        public void setInt(int paramIdx, int val) {
            try {
                int ignored = (int) MH_BIND_INT.invokeExact(stmt, paramIdx, val);
            } catch (Throwable t) {
                throw new CeebaseException(t.getMessage());
            }
        }

        public void setLong(int paramIdx, long val) {
            try {
                int ignored = (int) MH_BIND_INT64.invokeExact(stmt, paramIdx, val);
            } catch (Throwable t) {
                throw new CeebaseException(t.getMessage());
            }
        }

        public void setDouble(int paramIdx, double val) {
            try {
                int ignored = (int) MH_BIND_DOUBLE.invokeExact(stmt, paramIdx, val);
            } catch (Throwable t) {
                throw new CeebaseException(t.getMessage());
            }
        }

        public void setString(int paramIdx, String val) {
            try {
                if (val == null) {
                    setNull(paramIdx);
                    return;
                }
                Arena arena = Arena.ofShared();
                boundArenas.add(arena);
                MemorySegment cStr = arena.allocateFrom(val);
                int ignored = (int) MH_BIND_TEXT.invokeExact(stmt, paramIdx, cStr, (int) cStr.byteSize() - 1);
            } catch (Throwable t) {
                throw new CeebaseException(t.getMessage());
            }
        }

        public void setNull(int paramIdx) {
            try {
                int ignored = (int) MH_BIND_NULL.invokeExact(stmt, paramIdx);
            } catch (Throwable t) {
                throw new CeebaseException(t.getMessage());
            }
        }

        public int executeUpdate() {
            try {
                reset();
                int rc = (int) MH_STEP.invokeExact(stmt);
                if (rc == 1) { // ERROR
                    throw new CeebaseException("Execution failed: " + conn.getLastError());
                }
                return (int) MH_CHANGES.invokeExact(conn.dbHandle);
            } catch (Throwable t) {
                throw new CeebaseException(t.getMessage());
            } finally {
                clearArenas();
            }
        }

        public ResultSet executeQuery() {
            try {
                reset();
                return new ResultSet(this, stmt);
            } finally {
                clearArenas();
            }
        }

        public void reset() {
            try {
                int ignored = (int) MH_RESET.invokeExact(stmt);
            } catch (Throwable t) {
                throw new CeebaseException(t.getMessage());
            }
        }

        private void clearArenas() {
            for (Arena a : boundArenas) a.close();
            boundArenas.clear();
        }

        @Override
        public void close() {
            clearArenas();
            if (stmt != null && stmt.address() != 0) {
                try {
                    int ignored = (int) MH_FINALIZE.invokeExact(stmt);
                } catch (Throwable ignored) {}
                stmt = null;
            }
        }
    }

    public static class ResultSet implements AutoCloseable {
        private final PreparedStatement stmt;
        private final MemorySegment stmtHandle;
        private boolean wasNull = false;

        ResultSet(PreparedStatement stmt, MemorySegment stmtHandle) {
            this.stmt = stmt;
            this.stmtHandle = stmtHandle;
        }

        public boolean next() {
            try {
                int rc = (int) MH_STEP.invokeExact(stmtHandle);
                return rc == 100; // CEEBASE_ROW
            } catch (Throwable t) {
                throw new CeebaseException(t.getMessage());
            }
        }

        public int getInt(int colIdx) {
            checkNull(colIdx);
            if (wasNull) return 0;
            try {
                return (int) MH_COLUMN_INT.invokeExact(stmtHandle, colIdx);
            } catch (Throwable t) {
                throw new CeebaseException(t.getMessage());
            }
        }

        public long getLong(int colIdx) {
            checkNull(colIdx);
            if (wasNull) return 0L;
            try {
                return (long) MH_COLUMN_INT64.invokeExact(stmtHandle, colIdx);
            } catch (Throwable t) {
                throw new CeebaseException(t.getMessage());
            }
        }

        public double getDouble(int colIdx) {
            checkNull(colIdx);
            if (wasNull) return 0.0;
            try {
                return (double) MH_COLUMN_DOUBLE.invokeExact(stmtHandle, colIdx);
            } catch (Throwable t) {
                throw new CeebaseException(t.getMessage());
            }
        }

        public boolean getBoolean(int colIdx) {
            checkNull(colIdx);
            if (wasNull) return false;
            try {
                return ((int) MH_COLUMN_BOOLEAN.invokeExact(stmtHandle, colIdx)) != 0;
            } catch (Throwable t) {
                throw new CeebaseException(t.getMessage());
            }
        }

        public String getString(int colIdx) {
            checkNull(colIdx);
            if (wasNull) return null;
            try {
                MemorySegment val = (MemorySegment) MH_COLUMN_TEXT.invokeExact(stmtHandle, colIdx);
                return val.address() == 0 ? null : val.reinterpret(4096).getString(0);
            } catch (Throwable t) {
                throw new CeebaseException(t.getMessage());
            }
        }

        public int getColumnCount() {
            try {
                return (int) MH_COLUMN_COUNT.invokeExact(stmtHandle);
            } catch (Throwable t) {
                throw new CeebaseException(t.getMessage());
            }
        }

        public String getColumnName(int colIdx) {
            try {
                MemorySegment name = (MemorySegment) MH_COLUMN_NAME.invokeExact(stmtHandle, colIdx);
                return name.address() == 0 ? "" : name.reinterpret(128).getString(0);
            } catch (Throwable t) {
                throw new CeebaseException(t.getMessage());
            }
        }

        public boolean wasNull() {
            return wasNull;
        }

        private void checkNull(int colIdx) {
            try {
                wasNull = ((int) MH_COLUMN_IS_NULL.invokeExact(stmtHandle, colIdx)) != 0;
            } catch (Throwable t) {
                wasNull = false;
            }
        }

        @Override
        public void close() {
            stmt.close();
        }
    }

    public static class ConnectionPool {
        private final String dbPath;
        private final Queue<Connection> pool = new ConcurrentLinkedQueue<>();
        private final int maxSize;

        public ConnectionPool(String dbPath, int maxSize) {
            this.dbPath = dbPath;
            this.maxSize = maxSize;
        }

        public Connection acquire() {
            Connection conn = pool.poll();
            if (conn != null) return conn;
            return Ceebase.connect(dbPath);
        }

        public void release(Connection conn) {
            if (pool.size() < maxSize) {
                pool.offer(conn);
            } else {
                conn.close();
            }
        }

        public void closeAll() {
            Connection conn;
            while ((conn = pool.poll()) != null) {
                conn.close();
            }
        }
    }
}
