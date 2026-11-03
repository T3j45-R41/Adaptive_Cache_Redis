#include "../include/RedisDatabase.h"
#include "../include/LRUEvictionPolicy.h"
#include "../include/LFUEvictionPolicy.h"
#include "../include/FIFOEvictionPolicy.h"
#include "../include/RandomEvictionPolicy.h"
#include "../include/AdaptiveEvictionPolicy.h"

#include <fstream>
#include <sstream>
#include <algorithm>
#include <iterator>
#include <iostream>

// Singleton accessor
RedisDatabase& RedisDatabase::getInstance() {
    static RedisDatabase instance;
    return instance;
}

RedisDatabase::RedisDatabase() 
    : cache_capacity(1000),
      current_policy_type(PolicyType::LRU),
      start_time(std::chrono::steady_clock::now()) {
    eviction_policy = std::make_unique<LRUEvictionPolicy>(cache_capacity);
}

void RedisDatabase::setPolicy(PolicyType type, size_t newCapacity) {
    std::unique_lock<std::shared_mutex> lock(db_mutex);
    setPolicyInternalLocked(type, newCapacity);
}

void RedisDatabase::setPolicyInternalLocked(PolicyType type, size_t newCapacity) {
    if (newCapacity > 0) {
        cache_capacity = newCapacity;
    }
    current_policy_type = type;

    switch (type) {
        case PolicyType::LRU:
            eviction_policy = std::make_unique<LRUEvictionPolicy>(cache_capacity);
            break;
        case PolicyType::LFU:
            eviction_policy = std::make_unique<LFUEvictionPolicy>(cache_capacity);
            break;
        case PolicyType::FIFO:
            eviction_policy = std::make_unique<FIFOEvictionPolicy>(cache_capacity);
            break;
        case PolicyType::RANDOM:
            eviction_policy = std::make_unique<RandomEvictionPolicy>(cache_capacity);
            break;
        case PolicyType::ADAPTIVE:
            eviction_policy = std::make_unique<AdaptiveEvictionPolicy>(cache_capacity);
            break;
    }

    // Populate the new policy with currently existing keys
    for (const auto& kv : kv_store) eviction_policy->onInsert(kv.first);
    for (const auto& kv : list_store) eviction_policy->onInsert(kv.first);
    for (const auto& kv : hash_store) eviction_policy->onInsert(kv.first);

    enforceCapacityLocked();
}

PolicyType RedisDatabase::getPolicyType() const {
    std::shared_lock<std::shared_mutex> lock(db_mutex);
    return current_policy_type;
}

std::string RedisDatabase::getPolicyName() const {
    std::shared_lock<std::shared_mutex> lock(db_mutex);
    return policyTypeToString(current_policy_type);
}

void RedisDatabase::setCapacity(size_t capacity) {
    std::unique_lock<std::shared_mutex> lock(db_mutex);
    setPolicyInternalLocked(current_policy_type, capacity);
}

size_t RedisDatabase::getCapacity() const {
    std::shared_lock<std::shared_mutex> lock(db_mutex);
    return cache_capacity;
}

EvictionStats RedisDatabase::getEvictionStats() const {
    std::shared_lock<std::shared_mutex> lock(db_mutex);
    if (eviction_policy) {
        auto stats = eviction_policy->getStats();
        stats.currentSize = kv_store.size() + list_store.size() + hash_store.size();
        stats.capacity = cache_capacity;
        return stats;
    }
    return EvictionStats{};
}

std::vector<EvictionRecord> RedisDatabase::getRecentEvictions(size_t limit) const {
    std::shared_lock<std::shared_mutex> lock(db_mutex);
    std::vector<EvictionRecord> res;
    size_t count = 0;
    for (auto it = eviction_history.rbegin(); it != eviction_history.rend() && count < limit; ++it, ++count) {
        res.push_back(*it);
    }
    return res;
}

bool RedisDatabase::keyExistsLocked(const std::string& key) const {
    return (kv_store.find(key) != kv_store.end()) ||
           (list_store.find(key) != list_store.end()) ||
           (hash_store.find(key) != hash_store.end());
}

void RedisDatabase::deleteKeyInternalLocked(const std::string& key) {
    kv_store.erase(key);
    list_store.erase(key);
    hash_store.erase(key);
    expiry_map.erase(key);
    if (eviction_policy) {
        eviction_policy->onDelete(key);
    }
}

