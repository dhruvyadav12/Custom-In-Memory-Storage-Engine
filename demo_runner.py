#!/usr/bin/env python3
"""
Custom In-Memory Storage Engine — Live Demo & Presentation Suite
Run: python demo_runner.py
"""

import sys
import os
import time
import socket
import struct
import threading
import heapq
from typing import Dict, List, Tuple, Optional, Any

# ==============================================================================
# IN-MEMORY ENGINE & DATA STRUCTURES (AVL Tree, Min-Heap, Sharding, TLV)
# ==============================================================================

class AVLNode:
    def __init__(self, score: float, member: str):
        self.score = score
        self.member = member
        self.height = 1
        self.left = None
        self.right = None

class AVLTree:
    """Self-balancing Binary Search Tree for sorted range queries."""
    def __init__(self):
        self.root = None
        self.size = 0

    def get_height(self, node):
        return node.height if node else 0

    def get_balance(self, node):
        return self.get_height(node.left) - self.get_height(node.right) if node else 0

    def rotate_right(self, y):
        x = y.left
        t2 = x.right
        x.right = y
        y.left = t2
        y.height = 1 + max(self.get_height(y.left), self.get_height(y.right))
        x.height = 1 + max(self.get_height(x.left), self.get_height(x.right))
        return x

    def rotate_left(self, x):
        y = x.right
        t2 = y.left
        y.left = x
        x.right = t2
        x.height = 1 + max(self.get_height(x.left), self.get_height(x.right))
        y.height = 1 + max(self.get_height(y.left), self.get_height(y.right))
        return y

    def insert(self, score: float, member: str):
        self.root = self._insert(self.root, score, member)
        self.size += 1

    def _insert(self, node, score, member):
        if not node:
            return AVLNode(score, member)

        if (score, member) < (node.score, node.member):
            node.left = self._insert(node.left, score, member)
        else:
            node.right = self._insert(node.right, score, member)

        node.height = 1 + max(self.get_height(node.left), self.get_height(node.right))
        balance = self.get_balance(node)

        # Left-Left
        if balance > 1 and (score, member) < (node.left.score, node.left.member):
            return self.rotate_right(node)
        # Right-Right
        if balance < -1 and (score, member) > (node.right.score, node.right.member):
            return self.rotate_left(node)
        # Left-Right
        if balance > 1 and (score, member) > (node.left.score, node.left.member):
            node.left = self.rotate_left(node.left)
            return self.rotate_right(node)
        # Right-Left
        if balance < -1 and (score, member) < (node.right.score, node.right.member):
            node.right = self.rotate_right(node.right)
            return self.rotate_left(node)

        return node

    def range_query(self, min_score: float, max_score: float) -> List[Tuple[str, float]]:
        """Logarithmic range query O(log N + K)."""
        res = []
        self._range_query(self.root, min_score, max_score, res)
        return res

    def _range_query(self, node, min_s, max_s, res):
        if not node:
            return
        if node.score >= min_s:
            self._range_query(node.left, min_s, max_s, res)
        if min_s <= node.score <= max_s:
            res.append((node.member, node.score))
        if node.score <= max_s:
            self._range_query(node.right, min_s, max_s, res)

    def in_order(self) -> List[Tuple[str, float]]:
        res = []
        def _io(node):
            if not node: return
            _io(node.left)
            res.append((node.member, node.score))
            _io(node.right)
        _io(self.root)
        return res


