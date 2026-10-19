"""RESP client for the Adaptive_Cache_Redis server.

Speaks RESP2 over a persistent TCP connection and exposes typed helpers for the
cache-control and introspection commands the server implements. Errors returned
by the server are raised as :class:`RedisRESPError` rather than returned as
values, so a caller cannot silently treat a failure as data.
"""

from __future__ import annotations

import socket
from typing import Any, Iterable

__all__ = ["RedisRESPError", "RedisRESPClient", "POLICIES"]

#: Replacement policies the server accepts. "CACHEUS" is a legacy alias for
#: "ADAPTIVE" that the server still tolerates.
POLICIES = ["LRU", "LFU", "FIFO", "RANDOM", "ADAPTIVE"]


class RedisRESPError(RuntimeError):
    """Raised when the server replies with a RESP error."""


def encode_command(parts: Iterable[Any]) -> bytes:
    """Encode arguments as a RESP2 multibulk array.

    Only ``bytes``/``bytearray`` are treated as raw payloads. Everything else is
    converted with ``str()`` first, because RESP arguments are text: passing an
    ``int`` through ``bytes()`` would silently send that many NUL bytes instead
    of the number.
    """
    items = list(parts)
    out = [b"*%d\r\n" % len(items)]
    for item in items:
        if isinstance(item, (bytes, bytearray)):
            raw = bytes(item)
        else:
            raw = str(item).encode("utf-8")
        out.append(b"$%d\r\n" % len(raw))
        out.append(raw)
        out.append(b"\r\n")
    return b"".join(out)