void RedisDatabase::enforceCapacityLocked() {
    size_t current_keys = kv_store.size() + list_store.size() + hash_store.size();
    while (current_keys > cache_capacity && eviction_policy) {
        std::string victim = eviction_policy->evict();
        if (victim.empty()) break;

        deleteKeyInternalLocked(victim);

        auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        EvictionRecord rec;
        rec.key = victim;
        rec.policy = policyTypeToString(current_policy_type);
        rec.reason = "capacity_exceeded";
        rec.timestampMs = static_cast<uint64_t>(nowMs);

        if (eviction_history.size() >= MAX_EVICTION_HISTORY) {
            eviction_history.pop_front();
        }
        eviction_history.push_back(rec);

        current_keys = kv_store.size() + list_store.size() + hash_store.size();
    }
}

bool RedisDatabase::flushAll() {
    std::unique_lock<std::shared_mutex> lock(db_mutex);
    kv_store.clear();
    list_store.clear();
    hash_store.clear();
    expiry_map.clear();
    if (eviction_policy) {
        eviction_policy->reset();
    }
    eviction_history.clear();
    return true;
}

bool RedisDatabase::flushDb() {
    return flushAll();
}

size_t RedisDatabase::dbsize() {
    std::unique_lock<std::shared_mutex> lock(db_mutex);
    purgeExpired();
    return kv_store.size() + list_store.size() + hash_store.size();
}

bool RedisDatabase::exists(const std::string& key) {
    std::unique_lock<std::shared_mutex> lock(db_mutex);
    purgeExpired();
    return keyExistsLocked(key);
}

// Key/Value Operations
void RedisDatabase::set(const std::string& key, const std::string& value) {
    std::unique_lock<std::shared_mutex> lock(db_mutex);
    purgeExpired();
    bool isNew = !keyExistsLocked(key);

    kv_store[key] = value;
    // Overwrite list/hash if type change occurs
    list_store.erase(key);
    hash_store.erase(key);

    if (eviction_policy) {
        if (isNew) {
            eviction_policy->onInsert(key);
        } else {
            eviction_policy->onUpdate(key);
        }
    }

    enforceCapacityLocked();
}

bool RedisDatabase::get(const std::string& key, std::string& value) {
    std::unique_lock<std::shared_mutex> lock(db_mutex);
    purgeExpired();
    auto it = kv_store.find(key);
    if (it != kv_store.end()) {
        value = it->second;
        if (eviction_policy) {
            eviction_policy->recordHit();
            eviction_policy->onAccess(key);
        }
        return true;
    }
    if (eviction_policy) {
        eviction_policy->recordMiss();
    }
    return false;
}

std::vector<std::string> RedisDatabase::keys() {
    std::unique_lock<std::shared_mutex> lock(db_mutex);
    purgeExpired();
    std::vector<std::string> result;
    for (const auto& pair : kv_store) result.push_back(pair.first);
    for (const auto& pair : list_store) result.push_back(pair.first);
    for (const auto& pair : hash_store) result.push_back(pair.first);
    return result;
}

std::string RedisDatabase::type(const std::string& key) {
    std::unique_lock<std::shared_mutex> lock(db_mutex);
    purgeExpired();
    if (kv_store.find(key) != kv_store.end()) return "string";
    if (list_store.find(key) != list_store.end()) return "list";
    if (hash_store.find(key) != hash_store.end()) return "hash";
    return "none";    
}

bool RedisDatabase::del(const std::string& key) {
    std::unique_lock<std::shared_mutex> lock(db_mutex);
    purgeExpired();
    bool erased = false;
    if (kv_store.erase(key) > 0) erased = true;
    if (list_store.erase(key) > 0) erased = true;
    if (hash_store.erase(key) > 0) erased = true;
    expiry_map.erase(key);
    if (erased && eviction_policy) {
        eviction_policy->onDelete(key);
    }
    return erased;
}

bool RedisDatabase::expire(const std::string& key, int seconds) {
    return pexpire(key, static_cast<int64_t>(seconds) * 1000);
}

bool RedisDatabase::pexpire(const std::string& key, int64_t milliseconds) {
    std::unique_lock<std::shared_mutex> lock(db_mutex);
    purgeExpired();
    if (!keyExistsLocked(key)) return false;

    expiry_map[key] = std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);
    return true;
}

int64_t RedisDatabase::ttl(const std::string& key) {
    int64_t ms = pttl(key);
    if (ms < 0) return ms;
    return ms / 1000;
}

