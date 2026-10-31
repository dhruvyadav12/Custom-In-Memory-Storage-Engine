#pragma once

#include "datastructures/avl_tree.hpp"
#include "storage/ttl_manager.hpp"
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace storage {

enum class DataType {
    NONE = 0,
    STRING,
    LIST,
    SET,
    HASH,
    AVL_ZSET
};

struct SortedSetData {
    // Score -> Member mapping in balanced AVL Tree for range queries O(log N + K)
    // Key in AVL tree is pair<double, std::string> to support distinct members with identical scores
    struct ScoreMember {
        double score;
        std::string member;

        bool operator<(const ScoreMember& other) const {
            if (score != other.score) return score < other.score;
            return member < other.member;
        }
        bool operator>(const ScoreMember& other) const {
            if (score != other.score) return score > other.score;
            return member > other.member;
        }
    };

    AVLTree<ScoreMember, int> avl_tree;
    std::unordered_map<std::string, double> member_scores;
};

struct ValueEntry {
    DataType type = DataType::NONE;
    std::string str_val;
    std::deque<std::string> list_val;
    std::unordered_set<std::string> set_val;
    std::unordered_map<std::string, std::string> hash_val;
    std::unique_ptr<SortedSetData> zset_val;

    ValueEntry() = default;
    explicit ValueEntry(DataType t) : type(t) {
        if (t == DataType::AVL_ZSET) {
            zset_val = std::make_unique<SortedSetData>();
        }
    }
};

class StorageEngine {
public:
    static constexpr size_t NUM_SHARDS = 32;

    StorageEngine();
    ~StorageEngine() = default;

    // --- String Commands ---
    bool set(const std::string& key, const std::string& val, uint64_t ttl_sec = 0, uint64_t pttl_ms = 0);
    std::optional<std::string> get(const std::string& key);
    int64_t append(const std::string& key, const std::string& val);
    int64_t strlen_cmd(const std::string& key);
    std::optional<int64_t> incrby(const std::string& key, int64_t amount);
    std::vector<std::optional<std::string>> mget(const std::vector<std::string>& keys);
    bool mset(const std::vector<std::pair<std::string, std::string>>& kvs);

    // --- List Commands ---
    int64_t lpush(const std::string& key, const std::vector<std::string>& values);
    int64_t rpush(const std::string& key, const std::vector<std::string>& values);
    std::optional<std::string> lpop(const std::string& key);
    std::optional<std::string> rpop(const std::string& key);
    std::vector<std::string> lrange(const std::string& key, int64_t start, int64_t stop);
    int64_t llen(const std::string& key);
    std::optional<std::string> lindex(const std::string& key, int64_t index);

    // --- Set Commands ---
    int64_t sadd(const std::string& key, const std::vector<std::string>& members);
    int64_t srem(const std::string& key, const std::vector<std::string>& members);
    std::vector<std::string> smembers(const std::string& key);
    bool sismember(const std::string& key, const std::string& member);
    int64_t scard(const std::string& key);
    std::vector<std::string> sunion(const std::vector<std::string>& keys);
    std::vector<std::string> sinter(const std::vector<std::string>& keys);
    std::vector<std::string> sdiff(const std::vector<std::string>& keys);

    // --- Hash Commands ---
    int64_t hset(const std::string& key, const std::string& field, const std::string& val);
    std::optional<std::string> hget(const std::string& key, const std::string& field);
    int64_t hdel(const std::string& key, const std::vector<std::string>& fields);
    std::vector<std::pair<std::string, std::string>> hgetall(const std::string& key);
    bool hexists(const std::string& key, const std::string& field);
    std::vector<std::string> hkeys(const std::string& key);
    std::vector<std::string> hvals(const std::string& key);
    int64_t hlen(const std::string& key);

    // --- Sorted Set Commands (AVL Tree) ---
    int64_t zadd(const std::string& key, double score, const std::string& member);
    std::optional<double> zscore(const std::string& key, const std::string& member);
    std::vector<std::pair<std::string, double>> zrange(const std::string& key, int64_t start, int64_t stop);
    std::vector<std::pair<std::string, double>> zrangebyscore(const std::string& key, double min_score, double max_score);
    int64_t zrem(const std::string& key, const std::string& member);
    int64_t zcard(const std::string& key);

    // --- Generic & Key Expiration Commands ---
    int64_t del(const std::vector<std::string>& keys);
    bool exists(const std::string& key);
    std::string type(const std::string& key);
    std::vector<std::string> keys(const std::string& pattern = "*");
    size_t dbsize();
    void flushall();

    bool expire(const std::string& key, uint64_t seconds);
    bool pexpire(const std::string& key, uint64_t millis);
    int64_t ttl(const std::string& key);
    int64_t pttl(const std::string& key);
    bool persist(const std::string& key);

    // Active key eviction (invoked by background thread or periodic event)
    size_t purge_expired(size_t max_keys = 100);

    // Snapshot data export / import for TLV persistence
    struct DatabaseSnapshot {
        std::vector<std::pair<std::string, std::string>> strings;
        std::vector<std::pair<std::string, std::vector<std::string>>> lists;
        std::vector<std::pair<std::string, std::vector<std::string>>> sets;
        std::vector<std::pair<std::string, std::vector<std::pair<std::string, std::string>>>> hashes;
        std::vector<std::pair<std::string, std::vector<std::pair<std::string, double>>>> zsets;
        std::vector<std::pair<std::string, uint64_t>> ttls_ms;
    };
    DatabaseSnapshot create_snapshot();
    void load_snapshot(const DatabaseSnapshot& snapshot);

private:
    struct Shard {
        mutable std::mutex mutex;
        std::unordered_map<std::string, ValueEntry> entries;
    };

    std::vector<Shard> shards_;
    TTLManager ttl_manager_;

    size_t get_shard_index(const std::string& key) const {
        return std::hash<std::string>{}(key) % NUM_SHARDS;
    }

    // Must be called with shard lock held
    bool check_and_erase_expired_locked(size_t shard_idx, const std::string& key);
};

} // namespace storage
