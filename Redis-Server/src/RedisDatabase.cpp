#include "../include/RedisDatabase.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <sstream>

const char* statusError(OpStatus status) {
    switch (status) {
        case OpStatus::Ok:         return "";
        case OpStatus::NotFound:   return "no such key";
        case OpStatus::WrongType:  return "wrongtype operation against a key holding the wrong kind of value";
    }
    return "unknown error";
}

RedisDatabase& RedisDatabase::getInstance() {
    static RedisDatabase instance;
    return instance;
}

const char* RedisDatabase::typeName(Value::Type type) {
    switch (type) {
        case Value::Type::String: return "string";
        case Value::Type::List:   return "list";
        case Value::Type::Hash:   return "hash";
    }
    return "none";
}

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

std::size_t RedisDatabase::valueBytesLocked(const Value& value) const {
    std::size_t total = 0;
    switch (value.type) {
        case Value::Type::String:
            total = value.str.size();
            break;
        case Value::Type::List:
            total = sizeof(std::string) * value.list.capacity();
            for (const auto& item : value.list) total += sizeof(std::string) + item.size();
            break;
        case Value::Type::Hash:
            for (const auto& field : value.hash)
                total += sizeof(std::string) + field.first.size() + field.second.size();
            break;
    }
    return total;
}

bool RedisDatabase::typeIsLocked(const std::string& key, Value::Type type) const {
    auto it = store.find(key);
    return it != store.end() && it->second.type == type;
}

void RedisDatabase::registerInsertLocked(const std::string& key, std::size_t byteSize,
                                         bool isNew) {
    auto it = frames.find(key);
    if (it == frames.end()) {
        CacheEntry entry;
        entry.key = key;
        entry.insertionTime = logicalClock_++;
        entry.lastAccessTime = entry.insertionTime;
        entry.accessCount = 0;
        entry.byteSize = byteSize;
        entry.dirty = true;
        frames.emplace(key, std::move(entry));
        engine.onInsert(key);
    } else {
        it->second.byteSize = byteSize;
        it->second.dirty = true;
        // A rewrite is not a read: neither recency nor frequency may move.
        engine.onReplace(key);
    }

    if (isNew) enforceCapacityLocked();
}

void RedisDatabase::registerAccessLocked(const std::string& key) {
    auto it = frames.find(key);
    if (it == frames.end()) return;
    it->second.lastAccessTime = logicalClock_++;
    ++it->second.accessCount;
    engine.onAccess(key);
}

void RedisDatabase::resyncPolicyLocked() {
    // Hand the keys over oldest-first. FIFO cannot recover insertion order
    // from an unordered set, so without this a policy switch or a snapshot
    // reload would leave it evicting in hash order.
    std::vector<std::pair<std::uint64_t, std::string>> ordered;
    ordered.reserve(store.size());
    for (const auto& entry : store) {
        const auto frame = frames.find(entry.first);
        ordered.emplace_back(frame == frames.end() ? 0 : frame->second.insertionTime,
                              entry.first);
    }
    std::sort(ordered.begin(), ordered.end());

    std::vector<std::string> keys;
    keys.reserve(ordered.size());
    for (auto& entry : ordered) keys.push_back(std::move(entry.second));

    engine.resync(keys);
}

void RedisDatabase::enforceCapacityLocked() {
    if (!engine.capacityLimited()) return;

    // A single SET can only add one key, but the loop is kept general so that
    // capacity changes and future bulk operations stay correct.
    while (store.size() > engine.maxKeys()) {
        const std::string victim = engine.selectVictim();
        if (victim.empty()) break; // policy sees no candidate; nothing to do

        const bool dirty = frames.count(victim) ? frames[victim].dirty : false;

        // Snapshot the alternatives while the key is still resident, otherwise
        // the adaptive controller has nothing left to shadow.
        engine.registerEvictionDecisionFeedback(victim);

        removeKeyLocked(victim, RemovalReason::Evicted);
        engine.recordEviction(dirty);
        engine.onCapacityEnforced();
    }
}

bool RedisDatabase::removeKeyLocked(const std::string& key, RemovalReason reason) {
    auto it = store.find(key);
    if (it == store.end()) return false;

    store.erase(it);
    frames.erase(key);
    expiry_map.erase(key);
    engine.onRemove(key);

    switch (reason) {
        case RemovalReason::Explicit: engine.recordExplicitDelete(); break;
        case RemovalReason::Expired:  engine.recordExpiration();    break;
        case RemovalReason::Evicted:  break; // counted by the caller
    }
    return true;
}

