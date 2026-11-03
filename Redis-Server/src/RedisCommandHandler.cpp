#include "../include/RedisCommandHandler.h"
#include "../include/RedisDatabase.h"

#include <vector>
#include <sstream>
#include <algorithm>
#include <exception>
#include <iostream>

bool RedisCommandHandler::extractNextCommand(std::string& buffer, std::vector<std::string>& tokens) {
    tokens.clear();
    if (buffer.empty()) return false;

    // RESP Array format starts with '*'
    if (buffer[0] == '*') {
        size_t pos = 0;
        size_t crlf = buffer.find("\r\n", pos);
        if (crlf == std::string::npos) return false;

        int numElements = 0;
        try {
            numElements = std::stoi(buffer.substr(pos + 1, crlf - (pos + 1)));
        } catch (...) {
            // Malformed, drop leading byte
            buffer.erase(0, 1);
            return false;
        }

        if (numElements <= 0) {
            buffer.erase(0, crlf + 2);
            return true;
        }

        pos = crlf + 2;
        std::vector<std::string> tempTokens;
        tempTokens.reserve(numElements);

        for (int i = 0; i < numElements; ++i) {
            if (pos >= buffer.size() || buffer[pos] != '$') {
                return false; // Not enough data yet or malformed
            }
            size_t nextCrlf = buffer.find("\r\n", pos);
            if (nextCrlf == std::string::npos) return false;

            int len = 0;
            try {
                len = std::stoi(buffer.substr(pos + 1, nextCrlf - (pos + 1)));
            } catch (...) {
                return false;
            }

            size_t dataStart = nextCrlf + 2;
            if (buffer.size() < dataStart + len + 2) {
                return false; // Incomplete data bytes in buffer
            }

            if (buffer.substr(dataStart + len, 2) != "\r\n") {
                // Framing error, drop
                buffer.erase(0, 1);
                return false;
            }

            tempTokens.push_back(buffer.substr(dataStart, len));
            pos = dataStart + len + 2;
        }

        // Successfully extracted all tokens
        buffer.erase(0, pos);
        tokens = std::move(tempTokens);
        return true;
    }

    // Inline command format (plain text e.g. "PING\r\n")
    size_t lineEnd = buffer.find('\n');
    if (lineEnd == std::string::npos) return false;

    std::string line = buffer.substr(0, lineEnd);
    buffer.erase(0, lineEnd + 1);

    if (!line.empty() && line.back() == '\r') {
        line.pop_back();
    }

    std::istringstream iss(line);
    std::string word;
    while (iss >> word) {
        tokens.push_back(word);
    }
    return !tokens.empty();
}

