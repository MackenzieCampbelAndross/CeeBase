import com.ceebase.Ceebase;
import java.io.File;

public class Example {
    public static void main(String[] args) {
        System.out.println("CeeBase Java Engine Version: " + Ceebase.version());

        String dbFile = "java_demo.cbdb";

        try (Ceebase db = Ceebase.open(dbFile)) {
            System.out.println("Connected to: " + dbFile);

            db.execute("CREATE TABLE servers (id INTEGER PRIMARY KEY, host TEXT, latency DOUBLE);");
            db.execute("INSERT INTO servers VALUES (1, 'us-east-1.ceebase.io', 12.4);");
            db.execute("INSERT INTO servers VALUES (2, 'eu-central-1.ceebase.io', 45.1);");
            db.execute("INSERT INTO servers VALUES (3, 'ap-south-1.ceebase.io', 8.2);");

            Ceebase.QueryResult res = db.query("SELECT id, host, latency FROM servers ORDER BY latency;");

            System.out.println("\nJava Query Results (" + res.rows().size() + " rows):");
            System.out.println("-------------------------------------------------------------");
            System.out.println(String.join(" | ", res.columns()));
            System.out.println("-------------------------------------------------------------");
            for (var row : res.rows()) {
                System.out.println(String.join(" | ", row));
            }
            System.out.println("-------------------------------------------------------------");
        }

        // Cleanup
        new File(dbFile).delete();
        new File("java_demo.wal").delete();
        System.out.println("[SUCCESS] Java CeeBase workflow completed successfully!");
    }
}
