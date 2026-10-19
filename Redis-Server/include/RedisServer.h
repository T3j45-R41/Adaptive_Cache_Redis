#ifndef REDIS_SERVER_H
#define REDIS_SERVER_H

#include <atomic>
#include <string>
#include <thread>

class RedisServer {
public:
    RedisServer(int port, std::string dumpFile = "dump.my_rdb",
                int persistIntervalSeconds = 300);

    // Accepts connections until shutdown is requested, then stops the
    // background snapshot thread and writes one final dump.
    void run();

    // Async-signal-safe: sets the running flag and unblocks accept().
    void requestShutdown();

    // Full shutdown, including the final snapshot. Not signal-safe; call from
    // the main thread after the accept loop has returned.
    void shutdown();

private:
    int port;
    int server_socket;
    std::atomic<bool> running;
    // Mirrors server_socket so the signal handler can unblock accept() without
    // touching any non-signal-safe facility.
    std::atomic<int> listen_fd;

    std::string dumpFile;
    int persistIntervalSeconds;
    std::atomic<bool> persistRunning;
    std::thread persistThread;

    void setupSignalHandler();
    void startPersistence();
    void stopPersistence();
    bool persistNow();
    bool bindSocket();
};

#endif