std::string RedisCommandHandler::dispatchCommand(const std::vector<std::string>& tokens) {
    if (tokens.empty()) return "-ERR empty command\r\n";

    std::string cmd = tokens[0];
    std::transform(cmd.begin(), cmd.end(), cmd.begin(), ::toupper);
    RedisDatabase& db = RedisDatabase::getInstance();

    //---------------------------------------------------------
    // Core Server Commands
    //---------------------------------------------------------
    if (cmd == "PING") {
        if (tokens.size() > 1) {
            return "$" + std::to_string(tokens[1].size()) + "\r\n" + tokens[1] + "\r\n";
        }
        return "+PONG\r\n";
    }
    if (cmd == "ECHO") {
        if (tokens.size() < 2) return "-ERR wrong number of arguments for 'echo' command\r\n";
        return "$" + std::to_string(tokens[1].size()) + "\r\n" + tokens[1] + "\r\n";
    }
    if (cmd == "FLUSHALL" || cmd == "FLUSHDB") {
        db.flushAll();
        return "+OK\r\n";
    }
    if (cmd == "DBSIZE") {
        return ":" + std::to_string(db.dbsize()) + "\r\n";
    }

    //---------------------------------------------------------
    // Key/Value Operations
    //---------------------------------------------------------
    if (cmd == "SET") {
        if (tokens.size() < 3) return "-ERR wrong number of arguments for 'set' command\r\n";
        db.set(tokens[1], tokens[2]);
        // Handle optional EX/PX flags
        if (tokens.size() >= 5) {
            std::string opt = tokens[3];
            std::transform(opt.begin(), opt.end(), opt.begin(), ::toupper);
            try {
                if (opt == "EX") {
                    db.expire(tokens[1], std::stoi(tokens[4]));
                } else if (opt == "PX") {
                    db.pexpire(tokens[1], std::stoll(tokens[4]));
                }
            } catch (...) {}
        }
        return "+OK\r\n";
    }
    if (cmd == "GET") {
        if (tokens.size() < 2) return "-ERR wrong number of arguments for 'get' command\r\n";
        std::string val;
        if (db.get(tokens[1], val)) {
            return "$" + std::to_string(val.size()) + "\r\n" + val + "\r\n";
        }
        return "$-1\r\n";
    }
    if (cmd == "DEL" || cmd == "UNLINK") {
        if (tokens.size() < 2) return "-ERR wrong number of arguments for 'del' command\r\n";
        int deleted = 0;
        for (size_t i = 1; i < tokens.size(); ++i) {
            if (db.del(tokens[i])) deleted++;
        }
        return ":" + std::to_string(deleted) + "\r\n";
    }
    if (cmd == "EXISTS") {
        if (tokens.size() < 2) return "-ERR wrong number of arguments for 'exists' command\r\n";
        int count = 0;
        for (size_t i = 1; i < tokens.size(); ++i) {
            if (db.exists(tokens[i])) count++;
        }
        return ":" + std::to_string(count) + "\r\n";
    }
    if (cmd == "KEYS") {
        auto allKeys = db.keys();
        std::ostringstream oss;
        oss << "*" << allKeys.size() << "\r\n";
        for (const auto& k : allKeys) {
            oss << "$" << k.size() << "\r\n" << k << "\r\n";
        }
        return oss.str();
    }
    if (cmd == "TYPE") {
        if (tokens.size() < 2) return "-ERR wrong number of arguments for 'type' command\r\n";
        return "+" + db.type(tokens[1]) + "\r\n";
    }
    if (cmd == "RENAME") {
        if (tokens.size() < 3) return "-ERR wrong number of arguments for 'rename' command\r\n";
        if (db.rename(tokens[1], tokens[2])) return "+OK\r\n";
        return "-ERR no such key\r\n";
    }

    //---------------------------------------------------------
    // Expiration Operations
    //---------------------------------------------------------
    if (cmd == "EXPIRE") {
        if (tokens.size() < 3) return "-ERR wrong number of arguments for 'expire' command\r\n";
        try {
            int sec = std::stoi(tokens[2]);
            return db.expire(tokens[1], sec) ? ":1\r\n" : ":0\r\n";
        } catch (...) {
            return "-ERR value is not an integer or out of range\r\n";
        }
    }
    if (cmd == "PEXPIRE") {
        if (tokens.size() < 3) return "-ERR wrong number of arguments for 'pexpire' command\r\n";
        try {
            int64_t ms = std::stoll(tokens[2]);
            return db.pexpire(tokens[1], ms) ? ":1\r\n" : ":0\r\n";
        } catch (...) {
            return "-ERR value is not an integer or out of range\r\n";
        }
    }
    if (cmd == "TTL") {
        if (tokens.size() < 2) return "-ERR wrong number of arguments for 'ttl' command\r\n";
        return ":" + std::to_string(db.ttl(tokens[1])) + "\r\n";
    }
    if (cmd == "PTTL") {
        if (tokens.size() < 2) return "-ERR wrong number of arguments for 'pttl' command\r\n";
        return ":" + std::to_string(db.pttl(tokens[1])) + "\r\n";
    }
    if (cmd == "PERSIST") {
        if (tokens.size() < 2) return "-ERR wrong number of arguments for 'persist' command\r\n";
        return db.persist(tokens[1]) ? ":1\r\n" : ":0\r\n";
    }

    //---------------------------------------------------------
    // List Operations
    //---------------------------------------------------------
    if (cmd == "LPUSH") {
        if (tokens.size() < 3) return "-ERR wrong number of arguments for 'lpush' command\r\n";
        for (size_t i = 2; i < tokens.size(); ++i) db.lpush(tokens[1], tokens[i]);
        return ":" + std::to_string(db.llen(tokens[1])) + "\r\n";
    }
    if (cmd == "RPUSH") {
        if (tokens.size() < 3) return "-ERR wrong number of arguments for 'rpush' command\r\n";
        for (size_t i = 2; i < tokens.size(); ++i) db.rpush(tokens[1], tokens[i]);
        return ":" + std::to_string(db.llen(tokens[1])) + "\r\n";
    }
    if (cmd == "LPOP") {
        if (tokens.size() < 2) return "-ERR wrong number of arguments for 'lpop' command\r\n";
        std::string val;
        if (db.lpop(tokens[1], val)) {
            return "$" + std::to_string(val.size()) + "\r\n" + val + "\r\n";
        }
        return "$-1\r\n";
    }
    if (cmd == "RPOP") {
        if (tokens.size() < 2) return "-ERR wrong number of arguments for 'rpop' command\r\n";
        std::string val;
        if (db.rpop(tokens[1], val)) {
            return "$" + std::to_string(val.size()) + "\r\n" + val + "\r\n";
        }
        return "$-1\r\n";
    }
    if (cmd == "LLEN") {
        if (tokens.size() < 2) return "-ERR wrong number of arguments for 'llen' command\r\n";
        return ":" + std::to_string(db.llen(tokens[1])) + "\r\n";
    }
    if (cmd == "LRANGE") {
        if (tokens.size() < 4) return "-ERR wrong number of arguments for 'lrange' command\r\n";
        try {
            int start = std::stoi(tokens[2]);
            int stop = std::stoi(tokens[3]);
            auto elems = db.lrange(tokens[1], start, stop);
            std::ostringstream oss;
            oss << "*" << elems.size() << "\r\n";
            for (const auto& e : elems) {
                oss << "$" << e.size() << "\r\n" << e << "\r\n";
            }
            return oss.str();
        } catch (...) {
            return "-ERR value is not an integer or out of range\r\n";
        }
    }
    if (cmd == "LGET") { // Custom backward-compatibility command
        if (tokens.size() < 2) return "-ERR wrong number of arguments for 'lget' command\r\n";
        auto elems = db.lget(tokens[1]);
        std::ostringstream oss;
        oss << "*" << elems.size() << "\r\n";
        for (const auto& e : elems) oss << "$" << e.size() << "\r\n" << e << "\r\n";
        return oss.str();
    }
    if (cmd == "LINDEX") {
        if (tokens.size() < 3) return "-ERR wrong number of arguments for 'lindex' command\r\n";
        try {
            int idx = std::stoi(tokens[2]);
            std::string val;
            if (db.lindex(tokens[1], idx, val)) {
                return "$" + std::to_string(val.size()) + "\r\n" + val + "\r\n";
            }
            return "$-1\r\n";
        } catch (...) {
            return "-ERR value is not an integer or out of range\r\n";
        }
    }
    if (cmd == "LSET") {
        if (tokens.size() < 4) return "-ERR wrong number of arguments for 'lset' command\r\n";
        try {
            int idx = std::stoi(tokens[2]);
            if (db.lset(tokens[1], idx, tokens[3])) return "+OK\r\n";
            return "-ERR index out of range\r\n";
        } catch (...) {
            return "-ERR value is not an integer or out of range\r\n";
        }
    }
    if (cmd == "LREM") {
        if (tokens.size() < 4) return "-ERR wrong number of arguments for 'lrem' command\r\n";
        try {
            int count = std::stoi(tokens[2]);
            int removed = db.lrem(tokens[1], count, tokens[3]);
            return ":" + std::to_string(removed) + "\r\n";
        } catch (...) {
            return "-ERR value is not an integer or out of range\r\n";
        }
    }

    //---------------------------------------------------------
    // Hash Operations
    //---------------------------------------------------------
    if (cmd == "HSET") {
        if (tokens.size() < 4) return "-ERR wrong number of arguments for 'hset' command\r\n";
        int created = 0;
        for (size_t i = 2; i + 1 < tokens.size(); i += 2) {
            db.hset(tokens[1], tokens[i], tokens[i + 1]);
            created++;
        }
        return ":" + std::to_string(created) + "\r\n";
    }
    if (cmd == "HGET") {
        if (tokens.size() < 3) return "-ERR wrong number of arguments for 'hget' command\r\n";
        std::string val;
        if (db.hget(tokens[1], tokens[2], val)) {
            return "$" + std::to_string(val.size()) + "\r\n" + val + "\r\n";
        }
        return "$-1\r\n";
    }
    if (cmd == "HEXISTS") {
        if (tokens.size() < 3) return "-ERR wrong number of arguments for 'hexists' command\r\n";
        return db.hexists(tokens[1], tokens[2]) ? ":1\r\n" : ":0\r\n";
    }
    if (cmd == "HDEL") {
        if (tokens.size() < 3) return "-ERR wrong number of arguments for 'hdel' command\r\n";
        int deleted = 0;
        for (size_t i = 2; i < tokens.size(); ++i) {
            if (db.hdel(tokens[1], tokens[i])) deleted++;
        }
        return ":" + std::to_string(deleted) + "\r\n";
    }
    if (cmd == "HLEN") {
        if (tokens.size() < 2) return "-ERR wrong number of arguments for 'hlen' command\r\n";
        return ":" + std::to_string(db.hlen(tokens[1])) + "\r\n";
    }
    if (cmd == "HKEYS") {
        if (tokens.size() < 2) return "-ERR wrong number of arguments for 'hkeys' command\r\n";
        auto keys = db.hkeys(tokens[1]);
        std::ostringstream oss;
        oss << "*" << keys.size() << "\r\n";
        for (const auto& k : keys) oss << "$" << k.size() << "\r\n" << k << "\r\n";
        return oss.str();
    }
    if (cmd == "HVALS") {
        if (tokens.size() < 2) return "-ERR wrong number of arguments for 'hvals' command\r\n";
        auto vals = db.hvals(tokens[1]);
        std::ostringstream oss;
        oss << "*" << vals.size() << "\r\n";
        for (const auto& v : vals) oss << "$" << v.size() << "\r\n" << v << "\r\n";
        return oss.str();
    }
    if (cmd == "HGETALL") {
        if (tokens.size() < 2) return "-ERR wrong number of arguments for 'hgetall' command\r\n";
        auto hash = db.hgetall(tokens[1]);
        std::ostringstream oss;
        oss << "*" << hash.size() * 2 << "\r\n";
        for (const auto& kv : hash) {
            oss << "$" << kv.first.size() << "\r\n" << kv.first << "\r\n";
            oss << "$" << kv.second.size() << "\r\n" << kv.second << "\r\n";
        }
        return oss.str();
    }
    if (cmd == "HMSET") {
        if (tokens.size() < 4 || (tokens.size() % 2) != 0) {
            return "-ERR wrong number of arguments for 'hmset' command\r\n";
        }
        std::vector<std::pair<std::string, std::string>> fieldValues;
        for (size_t i = 2; i < tokens.size(); i += 2) {
            fieldValues.emplace_back(tokens[i], tokens[i + 1]);
        }
        db.hmset(tokens[1], fieldValues);
        return "+OK\r\n";
    }

    //---------------------------------------------------------
    // INFO & Diagnostics
    //---------------------------------------------------------
    if (cmd == "INFO") {
        std::string section = (tokens.size() > 1) ? tokens[1] : "default";
        std::transform(section.begin(), section.end(), section.begin(), ::tolower);
        std::string infoStr = db.getInfo(section);
        return "$" + std::to_string(infoStr.size()) + "\r\n" + infoStr + "\r\n";
    }

    //---------------------------------------------------------
    // CONFIG GET / CONFIG SET
    //---------------------------------------------------------
    if (cmd == "CONFIG") {
        if (tokens.size() < 2) return "-ERR wrong number of arguments for 'config' command\r\n";
        std::string sub = tokens[1];
        std::transform(sub.begin(), sub.end(), sub.begin(), ::toupper);

        if (sub == "GET") {
            if (tokens.size() < 3) return "-ERR wrong number of arguments for 'config get' command\r\n";
            std::string param = tokens[2];
            std::transform(param.begin(), param.end(), param.begin(), ::tolower);

            if (param == "maxmemory" || param == "capacity") {
                std::string val = std::to_string(db.getCapacity());
                return "*2\r\n$" + std::to_string(param.size()) + "\r\n" + param + "\r\n$" +
                       std::to_string(val.size()) + "\r\n" + val + "\r\n";
            } else if (param == "maxmemory-policy" || param == "eviction_policy" || param == "policy") {
                std::string val = db.getPolicyName();
                return "*2\r\n$" + std::to_string(param.size()) + "\r\n" + param + "\r\n$" +
                       std::to_string(val.size()) + "\r\n" + val + "\r\n";
            }
            return "*0\r\n";
        } else if (sub == "SET") {
            if (tokens.size() < 4) return "-ERR wrong number of arguments for 'config set' command\r\n";
            std::string param = tokens[2];
            std::transform(param.begin(), param.end(), param.begin(), ::tolower);
            std::string val = tokens[3];

            if (param == "maxmemory" || param == "capacity") {
                try {
                    size_t cap = std::stoul(val);
                    db.setCapacity(cap);
                    return "+OK\r\n";
                } catch (...) {
                    return "-ERR value is not an integer or out of range\r\n";
                }
            } else if (param == "maxmemory-policy" || param == "eviction_policy" || param == "policy") {
                PolicyType pt = stringToPolicyType(val);
                db.setPolicy(pt);
                return "+OK\r\n";
            }
            return "-ERR unsupported config parameter\r\n";
        }
        return "-ERR unknown config subcommand\r\n";
    }

    // Custom Extension Command: EVICTION.STATS
    if (cmd == "EVICTION.STATS") {
        auto stats = db.getEvictionStats();
        std::ostringstream oss;
        oss << "policy:" << stats.policyName << "\n"
            << "capacity:" << stats.capacity << "\n"
            << "current_size:" << stats.currentSize << "\n"
            << "hits:" << stats.totalHits << "\n"
            << "misses:" << stats.totalMisses << "\n"
            << "evictions:" << stats.totalEvictions << "\n"
            << "expert1_weight:" << stats.expert1Weight << "\n"
            << "expert2_weight:" << stats.expert2Weight << "\n"
            << "cumulative_regret1:" << stats.cumulativeRegret1 << "\n"
            << "cumulative_regret2:" << stats.cumulativeRegret2 << "\n"
            << "active_expert:" << stats.currentActiveExpert << "\n";
        std::string s = oss.str();
        return "$" + std::to_string(s.size()) + "\r\n" + s + "\r\n";
    }

    return "-ERR unknown command '" + tokens[0] + "'\r\n";
}

std::string RedisCommandHandler::processCommand(const std::string& commandLine) {
    std::string buf = commandLine;
    std::vector<std::string> tokens;
    if (extractNextCommand(buf, tokens)) {
        return dispatchCommand(tokens);
    }
    return "-ERR invalid or incomplete command\r\n";
}

RedisCommandHandler::RedisCommandHandler() {}
