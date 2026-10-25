#include "../include/RedisServer.h"
#include "../include/RedisCommandHandler.h"
#include "../include/RedisDatabase.h"

#include <arpa/inet.h>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <vector>

namespace {

// A client may pipeline many commands in one segment, and a single command may
// be far larger than a socket read. The per-connection buffer therefore grows
// to hold a partial command, with a ceiling so a client that never completes a
// line cannot exhaust server memory.
constexpr std::size_t kReadChunk = 8192;
constexpr std::size_t kMaxPendingBytes = 16u * 1024u * 1024u;

RedisServer* globalServer = nullptr;

// Async-signal-safe only: no allocation, no iostreams, no exit().
void signalHandler(int) {
    if (!globalServer) return;
    globalServer->requestShutdown();
}

// Writes the whole buffer, resuming after EINTR and partial writes.
bool sendAll(int socketFd, const std::string& data) {
    std::size_t sent = 0;
    while (sent < data.size()) {
        const ssize_t written = ::send(socketFd, data.data() + sent, data.size() - sent, MSG_NOSIGNAL);
        if (written < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        if (written == 0) return false;
        sent += static_cast<std::size_t>(written);
    }
    return true;
}

// Deliberately a free function taking the shared flag by reference: these
// threads are detached, so they must not dereference a RedisServer that may
// already have been destroyed by the time run() returns.
void serveClient(int clientSocket, const std::atomic<bool>& running) {
    RedisCommandHandler handler;
    std::string pending;
    std::vector<char> chunk(kReadChunk);

    while (running.load()) {
        const ssize_t bytes = recv(clientSocket, chunk.data(), chunk.size(), 0);
        if (bytes < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (bytes == 0) break; // peer closed

        pending.append(chunk.data(), static_cast<std::size_t>(bytes));

        if (pending.size() > kMaxPendingBytes) {
            sendAll(clientSocket,
                    "-ERR protocol error: request exceeded the maximum buffered size\r\n");
            break;
        }

        // Drain every complete command in the buffer, leaving any partial tail
        // for the next read.
        std::size_t offset = 0;
        bool closeConnection = false;

        while (true) {
            std::size_t consumed = 0;
            bool closeAfter = false;
            const std::string reply =
                handler.processCommand(pending.substr(offset), consumed, closeAfter);
            offset += consumed;

            if (!reply.empty() && !sendAll(clientSocket, reply)) {
                closeConnection = true;
                break;
            }
            if (closeAfter) { closeConnection = true; break; }
            if (consumed == 0) break; // incomplete command, wait for more bytes
        }

        pending.erase(0, offset);
        if (closeConnection) break;
    }

    close(clientSocket);
}

} // namespace

RedisServer::RedisServer(int port, std::string dumpFile, int persistIntervalSeconds)
    : port(port),
      server_socket(-1),
      running(true),
      listen_fd(-1),
      dumpFile(std::move(dumpFile)),
      persistIntervalSeconds(persistIntervalSeconds),
      persistRunning(false) {
    globalServer = this;
    setupSignalHandler();
}

void RedisServer::setupSignalHandler() {
    // SIGPIPE would otherwise kill the process when a client disconnects
    // mid-write; MSG_NOSIGNAL handles it at the send() call site.
    std::signal(SIGPIPE, SIG_IGN);
    struct sigaction action {};
    action.sa_handler = signalHandler;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0; // no SA_RESTART: accept() must return so we can exit
    sigaction(SIGINT, &action, nullptr);
    sigaction(SIGTERM, &action, nullptr);
}

void RedisServer::requestShutdown() {
    running.store(false);
    const int fd = listen_fd.load();
    if (fd != -1) ::shutdown(fd, SHUT_RDWR); // unblocks accept()
}

bool RedisServer::bindSocket() {
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        std::cerr << "Error creating server socket: " << std::strerror(errno) << "\n";
        return false;
    }

    int opt = 1;
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(static_cast<uint16_t>(port));
    serverAddr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_socket, reinterpret_cast<sockaddr*>(&serverAddr), sizeof(serverAddr)) < 0) {
        std::cerr << "Error binding server socket: " << std::strerror(errno) << "\n";
        close(server_socket);
        server_socket = -1;
        return false;
    }
    if (listen(server_socket, 128) < 0) {
        std::cerr << "Error listening on server socket: " << std::strerror(errno) << "\n";
        close(server_socket);
        server_socket = -1;
        return false;
    }

    listen_fd.store(server_socket);
    return true;
}

bool RedisServer::persistNow() {
    if (RedisDatabase::getInstance().dump(dumpFile)) {
        std::cout << "Database dumped to " << dumpFile << "\n";
        return true;
    }
    std::cerr << "Error dumping database to " << dumpFile << "\n";
    return false;
}

void RedisServer::startPersistence() {
    if (persistIntervalSeconds <= 0) return;

    persistRunning.store(true);
    persistThread = std::thread([this]() {
        // Sleep in short slices so shutdown is noticed promptly instead of
        // after a full interval.
        constexpr auto slice = std::chrono::milliseconds(200);
        const auto interval = std::chrono::seconds(persistIntervalSeconds);
        auto elapsed = std::chrono::steady_clock::duration::zero();

        while (persistRunning.load()) {
            std::this_thread::sleep_for(slice);
            elapsed += slice;
            if (elapsed < interval) continue;

            elapsed = std::chrono::steady_clock::duration::zero();
            if (!persistRunning.load()) break;
            persistNow();
        }
    });
}

void RedisServer::stopPersistence() {
    persistRunning.store(false);
    if (persistThread.joinable()) persistThread.join();
}

void RedisServer::run() {
    if (!bindSocket()) return;

    std::cout << "Redis server listening on port " << port << "\n";

    startPersistence();

    RedisDatabase& db = RedisDatabase::getInstance();
    std::cout << "Active eviction policy: " << db.activePolicy()
              << " (capacity limit: " << db.maxKeys() << " keys, 0 = unlimited)\n";

    while (running.load()) {
        const int clientSocket = accept(server_socket, nullptr, nullptr);
        if (clientSocket < 0) {
            if (errno == EINTR || errno == EINVAL) {
                if (!running.load()) break;
                continue;
            }
            std::cerr << "Error accepting connection: " << std::strerror(errno) << "\n";
            break;
        }
        // Detached on purpose. A thread-per-connection server that accumulates
        // std::thread objects in a vector and joins them only at shutdown
        // retains one object for every connection ever made; the benchmark
        // client opens a fresh connection per command. Detaching drops that
        // bookkeeping, at the cost of not being able to join at exit.
        std::thread(serveClient, clientSocket, std::cref(running)).detach();
    }

    // Give handlers that are mid-write a moment to finish before teardown.
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    stopPersistence();

    if (server_socket != -1) {
        listen_fd.store(-1);
        close(server_socket);
        server_socket = -1;
    }

    persistNow();
    std::cout << "Server shutdown complete\n";
}

// The signal handler calls requestShutdown(); run() performs the final
// snapshot once the accept loop has actually returned. Persisting here as
// well would write the dump twice on every Ctrl-C.
void RedisServer::shutdown() { requestShutdown(); }