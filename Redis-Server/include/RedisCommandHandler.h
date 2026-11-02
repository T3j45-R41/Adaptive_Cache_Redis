#ifndef REDIS_COMMAND_HANDLER_H
#define REDIS_COMMAND_HANDLER_H

#include <string>
#include <vector>

class RedisCommandHandler {
public:
    RedisCommandHandler();

    // Process a single command tokens vector and return a RESP-formatted response
    std::string dispatchCommand(const std::vector<std::string>& tokens);

    // Legacy method for single-string commands
    std::string processCommand(const std::string& commandLine);

    // Incremental streaming parser: extracts the next complete command from buffer.
    // If complete, removes consumed bytes from buffer, populates tokens, and returns true.
    // If incomplete, leaves buffer unchanged and returns false.
    static bool extractNextCommand(std::string& buffer, std::vector<std::string>& tokens);
};

#endif // REDIS_COMMAND_HANDLER_H