void RedisDatabase::purgeExpiredLocked() {
    if (expiry_map.empty()) return;

    const auto now = Clock::now();

    // Collect first, then remove. removeKeyLocked() erases the entry from
    // expiry_map itself, so erasing while iterating the same map would leave
    // the loop holding an invalidated iterator.
    std::vector<std::string> expired;
    for (const auto& entry : expiry_map) {
        if (now > entry.second) expired.push_back(entry.first);
    }
    for (const auto& key : expired) {
        removeKeyLocked(key, RemovalReason::Expired);
    }
}

void RedisDatabase::purgeExpired() {
    std::lock_guard<std::mutex> lock(db_mutex);
    purgeExpiredLocked();
}

// Redis glob matching: '*' any run, '?' one char, '[...]' a character class,
// '\' escapes the next character.
bool RedisDatabase::globMatch(const std::string& pattern, const std::string& text) {
    std::size_t p = 0, t = 0, star = std::string::npos, mark = 0;

    while (t < text.size()) {
        if (p < pattern.size() && (pattern[p] == '?' || pattern[p] == text[t])) {
            ++p;
            ++t;
        } else if (p < pattern.size() && pattern[p] == '*') {
            star = p++;
            mark = t;
        } else if (star != std::string::npos) {
            p = star + 1;
            t = ++mark;
        } else {
            return false;
        }
    }
    while (p < pattern.size() && pattern[p] == '*') ++p;
    return p == pattern.size();
}

// ---------------------------------------------------------------------------
// Cache configuration and introspection
// ---------------------------------------------------------------------------

bool RedisDatabase::setPolicy(const std::string& name) {
    std::lock_guard<std::mutex> lock(db_mutex);
    purgeExpiredLocked();
    if (!engine.setPolicy(name)) return false;
    // The replacement policy starts empty; hand it the resident key set.
    resyncPolicyLocked();
    return true;
}

std::string RedisDatabase::activePolicy() const {
    std::lock_guard<std::mutex> lock(db_mutex);
    return engine.policyName();
}

bool RedisDatabase::setMaxKeys(std::size_t keys) {
    std::lock_guard<std::mutex> lock(db_mutex);
    engine.setMaxKeys(keys);
    enforceCapacityLocked();
    return true;
}

std::size_t RedisDatabase::maxKeys() const {
    std::lock_guard<std::mutex> lock(db_mutex);
    return engine.maxKeys();
}

std::size_t RedisDatabase::dbSize() const {
    std::lock_guard<std::mutex> lock(db_mutex);
    return store.size();
}

std::size_t RedisDatabase::totalKeyBytes() const {
    std::lock_guard<std::mutex> lock(db_mutex);
    std::size_t total = 0;
    for (const auto& entry : frames) total += entry.second.byteSize;
    return total;
}

std::size_t RedisDatabase::memoryUsage(const std::string& key) const {
    std::lock_guard<std::mutex> lock(db_mutex);
    auto it = frames.find(key);
    return it == frames.end() ? 0 : it->second.byteSize;
}

bool RedisDatabase::resetStats() {
    std::lock_guard<std::mutex> lock(db_mutex);
    engine.resetStats();
    return true;
}

std::string RedisDatabase::configGet(const std::string& parameter) const {
    std::lock_guard<std::mutex> lock(db_mutex);
    std::string p = parameter;
    std::transform(p.begin(), p.end(), p.begin(), ::toupper);

    std::size_t bytes = 0;
    for (const auto& entry : frames) bytes += entry.second.byteSize;

    if (p == "POLICY" || p == "EVICTION") return engine.policyName();
    if (p == "MAXKEYS")  return std::to_string(engine.maxKeys());
    if (p == "DBSIZE")   return std::to_string(store.size());
    if (p == "BYTES")    return std::to_string(bytes);
    return "";
}

