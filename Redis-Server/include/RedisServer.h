#ifndef REDIS_SERVER_H
#define REDIS_SERVER_H

#include <string>
#include <atomic>
#include <vector>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    typedef SOCKET socket_t;
#else
    #include <sys/socket.h>
    #include <unistd.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    typedef int socket_t;
#endif

class RedisServer {
public:
    RedisServer(int port = 6379, size_t numWorkerThreads = 8);
    ~RedisServer();

    void run();
    void shutdown();
    bool isRunning() const { return running.load(); }

private:
    void setupSignalHandler();
    void workerThreadLoop();
    void handleClientSession(socket_t client_socket);

    int port;
    socket_t server_socket;
    std::atomic<bool> running;
    size_t numThreads;

    std::vector<std::thread> workerThreads;
    std::queue<socket_t> clientQueue;
    std::mutex queueMutex;
    std::condition_variable queueCv;
};

#endif // REDIS_SERVER_H
