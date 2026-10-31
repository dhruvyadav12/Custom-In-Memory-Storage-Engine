# Custom In-Memory Storage Engine

[![C++17](https://img.shields.io/badge/Language-C%2B%2B17-blue.svg)](https://en.cppreference.com/w/cpp/17)
[![Platform](https://img.shields.io/badge/Platform-Linux%20%7C%20POSIX-orange.svg)](https://www.kernel.org/)
[![Network](https://img.shields.io/badge/Multiplexing-epoll%20%2F%20Non--Blocking-green.svg)](https://man7.org/linux/man-pages/man7/epoll.7.html)
[![Build Status](https://img.shields.io/badge/Build-Passing-brightgreen.svg)]()
[![License](https://img.shields.io/badge/License-MIT-purple.svg)](LICENSE)

A high-performance, distributed-ready in-memory key-value storage engine engineered from scratch in **C++17**. Built with native Linux socket programming, non-blocking I/O multiplexing (`epoll`), a custom self-balancing **AVL tree** for sorted range queries, a **min-heap** TTL key expiration system, custom **TLV (Type-Length-Value)** binary wire serialization, and a **worker thread pool** with mutexes and condition variables for thread-safe concurrent processing.

---

## Key Highlights & Capabilities

- **Redis-Like In-Memory Key-Value Store**: Full native support for **Strings**, **Lists**, **Sets**, **Hash Maps**, and **Sorted Sets**.
- **Multi-Client Event-Driven TCP Server**: Powered by Linux `epoll` and non-blocking I/O to handle thousands of concurrent client connections with low latency.
- **TLV-Based Binary Serialization**: Custom Type-Length-Value protocol for zero-copy binary network communication and durable point-in-time snapshot persistence (`dump.tlv`).
- **TTL-Based Key Expiration via Min-Heap**: Dual expiration engine combining passive lazy eviction on access with active background sweeps using a min-heap to inspect the earliest expiring key in $\mathcal{O}(1)$ time.
- **AVL Tree Indexing for Range Queries**: Custom height-balanced binary search tree implementing 4-way self-balancing rotations (LL, RR, LR, RL) with logarithmic $\mathcal{O}(\log N + K)$ range queries.
- **Thread Pool & Concurrency**: Multi-threaded worker queue synchronized via `std::mutex` and `std::condition_variable` with 32-way partitioned key-space sharding for high-throughput concurrent access.

---

## System Architecture

```
                      +-----------------------------+
                      |   Client TCP Connections    |
                      +--------------+--------------+
                                     |
                       (Non-Blocking Socket I/O)
                                     v
                 +---------------------------------------+
                 |       Linux epoll Event Loop          |
                 |  - Multiplexes EPOLLIN / EPOLLOUT     |
                 |  - Accepts incoming connections       |
                 |  - Drives periodic 100ms timer ticks  |
                 +-------------------+-------------------+
                                     |
       +-----------------------------+-----------------------------+
       |                                                           |
       v                                                           v
+--------------+                                           +---------------+
| TLV Decoder  | (Magic 0xA5)                              | Text Protocol | (RESP / ASCII)
+-------+------+                                           +-------+-------+
        \                                                         /
         \---------------------------+---------------------------/
                                     |
                                     v
                      +-----------------------------+
                      |     Command Dispatcher      |
                      +--------------+--------------+
                                     |
        +----------------------------+----------------------------+
        |                            |                            |
        v                            v                            v
+---------------+            +---------------+            +---------------+
| Shard 0 Lock  |            | Shard 1 Lock  |   ...      | Shard 31 Lock |
| Hash Table    |            | Hash Table    |            | Hash Table    |
+-------+-------+            +-------+-------+            +-------+-------+
        |                            |                            |
        +----------------------------+----------------------------+
                                     |
                      +--------------+--------------+
                      |       Data Structures       |
                      |  - Strings (std::string)    |
                      |  - Lists (std::deque)       |
                      |  - Sets (std::unordered_set)|
                      |  - Hashes (unordered_map)   |
                      |  - Sorted Sets (AVL Tree)   |
                      +--------------+--------------+
                                     |
       +-----------------------------+-----------------------------+
       |                                                           |
       v                                                           v
+-------------------------------+                       +-------------------------------+
|     Min-Heap TTL Manager      |                       |       Worker ThreadPool       |
| - O(1) peek next expiring key |                       | - Mutex & Condition Variables |
| - Active background sweep     |                       | - Asynchronous BGSAVE snapshot|
| - Passive on-access check     |                       | - Heavy analytical operations |
+-------------------------------+                       +-------------------------------+
                                                                   |
                                                                   v
                                                        +-------------------------------+
                                                        |      dump.tlv Persistence     |
                                                        | - Binary TLV snapshot format  |
                                                        | - Atomic temporary file rename|
                                                        +-------------------------------+
```

---

## Project Structure

```
Custom-In-Memory-Storage-Engine/
├── .github/
│   └── workflows/
│       └── ci.yml               # Automated CI pipeline (Build, Unit Tests, Integration)
├── CMakeLists.txt               # Cross-platform CMake 3.14+ build configuration
├── Makefile                     # Native GNU Makefile
├── Dockerfile                   # Multi-stage production container build
├── docker-compose.yml           # Instant container orchestration
├── README.md                    # Comprehensive technical documentation
├── docs/
│   ├── ARCHITECTURE.md          # Deep dive into threading, sharding & event loop
│   ├── TLV_SPEC.md              # Binary TLV wire & snapshot protocol specification
│   └── BENCHMARKS.md            # Throughput & latency analysis under load
├── include/
│   ├── datastructures/
│   │   ├── avl_tree.hpp         # Height-balanced AVL tree with range queries
│   │   └── min_heap.hpp         # Min-heap priority queue for TTL expiration
│   ├── network/
│   │   ├── socket.hpp           # Non-blocking socket & network abstraction
│   │   ├── connection.hpp       # Connection buffer & protocol framer
│   │   └── event_loop.hpp       # Linux epoll & select event loop
│   ├── protocol/
│   │   ├── tlv.hpp              # TLV packet encoder/decoder
│   │   └── command.hpp          # Command parser & execution router
│   ├── storage/
│   │   ├── storage_engine.hpp   # 32-way sharded in-memory database engine
│   │   ├── ttl_manager.hpp      # Min-heap key expiration manager
│   │   └── persistence.hpp      # TLV snapshot persistence (SAVE / BGSAVE)
│   ├── threadpool/
│   │   └── thread_pool.hpp      # ThreadPool with mutexes & condition variables
│   └── utils/
│       └── logger.hpp           # Thread-safe colorized logging utility
├── src/
│   ├── server_main.cpp          # Server daemon entry point with signal handling
│   ├── network/
│   │   ├── socket.cpp
│   │   ├── connection.cpp
│   │   └── event_loop.cpp
│   ├── protocol/
│   │   ├── tlv.cpp
│   │   └── command.cpp
│   ├── storage/
│   │   ├── storage_engine.cpp
│   │   ├── ttl_manager.cpp
│   │   └── persistence.cpp
│   └── threadpool/
│       └── thread_pool.cpp
├── client/
│   ├── storage_client.cpp       # Interactive C++ REPL & CLI client (TLV & Text)
│   └── client.py                # Reusable Python client library
├── benchmark/
│   ├── bench_storage.cpp        # High-concurrency C++ load test tool
│   └── benchmark.py             # Python benchmarking script with latency graphs
└── tests/
    ├── test_avl_tree.cpp        # Unit tests: AVL balance, rotations, range queries
    ├── test_min_heap.cpp        # Unit tests: Min-heap ordering & TTL eviction
    ├── test_tlv.cpp             # Unit tests: TLV wire protocol framing
    ├── test_thread_pool.cpp     # Unit tests: Concurrency, futures & synchronization
    ├── test_storage_engine.cpp  # System tests: All data structures & snapshotting
    └── test_integration.py      # End-to-end TCP socket integration tests
```

---

## Command Reference

### 1. Strings
| Command | Syntax | Description |
|---------|--------|-------------|
| `SET` | `SET key value [EX seconds] [PX millis]` | Stores string with optional TTL |
| `GET` | `GET key` | Retrieves string value |
| `DEL` | `DEL key [key ...]` | Deletes one or more keys |
| `APPEND`| `APPEND key value` | Appends value to existing string |
| `STRLEN`| `STRLEN key` | Returns string length |
| `INCR`  | `INCR key` / `DECR key` | Increments or decrements integer value |
| `INCRBY`| `INCRBY key amount` | Increments by specified integer amount |
| `MGET`  | `MGET key [key ...]` | Retrieves multiple keys in batch |
| `MSET`  | `MSET key val [key val ...]` | Sets multiple keys in batch |

### 2. Lists
| Command | Syntax | Description |
|---------|--------|-------------|
| `LPUSH` | `LPUSH key value [value ...]` | Prepends values to list head |
| `RPUSH` | `RPUSH key value [value ...]` | Appends values to list tail |
| `LPOP`  | `LPOP key` | Removes and returns first element |
| `RPOP`  | `RPOP key` | Removes and returns last element |
| `LRANGE`| `LRANGE key start stop` | Returns range of elements |
| `LLEN`  | `LLEN key` | Returns list length |
| `LINDEX`| `LINDEX key index` | Returns element at index |

### 3. Sets
| Command | Syntax | Description |
|---------|--------|-------------|
| `SADD`  | `SADD key member [member ...]` | Adds unique members to set |
| `SREM`  | `SREM key member [member ...]` | Removes members from set |
| `SMEMBERS` | `SMEMBERS key` | Returns all set members |
| `SISMEMBER`| `SISMEMBER key member` | Tests set membership |
| `SCARD` | `SCARD key` | Returns set cardinality (size) |
| `SUNION`| `SUNION key [key ...]` | Computes union of sets |
| `SINTER`| `SINTER key [key ...]` | Computes intersection of sets |
| `SDIFF` | `SDIFF key [key ...]` | Computes difference of sets |

### 4. Hash Maps
| Command | Syntax | Description |
|---------|--------|-------------|
| `HSET`  | `HSET key field value` | Sets field in hash map |
| `HGET`  | `HGET key field` | Retrieves field value |
| `HDEL`  | `HDEL key field [field ...]` | Deletes one or more fields |
| `HGETALL`| `HGETALL key` | Returns all field-value pairs |
| `HEXISTS`| `HEXISTS key field` | Checks if field exists |
| `HKEYS` | `HKEYS key` | Returns all field names |
| `HVALS` | `HVALS key` | Returns all values |
| `HLEN`  | `HLEN key` | Returns number of fields |

### 5. Sorted Sets (AVL Tree Range Queries)
| Command | Syntax | Description |
|---------|--------|-------------|
| `ZADD`  | `ZADD key score member` | Inserts or updates member score in AVL tree |
| `ZSCORE`| `ZSCORE key member` | Returns member score |
| `ZRANGE`| `ZRANGE key start stop` | Returns members ordered by rank/score |
| `ZRANGEBYSCORE` | `ZRANGEBYSCORE key min max` | Logarithmic range query $\mathcal{O}(\log N + K)$ |
| `ZREM`  | `ZREM key member` | Removes member from AVL tree |
| `ZCARD` | `ZCARD key` | Returns number of members in sorted set |

### 6. Key Expiration & Server Management
| Command | Syntax | Description |
|---------|--------|-------------|
| `EXPIRE`| `EXPIRE key seconds` | Sets TTL expiration in seconds |
| `PEXPIRE`| `PEXPIRE key millis` | Sets TTL expiration in milliseconds |
| `TTL` / `PTTL` | `TTL key` / `PTTL key` | Returns remaining TTL |
| `PERSIST` | `PERSIST key` | Removes TTL, making key persistent |
| `KEYS`  | `KEYS [pattern]` | Matches keys by glob pattern |
| `EXISTS`| `EXISTS key` | Checks if key exists |
| `TYPE`  | `TYPE key` | Returns data type (`string`, `list`, `set`, `hash`, `zset`) |
| `DBSIZE`| `DBSIZE` | Returns total key count |
| `FLUSHALL` | `FLUSHALL` | Clears all keys from memory |
| `SAVE`  | `SAVE` | Synchronously saves database to `dump.tlv` |
| `BGSAVE`| `BGSAVE` | Asynchronously offloads snapshotting to ThreadPool |
| `INFO`  | `INFO` | Displays server runtime statistics |
| `PING`  | `PING [msg]` | Health check |

---

## Build & Installation

### Prerequisites
- Linux / POSIX system (or Docker / WSL on Windows)
- GCC / G++ (supporting C++17)
- CMake 3.14+ (or GNU Make)
- Python 3.8+ (optional, for client and integration test suite)

### Building with CMake
```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

### Building with Makefile
```bash
make -j$(nproc)
```

### Running with Docker
```bash
# Build and run with Docker Compose
docker compose up -d

# Or build standalone container
docker build -t storage-engine .
docker run -p 6379:6379 storage-engine
```

---

## Running the Server & Client

### 1. Launch the Storage Server
```bash
./build/storage_server --host 0.0.0.0 --port 6379 --threads 4
```

### 2. Connect with Interactive CLI Client
```bash
# Connect in interactive text mode
./build/storage_client --host 127.0.0.1 --port 6379

# Connect using Binary TLV mode
./build/storage_client --host 127.0.0.1 --port 6379 --tlv
```

Example session:
```text
storage-cli 127.0.0.1:6379> SET user:100 "Dhruv Yadav"
+ OK
storage-cli 127.0.0.1:6379> GET user:100
"Dhruv Yadav"
storage-cli 127.0.0.1:6379> ZADD leaderboard 1500 "alice"
:1
storage-cli 127.0.0.1:6379> ZADD leaderboard 2200 "bob"
:1
storage-cli 127.0.0.1:6379> ZRANGEBYSCORE leaderboard 1000 2000
1) "alice"
2) 1500.000000
storage-cli 127.0.0.1:6379> EXPIRE user:100 60
:1
storage-cli 127.0.0.1:6379> TTL user:100
:60
storage-cli 127.0.0.1:6379> BGSAVE
+ Background saving started
```

---

## Running Benchmarks & Tests

### Execute Unit Test Suite
```bash
cd build && ctest --output-on-failure
# Or via Makefile:
make test
```

### Run Multi-Client Concurrency Benchmark
```bash
./build/bench_storage --host 127.0.0.1 --port 6379 -c 20 -n 2500 -t SET
./build/bench_storage --host 127.0.0.1 --port 6379 -c 20 -n 2500 -t GET
./build/bench_storage --host 127.0.0.1 --port 6379 -c 20 -n 2500 -t ZADD
```

### Run Python Integration Tests
```bash
python3 tests/test_integration.py
```

---

## Author & License

Developed by **Dhruv Yadav** ([@dhruvyadav12](https://github.com/dhruvyadav12)).
Licensed under the [MIT License](LICENSE).
