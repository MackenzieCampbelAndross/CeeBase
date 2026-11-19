package com.ceebase;

import java.io.File;
import java.lang.foreign.*;
import java.lang.invoke.MethodHandle;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;

public class Ceebase implements AutoCloseable {
    private static final Linker LINKER = Linker.nativeLinker();
    private static final SymbolLookup LOOKUP;
    private static final MethodHandle MH_OPEN;
    private static final MethodHandle MH_CLOSE;
    private static final MethodHandle MH_EXEC;
    private static final MethodHandle MH_PREPARE;
    private static final MethodHandle MH_STEP;
    private static final MethodHandle MH_FINALIZE;
    private static final MethodHandle MH_COLUMN_COUNT;
    private static final MethodHandle MH_COLUMN_NAME;
    private static final MethodHandle MH_COLUMN_TEXT;
    private static final MethodHandle MH_CHANGES;
    private static final MethodHandle MH_ERRMSG;
    private static final MethodHandle MH_VERSION;

    static {
        // Load MinGW runtime dependency if available on Windows
        for (String p : List.of("C:/msys64/ucrt64/bin/libstdc++-6.dll", "C:/msys64/mingw64/bin/libstdc++-6.dll")) {
            if (new File(p).exists()) {
                try { System.load(p); } catch (Throwable ignored) {}
            }
        }

        // Find libceebase.dll
        Path dllPath = findLibrary();
        System.load(dllPath.toAbsolutePath().toString());
        LOOKUP = SymbolLookup.loaderLookup();

        MH_VERSION = downcall("ceebase_version", FunctionDescriptor.of(ValueLayout.ADDRESS));
        MH_OPEN = downcall("ceebase_open", FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS));
        MH_CLOSE = downcall("ceebase_close", FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS));
        MH_EXEC = downcall("ceebase_exec", FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS));
        MH_PREPARE = downcall("ceebase_prepare", FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS));
        MH_STEP = downcall("ceebase_step", FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS));
        MH_FINALIZE = downcall("ceebase_finalize", FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS));
        MH_COLUMN_COUNT = downcall("ceebase_column_count", FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS));
        MH_COLUMN_NAME = downcall("ceebase_column_name", FunctionDescriptor.of(ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.JAVA_INT));
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

    private final MemorySegment dbHandle;
    private final Arena arena;

    private Ceebase(MemorySegment dbHandle, Arena arena) {
        this.dbHandle = dbHandle;
        this.arena = arena;
    }

    public static String version() {
        try {
            MemorySegment res = (MemorySegment) MH_VERSION.invokeExact();
            return res.reinterpret(32).getString(0);
        } catch (Throwable e) {
            throw new RuntimeException(e);
        }
    }

    public static Ceebase open(String filename) {
        Arena arena = Arena.ofShared();
        MemorySegment dbPtrPtr = arena.allocate(ValueLayout.ADDRESS);
        MemorySegment cFilename = arena.allocateFrom(filename);

        try {
            int rc = (int) MH_OPEN.invokeExact(cFilename, dbPtrPtr);
            if (rc != 0) {
                arena.close();
                throw new RuntimeException("Failed to open database: " + filename);
            }
            MemorySegment dbHandle = dbPtrPtr.get(ValueLayout.ADDRESS, 0);
            return new Ceebase(dbHandle, arena);
        } catch (Throwable e) {
            arena.close();
            throw new RuntimeException(e);
        }
    }

    public void execute(String sql) {
        try (Arena local = Arena.ofConfined()) {
            MemorySegment cSql = local.allocateFrom(sql);
            MemorySegment stmtPtrPtr = local.allocate(ValueLayout.ADDRESS);
            int rc = (int) MH_PREPARE.invokeExact(dbHandle, cSql, stmtPtrPtr);
            if (rc != 0) {
                throw new RuntimeException("SQL prepare failed: " + getLastError());
            }
            MemorySegment stmt = stmtPtrPtr.get(ValueLayout.ADDRESS, 0);
            try {
                int ignored1 = (int) MH_STEP.invokeExact(stmt);
            } finally {
                int ignored2 = (int) MH_FINALIZE.invokeExact(stmt);
            }
        } catch (Throwable e) {
            throw new RuntimeException(e);
        }
    }

    public QueryResult query(String sql) {
        try (Arena local = Arena.ofConfined()) {
            MemorySegment cSql = local.allocateFrom(sql);
            MemorySegment stmtPtrPtr = local.allocate(ValueLayout.ADDRESS);
            int rc = (int) MH_PREPARE.invokeExact(dbHandle, cSql, stmtPtrPtr);
            if (rc != 0) {
                throw new RuntimeException("SQL query prepare failed: " + getLastError());
            }

            MemorySegment stmt = stmtPtrPtr.get(ValueLayout.ADDRESS, 0);
            try {
                int stepRc = (int) MH_STEP.invokeExact(stmt);
                int colCount = (int) MH_COLUMN_COUNT.invokeExact(stmt);
                List<String> columns = new ArrayList<>();
                for (int i = 0; i < colCount; i++) {
                    MemorySegment nameSeg = (MemorySegment) MH_COLUMN_NAME.invokeExact(stmt, i);
                    columns.add(nameSeg.reinterpret(128).getString(0));
                }

                List<List<String>> rows = new ArrayList<>();
                while (stepRc == 100) { // CEEBASE_ROW
                    List<String> row = new ArrayList<>();
                    for (int i = 0; i < colCount; i++) {
                        MemorySegment valSeg = (MemorySegment) MH_COLUMN_TEXT.invokeExact(stmt, i);
                        row.add(valSeg.address() == 0 ? null : valSeg.reinterpret(4096).getString(0));
                    }
                    rows.add(row);
                    stepRc = (int) MH_STEP.invokeExact(stmt);
                }

                return new QueryResult(columns, rows);
            } finally {
                int ignored3 = (int) MH_FINALIZE.invokeExact(stmt);
            }
        } catch (Throwable e) {
            throw new RuntimeException(e);
        }
    }

    private String getLastError() {
        try {
            MemorySegment err = (MemorySegment) MH_ERRMSG.invokeExact(dbHandle);
            return err.address() == 0 ? "Unknown error" : err.reinterpret(1024).getString(0);
        } catch (Throwable t) {
            return "Internal error";
        }
    }

    @Override
    public void close() {
        try {
            int ignored4 = (int) MH_CLOSE.invokeExact(dbHandle);
        } catch (Throwable ignored) {
        } finally {
            arena.close();
        }
    }

    public record QueryResult(List<String> columns, List<List<String>> rows) {}
}