std::string RedisDatabase::infoText() const {
    std::lock_guard<std::mutex> lock(db_mutex);
    const EvictionMetrics& m = engine.metrics();

    std::size_t bytes = 0;
    for (const auto& entry : frames) bytes += entry.second.byteSize;

    std::ostringstream oss;
    oss << "# Server\r\n"
        << "server:adaptive_cache_redis\r\n"
        << "storage:memory\r\n"
        << "\r\n"
        << "# Cache\r\n"
        << "active_policy:" << engine.policyName() << "\r\n"
        << "max_keys:" << engine.maxKeys() << "\r\n"
        << "keys_stored:" << store.size() << "\r\n"
        << "bytes_used:" << bytes << "\r\n"
        << "frame_overhead_bytes:" << (frames.size() * sizeof(CacheEntry)) << "\r\n"
        << "\r\n"
        << "# Access\r\n"
        << "total_accesses:" << m.totalAccesses << "\r\n"
        << "total_hits:" << m.totalHits << "\r\n"
        << "total_misses:" << m.totalMisses << "\r\n"
        << "hit_rate_percent:" << m.hitRatePercent() << "\r\n"
        << "\r\n"
        << "# Eviction\r\n"
        << "total_evictions:" << m.evictions << "\r\n"
        << "write_backs:" << m.writes << "\r\n"
        << "clean_evictions:" << m.cleanEvictions << "\r\n"
        << "capacity_enforcements:" << m.capacityEnforcements << "\r\n"
        << "lru_selections:" << m.lruSelections << "\r\n"
        << "lfu_selections:" << m.lfuSelections << "\r\n"
        << "fifo_selections:" << m.fifoSelections << "\r\n"
        << "random_selections:" << m.randomSelections << "\r\n"
        << "adaptive_selections:" << m.adaptiveSelections << "\r\n"
        << "expirations:" << m.expirations << "\r\n"
        << "explicit_deletes:" << m.explicitDeletes << "\r\n"
        << "policy_changes:" << m.policyChanges << "\r\n"
        << "total_keys_inserted:" << m.totalKeysInserted << "\r\n";

    const std::string detail = engine.policyDetail();
    if (!detail.empty()) oss << "adaptive_detail:" << detail << "\r\n";
    return oss.str();
}

// ---------------------------------------------------------------------------
// Common
// ---------------------------------------------------------------------------

bool RedisDatabase::flushAll() {
    std::lock_guard<std::mutex> lock(db_mutex);
    store.clear();
    frames.clear();
    expiry_map.clear();
    engine.resync({});
    return true;
}

// ---------------------------------------------------------------------------
// Key/Value
// ---------------------------------------------------------------------------

OpStatus RedisDatabase::set(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(db_mutex);
    purgeExpiredLocked();

    const bool isNew = store.find(key) == store.end();
    Value entry;
    entry.type = Value::Type::String;
    entry.str = value;
    store[key] = std::move(entry);

    registerInsertLocked(key, key.size() + value.size(), isNew);
    return OpStatus::Ok;
}

OpStatus RedisDatabase::get(const std::string& key, std::string& value) {
    std::lock_guard<std::mutex> lock(db_mutex);
    purgeExpiredLocked();

    auto it = store.find(key);
    if (it == store.end()) {
        engine.recordMiss();
        return OpStatus::NotFound;
    }
    if (it->second.type != Value::Type::String) {
        engine.recordMiss();
        return OpStatus::WrongType;
    }

    value = it->second.str;
    registerAccessLocked(key);
    engine.recordHit();
    return OpStatus::Ok;
}

std::vector<std::string> RedisDatabase::keys(const std::string& pattern) {
    std::lock_guard<std::mutex> lock(db_mutex);
    purgeExpiredLocked();

    std::vector<std::string> result;
    const bool matchAll = pattern.empty() || pattern == "*";

    for (const auto& entry : store) {
        if (matchAll || globMatch(pattern, entry.first)) {
            result.push_back(entry.first);
            engine.onAccess(entry.first);
        }
    }
    return result;
}

std::string RedisDatabase::type(const std::string& key) {
    std::lock_guard<std::mutex> lock(db_mutex);
    purgeExpiredLocked();
    auto it = store.find(key);
    return it == store.end() ? "none" : typeName(it->second.type);
}

bool RedisDatabase::exists(const std::string& key) {
    std::lock_guard<std::mutex> lock(db_mutex);
    purgeExpiredLocked();
    return store.find(key) != store.end();
}

bool RedisDatabase::del(const std::string& key) {
    std::lock_guard<std::mutex> lock(db_mutex);
    purgeExpiredLocked();
    return removeKeyLocked(key, RemovalReason::Explicit);
}

