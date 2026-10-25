#ifndef REDIS_COMMAND_HANDLER_H
#define REDIS_COMMAND_HANDLER_H

#include <cstddef>
#include <string>
#include <vector>

// Outcome of trying to pull one command out of a receive buffer.
enum class ParseResult {
    NeedMore,  // buffer holds a partial command; read more before retrying
    Complete,  // one or more tokens decoded
    Invalid    // framing is unrecoverable; the caller should drop the buffer
};

// Parses one command out of `buffer` and reports how many bytes it settled,
// so a caller can keep the remainder for the next command instead of the
// previous single-shot design, which truncated anything over 1024 bytes and
// corrupted commands split across TCP segments.
ParseResult parseRespCommand(const std::string& buffer, std::size_t& consumed,
                             std::vector<std::string>& tokens);

class RedisCommandHandler {
public:
    RedisCommandHandler();

    // Processes one command from a connection buffer. `consumed` receives the
    // number of bytes the command occupied, and `closeConnection` is set when
    // the client asked to disconnect (QUIT).
    std::string processCommand(const std::string& buffer, std::size_t& consumed,
                               bool& closeConnection);

    // Convenience form for callers holding exactly one command at a time.
    std::string processCommand(const std::string& buffer);
};

#endif