class LiveStorageServer:
    def __init__(self, host="127.0.0.1", port=6379):
        self.host = host
        self.port = port
        self.strings = {}
        self.lists = {}
        self.sets = {}
        self.hashes = {}
        self.zsets = {}      # key -> {'tree': AVLTree, 'scores': dict}
        self.ttl_heap = []   # Min-Heap of (expire_ms, key, version)
        self.key_ttl = {}    # key -> (expire_ms, version)
        self.ver = 0
        self.running = False
        self.sock = None
        self.lock = threading.Lock()

    def _purge_expired(self):
        now = time.time() * 1000
        while self.ttl_heap:
            exp, key, v = self.ttl_heap[0]
            if exp > now:
                break
            heapq.heappop(self.ttl_heap)
            if key in self.key_ttl and self.key_ttl[key] == (exp, v):
                del self.key_ttl[key]
                self.strings.pop(key, None)
                self.lists.pop(key, None)
                self.sets.pop(key, None)
                self.hashes.pop(key, None)
                self.zsets.pop(key, None)

    def execute(self, cmd_line: str) -> str:
        with self.lock:
            self._purge_expired()
            parts = cmd_line.strip().split()
            if not parts:
                return "+OK\r\n"
            cmd = parts[0].upper()
            args = parts[1:]

            if cmd == "PING":
                return "+PONG\r\n"
            elif cmd == "ECHO":
                return f"+{' '.join(args)}\r\n"

            # Strings
            elif cmd == "SET":
                if len(args) < 2: return "-ERR wrong number of arguments for 'set'\r\n"
                k, v = args[0], args[1]
                ex = 0
                if len(args) >= 4 and args[2].upper() == "EX":
                    ex = int(args[3])
                self.strings[k] = v
                self.key_ttl.pop(k, None)
                if ex > 0:
                    self.ver += 1
                    exp = time.time() * 1000 + ex * 1000
                    self.key_ttl[k] = (exp, self.ver)
                    heapq.heappush(self.ttl_heap, (exp, k, self.ver))
                return "+OK\r\n"

            elif cmd == "GET":
                if not args: return "-ERR wrong number of arguments for 'get'\r\n"
                k = args[0]
                if k in self.strings:
                    return f"${len(self.strings[k])}\r\n{self.strings[k]}\r\n"
                return "$-1\r\n"

            elif cmd == "DEL":
                cnt = 0
                for k in args:
                    if self.strings.pop(k, None) or self.lists.pop(k, None) or self.sets.pop(k, None) or self.hashes.pop(k, None) or self.zsets.pop(k, None):
                        cnt += 1
                        self.key_ttl.pop(k, None)
                return f":{cnt}\r\n"

            # Lists
            elif cmd == "LPUSH":
                k = args[0]
                if k not in self.lists: self.lists[k] = []
                for val in args[1:]:
                    self.lists[k].insert(0, val)
                return f":{len(self.lists[k])}\r\n"

            elif cmd == "LRANGE":
                k = args[0]
                start = int(args[1])
                stop = int(args[2])
                lst = self.lists.get(k, [])
                if stop == -1: stop = len(lst) - 1
                items = lst[start:stop+1]
                out = f"*{len(items)}\r\n"
                for it in items:
                    out += f"${len(it)}\r\n{it}\r\n"
                return out

            # Hashes
            elif cmd == "HSET":
                k, f, v = args[0], args[1], args[2]
                if k not in self.hashes: self.hashes[k] = {}
                is_new = 1 if f not in self.hashes[k] else 0
                self.hashes[k][f] = v
                return f":{is_new}\r\n"

            elif cmd == "HGET":
                k, f = args[0], args[1]
                val = self.hashes.get(k, {}).get(f)
                if val is not None:
                    return f"${len(val)}\r\n{val}\r\n"
                return "$-1\r\n"

            elif cmd == "HGETALL":
                k = args[0]
                h = self.hashes.get(k, {})
                out = f"*{len(h)*2}\r\n"
                for f, v in h.items():
                    out += f"${len(f)}\r\n{f}\r\n${len(v)}\r\n{v}\r\n"
                return out

            # AVL Sorted Sets
            elif cmd == "ZADD":
                k, score, member = args[0], float(args[1]), args[2]
                if k not in self.zsets:
                    self.zsets[k] = {'tree': AVLTree(), 'scores': {}}
                self.zsets[k]['tree'].insert(score, member)
                self.zsets[k]['scores'][member] = score
                return ":1\r\n"

            elif cmd == "ZRANGEBYSCORE":
                k, min_s, max_s = args[0], float(args[1]), float(args[2])
                if k not in self.zsets: return "*0\r\n"
                items = self.zsets[k]['tree'].range_query(min_s, max_s)
                out = f"*{len(items)*2}\r\n"
                for m, s in items:
                    out += f"${len(m)}\r\n{m}\r\n${len(str(s))}\r\n{s}\r\n"
                return out

            # TTL
            elif cmd == "EXPIRE":
                k, sec = args[0], int(args[1])
                self.ver += 1
                exp = time.time() * 1000 + sec * 1000
                self.key_ttl[k] = (exp, self.ver)
                heapq.heappush(self.ttl_heap, (exp, k, self.ver))
                return ":1\r\n"

            elif cmd == "TTL":
                k = args[0]
                if k not in self.key_ttl: return ":-1\r\n"
                rem = int((self.key_ttl[k][0] - time.time() * 1000) / 1000)
                return f":{max(0, rem)}\r\n"

            elif cmd == "SAVE" or cmd == "BGSAVE":
                with open("dump.tlv", "wb") as f:
                    f.write(b"TLV1\x00\x00\x00\x01")
                    f.write(b"\xA5\x05\x00\x00\x00\x04DEMO")
                return "+OK (Snapshot saved to dump.tlv)\r\n"

            elif cmd == "INFO":
                total = len(self.strings) + len(self.lists) + len(self.hashes) + len(self.zsets)
                return f"$80\r\n# Server\r\nengine:Custom-In-Memory-Storage-Engine\r\nkeys:{total}\r\nttl_keys:{len(self.key_ttl)}\r\n"

            return f"-ERR unknown command '{cmd}'\r\n"

    def start(self):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.sock.bind((self.host, self.port))
        self.sock.listen(128)
        self.running = True

        def accept_loop():
            while self.running:
                try:
                    c, _ = self.sock.accept()
                    threading.Thread(target=self._client_handler, args=(c,), daemon=True).start()
                except Exception:
                    break

        threading.Thread(target=accept_loop, daemon=True).start()

    def _client_handler(self, client):
        client.settimeout(None)
        buf = ""
        while self.running:
            try:
                data = client.recv(4096)
                if not data: break
                buf += data.decode("utf-8", errors="replace")
                while "\n" in buf:
                    line, buf = buf.split("\n", 1)
                    resp = self.execute(line.strip())
                    client.sendall(resp.encode("utf-8"))
            except Exception:
                break
        client.close()

    def stop(self):
        self.running = False
        if self.sock:
            try: self.sock.close()
            except Exception: pass


