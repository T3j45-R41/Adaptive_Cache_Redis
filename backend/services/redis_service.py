import socket
import os
from typing import Dict, Any, List, Optional

REDIS_HOST = os.getenv("REDIS_HOST", "127.0.0.1")
REDIS_PORT = int(os.getenv("REDIS_PORT", "6379"))

class RedisService:
    def __init__(self, host: str = REDIS_HOST, port: int = REDIS_PORT):
        self.host = host
        self.port = port

    def _execute(self, tokens: List[str]) -> str:
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.settimeout(0.5)
        try:
            s.connect((self.host, self.port))
            msg = f"*{len(tokens)}\r\n"
            for t in tokens:
                st = str(t)
                msg += f"${len(st.encode('utf-8'))}\r\n{st}\r\n"
            s.sendall(msg.encode("utf-8"))

            data = b""
            while True:
                chunk = s.recv(4096)
                if not chunk:
                    break
                data += chunk
                if data.endswith(b"\r\n"):
                    break
            return data.decode("utf-8", errors="replace")
        finally:
            s.close()

    def ping(self) -> bool:
        try:
            res = self._execute(["PING"])
            return "+PONG" in res
        except Exception:
            return False

    def get_info(self, section: str = "all") -> Dict[str, str]:
        try:
            res = self._execute(["INFO", section])
            info = {}
            for line in res.split("\r\n"):
                if ":" in line and not line.startswith("#"):
                    k, v = line.split(":", 1)
                    info[k.strip()] = v.strip()
            return info
        except Exception:
            return {}

    def get_eviction_stats(self) -> Dict[str, Any]:
        try:
            res = self._execute(["EVICTION.STATS"])
            stats = {}
            for line in res.split("\n"):
                if ":" in line:
                    k, v = line.split(":", 1)
                    stats[k.strip()] = v.strip()
            return stats
        except Exception:
            return {}

    def set_policy(self, policy: str) -> bool:
        try:
            res = self._execute(["CONFIG", "SET", "eviction_policy", policy])
            return "+OK" in res
        except Exception:
            return False

    def set_capacity(self, capacity: int) -> bool:
        try:
            res = self._execute(["CONFIG", "SET", "capacity", str(capacity)])
            return "+OK" in res
        except Exception:
            return False

    def get_cached_key(self, key: str) -> Optional[str]:
        try:
            res = self._execute(["GET", key])
            if res.startswith("$-1"):
                return None
            lines = res.split("\r\n")
            if len(lines) >= 2:
                return lines[1]
            return None
        except Exception:
            return None

redis_service = RedisService()
