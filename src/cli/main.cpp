#include <bit>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "ceebase/buffer/buffer_pool_manager.hpp"
#include "ceebase/catalog/catalog.hpp"
#include "ceebase/common/byte_utils.hpp"
#include "ceebase/common/config.hpp"
#include "ceebase/common/status.hpp"
#include "ceebase/common/types.hpp"
#include "ceebase/concurrency/transaction_manager.hpp"
#include "ceebase/execution/execution_engine.hpp"
#include "ceebase/recovery/log_manager.hpp"
#include "ceebase/recovery/recovery_engine.hpp"
#include "ceebase/storage/disk_manager.hpp"

namespace {

void PrintBanner() {
    std::cout << "========================================================\n";
    std::cout << " CeeBase: Relational Database Engine\n";
    std::cout << " Version: " << ceebase::CEEBASE_VERSION_STRING << " (C++20)\n";
    std::cout << " Storage: 4 KB Slotted Pages | B+ Tree | WAL | ACID\n";
    std::cout << "========================================================\n\n";
}

void PrintUsage(std::string_view program_name) {
    std::cout << "Usage: " << program_name << " <command> [arguments]\n\n";
    std::cout << "Commands:\n";
    std::cout << "  create <database_name>   Create a new persistent database file\n";
    std::cout << "  open <database_name>     Open database and launch interactive SQL shell\n";
    std::cout << "  info                     Display engine configuration and architecture diagnostics\n";
    std::cout << "  version                  Display version information\n";
    std::cout << "  help                     Display this help message\n\n";
}

void PrintEngineInfo() {
    std::cout << "CeeBase Engine Diagnostics:\n";
    std::cout << "  Version:              " << ceebase::CEEBASE_VERSION_STRING << "\n";
    std::cout << "  Page Size:            " << ceebase::PAGE_SIZE << " bytes (fixed 4 KB)\n";
    std::cout << "  Default Pool Size:    " << ceebase::DEFAULT_BUFFER_POOL_SIZE << " frames ("
              << (ceebase::DEFAULT_BUFFER_POOL_SIZE * ceebase::PAGE_SIZE) / 1024 << " KB)\n";
    std::cout << "  File Magic:           0x" << std::hex << ceebase::DB_FILE_MAGIC << std::dec << " (\"CBDB\")\n";
    std::cout << "  Host Endianness:      "
              << (std::endian::native == std::endian::little ? "Little-Endian" : "Big-Endian") << "\n";
    std::cout << "  Storage Wire Format:  Little-Endian (explicit canonical encoding)\n";
    std::cout << "  Supported Data Types: INTEGER (4B), BIGINT (8B), FLOAT (4B), DOUBLE (8B), BOOLEAN (1B), TEXT (Var)\n";
    std::cout << "  B+ Tree Indexing:     Internal & Leaf Page Splits, Merges, Range Iterators\n";
    std::cout << "  Crash Recovery:       ARIES-style Analysis, Redo, and Undo with WAL\n";
}

int HandleCreate(std::string_view db_name) {
    std::filesystem::path path(db_name);
    if (path.extension() != ".cbdb" && path.extension() != ".nxdb") {
        path += ".cbdb";
    }

    ceebase::DiskManager dm;
    ceebase::Status s = dm.CreateDatabase(path);
    if (!s.ok()) {
        std::cerr << "Database creation error: " << s.ToString() << "\n";
        return 1;
    }

    std::cout << "[SUCCESS] Database created successfully: " << path.string() << "\n";
    std::cout << "  Total Pages:   " << dm.GetNumPages() << " (Page 0 Header initialized)\n";
    std::cout << "  CRC-32 Valid:  0x" << std::hex << dm.GetHeader().checksum << std::dec << "\n";
    return 0;
}

int HandleInteractiveShell(std::string_view db_name) {
    std::filesystem::path db_path(db_name);
    if (!std::filesystem::exists(db_path)) {
        if (std::filesystem::exists(std::string(db_name) + ".cbdb")) {
            db_path = std::string(db_name) + ".cbdb";
        } else if (std::filesystem::exists(std::string(db_name) + ".nxdb")) {
            db_path = std::string(db_name) + ".nxdb";
        } else if (db_path.extension() != ".cbdb" && db_path.extension() != ".nxdb") {
            db_path += ".cbdb";
        }
    }

    ceebase::DiskManager dm;
    ceebase::Status s = dm.OpenDatabase(db_path);
    if (!s.ok()) {
        std::cerr << "Failed to open database: " << s.ToString() << "\n";
        return 1;
    }

    std::filesystem::path wal_path = db_path;
    wal_path.replace_extension(".wal");

    ceebase::LogManager log_mgr(wal_path);
    ceebase::BufferPoolManager bpm(ceebase::DEFAULT_BUFFER_POOL_SIZE, &dm, &log_mgr);
    ceebase::Catalog catalog(&bpm);
    ceebase::TransactionManager txn_mgr(&log_mgr, &bpm, &catalog);
    ceebase::RecoveryEngine recovery(&log_mgr, &bpm, &catalog);

    // Run crash recovery on open
    auto rec_status = recovery.Recover();
    if (!rec_status.ok()) {
        std::cerr << "Warning: Recovery notice: " << rec_status.ToString() << "\n";
    }

    ceebase::ExecutionEngine engine(&bpm, &catalog, &txn_mgr, &recovery);

    std::cout << "Connected to database: " << db_path.filename().string() << "\n";
    std::cout << "Type SQL statements ending with ';' (or 'exit' / 'quit' to exit).\n";
    std::cout << "Commands: SHOW TABLES; EXPLAIN <query>; BEGIN; COMMIT; ROLLBACK;\n\n";

    std::string line_buffer;
    while (true) {
        if (line_buffer.empty()) {
            std::cout << "ceebase> ";
        } else {
            std::cout << "   -> ";
        }

        std::string line;
        if (!std::getline(std::cin, line)) {
            break;
        }

        std::string trimmed = line;
        while (!trimmed.empty() && std::isspace(static_cast<unsigned char>(trimmed.back()))) trimmed.pop_back();
        while (!trimmed.empty() && std::isspace(static_cast<unsigned char>(trimmed.front()))) trimmed.erase(trimmed.begin());

        if (trimmed == "exit" || trimmed == "quit" || trimmed == "exit;" || trimmed == "quit;") {
            break;
        }

        if (trimmed == "SHOW TABLES;" || trimmed == "show tables;" || trimmed == "SHOW TABLES" || trimmed == "show tables") {
            auto tables = catalog.GetAllTables();
            std::cout << "+----------------------+\n";
            std::cout << "| Tables               |\n";
            std::cout << "+----------------------+\n";
            for (const auto* t : tables) {
                std::cout << "| " << std::left << std::setw(20) << t->name << " |\n";
            }
            std::cout << "+----------------------+\n";
            std::cout << tables.size() << " table(s) in database.\n\n";
            line_buffer.clear();
            continue;
        }

        line_buffer += " " + line;
        if (line_buffer.find(';') != std::string::npos) {
            auto result = engine.Execute(line_buffer);
            result.Format(std::cout);
            std::cout << "\n";
            line_buffer.clear();
        }
    }

    std::cout << "\nFlushing database pages and closing connection...\n";
    (void)bpm.FlushAllPages();
    dm.Close();
    std::cout << "Goodbye.\n";
    return 0;
}

} // namespace