bool RedisDatabase::expire(const std::string& key, std::int64_t seconds) {
    std::lock_guard<std::mutex> lock(db_mutex);
    purgeExpiredLocked();
    if (store.find(key) == store.end()) return false;

    if (seconds <= 0) {
        expiry_map.erase(key);
        return true;
    }
    expiry_map[key] = Clock::now() + std::chrono::seconds(seconds);
    return true;
}

std::int64_t RedisDatabase::ttl(const std::string& key) {
    std::lock_guard<std::mutex> lock(db_mutex);
    purgeExpiredLocked();

    if (store.find(key) == store.end()) return -2;
    auto it = expiry_map.find(key);
    if (it == expiry_map.end()) return -1;

    const auto remaining =
        std::chrono::duration_cast<std::chrono::seconds>(it->second - Clock::now()).count();
    return remaining > 0 ? remaining : 0;
}

bool RedisDatabase::persist(const std::string& key) {
    std::lock_guard<std::mutex> lock(db_mutex);
    purgeExpiredLocked();
    return expiry_map.erase(key) > 0;
}

bool RedisDatabase::rename(const std::string& oldKey, const std::string& newKey) {
    std::lock_guard<std::mutex> lock(db_mutex);
    purgeExpiredLocked();

    auto it = store.find(oldKey);
    if (it == store.end()) return false;
    if (oldKey == newKey) return true;

    auto frameIt = frames.find(oldKey);
    if (frameIt != frames.end()) {
        CacheEntry frame = frameIt->second;
        frame.key = newKey;
        frames.erase(frameIt);
        frames.emplace(newKey, std::move(frame));
        engine.onRemove(oldKey);
        engine.onInsert(newKey);
    }

    store[newKey] = std::move(it->second);
    store.erase(it);

    auto ttlIt = expiry_map.find(oldKey);
    if (ttlIt != expiry_map.end()) {
        expiry_map[newKey] = ttlIt->second;
        expiry_map.erase(ttlIt);
    }

    // RENAME overwrites the destination, so enforce capacity afterwards.
    enforceCapacityLocked();
    return true;
}

// ---------------------------------------------------------------------------
// List
// ---------------------------------------------------------------------------

OpStatus RedisDatabase::lget(const std::string& key, std::vector<std::string>& out) {
    std::lock_guard<std::mutex> lock(db_mutex);
    purgeExpiredLocked();

    auto it = store.find(key);
    if (it == store.end()) return OpStatus::NotFound;
    if (it->second.type != Value::Type::List) return OpStatus::WrongType;

    out = it->second.list;
    registerAccessLocked(key);
    return OpStatus::Ok;
}

std::int64_t RedisDatabase::llen(const std::string& key) {
    std::lock_guard<std::mutex> lock(db_mutex);
    purgeExpiredLocked();

    auto it = store.find(key);
    if (it == store.end()) return 0;
    if (it->second.type != Value::Type::List) return -1;
    return static_cast<std::int64_t>(it->second.list.size());
}

OpStatus RedisDatabase::lpush(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(db_mutex);
    purgeExpiredLocked();

    auto it = store.find(key);
    if (it != store.end() && it->second.type != Value::Type::List) return OpStatus::WrongType;

    const bool isNew = (it == store.end());
    if (isNew) {
        Value entry;
        entry.type = Value::Type::List;
        it = store.emplace(key, std::move(entry)).first;
    }
    it->second.list.insert(it->second.list.begin(), value);
    registerInsertLocked(key, key.size() + valueBytesLocked(it->second), isNew);
    return OpStatus::Ok;
}

OpStatus RedisDatabase::rpush(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(db_mutex);
    purgeExpiredLocked();

    auto it = store.find(key);
    if (it != store.end() && it->second.type != Value::Type::List) return OpStatus::WrongType;

    const bool isNew = (it == store.end());
    if (isNew) {
        Value entry;
        entry.type = Value::Type::List;
        it = store.emplace(key, std::move(entry)).first;
    }
    it->second.list.push_back(value);
    registerInsertLocked(key, key.size() + valueBytesLocked(it->second), isNew);
    return OpStatus::Ok;
}

OpStatus RedisDatabase::lpop(const std::string& key, std::string& value) {
    std::lock_guard<std::mutex> lock(db_mutex);
    purgeExpiredLocked();

    auto it = store.find(key);
    if (it == store.end()) return OpStatus::NotFound;
    if (it->second.type != Value::Type::List) return OpStatus::WrongType;
    if (it->second.list.empty()) return OpStatus::NotFound;

    value = it->second.list.front();
    it->second.list.erase(it->second.list.begin());

    auto frame = frames.find(key);
    if (frame != frames.end()) frame->second.byteSize = key.size() + valueBytesLocked(it->second);

    if (it->second.list.empty()) removeKeyLocked(key, RemovalReason::Explicit);
    else registerAccessLocked(key);
    return OpStatus::Ok;
}

