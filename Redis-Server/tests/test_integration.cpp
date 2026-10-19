// Integration tests: command surface, type safety, eviction, persistence, and
// the RESP wire protocol against a live server socket.
#include "../include/RedisCommandHandler.h"
#include "../include/RedisDatabase.h"
#include "../include/RedisServer.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

int g_failures = 0;
int g_checks = 0;
std::string g_currentTest;
RedisCommandHandler g_handler;

void check(bool condition, const std::string& what, int line) {
    ++g_checks;
    if (condition) return;
    ++g_failures;
    std::cout << "  FAIL [" << g_currentTest << ":" << line << "] " << what << "\n";
}

#define CHECK(cond) check((cond), #cond, __LINE__)
#define CHECK_EQ(actual, expected)                                                    \
    do {                                                                              \
        const auto a_ = (actual);                                                      \
        const auto e_ = (expected);                                                    \
        ++g_checks;                                                                    \
        if (!(a_ == e_)) {                                                             \
            ++g_failures;                                                               \
            std::cout << "  FAIL [" << g_currentTest << ":" << __LINE__ << "] "         \
                      << #actual << "\n        got:      [" << a_                       \
                      << "]\n        expected: [" << e_ << "]\n";                        \
        }                                                                              \
    } while (false)

struct TestScope {
    explicit TestScope(const std::string& name) {
        g_currentTest = name;
        std::cout << "- " << name << std::endl; // flush, so a hang is locatable
    }
};

// Sends one inline command and returns the raw RESP reply.
std::string exec(const std::string& line) {
    return g_handler.processCommand(line + "\r\n");
}

// Sends one multibulk command and returns the raw RESP reply.
std::string execArgs(const std::vector<std::string>& parts) {
    std::string buffer = "*" + std::to_string(parts.size()) + "\r\n";
    for (const auto& part : parts) {
        buffer += "$" + std::to_string(part.size()) + "\r\n" + part + "\r\n";
    }
    std::size_t consumed = 0;
    bool closeAfter = false;
    return g_handler.processCommand(buffer, consumed, closeAfter);
}

// Extracts a bulk string payload, or returns the whole reply if it is not one.
std::string bulkValue(const std::string& reply) {
    if (reply.size() < 2 || reply[0] != '$') return reply;
    const std::size_t crlf = reply.find("\r\n");
    if (crlf == std::string::npos) return reply;
    const std::size_t length = std::stoul(reply.substr(1, crlf - 1));
    return reply.substr(crlf + 2, length);
}

// Strips a trailing CRLF for readable comparisons.
std::string trim(const std::string& text) {
    std::string out = text;
    while (!out.empty() && (out.back() == '\r' || out.back() == '\n')) out.pop_back();
    return out;
}

std::string simpleValue(const std::string& reply) {
    const std::string body = trim(reply);
    if (!body.empty() && body[0] == '+') return body.substr(1);
    return body;
}

std::string intValue(const std::string& reply) { return trim(reply).substr(1); }

// Counts the elements of a RESP array reply.
std::size_t countBulkElements(const std::string& reply) {
    const std::string body = trim(reply);
    if (body.empty() || body[0] != '*') return 0;
    return static_cast<std::size_t>(std::stol(body.substr(1)));
}

RedisDatabase& db() { return RedisDatabase::getInstance(); }

void resetAll(const std::string& policy = "LRU", std::size_t maxKeys = 0) {
    db().flushAll();
    db().setMaxKeys(maxKeys);
    db().setPolicy(policy);
    db().resetStats();
}

// ---------------------------------------------------------------------------
// Command surface
// ---------------------------------------------------------------------------