int64_t RedisDatabase::pttl(const std::string& key) {
    std::unique_lock<std::shared_mutex> lock(db_mutex);
    purgeExpired();
    if (!keyExistsLocked(key)) return -2; // Key does not exist

    auto it = expiry_map.find(key);
    if (it == expiry_map.end()) return -1; // Key exists with no expiry

    auto now = std::chrono::steady_clock::now();
    if (now >= it->second) {
        deleteKeyInternalLocked(key);
        return -2;
    }
    return std::chrono::duration_cast<std::chrono::milliseconds>(it->second - now).count();
}

bool RedisDatabase::persist(const std::string& key) {
    std::unique_lock<std::shared_mutex> lock(db_mutex);
    purgeExpired();
    if (!keyExistsLocked(key)) return false;
    return expiry_map.erase(key) > 0;
}

void RedisDatabase::purgeExpired() {
    auto now = std::chrono::steady_clock::now();
    for (auto it = expiry_map.begin(); it != expiry_map.end(); ) {
        if (now > it->second) {
            std::string key = it->first;
            kv_store.erase(key);
            list_store.erase(key);
            hash_store.erase(key);
            if (eviction_policy) {
                eviction_policy->onDelete(key);
            }
            it = expiry_map.erase(it);
        } else {
            ++it;
        }
    }
}

bool RedisDatabase::rename(const std::string& oldKey, const std::string& newKey) {
    std::unique_lock<std::shared_mutex> lock(db_mutex);
    purgeExpired();
    bool found = false;

    auto itKv = kv_store.find(oldKey);
    if (itKv != kv_store.end()) {
        kv_store[newKey] = itKv->second;
        kv_store.erase(itKv);
        found = true;
    }

    auto itList = list_store.find(oldKey);
    if (itList != list_store.end()) {
        list_store[newKey] = itList->second;
        list_store.erase(itList);
        found = true;
    }

    auto itHash = hash_store.find(oldKey);
    if (itHash != hash_store.end()) {
        hash_store[newKey] = itHash->second;
        hash_store.erase(itHash);
        found = true;
    }

    auto itExpire = expiry_map.find(oldKey);
    if (itExpire != expiry_map.end()) {
        expiry_map[newKey] = itExpire->second;
        expiry_map.erase(itExpire);
    }

    if (found && eviction_policy) {
        eviction_policy->onDelete(oldKey);
        eviction_policy->onInsert(newKey);
    }

    return found;
}

// List Operations
std::vector<std::string> RedisDatabase::lget(const std::string& key) {
    std::shared_lock<std::shared_mutex> lock(db_mutex);
    auto it = list_store.find(key);
    if (it != list_store.end()) {
        if (eviction_policy) {
            eviction_policy->recordHit();
            eviction_policy->onAccess(key);
        }
        return it->second; 
    }
    if (eviction_policy) eviction_policy->recordMiss();
    return {}; 
}

std::vector<std::string> RedisDatabase::lrange(const std::string& key, int start, int stop) {
    std::shared_lock<std::shared_mutex> lock(db_mutex);
    auto it = list_store.find(key);
    if (it == list_store.end()) {
        if (eviction_policy) eviction_policy->recordMiss();
        return {};
    }

    if (eviction_policy) {
        eviction_policy->recordHit();
        eviction_policy->onAccess(key);
    }

    const auto& list = it->second;
    int size = static_cast<int>(list.size());
    if (size == 0) return {};

    if (start < 0) start += size;
    if (stop < 0) stop += size;
    if (start < 0) start = 0;
    if (start >= size || start > stop) return {};
    if (stop >= size) stop = size - 1;

    std::vector<std::string> res;
    for (int i = start; i <= stop; ++i) {
        res.push_back(list[i]);
    }
    return res;
}

ssize_t RedisDatabase::llen(const std::string& key) {
    std::shared_lock<std::shared_mutex> lock(db_mutex);
    auto it = list_store.find(key);
    if (it != list_store.end()) return it->second.size();
    return 0;
}

void RedisDatabase::lpush(const std::string& key, const std::string& value) {
    std::unique_lock<std::shared_mutex> lock(db_mutex);
    purgeExpired();
    bool isNew = !keyExistsLocked(key);

    list_store[key].insert(list_store[key].begin(), value);
    kv_store.erase(key);
    hash_store.erase(key);

    if (eviction_policy) {
        if (isNew) eviction_policy->onInsert(key);
        else eviction_policy->onUpdate(key);
    }
    enforceCapacityLocked();
}