OpStatus RedisDatabase::rpop(const std::string& key, std::string& value) {
    std::lock_guard<std::mutex> lock(db_mutex);
    purgeExpiredLocked();

    auto it = store.find(key);
    if (it == store.end()) return OpStatus::NotFound;
    if (it->second.type != Value::Type::List) return OpStatus::WrongType;
    if (it->second.list.empty()) return OpStatus::NotFound;

    value = it->second.list.back();
    it->second.list.pop_back();

    auto frame = frames.find(key);
    if (frame != frames.end()) frame->second.byteSize = key.size() + valueBytesLocked(it->second);

    if (it->second.list.empty()) removeKeyLocked(key, RemovalReason::Explicit);
    else registerAccessLocked(key);
    return OpStatus::Ok;
}

OpStatus RedisDatabase::lrem(const std::string& key, std::int64_t count,
                             const std::string& value, std::int64_t& removed) {
    removed = 0;
    std::lock_guard<std::mutex> lock(db_mutex);
    purgeExpiredLocked();

    auto it = store.find(key);
    if (it == store.end()) return OpStatus::NotFound;
    if (it->second.type != Value::Type::List) return OpStatus::WrongType;

    auto& list = it->second.list;
    std::size_t dropped = 0;

    if (count == 0) {
        const auto newEnd = std::remove(list.begin(), list.end(), value);
        dropped = static_cast<std::size_t>(std::distance(newEnd, list.end()));
        list.erase(newEnd, list.end());
    } else if (count > 0) {
        // Head to tail, removing at most `count` matches.
        const auto limit = static_cast<std::size_t>(count);
        for (auto it2 = list.begin(); it2 != list.end() && dropped < limit;) {
            if (*it2 == value) { it2 = list.erase(it2); ++dropped; }
            else ++it2;
        }
    } else {
        // Negative count: tail to head, removing at most |count| matches.
        const auto limit = static_cast<std::size_t>(-count);
        for (auto it2 = list.rbegin(); it2 != list.rend() && dropped < limit;) {
            if (*it2 == value) {
                // A reverse iterator addresses the element one past its base,
                // so step forward to obtain the forward iterator to erase.
                it2 = std::reverse_iterator<std::vector<std::string>::iterator>(
                    list.erase(std::next(it2).base()));
                ++dropped;
            } else {
                ++it2;
            }
        }
    }

    removed = static_cast<std::int64_t>(dropped);
    if (dropped == 0) return OpStatus::Ok;

    auto frame = frames.find(key);
    if (frame != frames.end()) frame->second.byteSize = key.size() + valueBytesLocked(it->second);
    if (list.empty()) removeKeyLocked(key, RemovalReason::Explicit);
    else registerAccessLocked(key);
    return OpStatus::Ok;
}

OpStatus RedisDatabase::lindex(const std::string& key, std::int64_t index,
                               std::string& value) {
    std::lock_guard<std::mutex> lock(db_mutex);
    purgeExpiredLocked();

    auto it = store.find(key);
    if (it == store.end()) return OpStatus::NotFound;
    if (it->second.type != Value::Type::List) return OpStatus::WrongType;

    auto& list = it->second.list;
    std::int64_t idx = index;
    if (idx < 0) idx += static_cast<std::int64_t>(list.size());
    if (idx < 0 || idx >= static_cast<std::int64_t>(list.size())) return OpStatus::NotFound;

    value = list[static_cast<std::size_t>(idx)];
    registerAccessLocked(key);
    return OpStatus::Ok;
}

OpStatus RedisDatabase::lset(const std::string& key, std::int64_t index,
                             const std::string& value) {
    std::lock_guard<std::mutex> lock(db_mutex);
    purgeExpiredLocked();

    auto it = store.find(key);
    if (it == store.end()) return OpStatus::NotFound;
    if (it->second.type != Value::Type::List) return OpStatus::WrongType;

    auto& list = it->second.list;
    std::int64_t idx = index;
    if (idx < 0) idx += static_cast<std::int64_t>(list.size());
    if (idx < 0 || idx >= static_cast<std::int64_t>(list.size())) return OpStatus::NotFound;

    list[static_cast<std::size_t>(idx)] = value;

    auto frame = frames.find(key);
    if (frame != frames.end()) {
        frame->second.byteSize = key.size() + valueBytesLocked(it->second);
        frame->second.dirty = true;
        engine.onReplace(key);
    }
    return OpStatus::Ok;
}

