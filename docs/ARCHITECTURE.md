# System Architecture: Custom In-Memory Storage Engine

A high-performance, Redis-inspired in-memory key-value database built with modern C++17, socket programming, non-blocking I/O, event-driven multiplexing (`epoll`), custom data structures (AVL Trees, Min-Heaps), and a thread pool.

```
                      +-----------------------------+
                      |   Client TCP Connections    |
                      +--------------+--------------+
                                     |
                       (Non-Blocking Socket I/O)
                                     v
                 +---------------------------------------+
                 |       Linux epoll Event Loop          |
                 |  - Accepts concurrent connections     |
                 |  - Multiplexes EPOLLIN / EPOLLOUT     |
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

## 1. Concurrency Model & Event-Driven I/O

The storage engine employs an **Event-Driven Non-Blocking Architecture** combined with a **Worker ThreadPool**:
- **Main Event Loop (`EventLoop`)**:
  - Uses Linux `epoll` (`epoll_create1`, `epoll_ctl`, `epoll_wait`).
  - Sockets are made non-blocking (`O_NONBLOCK`).
  - Reads data into per-connection ring/buffers without blocking other clients.
  - Flushes responses back when the socket becomes writable (`EPOLLOUT`).
- **Sharded Locking**:
  - The key space is partitioned across 32 shards: `shard_idx = hash(key) % 32`.
  - Each shard is protected by an independent mutex.
  - Concurrent operations on keys in different shards execute in parallel with zero lock contention!
- **Worker ThreadPool**:
  - Employs a thread pool synchronized using `std::mutex` and `std::condition_variable`.
  - Offloads snapshotting (`BGSAVE`) and CPU-intensive operations so the main event loop never stalls.

---

## 2. Data Structures & AVL Tree Queries

In addition to standard Redis types (Strings, Lists, Sets, Hashes), the engine features a custom, from-scratch **AVL Tree** (`storage::AVLTree<Key, Value>`):
- **Self-Balancing Guarantee**:
  - Height is strictly bounded: $h \le 1.44 \log_2(N)$.
  - Rebalances via Left-Left, Right-Right, Left-Right, and Right-Left rotations.
- **Sorted Set (`ZSET`) Range Queries**:
  - Elements are ordered by `(score, member)`.
  - Supports `ZRANGEBYSCORE key min max` executed via logarithmic range traversal in $\mathcal{O}(\log N + K)$ where $K$ is the number of matching elements.

---

## 3. Min-Heap Key Expiration (TTL)

Cache expiration uses a dual-action policy modeled after Redis:
1. **Passive / Lazy Expiration**:
   - On every read or write, `TTLManager::is_expired(key)` is checked. If expired, it is immediately deleted and `nil` is returned.
2. **Active / Scheduled Expiration**:
   - Keys with expiration timestamps are stored in a **Min-Heap** (`TTLMinHeap`).
   - The root of the min-heap always points to the key expiring soonest.
   - Every 100ms, the event loop inspects the root. If `root.expire_at <= now`, it is popped and evicted. If not, no keys in the heap have expired, completing in $\mathcal{O}(1)$ time!
   - Stale versions caused by key rewrites or `PERSIST` are version-tagged and lazily skipped.

---

## 4. TLV Serialization Protocol

All data can be serialized using the binary Type-Length-Value (TLV) wire protocol:
- **Magic**: `0xA5` (1 byte)
- **Type**: `0x01` - `0x14` (1 byte)
- **Length**: 32-bit big-endian unsigned integer (4 bytes)
- **Value**: Payload bytes (Length bytes)
- Used for both zero-overhead network communications and durable snapshots (`dump.tlv`).