void RedisDatabase::rpush(const std::string& key, const std::string& value) {
    std::unique_lock<std::shared_mutex> lock(db_mutex);
    purgeExpired();
    bool isNew = !keyExistsLocked(key);

    list_store[key].push_back(value);
    kv_store.erase(key);
    hash_store.erase(key);

    if (eviction_policy) {
        if (isNew) eviction_policy->onInsert(key);
        else eviction_policy->onUpdate(key);
    }
    enforceCapacityLocked();
}

bool RedisDatabase::lpop(const std::string& key, std::string& value) {
    std::unique_lock<std::shared_mutex> lock(db_mutex);
    purgeExpired();
    auto it = list_store.find(key);
    if (it == list_store.end() || it->second.empty()) {
        if (eviction_policy) eviction_policy->recordMiss();
        return false;
    }

    value = it->second.front();
    it->second.erase(it->second.begin());
    if (it->second.empty()) {
        list_store.erase(it);
        if (eviction_policy) eviction_policy->onDelete(key);
    } else if (eviction_policy) {
        eviction_policy->onUpdate(key);
        eviction_policy->recordHit();
    }
    return true;
}

bool RedisDatabase::rpop(const std::string& key, std::string& value) {
    std::unique_lock<std::shared_mutex> lock(db_mutex);
    purgeExpired();
    auto it = list_store.find(key);
    if (it == list_store.end() || it->second.empty()) {
        if (eviction_policy) eviction_policy->recordMiss();
        return false;
    }

    value = it->second.back();
    it->second.pop_back();
    if (it->second.empty()) {
        list_store.erase(it);
        if (eviction_policy) eviction_policy->onDelete(key);
    } else if (eviction_policy) {
        eviction_policy->onUpdate(key);
        eviction_policy->recordHit();
    }
    return true;
}

int RedisDatabase::lrem(const std::string& key, int count, const std::string& value) {
    std::unique_lock<std::shared_mutex> lock(db_mutex);
    purgeExpired();
    auto it = list_store.find(key);
    if (it == list_store.end()) return 0;

    auto& list = it->second;
    int removed = 0;

    if (count > 0) {
        for (auto iter = list.begin(); iter != list.end() && removed < count; ) {
            if (*iter == value) {
                iter = list.erase(iter);
                removed++;
            } else {
                ++iter;
            }
        }
    } else if (count < 0) {
        int target = -count;
        for (auto iter = list.rbegin(); iter != list.rend() && removed < target; ) {
            if (*iter == value) {
                auto forward_it = std::next(iter).base();
                list.erase(forward_it);
                removed++;
                iter = list.rbegin(); // re-anchor
            } else {
                ++iter;
            }
        }
    } else {
        for (auto iter = list.begin(); iter != list.end(); ) {
            if (*iter == value) {
                iter = list.erase(iter);
                removed++;
            } else {
                ++iter;
            }
        }
    }

    if (list.empty()) {
        list_store.erase(it);
        if (eviction_policy) eviction_policy->onDelete(key);
    } else if (eviction_policy && removed > 0) {
        eviction_policy->onUpdate(key);
    }
    return removed;
}

bool RedisDatabase::lindex(const std::string& key, int index, std::string& value) {
    std::shared_lock<std::shared_mutex> lock(db_mutex);
    auto it = list_store.find(key);
    if (it == list_store.end()) {
        if (eviction_policy) eviction_policy->recordMiss();
        return false;
    }

    const auto& list = it->second;
    int size = static_cast<int>(list.size());
    if (index < 0) index += size;
    if (index < 0 || index >= size) return false;

    value = list[index];
    if (eviction_policy) {
        eviction_policy->recordHit();
        eviction_policy->onAccess(key);
    }
    return true;
}

bool RedisDatabase::lset(const std::string& key, int index, const std::string& value) {
    std::unique_lock<std::shared_mutex> lock(db_mutex);
    purgeExpired();
    auto it = list_store.find(key);
    if (it == list_store.end()) return false;

    auto& list = it->second;
    int size = static_cast<int>(list.size());
    if (index < 0) index += size;
    if (index < 0 || index >= size) return false;

    list[index] = value;
    if (eviction_policy) eviction_policy->onUpdate(key);
    return true;
}

// Hash Operations
bool RedisDatabase::hset(const std::string& key, const std::string& field, const std::string& value) {
    std::unique_lock<std::shared_mutex> lock(db_mutex);
    purgeExpired();
    bool isNew = !keyExistsLocked(key);

    hash_store[key][field] = value;
    kv_store.erase(key);
    list_store.erase(key);

    if (eviction_policy) {
        if (isNew) eviction_policy->onInsert(key);
        else eviction_policy->onUpdate(key);
    }
    enforceCapacityLocked();
    return true;
}