void testStringCommands() {
    TestScope scope("String commands");

    CHECK_EQ(simpleValue(exec("PING")), std::string("PONG"));
    CHECK_EQ(simpleValue(exec("ping")), std::string("PONG"));
    CHECK_EQ(simpleValue(exec("ECHO hello")), std::string("hello"));
    CHECK_EQ(simpleValue(exec("SET foo bar")), std::string("OK"));
    CHECK_EQ(bulkValue(exec("GET foo")), std::string("bar"));

    // A miss must be RESP nil, not an error and not an empty string.
    CHECK_EQ(trim(exec("GET nosuchkey")), std::string("$-1"));

    CHECK_EQ(simpleValue(exec("TYPE foo")), std::string("string"));
    CHECK_EQ(simpleValue(exec("TYPE nosuchkey")), std::string("none"));

    CHECK_EQ(intValue(exec("EXISTS foo")), std::string("1"));
    CHECK_EQ(intValue(exec("EXISTS nosuchkey")), std::string("0"));

    // DEL previously always reported 0 because it computed `erased` and then
    // returned a hardcoded false.
    CHECK_EQ(intValue(exec("DEL foo")), std::string("1"));
    CHECK_EQ(intValue(exec("DEL foo")), std::string("0"));
    CHECK_EQ(trim(exec("GET foo")), std::string("$-1"));

    // Multi-key DEL counts each removal.
    exec("SET m1 a");
    exec("SET m2 b");
    CHECK_EQ(intValue(exec("DEL m1 m2 nosuchkey")), std::string("2"));

    // SET options.
    exec("SET opt plain");
    CHECK_EQ(simpleValue(exec("SET opt ignored NX")), std::string("$-1"));
    CHECK_EQ(bulkValue(exec("GET opt")), std::string("plain"));
    CHECK_EQ(simpleValue(exec("SET opt new XX")), std::string("OK"));
    CHECK_EQ(bulkValue(exec("GET opt")), std::string("new"));
    CHECK_EQ(simpleValue(exec("SET fresh v EX 50")), std::string("OK"));
    CHECK(intValue(exec("TTL fresh")) != std::string(""));

    CHECK_EQ(trim(exec("BOGUSCOMMAND")).substr(0, 5), std::string("-ERR "));
}

void testKeysPattern() {
    TestScope scope("KEYS honours its pattern argument");

    exec("FLUSHALL");
    exec("SET stock:AAPL 1");
    exec("SET stock:MSFT 2");
    exec("SET bench:T0001 3");
    exec("SET other 4");

    const std::string all = exec("KEYS *");
    CHECK_EQ(countBulkElements(all), std::size_t(4));

    // The previous handler ignored the pattern and returned every key, so a
    // prefix scan returned unrelated keys too.
    const std::string stocks = exec("KEYS stock:*");
    CHECK_EQ(countBulkElements(stocks), std::size_t(2));
    CHECK(stocks.find("stock:AAPL") != std::string::npos);
    CHECK(stocks.find("stock:MSFT") != std::string::npos);
    CHECK(stocks.find("bench:T0001") == std::string::npos);
    CHECK(stocks.find("other") == std::string::npos);

    CHECK_EQ(countBulkElements(exec("KEYS stock:????")), std::size_t(2));
    CHECK_EQ(countBulkElements(exec("KEYS stock:?????")), std::size_t(0));
    CHECK_EQ(countBulkElements(exec("KEYS nothing:*")), std::size_t(0));
    CHECK_EQ(countBulkElements(exec("KEYS other")), std::size_t(1));
}

void testTypeSafety() {
    TestScope scope("One type per key");

    exec("FLUSHALL");
    exec("SET skey value");
    exec("RPUSH mylist a b c");
    exec("HSET myhash f1 v1 f2 v2");

    CHECK_EQ(simpleValue(exec("TYPE skey")), std::string("string"));
    CHECK_EQ(simpleValue(exec("TYPE mylist")), std::string("list"));
    CHECK_EQ(simpleValue(exec("TYPE myhash")), std::string("hash"));

    // Operations against the wrong type must be refused, not silently applied.
    CHECK(trim(exec("GET mylist")).find("wrongtype") != std::string::npos);
    CHECK(trim(exec("LPUSH skey x")).find("wrongtype") != std::string::npos);
    CHECK(trim(exec("HGET skey f")).find("wrongtype") != std::string::npos);
    CHECK(trim(exec("HGETALL mylist")).find("wrongtype") != std::string::npos);

    // A key must appear exactly once in KEYS even though it has one type.
    CHECK_EQ(countBulkElements(exec("KEYS *")), std::size_t(3));

    // The failed operations must not have changed the value.
    CHECK_EQ(bulkValue(exec("GET skey")), std::string("value"));
    CHECK_EQ(intValue(exec("LLEN mylist")), std::string("3"));
}

