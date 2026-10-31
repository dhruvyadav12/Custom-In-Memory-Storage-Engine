#!/usr/bin/env python3
"""
Automated Multi-Scenario Benchmark Suite
Tests concurrent connections, operations/second, and latency distributions.
"""

import time
import threading
import statistics
import socket
from typing import List, Dict

def worker(host: str, port: int, commands: List[str], latencies: List[float]):
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        s.connect((host, port))
        
        for cmd in commands:
            t0 = time.perf_counter()
            s.sendall(cmd.encode("utf-8") + b"\r\n")
            # Read until newline
            resp = b""
            while b"\r\n" not in resp:
                chunk = s.recv(1024)
                if not chunk:
                    break
                resp += chunk
            t1 = time.perf_counter()
            latencies.append((t1 - t0) * 1000.0) # ms
        s.close()
    except Exception as e:
        print(f"Worker error: {e}")

def run_scenario(name: str, host: str, port: int, num_clients: int, requests_per_client: int, cmd_generator):
    print(f"\n--- Running Benchmark Scenario: {name} ---")
    threads = []
    latencies_per_thread = [[] for _ in range(num_clients)]

    total_reqs = num_clients * requests_per_client
    start = time.perf_counter()

    for i in range(num_clients):
        cmds = [cmd_generator(i, j) for j in range(requests_per_client)]
        t = threading.Thread(target=worker, args=(host, port, cmds, latencies_per_thread[i]))
        threads.append(t)
        t.start()

    for t in threads:
        t.join()

    elapsed = time.perf_counter() - start
    all_latencies = [l for t_lats in latencies_per_thread for l in t_lats]

    if not all_latencies:
        print("Scenario failed (no responses).")
        return

    all_latencies.sort()
    qps = len(all_latencies) / elapsed
    avg_l = statistics.mean(all_latencies)
    p50 = all_latencies[int(len(all_latencies) * 0.50)]
    p95 = all_latencies[int(len(all_latencies) * 0.95)]
    p99 = all_latencies[int(len(all_latencies) * 0.99)]

    print(f"Completed {len(all_latencies)} requests across {num_clients} concurrent clients in {elapsed:.3f}s")
    print(f"Throughput : {qps:.1f} ops/sec")
    print(f"Latency    : Avg: {avg_l:.2f} ms | P50: {p50:.2f} ms | P95: {p95:.2f} ms | P99: {p99:.2f} ms")

if __name__ == "__main__":
    HOST = "127.0.0.1"
    PORT = 6379
    CLIENTS = 10
    REQS = 500

    print("=========================================================")
    print(" CUSTOM IN-MEMORY STORAGE ENGINE - BENCHMARK SUITE")
    print("=========================================================")

    try:
        run_scenario("PING / PONG", HOST, PORT, CLIENTS, REQS,
                     lambda c, r: "PING")
        run_scenario("SET (Strings)", HOST, PORT, CLIENTS, REQS,
                     lambda c, r: f"SET key:{c}:{r} value_{r}")
        run_scenario("GET (Strings)", HOST, PORT, CLIENTS, REQS,
                     lambda c, r: f"GET key:{c}:{r % 100}")
        run_scenario("LPUSH (Lists)", HOST, PORT, CLIENTS, REQS,
                     lambda c, r: f"LPUSH list:{c} item_{r}")
        run_scenario("ZADD (AVL Tree Sorted Sets)", HOST, PORT, CLIENTS, REQS,
                     lambda c, r: f"ZADD leaderboard:{c} {r * 1.5} player_{r}")
        run_scenario("ZRANGEBYSCORE (AVL Tree Range)", HOST, PORT, CLIENTS, REQS,
                     lambda c, r: f"ZRANGEBYSCORE leaderboard:{c} 50 150")
    except ConnectionRefusedError:
        print("\nNote: Server is not running locally. Start './storage_server' first to run benchmarks live.")
