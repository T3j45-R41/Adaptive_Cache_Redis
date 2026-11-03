#include "../include/RedisServer.h"
#include "../include/RedisCommandHandler.h"
#include "../include/RedisDatabase.h"

#include <iostream>
#include <vector>
#include <cstring>
#include <signal.h>

#ifdef _WIN32
    #define CLOSE_SOCKET(s) closesocket(s)
    #define IS_VALID_SOCKET(s) ((s) != INVALID_SOCKET)
#else
    #define CLOSE_SOCKET(s) ::close(s)
    #define INVALID_SOCKET (-1)
    #define IS_VALID_SOCKET(s) ((s) >= 0)
#endif

// Global atomic server pointer for signal notification
static std::atomic<RedisServer*> globalServerInstance{nullptr};

static void safeSignalHandler(int signum) {
    RedisServer* s = globalServerInstance.load();
    if (s) {
        std::cout << "\n[Signal] Caught signal " << signum << ", initiating graceful shutdown...\n";
        s->shutdown();
    }
}

void RedisServer::setupSignalHandler() {
    globalServerInstance.store(this);
    signal(SIGINT, safeSignalHandler);
#ifndef _WIN32
    signal(SIGTERM, safeSignalHandler);
    signal(SIGPIPE, SIG_IGN); // Prevent process crash on broken client socket writes
#endif
}

RedisServer::RedisServer(int port, size_t numWorkerThreads)
    : port(port),
      server_socket(INVALID_SOCKET),
      running(true),
      numThreads(numWorkerThreads) {
#ifdef _WIN32
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif
    setupSignalHandler();
}

RedisServer::~RedisServer() {
    shutdown();
#ifdef _WIN32
    WSACleanup();
#endif
}

void RedisServer::shutdown() {
    if (!running.exchange(false)) {
        return; // Already shut down
    }

    // Unblock the accept loop by closing listening socket
    if (IS_VALID_SOCKET(server_socket)) {
        CLOSE_SOCKET(server_socket);
        server_socket = INVALID_SOCKET;
    }

    // Wake all waiting worker threads
    queueCv.notify_all();

    for (auto& t : workerThreads) {
        if (t.joinable()) {
            t.join();
        }
    }
    workerThreads.clear();

    // Persist dirty database state to dump.my_rdb
    if (RedisDatabase::getInstance().dump("dump.my_rdb")) {
        std::cout << "[Shutdown] Database successfully dumped to dump.my_rdb\n";
    } else {
        std::cerr << "[Shutdown] Error dumping database to disk\n";
    }

    std::cout << "[Shutdown] Server shutdown completed gracefully.\n";
}

void RedisServer::handleClientSession(socket_t client_socket) {
    RedisCommandHandler cmdHandler;
    std::string connectionBuffer;
    char recvBuffer[4096];

    while (running) {
        int bytes = recv(client_socket, recvBuffer, sizeof(recvBuffer), 0);
        if (bytes <= 0) {
            break; // Client closed connection or socket error
        }

        connectionBuffer.append(recvBuffer, bytes);

        // Process all complete commands present in the buffer (pipelining & framing)
        std::vector<std::string> tokens;
        while (RedisCommandHandler::extractNextCommand(connectionBuffer, tokens)) {
            std::string response = cmdHandler.dispatchCommand(tokens);

            size_t totalSent = 0;
            while (totalSent < response.size()) {
                int sent = send(client_socket, response.c_str() + totalSent, 
                                static_cast<int>(response.size() - totalSent), 0);
                if (sent <= 0) {
                    break;
                }
                totalSent += sent;
            }
        }
    }

    CLOSE_SOCKET(client_socket);
}

void RedisServer::workerThreadLoop() {
    while (running) {
        socket_t client_sock = INVALID_SOCKET;
        {
            std::unique_lock<std::mutex> lock(queueMutex);
            queueCv.wait(lock, [this]() {
                return !running || !clientQueue.empty();
            });

            if (!running && clientQueue.empty()) {
                return;
            }

            if (!clientQueue.empty()) {
                client_sock = clientQueue.front();
                clientQueue.pop();
            }
        }

        if (IS_VALID_SOCKET(client_sock)) {
            handleClientSession(client_sock);
        }
    }
}

void RedisServer::run() {
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (!IS_VALID_SOCKET(server_socket)) {
        std::cerr << "[Error] Failed to create server socket\n";
        return;
    }

    int opt = 1;
#ifdef _WIN32
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));
#else
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port);
    serverAddr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_socket, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        std::cerr << "[Error] Failed to bind server socket to port " << port << "\n";
        CLOSE_SOCKET(server_socket);
        server_socket = INVALID_SOCKET;
        return;
    }

    if (listen(server_socket, 128) < 0) {
        std::cerr << "[Error] Failed to listen on server socket\n";
        CLOSE_SOCKET(server_socket);
        server_socket = INVALID_SOCKET;
        return;
    }

    std::cout << "=========================================================\n";
    std::cout << " Adaptive Cache Redis-Compatible Server\n";
    std::cout << " Listening on TCP Port: " << port << "\n";
    std::cout << " Bounded Worker Thread Pool: " << numThreads << " threads\n";
    std::cout << " Active Eviction Policy: " << RedisDatabase::getInstance().getPolicyName() << "\n";
    std::cout << " Cache Capacity: " << RedisDatabase::getInstance().getCapacity() << " items\n";
    std::cout << "=========================================================\n";

    // Launch worker thread pool
    for (size_t i = 0; i < numThreads; ++i) {
        workerThreads.emplace_back(&RedisServer::workerThreadLoop, this);
    }

    // Accept loop
    while (running) {
        sockaddr_in clientAddr{};
#ifdef _WIN32
        int clientLen = sizeof(clientAddr);
#else
        socklen_t clientLen = sizeof(clientAddr);
#endif
        socket_t client_socket = accept(server_socket, (struct sockaddr*)&clientAddr, &clientLen);

        if (!IS_VALID_SOCKET(client_socket)) {
            if (running) {
                std::cerr << "[Warning] accept() returned invalid socket descriptor\n";
            }
            break;
        }

        {
            std::lock_guard<std::mutex> lock(queueMutex);
            clientQueue.push(client_socket);
        }
        queueCv.notify_one();
    }

    shutdown();
}
