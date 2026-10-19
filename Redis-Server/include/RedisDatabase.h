#ifndef REDIS_DATABASE_H
#define REDIS_DATABASE_H

#include "eviction/eviction_engine.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

// Outcome of a mutating or reading operation, so the command layer can map it
// onto a RESP reply. Distinguishing "no such key" from "wrong type" is only
// possible with a single typed keyspace, which is what this store uses.
enum class OpStatus {
    Ok,
    NotFound,
    WrongType
};

// RESP error text for a non-Ok status.
const char* statusError(OpStatus status);

class RedisDatabase {
public:
    static RedisDatabase& getInstance();

    // ---- Cache configuration and introspection ----
    bool setPolicy(const std::string& name);
    std::string activePolicy() const;
    bool setMaxKeys(std::size_t keys);
    std::size_t maxKeys() const;
    std::size_t dbSize() const;
    std::size_t totalKeyBytes() const;
    std::size_t memoryUsage(const std::string& key) const;
    bool resetStats();
    std::string configGet(const std::string& parameter) const;
    // Human-readable snapshot used by INFO.
    std::string infoText() const;

    // ---- Common ----
    bool flushAll();

    // ---- Key/Value ----
    OpStatus set(const std::string& key, const std::string& value);
    OpStatus get(const std::string& key, std::string& value);
    std::vector<std::string> keys(const std::string& pattern);
    std::string type(const std::string& key);
    bool exists(const std::string& key);
    bool del(const std::string& key);
    bool expire(const std::string& key, std::int64_t seconds);
    // -2 when the key does not exist, -1 when it exists without a TTL.
    std::int64_t ttl(const std::string& key);
    bool persist(const std::string& key);
    bool rename(const std::string& oldKey, const std::string& newKey);
    void purgeExpired();

    // ---- List ----
    OpStatus lget(const std::string& key, std::vector<std::string>& out);
    std::int64_t llen(const std::string& key);
    OpStatus lpush(const std::string& key, const std::string& value);
    OpStatus rpush(const std::string& key, const std::string& value);
    OpStatus lpop(const std::string& key, std::string& value);
    OpStatus rpop(const std::string& key, std::string& value);
    // `removed` reports how many matching elements were dropped.
    OpStatus lrem(const std::string& key, std::int64_t count, const std::string& value,
                  std::int64_t& removed);
    OpStatus lindex(const std::string& key, std::int64_t index, std::string& value);
    OpStatus lset(const std::string& key, std::int64_t index, const std::string& value);

    // ---- Hash ----
    // fieldCreated is set to true when the field did not previously exist,
    // which is how HSET reports 1 for a new field and 0 for an update.
    OpStatus hset(const std::string& key, const std::string& field, const std::string& value,
                  bool* fieldCreated = nullptr);
    OpStatus hget(const std::string& key, const std::string& field, std::string& value);
    OpStatus hexists(const std::string& key, const std::string& field);
    OpStatus hdel(const std::string& key, const std::string& field);
    OpStatus hgetall(const std::string& key,
                     std::unordered_map<std::string, std::string>& out);
    OpStatus hkeys(const std::string& key, std::vector<std::string>& out);
    OpStatus hvals(const std::string& key, std::vector<std::string>& out);
    std::int64_t hlen(const std::string& key);
    OpStatus hmset(const std::string& key,
                   const std::vector<std::pair<std::string, std::string>>& fieldValues);

    // ---- Persistence ----
    bool dump(const std::string& filename);
    bool load(const std::string& filename);

private:
    RedisDatabase() = default;
    ~RedisDatabase() = default;
    RedisDatabase(const RedisDatabase&) = delete;
    RedisDatabase& operator=(const RedisDatabase&) = delete;

    using Clock = std::chrono::steady_clock;

    // One keyspace, one type per key. The previous design kept three parallel
    // maps, so a single key could simultaneously hold a string, a list and a
    // hash, which duplicated every KEYS result and broke capacity accounting.
    struct Value {
        enum class Type { String, List, Hash };
        Type type{Type::String};
        std::string str;
        std::vector<std::string> list;
        std::unordered_map<std::string, std::string> hash;
    };

    // mutable: the read-only accessors still need to take the lock, because
    // every public entry point serialises against the writers.
    mutable std::mutex db_mutex;
    std::unordered_map<std::string, Value> store;
    std::unordered_map<std::string, CacheEntry> frames;
    std::unordered_map<std::string, Clock::time_point> expiry_map;
    EvictionEngine engine;
    std::uint64_t logicalClock_{0};

    // ---- Internal helpers. All require db_mutex to be held. ----
    enum class RemovalReason { Explicit, Expired, Evicted };

    void purgeExpiredLocked();
    bool removeKeyLocked(const std::string& key, RemovalReason reason);
    void enforceCapacityLocked();
    void registerInsertLocked(const std::string& key, std::size_t byteSize, bool isNew);
    void registerAccessLocked(const std::string& key);
    void resyncPolicyLocked();
    std::size_t valueBytesLocked(const Value& value) const;
    bool typeIsLocked(const std::string& key, Value::Type type) const;

    static bool globMatch(const std::string& pattern, const std::string& text);
    static const char* typeName(Value::Type type);
};

#endif