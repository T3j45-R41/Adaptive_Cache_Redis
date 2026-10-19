---
tags: [os, networking, framing, io, buffers, project/adaptive-cache-redis]
status: implemented
---

# Stream Framing

## Definition

TCP is a **byte stream**, not a message stream. It guarantees ordered, reliable
delivery of bytes and nothing else. A single `send()` by the peer may arrive as
one `recv()`, or several, or may be split across two. There is no packet
boundary preserved end to end.

**Framing** is the job of recovering message boundaries on top of that stream.
Two classic approaches:

| Technique | How |
| --- | --- |
| Delimiter | Read until a sentinel, e.g. `\r\n` |
| Length prefix | Read a count, then exactly that many bytes |
| Self-describing | Combined — a length-prefixed format *is* delimiter framing with an explicit count |

> [!important] Why this matters
> A server that assumes "one `recv()` = one request" is correct on loopback and
> wrong on a real network. This was the single most serious bug class in the
> original version of this project.

---

## In this project

### Before: the 1024-byte bug

```cpp
char buffer[1024];
while (true) {
    memset(buffer, 0, sizeof(buffer));
    int bytes = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
    if (bytes <= 0) break;
    std::string request(buffer, bytes);
    std::string response = cmdHandler.processCommand(request);
    send(client_socket, response.c_str(), response.size(), 0);
}
```

Three separate failures:
1. **Truncation** — anything over ~1000 bytes was silently cut.
2. **Split commands** — a command arriving in two segments was mangled.
3. **No pipelining** — a second command already sitting in the buffer was
   discarded on the next `memset`.

### After: accumulate, then drain

`src/RedisServer.cpp:52`–`src/RedisServer.cpp:96`
```cpp
pending.append(chunk.data(), static_cast<std::size_t>(bytes));

if (pending.size() > kMaxPendingBytes) {
    sendAll(clientSocket,
            "-ERR protocol error: request exceeded the maximum buffered size\r\n");
    break;
}

std::size_t offset = 0;
bool closeConnection = false;

while (true) {
    std::size_t consumed = 0;
    bool closeAfter = false;
    const std::string reply =
        handler.processCommand(pending.substr(offset), consumed, closeAfter);
    offset += consumed;

    if (!reply.empty() && !sendAll(clientSocket, reply)) { closeConnection = true; break; }
    if (closeAfter) { closeConnection = true; break; }
    if (consumed == 0) break;   // incomplete command, wait for more bytes
}

pending.erase(0, offset);
```

Three ideas:
- **`pending` is unbounded and grows** to hold a partial command, so a split
  command is simply re-parsed once the rest arrives.
- **`consumed` is reported by the parser**, so the loop keeps the remainder and
  handles pipelined commands in one pass.
- **`consumed == 0` means "need more bytes"** — the only exit from the inner
  loop for an incomplete command.

### Bounding the buffer

`kMaxPendingBytes` (16 MB) stops a malicious or broken client that never sends
a newline from exhausting server memory. Unbounded accumulation is a
denial-of-service vector; this is the mitigation.

### Wire-format bounds

The parser also caps what it will believe from the length prefix, so a hostile
`$999999999999` cannot trigger a huge allocation.

`src/RedisCommandHandler.cpp:32`–`src/RedisCommandHandler.cpp:33`
```cpp
constexpr long long kMaxMultibulkElements = 1024 * 1024;
constexpr long long kMaxBulkLength = 512LL * 1024 * 1024;
```

### Recovering from bad framing

`src/RedisCommandHandler.cpp:69`–`src/RedisCommandHandler.cpp:72`
```cpp
auto reject = [&]() {
    consumed = buffer.size();
    return ParseResult::Invalid;
};
```

> [!warning] The infinite-loop trap
> On invalid framing the parser must still report **non-zero** `consumed`. If it
> returned 0, the server loop would keep re-parsing the same bytes forever —
> a hang, not an error. Discarding the buffer is the only resynchronisation
> possible, since a corrupted length prefix leaves no safe restart point.

---

## Related

- [[OS/POSIX Sockets]] — where the stream comes from
- [[Project/RESP Protocol]] — the concrete length-prefixed format
- [[OS/System Calls]] — `recv` cost motivates reading in large chunks