class RedisRESPClient:
    """A small RESP2 client with a persistent connection and auto-reconnect."""

    def __init__(self, host: str = "127.0.0.1", port: int = 6379, timeout: float = 3.0):
        self.host = host
        self.port = int(port)
        self.timeout = float(timeout)
        self._sock: socket.socket | None = None
        self._buf = b""

    # ------------------------------------------------------------------ setup

    @property
    def connected(self) -> bool:
        return self._sock is not None

    def connect(self) -> None:
        self.close()
        self._sock = socket.create_connection((self.host, self.port), timeout=self.timeout)
        self._sock.settimeout(self.timeout)
        self._buf = b""

    def close(self) -> None:
        if self._sock is not None:
            try:
                self._sock.close()
            except OSError:
                pass
        self._sock = None
        self._buf = b""

    def __enter__(self) -> "RedisRESPClient":
        self.connect()
        return self

    def __exit__(self, *exc_info: object) -> None:
        self.close()

    # ------------------------------------------------------------- transport

    def _readline(self) -> bytes:
        while b"\r\n" not in self._buf:
            chunk = self._sock.recv(65536)
            if not chunk:
                raise RedisRESPError("server closed the connection")
            self._buf += chunk
        line, _, self._buf = self._buf.partition(b"\r\n")
        return line

    def _readexact(self, count: int) -> bytes:
        while len(self._buf) < count:
            chunk = self._sock.recv(65536)
            if not chunk:
                raise RedisRESPError("server closed the connection")
            self._buf += chunk
        data, self._buf = self._buf[:count], self._buf[count:]
        return data

    def _read_reply(self) -> Any:
        line = self._readline()
        kind, payload = line[:1], line[1:]

        if kind == b"+":
            return payload.decode("utf-8", "replace")
        if kind == b"-":
            raise RedisRESPError(payload.decode("utf-8", "replace"))
        if kind == b":":
            return int(payload)
        if kind == b"$":
            length = int(payload)
            if length < 0:
                return None
            return self._readexact(length + 2)[:length].decode("utf-8", "replace")
        if kind == b"*":
            count = int(payload)
            if count < 0:
                return None
            return [self._read_reply() for _ in range(count)]
        raise RedisRESPError(f"unexpected reply type {kind!r}")

    def command(self, *parts: Any, _retry: bool = True) -> Any:
        """Send one command and return its reply.

        On a transport failure the connection is re-established and the command
        is retried once. Every command exposed here is idempotent (a set is a
        set, a flush is a flush), so the retry is safe on a local server.
        """
        payload = encode_command(parts)
        if self._sock is None:
            self.connect()

        try:
            self._sock.sendall(payload)
            return self._read_reply()
        except (OSError, socket.timeout) as exc:
            self.close()
            if not _retry:
                raise
            try:
                self.connect()
            except (OSError, socket.timeout):
                raise exc
            self._sock.sendall(payload)
            return self._read_reply()

    # ------------------------------------------------------------ server info

    def ping(self) -> str:
        return self.command("PING")

    def info(self) -> dict[str, Any]:
        """Return ``INFO`` parsed into a dict, skipping section headers."""
        raw = self.command("INFO") or ""
        result: dict[str, Any] = {}
        for line in raw.splitlines():
            line = line.strip()
            if not line or line.startswith("#") or ":" not in line:
                continue
            key, _, value = line.partition(":")
            key, value = key.strip(), value.strip()
            try:
                result[key] = float(value) if "." in value else int(value)
            except ValueError:
                result[key] = value
        return result

    def dbsize(self) -> int:
        return int(self.command("DBSIZE"))

    def memory_usage(self, key: str) -> int | None:
        """Bytes for a key, or None if it does not exist."""
        value = self.command("MEMORY", "USAGE", key)
        return None if value is None else int(value)

    # -------------------------------------------------------- cache controls

    def eviction(self) -> str:
        return self.command("EVICTION")

    def set_policy(self, policy: str) -> str:
        return self.command("EVICTION", policy)

    def maxkeys(self) -> int:
        return int(self.command("MAXKEYS"))

    def set_maxkeys(self, limit: int) -> str:
        return self.command("MAXKEYS", int(limit))

    def config_get(self, parameter: str) -> str:
        return self.command("CONFIG", "GET", parameter)

    def reset_stats(self) -> str:
        return self.command("CONFIG", "RESETSTAT")

    def flushall(self) -> str:
        return self.command("FLUSHALL")

    # -------------------------------------------------------------- data ops

    def set(self, key: str, value: str) -> str:
        return self.command("SET", key, value)

    def get(self, key: str) -> str | None:
        return self.command("GET", key)

    def delete(self, *keys: str) -> int:
        if not keys:
            return 0
        return int(self.command("DEL", *keys))

    def keys(self, pattern: str = "*") -> list[str]:
        return list(self.command("KEYS", pattern) or [])

    def key_type(self, key: str) -> str:
        return self.command("TYPE", key)

    def ttl(self, key: str) -> int:
        """Seconds remaining; ``-1`` means no expiry, ``-2`` means no key."""
        return int(self.command("TTL", key))

    def exists(self, key: str) -> bool:
        return bool(self.command("EXISTS", key))

    def hgetall(self, key: str) -> dict[str, str]:
        flat = list(self.command("HGETALL", key) or [])
        return dict(zip(flat[0::2], flat[1::2]))

    def lget(self, key: str) -> list[str]:
        return list(self.command("LGET", key) or [])

    # ------------------------------------------------------------- inspection

    def describe(self, key: str) -> dict[str, Any]:
        """Type, TTL and byte footprint of one key, or ``{}`` if it is gone."""
        kind = self.key_type(key)
        if kind == "none":
            return {}
        return {
            "key": key,
            "type": kind,
            "ttl": self.ttl(key),
            "bytes": self.memory_usage(key) or 0,
        }

    def describe_many(self, keys: list[str], limit: int = 200) -> list[dict[str, Any]]:
        """Describe up to ``limit`` keys. Each key costs three round trips."""
        rows: list[dict[str, Any]] = []
        for key in keys[:limit]:
            try:
                row = self.describe(key)
            except RedisRESPError:
                continue
            if row:
                rows.append(row)
        return rows