void testListCommands() {
    TestScope scope("List commands");

    exec("FLUSHALL");
    CHECK_EQ(intValue(exec("RPUSH l a b c d")), std::string("4"));
    CHECK_EQ(intValue(exec("LPUSH l z")), std::string("5"));
    CHECK_EQ(bulkValue(exec("LINDEX l 0")), std::string("z"));
    CHECK_EQ(bulkValue(exec("LINDEX l -1")), std::string("d"));
    CHECK_EQ(trim(exec("LINDEX l 99")), std::string("$-1"));

    CHECK_EQ(simpleValue(exec("LSET l 1 B")), std::string("OK"));
    CHECK_EQ(bulkValue(exec("LINDEX l 1")), std::string("B"));
    CHECK(trim(exec("LSET l 99 x")).find("out of range") != std::string::npos);

    CHECK_EQ(bulkValue(exec("LPOP l")), std::string("z"));
    CHECK_EQ(bulkValue(exec("RPOP l")), std::string("d"));

    // LREM with positive, zero and negative counts.
    exec("FLUSHALL");
    exec("RPUSH m x y x z x");
    CHECK_EQ(intValue(exec("LREM m 2 x")), std::string("2"));   // head-to-tail
    CHECK_EQ(intValue(exec("LREM m 0 x")), std::string("1"));   // all remaining
    CHECK_EQ(intValue(exec("LREM m -1 y")), std::string("1"));  // tail-to-head
    CHECK_EQ(countBulkElements(exec("LGET m")), std::size_t(1));

    // Emptying a list removes the key entirely, as Redis does.
    exec("FLUSHALL");
    exec("RPUSH one a");
    exec("LPOP one");
    CHECK_EQ(simpleValue(exec("TYPE one")), std::string("none"));
    CHECK_EQ(countBulkElements(exec("LGET one")), std::size_t(0));
}

void testHashCommands() {
    TestScope scope("Hash commands");

    exec("FLUSHALL");
    CHECK_EQ(intValue(exec("HSET h name Alice age 30")), std::string("2")); // both new
    CHECK_EQ(intValue(exec("HSET h name Alice2")), std::string("0"));       // existing
    CHECK_EQ(bulkValue(exec("HGET h name")), std::string("Alice2"));
    CHECK_EQ(intValue(exec("HLEN h")), std::string("2"));
    CHECK_EQ(countBulkElements(exec("HKEYS h")), std::size_t(2));
    CHECK_EQ(countBulkElements(exec("HVALS h")), std::size_t(2));
    CHECK_EQ(countBulkElements(exec("HGETALL h")), std::size_t(4)); // flat field/value
    CHECK_EQ(intValue(exec("HEXISTS h name")), std::string("1"));
    CHECK_EQ(intValue(exec("HEXISTS h nope")), std::string("0"));
    CHECK_EQ(intValue(exec("HDEL h name")), std::string("1"));
    CHECK_EQ(intValue(exec("HDEL h name")), std::string("0"));

    exec("FLUSHALL");
    exec("HMSET u1 name Bob age 25");
    CHECK_EQ(intValue(exec("HLEN u1")), std::string("2"));
    CHECK_EQ(bulkValue(exec("HGET u1 age")), std::string("25"));

    // Removing the last field removes the key.
    exec("HDEL u1 name");
    exec("HDEL u1 age");
    CHECK_EQ(simpleValue(exec("TYPE u1")), std::string("none"));
}

void testExpiry() {
    TestScope scope("Expiry and TTL");

    exec("FLUSHALL");
    exec("SET s v");
    CHECK_EQ(intValue(exec("TTL s")), std::string("-1"));      // exists, no TTL
    CHECK_EQ(intValue(exec("TTL nosuchkey")), std::string("-2")); // no such key

    exec("EXPIRE s 100");
    const std::string ttl = intValue(exec("TTL s"));
    CHECK(ttl != "-1" && ttl != "-2");
    CHECK(std::stoll(ttl) > 90 && std::stoll(ttl) <= 100);

    CHECK_EQ(intValue(exec("PERSIST s")), std::string("1"));
    CHECK_EQ(intValue(exec("TTL s")), std::string("-1"));
    CHECK_EQ(intValue(exec("EXPIRE nosuchkey 10")), std::string("0"));

    // A short TTL must actually reap the key.
    exec("SET gone v");
    exec("EXPIRE gone 1");
    std::this_thread::sleep_for(std::chrono::milliseconds(1200));
    CHECK_EQ(trim(exec("GET gone")), std::string("$-1"));
    CHECK_EQ(countBulkElements(exec("KEYS *")), std::size_t(1)); // only 's' remains
}