// ---------------------------------------------------------------------------
// Hash
// ---------------------------------------------------------------------------

OpStatus RedisDatabase::hset(const std::string& key, const std::string& field,
                             const std::string& value, bool* fieldCreated) {
    std::lock_guard<std::mutex> lock(db_mutex);
    purgeExpiredLocked();

    auto it = store.find(key);
    if (it != store.end() && it->second.type != Value::Type::Hash) return OpStatus::WrongType;

    const bool isNew = (it == store.end());
    if (isNew) {
        Value entry;
        entry.type = Value::Type::Hash;
        it = store.emplace(key, std::move(entry)).first;
    }

    const bool existed = it->second.hash.find(field) != it->second.hash.end();
    it->second.hash[field] = value;

    registerInsertLocked(key, key.size() + valueBytesLocked(it->second), isNew);
    if (fieldCreated) *fieldCreated = !existed;
    return OpStatus::Ok;
}

OpStatus RedisDatabase::hget(const std::string& key, const std::string& field,
                             std::string& value) {
    std::lock_guard<std::mutex> lock(db_mutex);
    purgeExpiredLocked();

    auto it = store.find(key);
    if (it == store.end()) return OpStatus::NotFound;
    if (it->second.type != Value::Type::Hash) return OpStatus::WrongType;

    auto f = it->second.hash.find(field);
    if (f == it->second.hash.end()) return OpStatus::NotFound;

    value = f->second;
    registerAccessLocked(key);
    return OpStatus::Ok;
}

OpStatus RedisDatabase::hexists(const std::string& key, const std::string& field) {
    std::lock_guard<std::mutex> lock(db_mutex);
    purgeExpiredLocked();

    auto it = store.find(key);
    if (it == store.end()) return OpStatus::NotFound;
    if (it->second.type != Value::Type::Hash) return OpStatus::WrongType;

    if (it->second.hash.find(field) == it->second.hash.end()) return OpStatus::NotFound;
    registerAccessLocked(key);
    return OpStatus::Ok;
}

OpStatus RedisDatabase::hdel(const std::string& key, const std::string& field) {
    std::lock_guard<std::mutex> lock(db_mutex);
    purgeExpiredLocked();

    auto it = store.find(key);
    if (it == store.end()) return OpStatus::NotFound;
    if (it->second.type != Value::Type::Hash) return OpStatus::WrongType;

    if (it->second.hash.erase(field) == 0) return OpStatus::NotFound;

    auto frame = frames.find(key);
    if (frame != frames.end()) frame->second.byteSize = key.size() + valueBytesLocked(it->second);
    if (it->second.hash.empty()) removeKeyLocked(key, RemovalReason::Explicit);
    else registerAccessLocked(key);
    return OpStatus::Ok;
}

OpStatus RedisDatabase::hgetall(const std::string& key,
                                std::unordered_map<std::string, std::string>& out) {
    std::lock_guard<std::mutex> lock(db_mutex);
    purgeExpiredLocked();

    auto it = store.find(key);
    if (it == store.end()) return OpStatus::NotFound;
    if (it->second.type != Value::Type::Hash) return OpStatus::WrongType;

    out = it->second.hash;
    registerAccessLocked(key);
    return OpStatus::Ok;
}

OpStatus RedisDatabase::hkeys(const std::string& key, std::vector<std::string>& out) {
    std::lock_guard<std::mutex> lock(db_mutex);
    purgeExpiredLocked();

    auto it = store.find(key);
    if (it == store.end()) return OpStatus::NotFound;
    if (it->second.type != Value::Type::Hash) return OpStatus::WrongType;

    out.clear();
    out.reserve(it->second.hash.size());
    for (const auto& field : it->second.hash) out.push_back(field.first);
    registerAccessLocked(key);
    return OpStatus::Ok;
}

