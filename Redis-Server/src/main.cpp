#include "../include/RedisServer.h"
#include "../include/RedisDatabase.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <string>

int main(int argc, char* argv[]) {
    int port = 6379;
    std::string policyName = "LRU";
    size_t capacity = 1000;

    // Parse command-line arguments
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--port" && i + 1 < argc) {
            port = std::stoi(argv[++i]);
        } else if (arg == "--policy" && i + 1 < argc) {
            policyName = argv[++i];
        } else if (arg == "--capacity" && i + 1 < argc) {
            capacity = std::stoul(argv[++i]);
        } else if (i == 1 && arg[0] != '-') {
            try { port = std::stoi(arg); } catch (...) {}
        } else if (i == 2 && arg[0] != '-') {
            policyName = arg;
        } else if (i == 3 && arg[0] != '-') {
            try { capacity = std::stoul(arg); } catch (...) {}
        }
    }

    // Initialize database configuration
    RedisDatabase& db = RedisDatabase::getInstance();
    db.setPolicy(stringToPolicyType(policyName), capacity);

    if (db.load("dump.my_rdb")) {
        std::cout << "[Init] Database state restored from dump.my_rdb\n";
    } else {
        std::cout << "[Init] No existing dump found; starting with clean in-memory cache.\n";
    }

    RedisServer server(port, 8);

    // Periodic persistence thread (dumps every 300 seconds if server is running)
    std::thread persistenceThread([&server, &db]() {
        while (server.isRunning()) {
            for (int i = 0; i < 300 && server.isRunning(); ++i) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
            if (server.isRunning()) {
                if (db.dump("dump.my_rdb")) {
                    std::cout << "[Persistence] Background snapshot saved to dump.my_rdb\n";
                }
            }
        }
    });

    server.run();

    if (persistenceThread.joinable()) {
        persistenceThread.join();
    }

    return 0;
}