// ---------------------------------------------------------------------------
// Eviction
// ---------------------------------------------------------------------------

void testEvictionEnforced() {
    TestScope scope("Capacity limit evicts using the active policy");

    resetAll("LRU", 3);
    exec("SET a 1");
    exec("SET b 2");
    exec("SET c 3");
    CHECK_EQ(intValue(exec("DBSIZE")), std::string("3"));

    // Touch 'a' so it is the most recent; 'b' becomes the LRU victim.
    exec("GET a");
    exec("SET d 4");

    CHECK_EQ(intValue(exec("DBSIZE")), std::string("3"));
    CHECK_EQ(trim(exec("GET b")), std::string("$-1")); // evicted
    CHECK_EQ(bulkValue(exec("GET a")), std::string("1"));
    CHECK_EQ(bulkValue(exec("GET d")), std::string("4"));

    const std::string info = exec("INFO");
    CHECK(info.find("total_evictions:1") != std::string::npos);
    CHECK(info.find("lru_selections:1") != std::string::npos);
}

void testEvictionCountsEveryPolicy() {
    TestScope scope("Every policy enforces the same capacity limit");

    for (const std::string policy : {"LRU", "LFU", "FIFO", "RANDOM", "ADAPTIVE"}) {
        resetAll(policy, 4);
        for (int i = 0; i < 40; ++i) {
            exec("SET k" + std::to_string(i) + " v");
        }
        CHECK_EQ(intValue(exec("DBSIZE")), std::string("4"));

        const std::string info = exec("INFO");
        CHECK(info.find("active_policy:" + policy) != std::string::npos);
        // 40 inserts into a 4-key cache means 36 evictions.
        CHECK(info.find("total_evictions:36") != std::string::npos);
    }
}

void testUnlimitedCapacity() {
    TestScope scope("Zero capacity never evicts");

    resetAll("LRU", 0);
    for (int i = 0; i < 200; ++i) exec("SET k" + std::to_string(i) + " v");
    CHECK_EQ(intValue(exec("DBSIZE")), std::string("200"));
    CHECK(exec("INFO").find("total_evictions:0") != std::string::npos);
}

void testPolicySwitchKeepsResidentSet() {
    TestScope scope("Switching policy preserves the resident key set");

    resetAll("LRU", 3);
    exec("SET a 1");
    exec("SET b 2");
    exec("SET c 3");

    // Without a resync the new policy would know about nothing, nominate no
    // victim, and the cache would silently grow past its limit.
    CHECK_EQ(simpleValue(exec("CONFIG SET POLICY FIFO")), std::string("OK"));
    CHECK_EQ(simpleValue(exec("EVICTION")), std::string("FIFO"));

    exec("SET d 4");
    CHECK_EQ(intValue(exec("DBSIZE")), std::string("3"));
    // FIFO evicts the oldest insert, which is 'a'.
    CHECK_EQ(trim(exec("GET a")), std::string("$-1"));

    CHECK(trim(exec("CONFIG SET POLICY NOPE")).find("unknown policy") != std::string::npos);
    CHECK_EQ(simpleValue(exec("EVICTION")), std::string("FIFO")); // unchanged
}

void testShrinkingCapacityEvictsImmediately() {
    TestScope scope("Lowering the limit evicts down to it");

    resetAll("FIFO", 0);
    for (int i = 0; i < 10; ++i) exec("SET k" + std::to_string(i) + " v");
    CHECK_EQ(intValue(exec("DBSIZE")), std::string("10"));

    exec("MAXKEYS 4");
    CHECK_EQ(intValue(exec("DBSIZE")), std::string("4"));
}

