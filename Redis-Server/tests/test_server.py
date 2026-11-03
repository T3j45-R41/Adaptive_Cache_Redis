import socket
import time
import sys

def send_command(s, cmd_tokens):
    # Formulate RESP Array
    msg = f"*{len(cmd_tokens)}\r\n"
    for token in cmd_tokens:
        msg += f"${len(str(token))}\r\n{token}\r\n"
    s.sendall(msg.encode('utf-8'))

def read_response(s):
    # Read until complete response
    data = b""
    while True:
        chunk = s.recv(4096)
        if not chunk:
            break
        data += chunk
        # Simple heuristic check for complete response
        if data.endswith(b"\r\n"):
            break
    return data.decode('utf-8', errors='replace')

def run_tests(port=6380):
    print(f"Connecting to Redis server on port {port}...")
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(5.0)
    s.connect(("127.0.0.1", port))
    
    passed = 0
    failed = 0

    def assert_eq(test_name, actual, expected):
        nonlocal passed, failed
        if actual == expected:
            print(f"[PASS] {test_name}")
            passed += 1
        else:
            print(f"[FAIL] {test_name}: expected {repr(expected)}, got {repr(actual)}")
            failed += 1

    # 1. PING & ECHO
    send_command(s, ["PING"])
    assert_eq("PING", read_response(s), "+PONG\r\n")

    send_command(s, ["ECHO", "AdaptiveCache"])
    assert_eq("ECHO", read_response(s), "$13\r\nAdaptiveCache\r\n")

    # 2. FLUSHALL & DBSIZE
    send_command(s, ["FLUSHALL"])
    assert_eq("FLUSHALL", read_response(s), "+OK\r\n")

    send_command(s, ["DBSIZE"])
    assert_eq("DBSIZE empty", read_response(s), ":0\r\n")

    # 3. SET & GET & DEL & EXISTS
    send_command(s, ["SET", "k1", "val1"])
    assert_eq("SET k1", read_response(s), "+OK\r\n")

    send_command(s, ["GET", "k1"])
    assert_eq("GET k1", read_response(s), "$4\r\nval1\r\n")

    send_command(s, ["EXISTS", "k1", "nonexistent"])
    assert_eq("EXISTS", read_response(s), ":1\r\n")

    send_command(s, ["DEL", "k1"])
    assert_eq("DEL k1", read_response(s), ":1\r\n")

    send_command(s, ["GET", "k1"])
    assert_eq("GET after DEL", read_response(s), "$-1\r\n")

    # 4. Expiration
    send_command(s, ["SET", "tempKey", "123"])
    read_response(s)
    send_command(s, ["EXPIRE", "tempKey", "2"])
    assert_eq("EXPIRE", read_response(s), ":1\r\n")

    send_command(s, ["TTL", "tempKey"])
    ttl_resp = read_response(s)
    assert_eq("TTL positive", ttl_resp.startswith(":"), True)

    send_command(s, ["PERSIST", "tempKey"])
    assert_eq("PERSIST", read_response(s), ":1\r\n")

    send_command(s, ["TTL", "tempKey"])
    assert_eq("TTL after PERSIST", read_response(s), ":-1\r\n")

    # 5. Lists (LPUSH, RPUSH, LRANGE, LLEN, LPOP, RPOP)
    send_command(s, ["RPUSH", "mylist", "a", "b", "c"])
    assert_eq("RPUSH", read_response(s), ":3\r\n")

    send_command(s, ["LLEN", "mylist"])
    assert_eq("LLEN", read_response(s), ":3\r\n")

    send_command(s, ["LRANGE", "mylist", "0", "-1"])
    assert_eq("LRANGE", read_response(s), "*3\r\n$1\r\na\r\n$1\r\nb\r\n$1\r\nc\r\n")

    send_command(s, ["LPOP", "mylist"])
    assert_eq("LPOP", read_response(s), "$1\r\na\r\n")

    send_command(s, ["RPOP", "mylist"])
    assert_eq("RPOP", read_response(s), "$1\r\nc\r\n")

    # 6. Hashes (HSET, HGET, HEXISTS, HGETALL)
    send_command(s, ["HSET", "stock:AAPL", "price", "180.5", "volume", "1000000"])
    assert_eq("HSET", read_response(s), ":2\r\n")

    send_command(s, ["HGET", "stock:AAPL", "price"])
    assert_eq("HGET", read_response(s), "$5\r\n180.5\r\n")

    send_command(s, ["HEXISTS", "stock:AAPL", "price"])
    assert_eq("HEXISTS", read_response(s), ":1\r\n")

    send_command(s, ["HLEN", "stock:AAPL"])
    assert_eq("HLEN", read_response(s), ":2\r\n")

    # 7. Eviction Engine Tests
    # A) Test LRU Policy
    send_command(s, ["FLUSHALL"])
    read_response(s)
    send_command(s, ["CONFIG", "SET", "eviction_policy", "LRU"])
    assert_eq("CONFIG SET LRU", read_response(s), "+OK\r\n")
    send_command(s, ["CONFIG", "SET", "capacity", "3"])
    assert_eq("CONFIG SET capacity 3", read_response(s), "+OK\r\n")

    send_command(s, ["SET", "k1", "v1"])
    read_response(s)
    send_command(s, ["SET", "k2", "v2"])
    read_response(s)
    send_command(s, ["SET", "k3", "v3"])
    read_response(s)
    # Access k1 so k2 becomes least recently used
    send_command(s, ["GET", "k1"])
    read_response(s)
    # Insert k4 -> should evict k2
    send_command(s, ["SET", "k4", "v4"])
    read_response(s)

    send_command(s, ["GET", "k2"])
    assert_eq("LRU evicted k2", read_response(s), "$-1\r\n")
    send_command(s, ["GET", "k1"])
    assert_eq("LRU retained k1", read_response(s), "$2\r\nv1\r\n")

    # B) Test FIFO Policy
    send_command(s, ["FLUSHALL"])
    read_response(s)
    send_command(s, ["CONFIG", "SET", "eviction_policy", "FIFO"])
    read_response(s)
    send_command(s, ["CONFIG", "SET", "capacity", "3"])
    read_response(s)
    send_command(s, ["SET", "k1", "v1"])
    read_response(s)
    send_command(s, ["SET", "k2", "v2"])
    read_response(s)
    send_command(s, ["SET", "k3", "v3"])
    read_response(s)
    # Access k1 (in FIFO, access should NOT prevent eviction of oldest)
    send_command(s, ["GET", "k1"])
    read_response(s)
    # Insert k4 -> should evict k1 (oldest inserted)
    send_command(s, ["SET", "k4", "v4"])
    read_response(s)
    send_command(s, ["GET", "k1"])
    assert_eq("FIFO evicted k1", read_response(s), "$-1\r\n")

    # C) Test LFU Policy
    send_command(s, ["FLUSHALL"])
    read_response(s)
    send_command(s, ["CONFIG", "SET", "eviction_policy", "LFU"])
    read_response(s)
    send_command(s, ["CONFIG", "SET", "capacity", "3"])
    read_response(s)
    send_command(s, ["SET", "k1", "v1"])
    read_response(s)
    send_command(s, ["SET", "k2", "v2"])
    read_response(s)
    send_command(s, ["SET", "k3", "v3"])
    read_response(s)
    # Access k1 and k2 multiple times
    send_command(s, ["GET", "k1"])
    read_response(s)
    send_command(s, ["GET", "k1"])
    read_response(s)
    send_command(s, ["GET", "k2"])
    read_response(s)
    # k3 has lowest freq (1) -> should be evicted
    send_command(s, ["SET", "k4", "v4"])
    read_response(s)
    send_command(s, ["GET", "k3"])
    assert_eq("LFU evicted k3", read_response(s), "$-1\r\n")

    # D) Test Adaptive Policy
    send_command(s, ["FLUSHALL"])
    read_response(s)
    send_command(s, ["CONFIG", "SET", "eviction_policy", "ADAPTIVE"])
    read_response(s)
    send_command(s, ["CONFIG", "SET", "capacity", "5"])
    read_response(s)
    for i in range(10):
        send_command(s, ["SET", f"key:{i}", f"val:{i}"])
        read_response(s)
    send_command(s, ["EVICTION.STATS"])
    evict_stats = read_response(s)
    assert_eq("ADAPTIVE stats retrieved", "policy:ADAPTIVE" in evict_stats, True)

    # 8. Pipelining Test (Multiple commands in single buffer)
    pipeline_msg = "*2\r\n$4\r\nPING\r\n$5\r\nhello\r\n*1\r\n$4\r\nPING\r\n"
    s.sendall(pipeline_msg.encode('utf-8'))
    pipe_resp1 = read_response(s)
    pipe_resp2 = read_response(s)
    assert_eq("Pipelining 1", pipe_resp1, "$5\r\nhello\r\n")
    assert_eq("Pipelining 2", pipe_resp2, "+PONG\r\n")

    s.close()
    print(f"\nTest Summary: {passed} PASSED, {failed} FAILED")
    return failed == 0

if __name__ == "__main__":
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 6380
    success = run_tests(port)
    sys.exit(0 if success else 1)
