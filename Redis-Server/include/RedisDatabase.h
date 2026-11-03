#ifndef REDIS_DATABASE_H
#define REDIS_DATABASE_H

#include <string>
#include <shared_mutex>
#include <mutex>
#include <unordered_map>
#include <vector>
#include <chrono>
#include <memory>
#include <deque>
#include "EvictionPolicy.h"

struct EvictionRecord {
    std::string key;
    std::string policy;
    std::string reason;
    uint64_t timestampMs;
};

class RedisDatabase {
public: 
    // Get singleton instance
    static RedisDatabase& getInstance();

    // Common Commands
    bool flushAll();
    bool flushDb();
    size_t dbsize();
    bool exists(const std::string& key);

    // Key/Value Operations
    void set(const std::string& key, const std::string& value);
    bool get(const std::string& key, std::string& value);
    std::vector<std::string> keys();
    std::string type(const std::string& key);
    bool del(const std::string& key);
    bool expire(const std::string& key, int seconds);
    bool pexpire(const std::string& key, int64_t milliseconds);
    int64_t ttl(const std::string& key);
    int64_t pttl(const std::string& key);
    bool persist(const std::string& key);
    void purgeExpired();
    bool rename(const std::string& oldKey, const std::string& newKey);

    // List Operations
    std::vector<std::string> lget(const std::string& key);
    std::vector<std::string> lrange(const std::string& key, int start, int stop);
    ssize_t llen(const std::string& key);
    void lpush(const std::string& key, const std::string& value);
    void rpush(const std::string& key, const std::string& value);
    bool lpop(const std::string& key, std::string& value);
    bool rpop(const std::string& key, std::string& value);
    int lrem(const std::string& key, int count, const std::string& value);
    bool lindex(const std::string& key, int index, std::string& value);
    bool lset(const std::string& key, int index, const std::string& value);

    // Hash Operations
    bool hset(const std::string& key, const std::string& field, const std::string& value);
    bool hget(const std::string& key, const std::string& field, std::string& value);
    bool hexists(const std::string& key, const std::string& field);
    bool hdel(const std::string& key, const std::string& field);
    std::unordered_map<std::string, std::string> hgetall(const std::string& key);
    std::vector<std::string> hkeys(const std::string& key);
    std::vector<std::string> hvals(const std::string& key);
    ssize_t hlen(const std::string& key);
    bool hmset(const std::string& key, const std::vector<std::pair<std::string, std::string>>& fieldValues);

    // Eviction Engine & Configuration
    void setPolicy(PolicyType type, size_t newCapacity = 0);
    PolicyType getPolicyType() const;
    std::string getPolicyName() const;
    void setCapacity(size_t capacity);
    size_t getCapacity() const;
    EvictionStats getEvictionStats() const;
    std::vector<EvictionRecord> getRecentEvictions(size_t limit = 100) const;

    // Info & Diagnostics
    std::string getInfo(const std::string& section = "default") const;

    // Persistence: Dump / load database from file
    bool dump(const std::string& filename);
    bool load(const std::string& filename);

private:
    RedisDatabase();
    ~RedisDatabase() = default;
    RedisDatabase(const RedisDatabase&) = delete;
    RedisDatabase& operator=(const RedisDatabase&) = delete;

    void setPolicyInternalLocked(PolicyType type, size_t newCapacity = 0);
    void enforceCapacityLocked();
    bool keyExistsLocked(const std::string& key) const;
    void deleteKeyInternalLocked(const std::string& key);

    mutable std::shared_mutex db_mutex;
    std::unordered_map<std::string, std::string> kv_store;
    std::unordered_map<std::string, std::vector<std::string>> list_store;
    std::unordered_map<std::string, std::unordered_map<std::string, std::string>> hash_store;

    std::unordered_map<std::string, std::chrono::steady_clock::time_point> expiry_map;

    // Eviction engine members
    size_t cache_capacity = 1000;
    PolicyType current_policy_type = PolicyType::LRU;
    std::unique_ptr<EvictionPolicy> eviction_policy;
    std::deque<EvictionRecord> eviction_history;
    static constexpr size_t MAX_EVICTION_HISTORY = 1000;

    // Global stats
    uint64_t total_commands_processed = 0;
    std::chrono::steady_clock::time_point start_time;
};

#endif // REDIS_DATABASE_H