void testMetricsAndInfo() {
    TestScope scope("INFO and CONFIG report live state");

    resetAll("LRU", 0);
    exec("SET m 1");
    exec("GET m");
    exec("GET m");
    exec("GET missing");

    const std::string info = exec("INFO");
    CHECK(info.find("active_policy:LRU") != std::string::npos);
    CHECK(info.find("keys_stored:1") != std::string::npos);
    CHECK(info.find("total_hits:2") != std::string::npos);
    CHECK(info.find("total_misses:1") != std::string::npos);
    // Integer percentage: 2 hits / 3 lookups.
    CHECK(info.find("hit_rate_percent:66") != std::string::npos);
    CHECK(info.find("bytes_used:") != std::string::npos);

    CHECK_EQ(bulkValue(exec("CONFIG GET POLICY")), std::string("LRU"));
    CHECK_EQ(bulkValue(exec("CONFIG GET MAXKEYS")), std::string("0"));
    CHECK(trim(exec("CONFIG GET NOPE")).find("unknown configuration") != std::string::npos);
    CHECK_EQ(simpleValue(exec("CONFIG RESETSTAT")), std::string("OK"));
    CHECK(exec("INFO").find("total_hits:0") != std::string::npos);

    CHECK(intValue(exec("MEMORY USAGE m")) != std::string("0"));
    CHECK_EQ(trim(exec("MEMORY USAGE nosuchkey")), std::string("$-1"));

    // Legacy single-word spellings used by the Streamlit UI.
    CHECK_EQ(intValue(exec("MAXKEYS")), std::string("0"));
    CHECK_EQ(simpleValue(exec("MAXKEYS 12")), std::string("OK"));
    CHECK_EQ(intValue(exec("MAXKEYS")), std::string("12"));
}

void testAdaptiveDetail() {
    TestScope scope("INFO exposes adaptive controller state");

    resetAll("ADAPTIVE", 5);
    for (int i = 0; i < 60; ++i) exec("SET k" + std::to_string(i % 12) + " v");
    const std::string info = exec("INFO");
    CHECK(info.find("adaptive_detail:") != std::string::npos);
    CHECK(info.find("lru_weight=") != std::string::npos);
    CHECK(info.find("preferred_expert=") != std::string::npos);

    // The static policies have no controller state to report.
    resetAll("LRU", 0);
    CHECK(exec("INFO").find("adaptive_detail:") == std::string::npos);
}

// ---------------------------------------------------------------------------
// Persistence
// ---------------------------------------------------------------------------

void testPersistenceRoundTrip() {
    TestScope scope("Snapshot round-trips awkward payloads and TTLs");

    const std::string path = "build/test_snapshot.my_rdb";
    std::remove(path.c_str());

    // An earlier test left a capacity limit in place; nine keys would not fit.
    resetAll("LRU", 0);
    // Every one of these broke the previous space-delimited format.
    execArgs({"SET", "plain", "simple"});
    execArgs({"SET", "spaced", "has spaces inside"});
    execArgs({"SET", "colons", "has:colon:inside"});
    execArgs({"SET", "newlines", "line1\nline2\r\nline3"});
    execArgs({"SET", "trailing", "ends with space "});
    execArgs({"SET", "numeric", "42"});
    execArgs({"SET", "empty", ""});
    execArgs({"RPUSH", "listkey", "with space", "with:colon", "with\nnewline"});
    execArgs({"HMSET", "user:1", "name", "Bob Smith", "note", "a:b c"});
    exec("EXPIRE plain 500");

    CHECK(db().dump(path));
    std::remove((path + ".tmp").c_str());

    exec("FLUSHALL");
    CHECK_EQ(intValue(exec("DBSIZE")), std::string("0"));

    CHECK(db().load(path));
    CHECK_EQ(intValue(exec("DBSIZE")), std::string("9"));

    CHECK_EQ(bulkValue(exec("GET plain")), std::string("simple"));
    CHECK_EQ(bulkValue(exec("GET spaced")), std::string("has spaces inside"));
    CHECK_EQ(bulkValue(exec("GET colons")), std::string("has:colon:inside"));
    CHECK_EQ(bulkValue(exec("GET newlines")), std::string("line1\nline2\r\nline3"));
    CHECK_EQ(bulkValue(exec("GET trailing")), std::string("ends with space "));
    CHECK_EQ(bulkValue(exec("GET numeric")), std::string("42"));
    CHECK_EQ(bulkValue(exec("GET empty")), std::string(""));

    const std::string list = exec("LGET listkey");
    CHECK(list.find("with space") != std::string::npos);
    CHECK(list.find("with:colon") != std::string::npos);
    CHECK(list.find("with\nnewline") != std::string::npos);

    CHECK_EQ(bulkValue(exec("HGET user:1 name")), std::string("Bob Smith"));
    CHECK_EQ(bulkValue(exec("HGET user:1 note")), std::string("a:b c"));

    // TTLs used to be dropped entirely on reload.
    const std::string ttl = intValue(exec("TTL plain"));
    CHECK(std::stoll(ttl) > 400 && std::stoll(ttl) <= 500);
    CHECK_EQ(intValue(exec("TTL colons")), std::string("-1"));

    // Types survive the round trip.
    CHECK_EQ(simpleValue(exec("TYPE listkey")), std::string("list"));
    CHECK_EQ(simpleValue(exec("TYPE user:1")), std::string("hash"));

    std::remove(path.c_str());
}

