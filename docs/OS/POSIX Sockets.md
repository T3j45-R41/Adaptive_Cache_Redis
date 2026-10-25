---
tags: [os, networking, sockets, tcp, ipc, project/adaptive-cache-redis]
status: implemented
---

# POSIX Sockets

## Definition

Sockets are the POSIX API for network IPC, inherited from Berkeley BSD. The
model is **file-descriptor based**: `socket()` returns an integer index into
the per-process descriptor table, and that descriptor is used with ordinary
file-style calls.

The TCP server lifecycle is a fixed sequence:

| Call | Role |
| --- | --- |
| `socket()` | Create a descriptor, ask the kernel for a stream (TCP) socket |
| `setsockopt()` | Set options, e.g. `SO_REUSEADDR` |
| `bind()` | Attach a local address and port |
| `listen()` | Mark it passive, set the accept backlog |
| `accept()` | Block until a connection completes; return a **new** descriptor |
| `recv()` / `send()` | Move bytes |
| `close()` | Release the descriptor |

> [!important] `accept()` returns a new descriptor
> It does not return the listening descriptor. Each connection gets its own
> descriptor in the same table as files, pipes, and directories — which is why a
> socket can be `dup`'d, `fcntl`'d, or inherited across `fork()` like anything
> else.

---

## In this project

`src/RedisServer.cpp:133`–`src/RedisServer.cpp:155`
```cpp
server_socket = socket(AF_INET, SOCK_STREAM, 0);

int opt = 1;
setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

sockaddr_in serverAddr{};
serverAddr.sin_family = AF_INET;
serverAddr.sin_port = htons(static_cast<uint16_t>(port));
serverAddr.sin_addr.s_addr = INADDR_ANY;

bind(server_socket, reinterpret_cast<sockaddr*>(&serverAddr), sizeof(serverAddr));
listen(server_socket, 128);
```

### `SO_REUSEADDR`

Without it, closing a listening socket leaves it in `TIME_WAIT` for minutes.
On a restart the `bind()` fails with `EADDRINUSE` even though nothing is
listening. This is why restarting the server during a demo is instant.

### `htons` / byte order

The network byte order for multi-byte integers is **big-endian**. x86 is
little-endian, so `htons()` (`host to network short`) converts a port number
into wire order. The server stores its port as an `int` internally and converts
only at the `bind()` call.

### `INADDR_ANY`

Binds to every interface, so the server is reachable from other machines on the
network — not just `127.0.0.1`.

### `MSG_NOSIGNAL`

Writing to a socket whose peer has already disconnected raises `SIGPIPE` by
default, which would **terminate the process**. Rather than ignoring the signal
globally only, each send opts out at the call site.

`src/RedisServer.cpp:38`
```cpp
const ssize_t written =
    ::send(socketFd, data.data() + sent, data.size() - sent, MSG_NOSIGNAL);
```

---

## Partial writes and `EINTR`

A single `send()` may transfer fewer bytes than requested — the kernel buffer
fills, or a signal interrupts the call. Treating that as a completed write
corrupts the response stream.

`src/RedisServer.cpp:35`–`src/RedisServer.cpp:46`
```cpp
bool sendAll(int socketFd, const std::string& data) {
    std::size_t sent = 0;
    while (sent < data.size()) {
        const ssize_t written = ::send(...);
        if (written < 0) {
            if (errno == EINTR) continue;   // signal, not failure
            return false;
        }
        if (written == 0) return false;
        sent += static_cast<std::size_t>(written);
    }
    return true;
}
```

> [!warning] Not implemented
> - **UDP / datagram sockets.** The server is TCP-only.
> - **Unix domain sockets.** Clients must use TCP even locally.
> - **TLS.** Traffic is plaintext; there is no encryption or authentication.
> - **IPv6.** `AF_INET` only; no `AF_INET6` or dual-stack `bind`.
> - **Non-blocking I/O / `epoll`.** Every handler thread blocks in `recv()`,
>   so a connection parked on a slow client still occupies a thread.

---

## Related

- [[OS/Stream Framing]] — TCP has no message boundaries
- [[OS/Signals and Interrupts]] — `MSG_NOSIGNAL` and `SIGPIPE`
- [[OS/System Calls]] — every function above is a syscall
- [[Project/RESP Protocol]] — the application protocol running on top