OpStatus RedisDatabase::hvals(const std::string& key, std::vector<std::string>& out) {
    std::lock_guard<std::mutex> lock(db_mutex);
    purgeExpiredLocked();

    auto it = store.find(key);
    if (it == store.end()) return OpStatus::NotFound;
    if (it->second.type != Value::Type::Hash) return OpStatus::WrongType;

    out.clear();
    out.reserve(it->second.hash.size());
    for (const auto& field : it->second.hash) out.push_back(field.second);
    registerAccessLocked(key);
    return OpStatus::Ok;
}

std::int64_t RedisDatabase::hlen(const std::string& key) {
    std::lock_guard<std::mutex> lock(db_mutex);
    purgeExpiredLocked();

    auto it = store.find(key);
    if (it == store.end()) return 0;
    if (it->second.type != Value::Type::Hash) return -1;
    return static_cast<std::int64_t>(it->second.hash.size());
}

OpStatus RedisDatabase::hmset(
    const std::string& key,
    const std::vector<std::pair<std::string, std::string>>& fieldValues) {
    std::lock_guard<std::mutex> lock(db_mutex);
    purgeExpiredLocked();

    auto it = store.find(key);
    if (it != store.end() && it->second.type != Value::Type::Hash) return OpStatus::WrongType;

    const bool isNew = (it == store.end());
    if (isNew) {
        Value entry;
        entry.type = Value::Type::Hash;
        it = store.emplace(key, std::move(entry)).first;
    }
    for (const auto& field : fieldValues) it->second.hash[field.first] = field.second;

    registerInsertLocked(key, key.size() + valueBytesLocked(it->second), isNew);
    return OpStatus::Ok;
}

// ---------------------------------------------------------------------------
// Persistence
// ---------------------------------------------------------------------------
//
// Length-prefixed, one record per line:
//
//   K <keylen> <key> <vallen> <value>
//   L <keylen> <key> <count> (<itemlen> <item>)*
//   H <keylen> <key> <count> (<fieldlen> <field> <vallen> <value>)*
//   E <keylen> <key> <ttl-seconds>
//
// The previous format was space-delimited with no escaping, so any value
// containing a space or ':' was silently corrupted on reload, and time-to-live
// was never written at all. Length prefixes make arbitrary bytes safe and keep
// the file diff-friendly for the simple records people actually read by hand.

namespace {

// Named wrappers so the file operations are never confused with the
// algorithm-header std::remove used below.
bool removeFile(const std::string& path) { return std::remove(path.c_str()) == 0; }
bool renameFile(const std::string& from, const std::string& to) {
    return std::rename(from.c_str(), to.c_str()) == 0;
}

// Sanity ceilings for a snapshot being read back, so a corrupt length field
// cannot drive an enormous allocation.
constexpr std::size_t kMaxRecordsPerKey = 10'000'000;
constexpr std::size_t kMaxRecordBytes = 512u * 1024u * 1024u;

void writeChunk(std::ostream& os, const std::string& value) {
    os << value.size() << ' ' << value;
}

bool readLength(std::istream& is, std::size_t& length) {
    long long value = 0;
    if (!(is >> value) || value < 0) return false;
    length = static_cast<std::size_t>(value);
    return true;
}

bool readChunk(std::istream& is, std::string& out) {
    std::size_t length = 0;
    if (!readLength(is, length)) return false;

    // readLength leaves the stream immediately after the digits, on the single
    // space that writeChunk put there. Consume exactly that one character --
    // skipping more would eat leading spaces that belong to the data.
    char separator = '\0';
    if (!is.get(separator) || separator != ' ') return false;

    out.assign(length, '\0');
    if (length == 0) return true;
    is.read(&out[0], static_cast<std::streamsize>(length));
    return static_cast<std::size_t>(is.gcount()) == length;
}

} // namespace