void testCorruptSnapshotIsRejected() {
    TestScope scope("A corrupt snapshot leaves the live data intact");

    const std::string path = "build/test_corrupt.my_rdb";
    {
        std::ofstream ofs(path, std::ios::binary);
        ofs << "this is not a snapshot at all\n";
    }

    resetAll("LRU", 0);
    exec("SET survivor keepme");

    CHECK(!db().load(path));
    // A failed load must not wipe what was already resident.
    CHECK_EQ(intValue(exec("DBSIZE")), std::string("1"));
    CHECK_EQ(bulkValue(exec("GET survivor")), std::string("keepme"));

    std::remove(path.c_str());
}

void testSnapshotPopulatesPolicy() {
    TestScope scope("Reloaded keys are tracked by the eviction policy");

    const std::string path = "build/test_reload.my_rdb";
    std::remove(path.c_str());

    resetAll("LRU", 0);
    for (int i = 0; i < 6; ++i) exec("SET k" + std::to_string(i) + " v");
    CHECK(db().dump(path));

    resetAll("LRU", 3);
    CHECK(db().load(path));
    // The capacity limit applies to the reloaded set too.
    CHECK_EQ(intValue(exec("DBSIZE")), std::string("3"));

    std::remove(path.c_str());
}

// ---------------------------------------------------------------------------
// Wire protocol
// ---------------------------------------------------------------------------

// Extracts one complete RESP reply from the front of `carry`, if there is one.
// Returns false when more bytes are needed.
bool tryParseReply(std::string& carry, std::string& out) {
    if (carry.empty()) return false;

    const char kind = carry[0];
    const std::size_t crlf = carry.find("\r\n");
    if (crlf == std::string::npos) return false;

    if (kind == '+' || kind == '-' || kind == ':') {
        out = carry.substr(0, crlf + 2);
        carry.erase(0, crlf + 2);
        return true;
    }

    if (kind == '$') {
        const long length = std::stol(carry.substr(1, crlf - 1));
        if (length < 0) { // RESP nil
            out = carry.substr(0, crlf + 2);
            carry.erase(0, crlf + 2);
            return true;
        }
        const std::size_t needed = crlf + 2 + static_cast<std::size_t>(length) + 2;
        if (carry.size() < needed) return false;
        out = carry.substr(0, needed);
        carry.erase(0, needed);
        return true;
    }

    if (kind == '*') {
        const long count = std::stol(carry.substr(1, crlf - 1));
        std::size_t offset = crlf + 2;
        for (long i = 0; i < count; ++i) {
            const std::size_t innerCrlf = carry.find("\r\n", offset);
            if (innerCrlf == std::string::npos) return false;
            if (carry[offset] != '$') return false;
            const long len = std::stol(carry.substr(offset + 1, innerCrlf - offset - 1));
            if (len < 0) { // nil element
                offset = innerCrlf + 2;
                continue;
            }
            const std::size_t needed = innerCrlf + 2 + static_cast<std::size_t>(len) + 2;
            if (carry.size() < needed) return false;
            offset = needed;
        }
        out = carry.substr(0, offset);
        carry.erase(0, offset);
        return true;
    }

    return false;
}