int main(int argc, char* argv[]) {
    std::vector<std::string_view> args;
    args.reserve(static_cast<size_t>(argc));
    for (int i = 0; i < argc; ++i) {
        args.emplace_back(argv[i]);
    }

    if (args.size() < 2) {
        PrintBanner();
        PrintUsage(args[0]);
        return 0;
    }

    const std::string_view command = args[1];

    if (command == "version" || command == "--version" || command == "-v") {
        std::cout << "CeeBase version " << ceebase::CEEBASE_VERSION_STRING << "\n";
        return 0;
    }

    if (command == "help" || command == "--help" || command == "-h") {
        PrintBanner();
        PrintUsage(args[0]);
        return 0;
    }

    if (command == "info") {
        PrintBanner();
        PrintEngineInfo();
        return 0;
    }

    if (command == "create") {
        if (args.size() < 3) {
            std::cerr << "Error: 'create' requires a database name.\n";
            std::cerr << "Usage: " << args[0] << " create <database_name>\n";
            return 1;
        }
        return HandleCreate(args[2]);
    }

    if (command == "open") {
        if (args.size() < 3) {
            std::cerr << "Error: 'open' requires a database name.\n";
            std::cerr << "Usage: " << args[0] << " open <database_name>\n";
            return 1;
        }
        PrintBanner();
        return HandleInteractiveShell(args[2]);
    }

    std::cerr << "Unknown command: '" << command << "'\n\n";
    PrintUsage(args[0]);
    return 1;
}