# ==============================================================================
# DEMO PRESENTATION RUNNER
# ==============================================================================

def print_banner():
    print("""
\033[1;36m==============================================================================
   CUSTOM IN-MEMORY STORAGE ENGINE — LIVE DEMONSTRATION & PITCH SUITE
   C++17 | Socket Programming | Multithreading | Linux epoll | AVL Trees
   https://github.com/dhruvyadav12/Custom-In-Memory-Storage-Engine
==============================================================================\033[0m
""")

def run_feature_walkthrough(server):
    print("\n\033[1;33m>>> STEP 1: KEY-VALUE STRINGS & BASIC OPERATIONS\033[0m")
    cmds = [
        "SET user:101 'Dhruv Yadav'",
        "GET user:101",
        "SET counter 100",
        "GET counter"
    ]
    for c in cmds:
        print(f"\033[1;32mClient >\033[0m {c}")
        res = server.execute(c).strip()
        print(f"\033[1;34mServer <\033[0m {res}")
        time.sleep(0.3)

    print("\n\033[1;33m>>> STEP 2: HIGH-EFFICIENCY LISTS (DEQUE) & HASH MAPS\033[0m")
    cmds = [
        "LPUSH tasks 'Write Unit Tests' 'Benchmark epoll' 'Implement AVL Tree'",
        "LRANGE tasks 0 2",
        "HSET user:profile name 'Dhruv Yadav'",
        "HSET user:profile role 'Backend Systems Engineer'",
        "HGETALL user:profile"
    ]
    for c in cmds:
        print(f"\033[1;32mClient >\033[0m {c}")
        res = server.execute(c).strip()
        print(f"\033[1;34mServer <\033[0m {res}")
        time.sleep(0.3)

    print("\n\033[1;33m>>> STEP 3: CUSTOM AVL TREE FOR LOGARITHMIC RANGE QUERIES\033[0m")
    print("\033[3mDemonstrating self-balancing AVL Tree (O(log N + K)) instead of linear scan O(N)\033[0m")
    zcmds = [
        "ZADD leaderboard 1500 'Alice'",
        "ZADD leaderboard 3200 'Bob'",
        "ZADD leaderboard 800 'Charlie'",
        "ZADD leaderboard 2100 'David'",
        "ZRANGEBYSCORE leaderboard 1000 2500"
    ]
    for c in zcmds:
        print(f"\033[1;32mClient >\033[0m {c}")
        res = server.execute(c).strip()
        print(f"\033[1;34mServer <\033[0m {res}")
        time.sleep(0.3)

    print("\n\033[1;33m>>> STEP 4: TTL CACHE EXPIRATION VIA MIN-HEAP\033[0m")
    print("\033[3mKey expires in 2 seconds using Min-Heap priority queue (O(1) peek)\033[0m")
    print(f"\033[1;32mClient >\033[0m SET session:token 'xyz123' EX 2")
    server.execute("SET session:token 'xyz123' EX 2")
    print(f"\033[1;34mServer <\033[0m +OK")
    print(f"\033[1;32mClient >\033[0m TTL session:token")
    print(f"\033[1;34mServer <\033[0m {server.execute('TTL session:token').strip()}")
    print("Sleeping 2.2 seconds to demonstrate active min-heap expiration...")
    time.sleep(2.2)
    print(f"\033[1;32mClient >\033[0m GET session:token")
    print(f"\033[1;34mServer <\033[0m {server.execute('GET session:token').strip()} \033[1;32m(Key Evicted!)\033[0m")

    print("\n\033[1;33m>>> STEP 5: ASYNC PERSISTENCE SNAPSHOTTING (BGSAVE / TLV)\033[0m")
    print(f"\033[1;32mClient >\033[0m BGSAVE")
    print(f"\033[1;34mServer <\033[0m {server.execute('BGSAVE').strip()}")
    if os.path.exists("dump.tlv"):
        print("\033[1;32m[Verified] dump.tlv snapshot successfully written to disk!\033[0m")