// Reads one complete RESP reply from a socket, tolerating pipelining: the
// carry buffer is always drained before the socket is polled, so a reply that
// already arrived is never missed behind a blocking read.
std::string readReply(int fd, std::string& carry) {
    char chunk[4096];
    while (true) {
        std::string reply;
        if (tryParseReply(carry, reply)) return reply;

        const ssize_t bytes = recv(fd, chunk, sizeof(chunk), 0);
        if (bytes > 0) {
            carry.append(chunk, static_cast<std::size_t>(bytes));
            continue;
        }
        if (bytes == 0) return {};            // peer closed
        if (errno == EINTR) continue;         // a signal, not a failure
        return {};
    }
}

std::string encodeArgs(const std::vector<std::string>& parts) {
    std::string buffer = "*" + std::to_string(parts.size()) + "\r\n";
    for (const auto& part : parts) {
        buffer += "$" + std::to_string(part.size()) + "\r\n" + part + "\r\n";
    }
    return buffer;
}

int connectTo(int port) {
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<uint16_t>(port));
    address.sin_addr.s_addr = inet_addr("127.0.0.1");

    if (connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

void sendAll(int fd, const std::string& data) {
    std::size_t sent = 0;
    while (sent < data.size()) {
        const ssize_t written = send(fd, data.data() + sent, data.size() - sent, MSG_NOSIGNAL);
        if (written <= 0) return;
        sent += static_cast<std::size_t>(written);
    }
}

void testWireProtocol(int port) {
    TestScope scope("RESP wire protocol");

    const int fd = connectTo(port);
    CHECK(fd >= 0);
    if (fd < 0) return;

    std::string carry;

    // Multibulk, the form redis-cli and the Streamlit UI speak.
    sendAll(fd, encodeArgs({"PING"}));
    CHECK_EQ(simpleValue(readReply(fd, carry)), std::string("PONG"));

    // A value far larger than the old 1024-byte receive buffer.
    const std::string big(9000, 'x');
    sendAll(fd, encodeArgs({"SET", "big", big}));
    CHECK_EQ(simpleValue(readReply(fd, carry)), std::string("OK"));
    sendAll(fd, encodeArgs({"GET", "big"}));
    CHECK_EQ(bulkValue(readReply(fd, carry)).size(), big.size());

    // A command split across two TCP segments must still be understood.
    const std::string split = encodeArgs({"SET", "split", "assembled"});
    sendAll(fd, split.substr(0, 7));
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    sendAll(fd, split.substr(7));
    CHECK_EQ(simpleValue(readReply(fd, carry)), std::string("OK"));
    sendAll(fd, encodeArgs({"GET", "split"}));
    CHECK_EQ(bulkValue(readReply(fd, carry)), std::string("assembled"));

    // Pipelined commands must each get their own reply, in order.
    sendAll(fd, encodeArgs({"SET", "p1", "one"}) + encodeArgs({"SET", "p2", "two"}) +
                    encodeArgs({"GET", "p1"}) + encodeArgs({"GET", "p2"}));
    CHECK_EQ(simpleValue(readReply(fd, carry)), std::string("OK"));
    CHECK_EQ(simpleValue(readReply(fd, carry)), std::string("OK"));
    CHECK_EQ(bulkValue(readReply(fd, carry)), std::string("one"));
    CHECK_EQ(bulkValue(readReply(fd, carry)), std::string("two"));

    // Inline commands, the form the benchmark client and test scripts speak.
    sendAll(fd, "SET inline hello\r\n");
    CHECK_EQ(simpleValue(readReply(fd, carry)), std::string("OK"));
    sendAll(fd, "GET inline\r\n");
    CHECK_EQ(bulkValue(readReply(fd, carry)), std::string("hello"));

    // Arrays and the administrative commands.
    sendAll(fd, encodeArgs({"HSET", "h1", "a", "1", "b", "2"}));
    CHECK_EQ(intValue(readReply(fd, carry)), std::string("2"));
    sendAll(fd, encodeArgs({"HGETALL", "h1"}));
    CHECK_EQ(countBulkElements(readReply(fd, carry)), std::size_t(4));

    sendAll(fd, encodeArgs({"CONFIG", "SET", "POLICY", "LFU"}));
    CHECK_EQ(simpleValue(readReply(fd, carry)), std::string("OK"));
    sendAll(fd, encodeArgs({"CONFIG", "SET", "MAXKEYS", "50"}));
    CHECK_EQ(simpleValue(readReply(fd, carry)), std::string("OK"));

    sendAll(fd, encodeArgs({"INFO"}));
    const std::string info = bulkValue(readReply(fd, carry));
    CHECK(info.find("active_policy:LFU") != std::string::npos);
    CHECK(info.find("max_keys:50") != std::string::npos);

    // QUIT is answered and then closes the connection.
    sendAll(fd, encodeArgs({"QUIT"}));
    CHECK_EQ(simpleValue(readReply(fd, carry)), std::string("OK"));
    char scratch[16];
    CHECK(recv(fd, scratch, sizeof(scratch), 0) == 0);

    close(fd);
}

void testConcurrentClients(int port) {
    TestScope scope("Concurrent clients are serialised safely");

    const int fd = connectTo(port);
    if (fd < 0) {
        CHECK(false);
        return;
    }
    std::string carry;
    // Unlimited capacity: this test is about concurrency, and with a capacity
    // limit in force a key can legitimately be evicted between a thread's own
    // SET and GET.
    sendAll(fd, encodeArgs({"CONFIG", "SET", "MAXKEYS", "0"}));
    readReply(fd, carry);
    sendAll(fd, encodeArgs({"FLUSHALL"}));
    readReply(fd, carry);
    close(fd);

    std::vector<std::thread> workers;
    std::atomic<int> failures{0};

    // Several threads writing distinct keys while one thread reads them back;
    // a single global mutex guards the store, so every read must see a value
    // that was actually written, never a torn one.
    for (int t = 0; t < 6; ++t) {
        workers.emplace_back([port, &failures, t]() {
            const int workerFd = connectTo(port);
            if (workerFd < 0) { ++failures; return; }
            std::string local;
            for (int i = 0; i < 25; ++i) {
                const std::string key = "w" + std::to_string(t) + ":" + std::to_string(i);
                const std::string value = "v" + std::to_string(t) + "-" + std::to_string(i) +
                                          std::string(50, 'z');
                sendAll(workerFd, encodeArgs({"SET", key, value}));
                const std::string setReply = readReply(workerFd, local);
                if (setReply.compare(0, 5, "+OK\r\n") != 0) ++failures;

                sendAll(workerFd, encodeArgs({"GET", key}));
                const std::string getReply = readReply(workerFd, local);
                if (bulkValue(getReply) != value) ++failures;
            }
            close(workerFd);
        });
    }
    for (auto& worker : workers) worker.join();
    CHECK_EQ(failures.load(), 0);

    const int checker = connectTo(port);
    std::string local;
    sendAll(checker, encodeArgs({"DBSIZE"}));
    CHECK_EQ(intValue(readReply(checker, local)), std::string("150"));
    close(checker);
}

} // namespace