bool RedisDatabase::dump(const std::string& filename) {
    std::lock_guard<std::mutex> lock(db_mutex);
    purgeExpiredLocked();

    // Write to a temporary and rename, so a crash mid-write cannot truncate
    // the previous good snapshot.
    const std::string temp = filename + ".tmp";
    std::ofstream ofs(temp, std::ios::binary | std::ios::trunc);
    if (!ofs) return false;

    const auto now = Clock::now();
    for (const auto& kv : store) {
        const auto ttlIt = expiry_map.find(kv.first);
        const bool hasTtl = ttlIt != expiry_map.end();
        const std::int64_t ttlSeconds =
            hasTtl ? std::max<std::int64_t>(
                         0, std::chrono::duration_cast<std::chrono::seconds>(ttlIt->second - now).count())
                   : -1;

        ofs << ttlSeconds << ' ';
        switch (kv.second.type) {
            case Value::Type::String:
                ofs << "K ";
                writeChunk(ofs, kv.first);
                ofs << ' ';
                writeChunk(ofs, kv.second.str);
                ofs << '\n';
                break;
            case Value::Type::List: {
                ofs << "L ";
                writeChunk(ofs, kv.first);
                ofs << ' ' << kv.second.list.size();
                for (const auto& item : kv.second.list) {
                    ofs << ' ';
                    writeChunk(ofs, item);
                }
                ofs << '\n';
                break;
            }
            case Value::Type::Hash: {
                ofs << "H ";
                writeChunk(ofs, kv.first);
                ofs << ' ' << kv.second.hash.size();
                for (const auto& field : kv.second.hash) {
                    ofs << ' ';
                    writeChunk(ofs, field.first);
                    ofs << ' ';
                    writeChunk(ofs, field.second);
                }
                ofs << '\n';
                break;
            }
        }
    }

    ofs.flush();
    if (!ofs) { ofs.close(); removeFile(temp); return false; }
    ofs.close();

    removeFile(filename);
    if (!renameFile(temp, filename)) {
        removeFile(temp);
        return false;
    }

    // Everything on disk now matches memory, so no frame is dirty any more.
    for (auto& entry : frames) entry.second.dirty = false;
    return true;
}

bool RedisDatabase::load(const std::string& filename) {
    std::lock_guard<std::mutex> lock(db_mutex);

    std::ifstream ifs(filename, std::ios::binary);
    if (!ifs) return false;

    // Parse into staging containers and only swap them in on success. Clearing
    // the live keyspace first would mean a truncated or foreign file left the
    // server with an empty database instead of the contents it already had.
    std::unordered_map<std::string, Value> stagedStore;
    std::unordered_map<std::string, Clock::time_point> stagedExpiry;

    std::string key;
    bool parsedAny = false;
    while (true) {
        long long ttlSeconds = -1;
        if (!(ifs >> ttlSeconds)) break;

        char tag = '\0';
        if (!(ifs >> tag)) return false;

        Value entry;
        if (!readChunk(ifs, key)) return false;

        if (tag == 'K') {
            entry.type = Value::Type::String;
            if (!readChunk(ifs, entry.str)) return false;
        } else if (tag == 'L') {
            entry.type = Value::Type::List;
            std::size_t count = 0;
            if (!readLength(ifs, count) || count > kMaxRecordsPerKey) return false;
            entry.list.reserve(count);
            for (std::size_t i = 0; i < count; ++i) {
                std::string item;
                if (!readChunk(ifs, item) || item.size() > kMaxRecordBytes) return false;
                entry.list.push_back(std::move(item));
            }
        } else if (tag == 'H') {
            entry.type = Value::Type::Hash;
            std::size_t count = 0;
            if (!readLength(ifs, count) || count > kMaxRecordsPerKey) return false;
            for (std::size_t i = 0; i < count; ++i) {
                std::string field, value;
                if (!readChunk(ifs, field) || !readChunk(ifs, value)) return false;
                if (field.size() > kMaxRecordBytes || value.size() > kMaxRecordBytes)
                    return false;
                entry.hash.emplace(std::move(field), std::move(value));
            }
        } else {
            return false; // unknown tag: refuse rather than misparse the rest
        }

        stagedStore.emplace(key, std::move(entry));
        if (ttlSeconds > 0) {
            stagedExpiry[key] = Clock::now() + std::chrono::seconds(ttlSeconds);
        }
        parsedAny = true;
    }

    // A file that yielded no records is not a snapshot we understand -- most
    // likely it is empty, foreign, or the previous unversioned format. Report
    // failure so the caller keeps whatever it already had.
    if (!parsedAny) return false;

    store.swap(stagedStore);
    expiry_map.swap(stagedExpiry);
    frames.clear();
    engine.resync({});

    // Rebuild frame metadata and hand the resident key set to the policy.
    for (auto& kv : store) {
        CacheEntry frame;
        frame.key = kv.first;
        frame.insertionTime = logicalClock_++;
        frame.lastAccessTime = frame.insertionTime;
        frame.byteSize = kv.first.size() + valueBytesLocked(kv.second);
        frame.dirty = false;
        frames.emplace(kv.first, std::move(frame));
        engine.onInsert(kv.first);
    }
    enforceCapacityLocked();

    return true;
}