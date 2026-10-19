#include "../include/RedisCommandHandler.h"
#include "../include/RedisDatabase.h"
#include "../include/eviction/eviction_engine.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <exception>
#include <sstream>
#include <unordered_map>
#include <utility>
#include <vector>

// ---------------------------------------------------------------------------
// RESP parsing
// ---------------------------------------------------------------------------
//
// Inbound commands may arrive in two shapes:
//
//   * multibulk:  *3\r\n$3\r\nSET\r\n$1\r\na\r\n$1\r\nb\r\n
//   * inline:    SET a b\r\n
//
// Both are supported because the benchmark client and the test scripts speak
// inline while redis-cli and the Streamlit UI speak multibulk. The parser
// reports how many bytes it consumed so a caller can process several pipelined
// commands from one read, or keep a partial command until the rest arrives.

namespace {

// Bounds on untrusted wire input, so a malformed or hostile length prefix
// cannot make the server attempt a huge allocation.
constexpr long long kMaxMultibulkElements = 1024 * 1024;
constexpr long long kMaxBulkLength = 512LL * 1024 * 1024;

// Reads one CRLF-terminated header line starting at pos. Returns false when
// the line is not yet complete.
bool readLine(const std::string& in, std::size_t& pos, std::string& line) {
    const std::size_t crlf = in.find("\r\n", pos);
    if (crlf == std::string::npos) return false;
    line = in.substr(pos, crlf - pos);
    pos = crlf + 2;
    return true;
}

bool parseLong(const std::string& text, long long& out) {
    if (text.empty()) return false;
    try {
        std::size_t used = 0;
        const long long value = std::stoll(text, &used);
        if (used != text.size()) return false;
        out = value;
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

} // namespace

ParseResult parseRespCommand(const std::string& buffer, std::size_t& consumed,
                             std::vector<std::string>& tokens) {
    consumed = 0;
    tokens.clear();
    if (buffer.empty()) return ParseResult::NeedMore;

    // Framing that cannot be recovered from: consume the whole buffer so the
    // caller's read loop makes progress instead of retrying the same bytes
    // forever.
    auto reject = [&]() {
        consumed = buffer.size();
        return ParseResult::Invalid;
    };

    if (buffer[0] != '*') {
        // Inline command: terminated by a line feed.
        const std::size_t eol = buffer.find('\n');
        if (eol == std::string::npos) return ParseResult::NeedMore;

        std::string line = buffer.substr(0, eol);
        consumed = eol + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();

        std::istringstream iss(line);
        std::string token;
        while (iss >> token) tokens.push_back(token);

        // A blank line carries no command but is safely consumed.
        if (tokens.empty()) return ParseResult::NeedMore;
        return ParseResult::Complete;
    }

    // Multibulk. Nothing is consumed until the whole array has arrived, so a
    // partial command is retried against the next read.
    std::size_t pos = 1;
    std::string header;
    if (!readLine(buffer, pos, header)) return ParseResult::NeedMore;

    // readLine already returns the line without its leading '*', so the count
    // is the header itself.
    long long count = 0;
    if (!parseLong(header, count) || count < 0) return reject();
    if (count > kMaxMultibulkElements) return reject();

    std::vector<std::string> decoded;
    decoded.reserve(static_cast<std::size_t>(count));
    for (long long i = 0; i < count; ++i) {
        if (pos >= buffer.size() || buffer[pos] != '$') return reject();
        ++pos;

        if (!readLine(buffer, pos, header)) return ParseResult::NeedMore;
        long long length = 0;
        // The '$' was consumed above, so the header is the length itself.
        if (!parseLong(header, length) || length < 0) return reject();
        if (length > kMaxBulkLength) return reject();

        // +2 for the trailing CRLF; if that is not here yet, wait for more.
        if (pos + static_cast<std::size_t>(length) + 2 > buffer.size())
            return ParseResult::NeedMore;

        decoded.emplace_back(buffer, pos, static_cast<std::size_t>(length));
        pos += static_cast<std::size_t>(length) + 2;
    }

    tokens = std::move(decoded);
    consumed = pos;
    return ParseResult::Complete;
}

// ---------------------------------------------------------------------------
// Reply helpers
// ---------------------------------------------------------------------------

namespace {

std::string simpleReply(const std::string& text) { return "+" + text + "\r\n"; }
std::string errorReply(const std::string& text) { return "-ERR " + text + "\r\n"; }
std::string nilReply() { return "$-1\r\n"; }
std::string integerReply(std::int64_t value) { return ":" + std::to_string(value) + "\r\n"; }

std::string bulkReply(const std::string& value) {
    return "$" + std::to_string(value.size()) + "\r\n" + value + "\r\n";
}

std::string statusReply(OpStatus status) {
    if (status == OpStatus::Ok) return "";
    return errorReply(statusError(status));
}

std::string bulkArray(const std::vector<std::string>& items) {
    std::ostringstream oss;
    oss << "*" << items.size() << "\r\n";
    for (const auto& item : items) oss << "$" << item.size() << "\r\n" << item << "\r\n";
    return oss.str();
}

std::string upper(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return value;
}

} // namespace

// ---------------------------------------------------------------------------
// Common commands
// ---------------------------------------------------------------------------

static std::string handlePing(const std::vector<std::string>&, RedisDatabase&) {
    return simpleReply("PONG");
}

static std::string handleEcho(const std::vector<std::string>& tokens, RedisDatabase&) {
    if (tokens.size() < 2) return errorReply("ECHO requires a message");
    return simpleReply(tokens[1]);
}

static std::string handleFlushAll(const std::vector<std::string>&, RedisDatabase& db) {
    db.flushAll();
    return simpleReply("OK");
}

// ---------------------------------------------------------------------------
// Key/value commands
// ---------------------------------------------------------------------------

static std::string handleSet(const std::vector<std::string>& tokens, RedisDatabase& db) {
    if (tokens.size() < 3) return errorReply("SET requires key and value");

    std::int64_t expireSeconds = 0;
    bool hasExpiry = false;
    bool requireExists = false;
    bool requireMissing = false;

    for (std::size_t i = 3; i < tokens.size(); ++i) {
        const std::string option = upper(tokens[i]);
        if (option == "EX" || option == "PX") {
            if (i + 1 >= tokens.size()) return errorReply("syntax error");
            long long amount = 0;
            if (!parseLong(tokens[i + 1], amount) || amount <= 0)
                return errorReply("expire time must be a positive integer");
            // PX is milliseconds; the TTL map stores seconds, so round up
            // rather than silently turning a sub-second TTL into "expired".
            expireSeconds = (option == "EX") ? amount : (amount + 999) / 1000;
            hasExpiry = true;
            ++i;
        } else if (option == "NX") {
            requireMissing = true;
        } else if (option == "XX") {
            requireExists = true;
        } else {
            return errorReply("syntax error");
        }
    }

    const std::string& key = tokens[1];
    if (requireMissing && db.exists(key)) return nilReply();
    if (requireExists && !db.exists(key)) return nilReply();

    db.set(key, tokens[2]);
    if (hasExpiry) db.expire(key, expireSeconds);
    return simpleReply("OK");
}

static std::string handleGet(const std::vector<std::string>& tokens, RedisDatabase& db) {
    if (tokens.size() < 2) return errorReply("GET requires key");
    std::string value;
    const OpStatus status = db.get(tokens[1], value);
    if (status == OpStatus::NotFound) return nilReply();
    if (status == OpStatus::WrongType) return statusReply(status);
    return bulkReply(value);
}

static std::string handleKeys(const std::vector<std::string>& tokens, RedisDatabase& db) {
    if (tokens.size() < 2) return errorReply("KEYS requires a pattern");
    return bulkArray(db.keys(tokens[1]));
}

static std::string handleType(const std::vector<std::string>& tokens, RedisDatabase& db) {
    if (tokens.size() < 2) return errorReply("TYPE requires key");
    return simpleReply(db.type(tokens[1]));
}

static std::string handleDel(const std::vector<std::string>& tokens, RedisDatabase& db) {
    if (tokens.size() < 2) return errorReply("DEL requires key");
    std::int64_t removed = 0;
    for (std::size_t i = 1; i < tokens.size(); ++i) {
        if (db.del(tokens[i])) ++removed;
    }
    return integerReply(removed);
}

static std::string handleExists(const std::vector<std::string>& tokens, RedisDatabase& db) {
    if (tokens.size() < 2) return errorReply("EXISTS requires key");
    std::int64_t count = 0;
    for (std::size_t i = 1; i < tokens.size(); ++i) {
        if (db.exists(tokens[i])) ++count;
    }
    return integerReply(count);
}

static std::string handleExpire(const std::vector<std::string>& tokens, RedisDatabase& db) {
    if (tokens.size() < 3) return errorReply("EXPIRE requires key and seconds");
    long long seconds = 0;
    if (!parseLong(tokens[2], seconds)) return errorReply("invalid expire time");
    return integerReply(db.expire(tokens[1], seconds) ? 1 : 0);
}

static std::string handleTtl(const std::vector<std::string>& tokens, RedisDatabase& db) {
    if (tokens.size() < 2) return errorReply("TTL requires key");
    return integerReply(db.ttl(tokens[1]));
}

static std::string handlePersist(const std::vector<std::string>& tokens, RedisDatabase& db) {
    if (tokens.size() < 2) return errorReply("PERSIST requires key");
    return integerReply(db.persist(tokens[1]) ? 1 : 0);
}

static std::string handleRename(const std::vector<std::string>& tokens, RedisDatabase& db) {
    if (tokens.size() < 3) return errorReply("RENAME requires old and new key");
    if (!db.rename(tokens[1], tokens[2])) return errorReply("no such key");
    return simpleReply("OK");
}

// ---------------------------------------------------------------------------
// List commands
// ---------------------------------------------------------------------------

static std::string handleLget(const std::vector<std::string>& tokens, RedisDatabase& db) {
    if (tokens.size() < 2) return errorReply("LGET requires key");
    std::vector<std::string> items;
    const OpStatus status = db.lget(tokens[1], items);
    // A missing list is an empty array, not an error.
    if (status == OpStatus::WrongType) return statusReply(status);
    return bulkArray(items);
}

static std::string handleLlen(const std::vector<std::string>& tokens, RedisDatabase& db) {
    if (tokens.size() < 2) return errorReply("LLEN requires key");
    return integerReply(db.llen(tokens[1]));
}

static std::string handleLpush(const std::vector<std::string>& tokens, RedisDatabase& db) {
    if (tokens.size() < 3) return errorReply("LPUSH requires key and value");
    OpStatus status = OpStatus::Ok;
    for (std::size_t i = 2; i < tokens.size(); ++i) {
        status = db.lpush(tokens[1], tokens[i]);
        if (status != OpStatus::Ok) return statusReply(status);
    }
    return integerReply(db.llen(tokens[1]));
}

static std::string handleRpush(const std::vector<std::string>& tokens, RedisDatabase& db) {
    if (tokens.size() < 3) return errorReply("RPUSH requires key and value");
    OpStatus status = OpStatus::Ok;
    for (std::size_t i = 2; i < tokens.size(); ++i) {
        status = db.rpush(tokens[1], tokens[i]);
        if (status != OpStatus::Ok) return statusReply(status);
    }
    return integerReply(db.llen(tokens[1]));
}

static std::string handleLpop(const std::vector<std::string>& tokens, RedisDatabase& db) {
    if (tokens.size() < 2) return errorReply("LPOP requires key");
    std::string value;
    const OpStatus status = db.lpop(tokens[1], value);
    if (status == OpStatus::WrongType) return statusReply(status);
    if (status != OpStatus::Ok) return nilReply(); // missing or empty list
    return bulkReply(value);
}

static std::string handleRpop(const std::vector<std::string>& tokens, RedisDatabase& db) {
    if (tokens.size() < 2) return errorReply("RPOP requires key");
    std::string value;
    const OpStatus status = db.rpop(tokens[1], value);
    if (status == OpStatus::WrongType) return statusReply(status);
    if (status != OpStatus::Ok) return nilReply(); // missing or empty list
    return bulkReply(value);
}

static std::string handleLrem(const std::vector<std::string>& tokens, RedisDatabase& db) {
    if (tokens.size() < 4) return errorReply("LREM requires key, count and value");
    long long count = 0;
    if (!parseLong(tokens[2], count)) return errorReply("invalid count");

    std::int64_t removed = 0;
    const OpStatus status = db.lrem(tokens[1], count, tokens[3], removed);
    if (status != OpStatus::Ok) return statusReply(status);
    return integerReply(removed);
}

static std::string handleLindex(const std::vector<std::string>& tokens, RedisDatabase& db) {
    if (tokens.size() < 3) return errorReply("LINDEX requires key and index");
    long long index = 0;
    if (!parseLong(tokens[2], index)) return errorReply("invalid index");

    std::string value;
    const OpStatus status = db.lindex(tokens[1], index, value);
    if (status == OpStatus::WrongType) return statusReply(status);
    // Out of range is nil, matching Redis.
    if (status != OpStatus::Ok) return nilReply();
    return bulkReply(value);
}

static std::string handleLset(const std::vector<std::string>& tokens, RedisDatabase& db) {
    if (tokens.size() < 4) return errorReply("LSET requires key, index and value");
    long long index = 0;
    if (!parseLong(tokens[2], index)) return errorReply("invalid index");
    const OpStatus status = db.lset(tokens[1], index, tokens[3]);
    if (status == OpStatus::NotFound) return errorReply("index out of range");
    if (status != OpStatus::Ok) return statusReply(status);
    return simpleReply("OK");
}

// ---------------------------------------------------------------------------
// Hash commands
// ---------------------------------------------------------------------------

static std::string handleHset(const std::vector<std::string>& tokens, RedisDatabase& db) {
    // key followed by one or more field/value pairs, so the token count is even.
    if (tokens.size() < 4 || (tokens.size() % 2) == 1)
        return errorReply("HSET requires key followed by field value pairs");

    // Like Redis, the reply counts how many fields did not already exist.
    std::int64_t created = 0;
    for (std::size_t i = 2; i < tokens.size(); i += 2) {
        bool fieldCreated = false;
        const OpStatus status = db.hset(tokens[1], tokens[i], tokens[i + 1], &fieldCreated);
        if (status != OpStatus::Ok) return statusReply(status);
        if (fieldCreated) ++created;
    }
    return integerReply(created);
}

static std::string handleHget(const std::vector<std::string>& tokens, RedisDatabase& db) {
    if (tokens.size() < 3) return errorReply("HGET requires key and field");
    std::string value;
    const OpStatus status = db.hget(tokens[1], tokens[2], value);
    if (status != OpStatus::Ok) return statusReply(status);
    return bulkReply(value);
}

static std::string handleHexists(const std::vector<std::string>& tokens, RedisDatabase& db) {
    if (tokens.size() < 3) return errorReply("HEXISTS requires key and field");
    return integerReply(db.hexists(tokens[1], tokens[2]) == OpStatus::Ok ? 1 : 0);
}

static std::string handleHdel(const std::vector<std::string>& tokens, RedisDatabase& db) {
    if (tokens.size() < 3) return errorReply("HDEL requires key and field");
    return integerReply(db.hdel(tokens[1], tokens[2]) == OpStatus::Ok ? 1 : 0);
}

static std::string handleHgetall(const std::vector<std::string>& tokens, RedisDatabase& db) {
    if (tokens.size() < 2) return errorReply("HGETALL requires key");
    std::unordered_map<std::string, std::string> fields;
    const OpStatus status = db.hgetall(tokens[1], fields);
    if (status != OpStatus::Ok) return statusReply(status);

    std::vector<std::string> flat;
    flat.reserve(fields.size() * 2);
    for (const auto& field : fields) {
        flat.push_back(field.first);
        flat.push_back(field.second);
    }
    return bulkArray(flat);
}

static std::string handleHkeys(const std::vector<std::string>& tokens, RedisDatabase& db) {
    if (tokens.size() < 2) return errorReply("HKEYS requires key");
    std::vector<std::string> fields;
    const OpStatus status = db.hkeys(tokens[1], fields);
    if (status != OpStatus::Ok) return statusReply(status);
    return bulkArray(fields);
}

static std::string handleHvals(const std::vector<std::string>& tokens, RedisDatabase& db) {
    if (tokens.size() < 2) return errorReply("HVALS requires key");
    std::vector<std::string> values;
    const OpStatus status = db.hvals(tokens[1], values);
    if (status != OpStatus::Ok) return statusReply(status);
    return bulkArray(values);
}

static std::string handleHlen(const std::vector<std::string>& tokens, RedisDatabase& db) {
    if (tokens.size() < 2) return errorReply("HLEN requires key");
    return integerReply(db.hlen(tokens[1]));
}

static std::string handleHmset(const std::vector<std::string>& tokens, RedisDatabase& db) {
    if (tokens.size() < 4 || tokens.size() % 2 == 1)
        return errorReply("HMSET requires key followed by field value pairs");

    std::vector<std::pair<std::string, std::string>> pairs;
    for (std::size_t i = 2; i < tokens.size(); i += 2) {
        pairs.emplace_back(tokens[i], tokens[i + 1]);
    }
    const OpStatus status = db.hmset(tokens[1], pairs);
    if (status != OpStatus::Ok) return statusReply(status);
    return simpleReply("OK");
}

// ---------------------------------------------------------------------------
// Administration and introspection
//
// These are the commands the benchmark client and the Streamlit UI already
// issue; the server previously had no way to expose policy, capacity, or
// counters, so both tools ran but reported nothing.
// ---------------------------------------------------------------------------

static std::string handleInfo(const std::vector<std::string>&, RedisDatabase& db) {
    return bulkReply(db.infoText());
}

static std::string handleDbSize(const std::vector<std::string>&, RedisDatabase& db) {
    return integerReply(static_cast<std::int64_t>(db.dbSize()));
}

static std::string handleMemoryUsage(const std::vector<std::string>& tokens, RedisDatabase& db) {
    if (tokens.size() < 3 || upper(tokens[1]) != "USAGE")
        return errorReply("syntax error, expected MEMORY USAGE <key>");
    if (tokens.size() > 3) return errorReply("syntax error");
    if (!db.exists(tokens[2])) return nilReply();
    return integerReply(static_cast<std::int64_t>(db.memoryUsage(tokens[2])));
}

static std::string applyPolicy(const std::string& requested, RedisDatabase& db) {
    if (!EvictionEngine::isKnownPolicy(requested)) {
        std::ostringstream oss;
        oss << "unknown policy '" << requested << "', expected one of";
        for (const auto& name : EvictionEngine::policyNames()) oss << ' ' << name;
        return errorReply(oss.str());
    }
    db.setPolicy(requested);
    return simpleReply("OK");
}

static std::string applyMaxKeys(const std::string& requested, RedisDatabase& db) {
    long long limit = 0;
    if (!parseLong(requested, limit) || limit < 0)
        return errorReply("MAXKEYS must be a non-negative integer");
    db.setMaxKeys(static_cast<std::size_t>(limit));
    return simpleReply("OK");
}

static std::string handleEviction(const std::vector<std::string>& tokens, RedisDatabase& db) {
    if (tokens.size() == 1) return simpleReply(db.activePolicy());
    if (tokens.size() != 2) return errorReply("EVICTION takes at most one policy name");
    return applyPolicy(tokens[1], db);
}

static std::string handleMaxKeys(const std::vector<std::string>& tokens, RedisDatabase& db) {
    // Read back as an integer reply, so clients get a number rather than text.
    if (tokens.size() == 1) return integerReply(static_cast<std::int64_t>(db.maxKeys()));
    if (tokens.size() != 2) return errorReply("MAXKEYS takes at most one value");
    return applyMaxKeys(tokens[1], db);
}

static std::string handleConfig(const std::vector<std::string>& tokens, RedisDatabase& db) {
    if (tokens.size() < 2) return errorReply("CONFIG requires GET or SET");

    const std::string action = upper(tokens[1]);

    if (action == "RESETSTAT") {
        db.resetStats();
        return simpleReply("OK");
    }

    if (action == "GET") {
        if (tokens.size() != 3) return errorReply("CONFIG GET requires one parameter");
        const std::string value = db.configGet(tokens[2]);
        if (value.empty()) {
            return errorReply("unknown configuration parameter '" + tokens[2] + "'");
        }
        return bulkReply(value);
    }

    if (action == "SET") {
        if (tokens.size() != 4) return errorReply("CONFIG SET requires a parameter and value");

        const std::string parameter = upper(tokens[2]);
        if (parameter == "POLICY") return applyPolicy(tokens[3], db);
        if (parameter == "MAXKEYS") return applyMaxKeys(tokens[3], db);
        return errorReply("unknown configuration parameter '" + tokens[2] + "'");
    }

    return errorReply("unknown CONFIG subcommand '" + tokens[1] + "'");
}

// ---------------------------------------------------------------------------
// Dispatch
// ---------------------------------------------------------------------------

RedisCommandHandler::RedisCommandHandler() {}

std::string RedisCommandHandler::processCommand(const std::string& buffer) {
    std::size_t consumed = 0;
    bool closeConnection = false;
    return processCommand(buffer, consumed, closeConnection);
}

std::string RedisCommandHandler::processCommand(const std::string& buffer,
                                               std::size_t& consumed,
                                               bool& closeConnection) {
    closeConnection = false;

    std::vector<std::string> tokens;
    const ParseResult parsed = parseRespCommand(buffer, consumed, tokens);

    if (parsed == ParseResult::NeedMore) return "";
    if (parsed == ParseResult::Invalid) return errorReply("protocol error");

    const std::string cmd = upper(tokens[0]);
    RedisDatabase& db = RedisDatabase::getInstance();

    if (cmd == "PING")                    return handlePing(tokens, db);
    if (cmd == "ECHO")                    return handleEcho(tokens, db);
    if (cmd == "FLUSHALL")                return handleFlushAll(tokens, db);
    if (cmd == "QUIT")                    { closeConnection = true; return simpleReply("OK"); }

    if (cmd == "SET")                     return handleSet(tokens, db);
    if (cmd == "GET")                     return handleGet(tokens, db);
    if (cmd == "KEYS")                    return handleKeys(tokens, db);
    if (cmd == "TYPE")                    return handleType(tokens, db);
    if (cmd == "DEL" || cmd == "UNLINK")  return handleDel(tokens, db);
    if (cmd == "EXISTS")                  return handleExists(tokens, db);
    if (cmd == "EXPIRE")                  return handleExpire(tokens, db);
    if (cmd == "TTL")                     return handleTtl(tokens, db);
    if (cmd == "PERSIST")                 return handlePersist(tokens, db);
    if (cmd == "RENAME")                  return handleRename(tokens, db);

    if (cmd == "LGET")                    return handleLget(tokens, db);
    if (cmd == "LLEN")                    return handleLlen(tokens, db);
    if (cmd == "LPUSH")                   return handleLpush(tokens, db);
    if (cmd == "RPUSH")                   return handleRpush(tokens, db);
    if (cmd == "LPOP")                    return handleLpop(tokens, db);
    if (cmd == "RPOP")                    return handleRpop(tokens, db);
    if (cmd == "LREM")                    return handleLrem(tokens, db);
    if (cmd == "LINDEX")                  return handleLindex(tokens, db);
    if (cmd == "LSET")                    return handleLset(tokens, db);

    if (cmd == "HSET")                    return handleHset(tokens, db);
    if (cmd == "HGET")                    return handleHget(tokens, db);
    if (cmd == "HEXISTS")                 return handleHexists(tokens, db);
    if (cmd == "HDEL")                    return handleHdel(tokens, db);
    if (cmd == "HGETALL")                 return handleHgetall(tokens, db);
    if (cmd == "HKEYS")                   return handleHkeys(tokens, db);
    if (cmd == "HVALS")                   return handleHvals(tokens, db);
    if (cmd == "HLEN")                    return handleHlen(tokens, db);
    if (cmd == "HMSET")                   return handleHmset(tokens, db);

    if (cmd == "INFO")                    return handleInfo(tokens, db);
    if (cmd == "DBSIZE")                  return handleDbSize(tokens, db);
    if (cmd == "MEMORY")                  return handleMemoryUsage(tokens, db);
    if (cmd == "EVICTION")                return handleEviction(tokens, db);
    if (cmd == "MAXKEYS")                 return handleMaxKeys(tokens, db);
    if (cmd == "CONFIG")                  return handleConfig(tokens, db);

    return errorReply("unknown command '" + tokens[0] + "'");
}