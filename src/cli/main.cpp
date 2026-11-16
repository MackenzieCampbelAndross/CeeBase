#include <bit>
#include <iostream>
#include <string_view>
#include <vector>

#include "nexus/common/byte_utils.hpp"
#include "nexus/common/config.hpp"
#include "nexus/common/status.hpp"
#include "nexus/common/types.hpp"

namespace {

void PrintBanner() {
    std::cout << "========================================================\n";
    std::cout << " NexusDB: Relational Database Engine\n";
    std::cout << " Version: " << nexus::NEXUS_VERSION_STRING << " (C++20)\n";
    std::cout << " Built from first principles for performance and clarity\n";
    std::cout << "========================================================\n\n";
}

void PrintUsage(std::string_view program_name) {
    std::cout << "Usage: " << program_name << " <command> [arguments]\n\n";
    std::cout << "Commands:\n";
    std::cout << "  create <database_name>   Create a new persistent database\n";
    std::cout << "  open <database_name>     Open an existing database and launch SQL shell\n";
    std::cout << "  info                     Display engine configuration and architecture diagnostics\n";
    std::cout << "  version                  Display version information\n";
    std::cout << "  help                     Display this help message\n\n";
}

void PrintEngineInfo() {
    std::cout << "NexusDB Engine Diagnostics:\n";
    std::cout << "  Version:            " << nexus::NEXUS_VERSION_STRING << "\n";
    std::cout << "  Page Size:          " << nexus::PAGE_SIZE << " bytes (fixed 4 KB)\n";
    std::cout << "  Default Pool Size:  " << nexus::DEFAULT_BUFFER_POOL_SIZE << " frames ("
              << (nexus::DEFAULT_BUFFER_POOL_SIZE * nexus::PAGE_SIZE) / 1024 << " KB)\n";
    std::cout << "  File Magic:         0x" << std::hex << nexus::DB_FILE_MAGIC << std::dec << " (\"NXDB\")\n";
    std::cout << "  Host Endianness:    "
              << (std::endian::native == std::endian::little ? "Little-Endian" : "Big-Endian") << "\n";
    std::cout << "  Storage Wire Format: Little-Endian (explicit canonical encoding)\n";
    std::cout << "  Supported Types:    INTEGER (4B), BIGINT (8B), FLOAT (4B), DOUBLE (8B), BOOLEAN (1B), TEXT (Var)\n";
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
        std::cout << "NexusDB version " << nexus::NEXUS_VERSION_STRING << "\n";
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
        const std::string_view db_name = args[2];
        PrintBanner();
        std::cout << "[Phase 1 Core Infrastructure Active]\n";
        std::cout << "Request to create database: '" << db_name << "'\n";
        std::cout << "Storage subsystem (Phase 2) will create the file and 4KB page header.\n";
        return 0;
    }

    if (command == "open") {
        if (args.size() < 3) {
            std::cerr << "Error: 'open' requires a database name.\n";
            std::cerr << "Usage: " << args[0] << " open <database_name>\n";
            return 1;
        }
        const std::string_view db_name = args[2];
        PrintBanner();
        std::cout << "[Phase 1 Core Infrastructure Active]\n";
        std::cout << "Request to open database: '" << db_name << "'\n";
        std::cout << "Buffer pool and page manager (Phases 2-3) will load the database file.\n";
        return 0;
    }

    std::cerr << "Unknown command: '" << command << "'\n\n";
    PrintUsage(args[0]);
    return 1;
}
