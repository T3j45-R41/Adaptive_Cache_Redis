---
tags: [project/adaptive-cache-redis, protocol, resp, parsing, project/adaptive-cache-redis]
status: implemented
---

# RESP Protocol

## Definition

**RESP** (REdis Serialization Protocol) is Redis's wire format. Five reply
types, each introduced by a one-byte type marker:

| Marker | Type | Payload |
| --- | --- |
| `+` | Simple string | `+OK\r\n` |
| `-` | Error | `-ERR unknown command\r\n` |
| `:` | Integer | `:42\r\n` |
| `$` | Bulk string | `$5\r\nhello\r\n`, or `$-1\r\n` for nil |
| `*` | Array | `*2\r\n$3\r\nfoo\r\n$3\r\nbar\r\n` |

Clients may also send commands in **inline** form (`SET foo bar\r\n`) or as an
array of bulk strings (multibulk).

---

## In this project

Both inbound forms are accepted, because the project's own clients need both:
`redis-cli` and the Streamlit panel speak multibulk, while the benchmark client
and shell scripts speak inline.

### Parse result

`include/RedisCommandHandler.h`
```cpp
enum class ParseResult {
    NeedMore,  // buffer holds a partial command; read more before retrying
    Complete,  // one or more tokens decoded
    Invalid    // framing is unrecoverable; the caller should drop the buffer
};

ParseResult parseRespCommand(const std::string& buffer, std::size_t& consumed,
                             std::vector<std::string>& tokens);
```

> [!important] `consumed` is the whole design
> The previous signature returned only tokens, so the caller could not know how
> many bytes the command occupied. That is exactly why pipelining was impossible
> and partial commands had to be discarded.
>
> Returning bytes-consumed turns the parser into a framing layer that can sit
> underneath a stream.

### Inline framing

`src/RedisCommandHandler.cpp:74`–`src/RedisCommandHandler.cpp:89`
```cpp
const std::size_t eol = buffer.find('\n');
if (eol == std::string::npos) return ParseResult::NeedMore;

std::string line = buffer.substr(0, eol);
consumed = eol + 1;
if (!line.empty() && line.back() == '\r') line.pop_back();
```

A blank line consumes its newline and returns `NeedMore`, so the caller makes
progress instead of spinning.

### Multibulk framing

`src/RedisCommandHandler.cpp:92`–`src/RedisCommandHandler.cpp:126`
```cpp
// Multibulk. Nothing is consumed until the whole array has arrived, so a
// partial command is retried against the next read.
...
for (long long i = 0; i < count; ++i) {
    if (pos >= buffer.size() || buffer[pos] != '$') return reject();
    ++pos;
    if (!readLine(buffer, pos, header)) return ParseResult::NeedMore;
    long long length = 0;
    if (!parseLong(header, length) || length < 0) return reject();
    if (length > kMaxBulkLength) return reject();

    // +2 for the trailing CRLF; if that is not here yet, wait for more.
    if (pos + static_cast<std::size_t>(length) + 2 > buffer.size())
        return ParseResult::NeedMore;

    decoded.emplace_back(buffer, pos, static_cast<std::size_t>(length));
    pos += static_cast<std::size_t>(length) + 2;
}
```

> [!warning] A bug worth explaining in a viva
> `readLine` returns the line **without** its leading `*` or `$`, because those
> bytes were already consumed before the call. An early version applied
> `header.substr(1)` to strip a prefix that was no longer there, dropping the
> first digit of every count and every length. The effect was that *every*
> multibulk command — including every `redis-cli` command — failed with
> `-ERR protocol error`.
>
> The comment at `src/RedisCommandHandler.cpp:98` exists to stop it recurring.

### Rejecting bad framing

`src/RedisCommandHandler.cpp:69`–`src/RedisCommandHandler.cpp:72`
```cpp
auto reject = [&]() {
    consumed = buffer.size();
    return ParseResult::Invalid;
};
```

Discarding the buffer is the only resynchronisation possible: once a length
prefix is wrong, there is no safe point to restart from.

---

## Reply encoding

| Command | Reply |
| --- | --- |
| `PING` | `+PONG` |
| `SET` | `+OK`, or `$-1` when an `NX`/`XX` condition fails |
| `GET` (hit) | `$<len>\r\n<value>\r\n` |
| `GET` (miss) | `$-1` |
| `GET` (wrong type) | `-ERR wrongtype ...` |
| `DEL` | `:<count>` |
| `KEYS` | `*<n>` then *n* bulk strings |
| `HGETALL` | flat array of `2n` bulk strings |
| `INFO` | one bulk string of `key:value\r\n` lines |
| `MAXKEYS` (get) | `:<n>` — an integer reply, so clients get a number |
| `MEMORY USAGE <missing>` | `$-1` |

---

## Supported commands

**Common** — `PING` `ECHO` `FLUSHALL` `QUIT`
**Strings** — `SET` (with `EX`/`PX`/`NX`/`XX`) `GET` `KEYS` `TYPE` `DEL`/`UNLINK` `EXISTS` `EXPIRE` `TTL` `PERSIST` `RENAME`
**Lists** — `LGET` `LLEN` `LPUSH` `RPUSH` `LPOP` `RPOP` `LREM` `LINDEX` `LSET`
**Hashes** — `HSET` `HGET` `HEXISTS` `HDEL` `HLEN` `HKEYS` `HVALS` `HGETALL` `HMSET`
**Admin** — `INFO` `CONFIG GET|SET|RESETSTAT` `DBSIZE` `MEMORY USAGE` `EVICTION` `MAXKEYS`

> [!info] Why the last group exists
> `Benchmark/benchmark_client.py` and `ui_app.py` were already issuing
> `CONFIG SET POLICY`, `CONFIG SET MAXKEYS`, `CONFIG RESETSTAT`, `INFO`,
> `EVICTION` and `MAXKEYS`. The server implemented none of them, so both tools
> ran but reported nothing. The commands were added to satisfy existing
> clients, not to invent an interface.

---

## Not supported

> [!warning] Absent — and why off-the-shelf GUIs cannot connect
> `HELLO`, `AUTH`, `SELECT`, `CLIENT`, `COMMAND`, `CONFIG GET *`, `CLUSTER`,
> `LATENCY`, `MODULE`, `SCAN`, `MULTI`/`EXEC`, pub/sub, scripting.
>
> Redis desktop tools open a session with `HELLO` to negotiate RESP3, so they
> fail on connect. See [[Project/Gaps and Future Work]].

---

## Related

- [[OS/Stream Framing]] — the general problem this solves
- [[OS/POSIX Sockets]] — the transport underneath
- [[Project/Architecture]] — where dispatch happens
- [[Project/Concept-to-Code Map]]