#!/usr/bin/env python3
"""
Python Client for Custom In-Memory Storage Engine
Supports both Binary TLV Protocol and Text / RESP-like Protocol.
"""

import socket
import struct
from typing import List, Optional, Tuple, Union, Dict, Any

TLV_MAGIC = 0xA5

class TLVType:
    CMD_REQUEST    = 0x01
    RESP_OK        = 0x02
    RESP_ERR       = 0x03
    RESP_INT       = 0x04
    RESP_STR       = 0x05
    RESP_ARRAY     = 0x06
    RESP_NIL       = 0x07
    RESP_MAP       = 0x08
    RESP_AVL_RANGE = 0x09

class StorageClient:
    def __init__(self, host: str = "127.0.0.1", port: int = 6379, use_tlv: bool = False, timeout: float = 5.0):
        self.host = host
        self.port = port
        self.use_tlv = use_tlv
        self.timeout = timeout
        self.sock: Optional[socket.socket] = None
        self.connect()

    def connect(self):
        if self.sock:
            self.close()
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.settimeout(self.timeout)
        self.sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        self.sock.connect((self.host, self.port))

    def close(self):
        if self.sock:
            try:
                self.sock.close()
            except Exception:
                pass
            self.sock = None

    def execute_command(self, *args: Union[str, int, float]) -> Any:
        str_args = [str(a) for a in args]
        if self.use_tlv:
            return self._execute_tlv(str_args)
        else:
            return self._execute_text(str_args)

    def _execute_text(self, args: List[str]) -> str:
        cmd_str = " ".join(f'"{a}"' if " " in a else a for a in args) + "\r\n"
        assert self.sock is not None
        self.sock.sendall(cmd_str.encode("utf-8"))

        data = b""
        while not data.endswith(b"\r\n"):
            chunk = self.sock.recv(4096)
            if not chunk:
                raise ConnectionResetError("Server closed connection")
            data += chunk
        return data.decode("utf-8", errors="replace").strip()

    def _execute_tlv(self, args: List[str]) -> Any:
        # Encode command arguments into TLV payload
        payload = bytearray()
        for arg in args:
            encoded = arg.encode("utf-8")
            payload += struct.pack("!I", len(encoded))
            payload += encoded

        packet = bytearray()
        packet.append(TLV_MAGIC)
        packet.append(TLVType.CMD_REQUEST)
        packet += struct.pack("!I", len(payload))
        packet += payload

        assert self.sock is not None
        self.sock.sendall(packet)

        # Read response header (Magic 1, Type 1, Length 4)
        header = self._recv_exact(6)
        magic, resp_type, length = struct.unpack("!BBI", header)
        if magic != TLV_MAGIC:
            raise ValueError(f"Invalid TLV Magic received: {hex(magic)}")

        value = self._recv_exact(length) if length > 0 else b""

        if resp_type == TLVType.RESP_OK:
            return value.decode("utf-8")
        elif resp_type == TLVType.RESP_ERR:
            raise RuntimeError(f"StorageEngine Error: {value.decode('utf-8')}")
        elif resp_type == TLVType.RESP_INT:
            return struct.unpack("!q", value)[0]
        elif resp_type == TLVType.RESP_STR:
            return value.decode("utf-8")
        elif resp_type == TLVType.RESP_NIL:
            return None
        elif resp_type in (TLVType.RESP_ARRAY, TLVType.RESP_MAP, TLVType.RESP_AVL_RANGE):
            # Parse list of strings
            items = []
            offset = 0
            while offset + 4 <= len(value):
                item_len = struct.unpack_from("!I", value, offset)[0]
                offset += 4
                items.append(value[offset:offset+item_len].decode("utf-8"))
                offset += item_len
            return items
        return value

    def _recv_exact(self, n: int) -> bytes:
        data = bytearray()
        assert self.sock is not None
        while len(data) < n:
            chunk = self.sock.recv(n - len(data))
            if not chunk:
                raise ConnectionResetError("Server closed connection while receiving data")
            data += chunk
        return bytes(data)

    # Convenience Methods
    def set(self, key: str, value: str, ex: int = 0, px: int = 0):
        args = ["SET", key, value]
        if ex > 0:
            args += ["EX", str(ex)]
        elif px > 0:
            args += ["PX", str(px)]
        return self.execute_command(*args)

    def get(self, key: str) -> Optional[str]:
        return self.execute_command("GET", key)

    def lpush(self, key: str, *values: str):
        return self.execute_command("LPUSH", key, *values)

    def lrange(self, key: str, start: int = 0, stop: int = -1):
        return self.execute_command("LRANGE", key, start, stop)

    def sadd(self, key: str, *members: str):
        return self.execute_command("SADD", key, *members)

    def smembers(self, key: str):
        return self.execute_command("SMEMBERS", key)

    def hset(self, key: str, field: str, value: str):
        return self.execute_command("HSET", key, field, value)

    def hget(self, key: str, field: str):
        return self.execute_command("HGET", key, field)

    def zadd(self, key: str, score: float, member: str):
        return self.execute_command("ZADD", key, score, member)

    def zrangebyscore(self, key: str, min_score: float, max_score: float):
        return self.execute_command("ZRANGEBYSCORE", key, min_score, max_score)


if __name__ == "__main__":
    import sys
    print("Testing Custom In-Memory Storage Engine Python Client...")
    cli = StorageClient(use_tlv=True)
    cli.set("greeting", "Hello from Python TLV Client!")
    print("GET greeting ->", cli.get("greeting"))
    cli.close()
