#include "../include/RedisDatabase.h"
#include "../include/RedisServer.h"
#include "../include/eviction/eviction_engine.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

// Parses an integer argument, reporting failure instead of throwing out of main.
bool parseInt(const char* text, int& out) {
    try {
        std::size_t used = 0;
        const int value = std::stoi(text, &used);
        if (used != std::string(text).size()) return false;
        out = value;
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

void printUsage(const char* program) {
    std::cout << "Usage: " << program << " [port] [dump-file] [snapshot-interval-seconds]\n"
              << "  port                      TCP port to listen on (default 6379)\n"
              << "  dump-file                 snapshot path (default dump.my_rdb)\n"
              << "  snapshot-interval-seconds automatic snapshot period, 0 disables\n"
              << "                            (default 300)\n\n"
              << "Policies:";
    for (const auto& name : EvictionEngine::policyNames()) std::cout << ' ' << name;
    std::cout << "\n";
}

} // namespace

int main(int argc, char* argv[]) {
    int port = 6379;
    std::string dumpFile = "dump.my_rdb";
    int persistInterval = 300;

    if (argc >= 2 && std::string(argv[1]) == "-h") {
        printUsage(argv[0]);
        return 0;
    }
    if (argc >= 2 && !parseInt(argv[1], port)) {
        std::cerr << "Invalid port: " << argv[1] << "\n";
        printUsage(argv[0]);
        return 1;
    }
    if (argc >= 3) dumpFile = argv[2];
    if (argc >= 4 && !parseInt(argv[3], persistInterval)) {
        std::cerr << "Invalid snapshot interval: " << argv[3] << "\n";
        return 1;
    }

    if (port < 1 || port > 65535) {
        std::cerr << "Port must be between 1 and 65535\n";
        return 1;
    }

    // The snapshot thread is owned by RedisServer and joined on exit, so the
    // previous detached thread could no longer outlive the process.
    RedisDatabase& db = RedisDatabase::getInstance();
    if (db.load(dumpFile)) {
        std::cout << "Database loaded from " << dumpFile << " (" << db.dbSize() << " keys)\n";
    } else {
        std::cout << "No usable snapshot at " << dumpFile << "; starting empty\n";
    }

    RedisServer server(port, dumpFile, persistInterval);
    server.run();
    return 0;
}