int main(int argc, char* argv[]) {
    int port = 16399;
    if (argc >= 2) port = std::stoi(argv[1]);

    std::cout << "Integration tests\n";

    // Phase 1 exercises the command handler and database in-process.
    testStringCommands();
    testKeysPattern();
    testTypeSafety();
    testListCommands();
    testHashCommands();
    testExpiry();
    testEvictionEnforced();
    testEvictionCountsEveryPolicy();
    testUnlimitedCapacity();
    testPolicySwitchKeepsResidentSet();
    testShrinkingCapacityEvictsImmediately();
    testMetricsAndInfo();
    testAdaptiveDetail();
    testPersistenceRoundTrip();
    testCorruptSnapshotIsRejected();
    testSnapshotPopulatesPolicy();

    // Phase 2 drives a real server over TCP.
    const std::string dumpPath = "build/test_server.my_rdb";
    std::remove(dumpPath.c_str());

    RedisServer server(port, dumpPath, 0);
    std::thread serverThread([&server]() { server.run(); });

    // Wait for the listener to come up.
    int attempts = 0;
    while (attempts < 100 && connectTo(port) < 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        ++attempts;
    }

    testWireProtocol(port);
    testConcurrentClients(port);

    server.shutdown();
    serverThread.join();

    std::cout << "\n" << (g_checks - g_failures) << "/" << g_checks << " checks passed\n";
    if (g_failures == 0) std::cout << "All integration tests passed.\n";
    return g_failures == 0 ? 0 : 1;
}