bool RedisDatabase::hget(const std::string& key, const std::string& field, std::string& value) {
    std::shared_lock<std::shared_mutex> lock(db_mutex);
    auto it = hash_store.find(key);
    if (it == hash_store.end()) {
        if (eviction_policy) eviction_policy->recordMiss();
        return false;
    }

    auto fIt = it->second.find(field);
    if (fIt == it->second.end()) {
        if (eviction_policy) eviction_policy->recordMiss();
        return false;
    }

    value = fIt->second;
    if (eviction_policy) {
        eviction_policy->recordHit();
        eviction_policy->onAccess(key);
    }
    return true;
}

bool RedisDatabase::hexists(const std::string& key, const std::string& field) {
    std::shared_lock<std::shared_mutex> lock(db_mutex);
    auto it = hash_store.find(key);
    if (it == hash_store.end()) return false;
    return it->second.find(field) != it->second.end();
}

bool RedisDatabase::hdel(const std::string& key, const std::string& field) {
    std::unique_lock<std::shared_mutex> lock(db_mutex);
    purgeExpired();
    auto it = hash_store.find(key);
    if (it == hash_store.end()) return false;

    bool erased = it->second.erase(field) > 0;
    if (it->second.empty()) {
        hash_store.erase(it);
        if (eviction_policy) eviction_policy->onDelete(key);
    } else if (eviction_policy) {
        eviction_policy->onUpdate(key);
    }
    return erased;
}

std::unordered_map<std::string, std::string> RedisDatabase::hgetall(const std::string& key) {
    std::shared_lock<std::shared_mutex> lock(db_mutex);
    auto it = hash_store.find(key);
    if (it == hash_store.end()) {
        if (eviction_policy) eviction_policy->recordMiss();
        return {};
    }

    if (eviction_policy) {
        eviction_policy->recordHit();
        eviction_policy->onAccess(key);
    }
    return it->second;
}

std::vector<std::string> RedisDatabase::hkeys(const std::string& key) {
    std::shared_lock<std::shared_mutex> lock(db_mutex);
    auto it = hash_store.find(key);
    if (it == hash_store.end()) return {};

    std::vector<std::string> res;
    for (const auto& kv : it->second) res.push_back(kv.first);
    return res;
}

std::vector<std::string> RedisDatabase::hvals(const std::string& key) {
    std::shared_lock<std::shared_mutex> lock(db_mutex);
    auto it = hash_store.find(key);
    if (it == hash_store.end()) return {};

    std::vector<std::string> res;
    for (const auto& kv : it->second) res.push_back(kv.second);
    return res;
}

ssize_t RedisDatabase::hlen(const std::string& key) {
    std::shared_lock<std::shared_mutex> lock(db_mutex);
    auto it = hash_store.find(key);
    if (it == hash_store.end()) return 0;
    return it->second.size();
}

bool RedisDatabase::hmset(const std::string& key, const std::vector<std::pair<std::string, std::string>>& fieldValues) {
    std::unique_lock<std::shared_mutex> lock(db_mutex);
    purgeExpired();
    bool isNew = !keyExistsLocked(key);

    for (const auto& fv : fieldValues) {
        hash_store[key][fv.first] = fv.second;
    }
    kv_store.erase(key);
    list_store.erase(key);

    if (eviction_policy) {
        if (isNew) eviction_policy->onInsert(key);
        else eviction_policy->onUpdate(key);
    }
    enforceCapacityLocked();
    return true;
}