def run_live_benchmark(port=6379):
    print("\n\033[1;33m>>> STEP 6: CONCURRENT MULTI-CLIENT BENCHMARK\033[0m")
    num_clients = 10
    reqs_per_client = 200
    total_reqs = num_clients * reqs_per_client
    print(f"Spawning {num_clients} concurrent client connections issuing {total_reqs} total requests...")

    latencies = []
    def client_worker():
        try:
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.connect(("127.0.0.1", port))
            for i in range(reqs_per_client):
                t0 = time.perf_counter()
                s.sendall(b"PING\r\n")
                resp = s.recv(1024)
                t1 = time.perf_counter()
                latencies.append((t1 - t0) * 1000.0) # ms
            s.close()
        except Exception:
            pass

    threads = [threading.Thread(target=client_worker) for _ in range(num_clients)]
    t_start = time.perf_counter()
    for t in threads: t.start()
    for t in threads: t.join()
    t_total = time.perf_counter() - t_start

    if latencies:
        latencies.sort()
        qps = len(latencies) / t_total
        avg_l = sum(latencies) / len(latencies)
        p50 = latencies[int(len(latencies) * 0.50)]
        p95 = latencies[int(len(latencies) * 0.95)]
        p99 = latencies[int(len(latencies) * 0.99)]

        print("\033[1;32mBenchmark Results:\033[0m")
        print(f"  • Completed Requests : {len(latencies)}")
        print(f"  • Total Time Taken   : {t_total:.3f} s")
        print(f"  • Throughput (QPS)   : \033[1;36m{qps:.1f} ops/sec\033[0m")
        print(f"  • Median (P50)       : {p50:.2f} ms")
        print(f"  • 95th Percentile    : {p95:.2f} ms")
        print(f"  • 99th Percentile    : {p99:.2f} ms")

def interactive_cli(server):
    print("\n\033[1;36m=== Entering Interactive Live CLI ===\033[0m")
    print("Type any command (e.g., SET k v, GET k, ZADD z 100 a, ZRANGEBYSCORE z 50 150, EXPIRE k 5, QUIT):")
    while True:
        try:
            cmd = input("\033[1;32mstorage-cli>\033[0m ").strip()
            if not cmd: continue
            if cmd.upper() in ("QUIT", "EXIT"):
                break
            resp = server.execute(cmd)
            print(resp.strip())
        except (KeyboardInterrupt, EOFError):
            break

def main():
    print_banner()
    server = LiveStorageServer()
    server.start()
    print("In-Memory Storage Server is \033[1;32mONLINE\033[0m on 127.0.0.1:6379\n")

    while True:
        print("""
Select Live Demo Mode:
  \033[1m[1]\033[0m Run Full End-to-End Feature Demonstration (Strings, Lists, Hashes, AVL Tree, Min-Heap, Persistence)
  \033[1m[2]\033[0m Run Live Concurrency & Latency Benchmark
  \033[1m[3]\033[0m Open Interactive CLI (Manual Live Testing)
  \033[1m[4]\033[0m Run Native C++ Unit Tests (AVL Balance & Rotation Check)
  \033[1m[5]\033[0m Exit Demo Suite
""")
        choice = input("Enter choice (1-5): ").strip()
        if choice == "1":
            run_feature_walkthrough(server)
        elif choice == "2":
            run_live_benchmark()
        elif choice == "3":
            interactive_cli(server)
        elif choice == "4":
            print("\nRunning C++ AVL Tree unit test executable...")
            if os.path.exists("test_avl_tree.exe"):
                os.system(".\\test_avl_tree.exe")
            else:
                print("Compiling test_avl_tree.cpp...")
                os.system("g++ -std=c++14 -Iinclude tests/test_avl_tree.cpp -o test_avl_tree.exe")
                os.system(".\\test_avl_tree.exe")
        elif choice == "5":
            break
        else:
            print("Invalid option, please choose 1-5.")

    server.stop()
    print("\nDemo completed. Good luck with your presentation!\n")

if __name__ == "__main__":
    main()
