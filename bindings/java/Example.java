import com.ceebase.Ceebase;
import java.io.File;

public class Example {
    public static void main(String[] args) {
        System.out.println("CeeBase Java Engine Version: " + Ceebase.version());

        String dbFile = "java_demo.cbdb";
        new File(dbFile).delete();
        new File("java_demo.wal").delete();

        try (Ceebase.Connection conn = Ceebase.connect(dbFile)) {
            System.out.println("Connected to: " + dbFile);

            // Create table
            conn.execute("CREATE TABLE products (id INTEGER PRIMARY KEY, name TEXT, price DOUBLE, in_stock BOOLEAN);");

            // Parameterized inserts
            try (Ceebase.PreparedStatement stmt = conn.prepareStatement("INSERT INTO products VALUES (?, ?, ?, ?);")) {
                stmt.setInt(1, 1);
                stmt.setString(2, "Mechanical Keyboard");
                stmt.setDouble(3, 89.99);
                stmt.setInt(4, 1);
                stmt.executeUpdate();

                stmt.setInt(1, 2);
                stmt.setString(2, "Wireless Mouse");
                stmt.setDouble(3, 49.50);
                stmt.setInt(4, 1);
                stmt.executeUpdate();

                stmt.setInt(1, 3);
                stmt.setString(2, "4K Gaming Monitor");
                stmt.setDouble(3, 349.95);
                stmt.setInt(4, 0);
                stmt.executeUpdate();
            }

            // Query results
            try (Ceebase.PreparedStatement stmt = conn.prepareStatement("SELECT id, name, price, in_stock FROM products ORDER BY id;");
                 Ceebase.ResultSet rs = stmt.executeQuery()) {

                System.out.println("\nJava Query Results:");
                System.out.println("------------------------------------------------------------------");
                while (rs.next()) {
                    int id = rs.getInt(0);
                    String name = rs.getString(1);
                    double price = rs.getDouble(2);
                    boolean inStock = rs.getBoolean(3);
                    System.out.printf("ID: %d | Name: %-22s | Price: $%-7.2f | InStock: %b%n", id, name, price, inStock);
                }
                System.out.println("------------------------------------------------------------------");
            }

            // Transaction demonstration
            conn.beginTransaction();
            conn.execute("INSERT INTO products VALUES (4, 'USB-C Cable', 12.50, 1);");
            conn.commit();
        }

        // Reopening / Connection Pool test after conn closed
        Ceebase.ConnectionPool pool = new Ceebase.ConnectionPool(dbFile, 3);
        try {
            Ceebase.Connection pooledConn = pool.acquire();
            try (Ceebase.PreparedStatement stmt = pooledConn.prepareStatement("SELECT COUNT(*) FROM products;");
                 Ceebase.ResultSet rs = stmt.executeQuery()) {
                if (rs.next()) {
                    System.out.println("[POOL] Total products in database: " + rs.getInt(0));
                }
            }
            pool.release(pooledConn);
        } finally {
            pool.closeAll();
        }

        // Cleanup
        new File(dbFile).delete();
        new File("java_demo.wal").delete();

        System.out.println("[SUCCESS] Java CeeBase workflow completed successfully!");
    }
}