std::string RedisDatabase::getInfo(const std::string& section) const {
    std::shared_lock<std::shared_mutex> lock(db_mutex);
    std::ostringstream oss;

    auto now = std::chrono::steady_clock::now();
    auto uptimeSeconds = std::chrono::duration_cast<std::chrono::seconds>(now - start_time).count();
    size_t totalKeys = kv_store.size() + list_store.size() + hash_store.size();

    auto stats = eviction_policy ? eviction_policy->getStats() : EvictionStats{};

    if (section == "server" || section == "default" || section == "all") {
        oss << "# Server\r\n";
        oss << "redis_version:7.0.0-adaptive\r\n";
        oss << "os_model:Thread-Safe Multi-Client In-Memory Server\r\n";
        oss << "uptime_in_seconds:" << uptimeSeconds << "\r\n";
    }

    if (section == "memory" || section == "default" || section == "all") {
        oss << "# Memory\r\n";
        oss << "maxmemory_keys:" << cache_capacity << "\r\n";
        oss << "used_memory_keys:" << totalKeys << "\r\n";
        oss << "used_memory_kv:" << kv_store.size() << "\r\n";
        oss << "used_memory_list:" << list_store.size() << "\r\n";
        oss << "used_memory_hash:" << hash_store.size() << "\r\n";
    }

    if (section == "stats" || section == "default" || section == "all") {
        oss << "# Stats\r\n";
        oss << "total_keys:" << totalKeys << "\r\n";
        oss << "keyspace_hits:" << stats.totalHits << "\r\n";
        oss << "keyspace_misses:" << stats.totalMisses << "\r\n";
        double hitRate = (stats.totalHits + stats.totalMisses > 0) 
            ? (100.0 * stats.totalHits / (stats.totalHits + stats.totalMisses)) : 0.0;
        oss << "hit_rate_pct:" << hitRate << "\r\n";
        oss << "evicted_keys:" << stats.totalEvictions << "\r\n";
    }

    if (section == "eviction" || section == "default" || section == "all") {
        oss << "# Eviction\r\n";
        oss << "policy_name:" << policyTypeToString(current_policy_type) << "\r\n";
        oss << "policy_capacity:" << cache_capacity << "\r\n";
        oss << "policy_current_size:" << totalKeys << "\r\n";
        oss << "policy_total_evictions:" << stats.totalEvictions << "\r\n";
        if (current_policy_type == PolicyType::ADAPTIVE) {
            oss << "adaptive_expert_lru_weight:" << stats.expert1Weight << "\r\n";
            oss << "adaptive_expert_lfu_weight:" << stats.expert2Weight << "\r\n";
            oss << "adaptive_regret_lru:" << stats.cumulativeRegret1 << "\r\n";
            oss << "adaptive_regret_lfu:" << stats.cumulativeRegret2 << "\r\n";
            oss << "adaptive_lru_selections:" << stats.expert1Selections << "\r\n";
            oss << "adaptive_lfu_selections:" << stats.expert2Selections << "\r\n";
            oss << "adaptive_active_expert:" << stats.currentActiveExpert << "\r\n";
        }
    }

    return oss.str();
}

// Persistence
bool RedisDatabase::dump(const std::string& filename) {
    std::shared_lock<std::shared_mutex> lock(db_mutex);
    std::ofstream ofs(filename, std::ios::binary);
    if (!ofs) return false;

    // Format:
    // K <key> <val>
    // L <key> <count> <v1> <v2> ...
    // H <key> <count> <f1> <v1> ...
    for (const auto& kv : kv_store) {
        ofs << "K " << kv.first << " " << kv.second << "\n";
    }
    for (const auto& l : list_store) {
        ofs << "L " << l.first << " " << l.second.size();
        for (const auto& item : l.second) {
            ofs << " " << item;
        }
        ofs << "\n";
    }
    for (const auto& h : hash_store) {
        ofs << "H " << h.first << " " << h.second.size();
        for (const auto& fv : h.second) {
            ofs << " " << fv.first << " " << fv.second;
        }
        ofs << "\n";
    }
    return true;
}

bool RedisDatabase::load(const std::string& filename) {
    std::unique_lock<std::shared_mutex> lock(db_mutex);
    std::ifstream ifs(filename, std::ios::binary);
    if (!ifs) return false;

    kv_store.clear();
    list_store.clear();
    hash_store.clear();
    expiry_map.clear();
    if (eviction_policy) eviction_policy->reset();

    std::string line;
    while (std::getline(ifs, line)) {
        if (line.empty()) continue;
        std::istringstream iss(line);
        char type;
        iss >> type;
        if (type == 'K') {
            std::string key, val;
            iss >> key >> val;
            kv_store[key] = val;
            if (eviction_policy) eviction_policy->onInsert(key);
        } else if (type == 'L') {
            std::string key;
            size_t count = 0;
            iss >> key >> count;
            for (size_t i = 0; i < count; ++i) {
                std::string item;
                iss >> item;
                list_store[key].push_back(item);
            }
            if (eviction_policy) eviction_policy->onInsert(key);
        } else if (type == 'H') {
            std::string key;
            size_t count = 0;
            iss >> key >> count;
            for (size_t i = 0; i < count; ++i) {
                std::string field, val;
                iss >> field >> val;
                hash_store[key][field] = val;
            }
            if (eviction_policy) eviction_policy->onInsert(key);
        }
    }
    enforceCapacityLocked();
    return true;
}
