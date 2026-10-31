#include "storage/storage_engine.hpp"
#include <algorithm>
#include <limits>
#include <regex>

namespace storage {

StorageEngine::StorageEngine() : shards_(NUM_SHARDS) {}

bool StorageEngine::check_and_erase_expired_locked(size_t shard_idx, const std::string& key) {
    if (ttl_manager_.is_expired(key)) {
        shards_[shard_idx].entries.erase(key);
        ttl_manager_.remove_key(key);
        return true;
    }
    return false;
}

// --- Strings ---
bool StorageEngine::set(const std::string& key, const std::string& val, uint64_t ttl_sec, uint64_t pttl_ms) {
    size_t sidx = get_shard_index(key);
    std::unique_lock<std::mutex> lock(shards_[sidx].mutex);

    ValueEntry entry(DataType::STRING);
    entry.str_val = val;
    shards_[sidx].entries[key] = std::move(entry);

    if (pttl_ms > 0) {
        ttl_manager_.set_pttl(key, pttl_ms);
    } else if (ttl_sec > 0) {
        ttl_manager_.set_ttl(key, ttl_sec);
    } else {
        ttl_manager_.persist(key);
    }
    return true;
}

std::optional<std::string> StorageEngine::get(const std::string& key) {
    size_t sidx = get_shard_index(key);
    std::unique_lock<std::mutex> lock(shards_[sidx].mutex);

    if (check_and_erase_expired_locked(sidx, key)) {
        return std::nullopt;
    }

    auto it = shards_[sidx].entries.find(key);
    if (it == shards_[sidx].entries.end() || it->second.type != DataType::STRING) {
        return std::nullopt;
    }
    return it->second.str_val;
}

int64_t StorageEngine::append(const std::string& key, const std::string& val) {
    size_t sidx = get_shard_index(key);
    std::unique_lock<std::mutex> lock(shards_[sidx].mutex);

    check_and_erase_expired_locked(sidx, key);
    auto it = shards_[sidx].entries.find(key);
    if (it == shards_[sidx].entries.end()) {
        ValueEntry entry(DataType::STRING);
        entry.str_val = val;
        shards_[sidx].entries[key] = std::move(entry);
        return static_cast<int64_t>(val.size());
    }

    if (it->second.type != DataType::STRING) {
        return -1;
    }
    it->second.str_val += val;
    return static_cast<int64_t>(it->second.str_val.size());
}

int64_t StorageEngine::strlen_cmd(const std::string& key) {
    size_t sidx = get_shard_index(key);
    std::unique_lock<std::mutex> lock(shards_[sidx].mutex);

    if (check_and_erase_expired_locked(sidx, key)) return 0;
    auto it = shards_[sidx].entries.find(key);
    if (it == shards_[sidx].entries.end() || it->second.type != DataType::STRING) {
        return 0;
    }
    return static_cast<int64_t>(it->second.str_val.size());
}

std::optional<int64_t> StorageEngine::incrby(const std::string& key, int64_t amount) {
    size_t sidx = get_shard_index(key);
    std::unique_lock<std::mutex> lock(shards_[sidx].mutex);

    check_and_erase_expired_locked(sidx, key);
    auto it = shards_[sidx].entries.find(key);
    int64_t current_val = 0;
    if (it != shards_[sidx].entries.end()) {
        if (it->second.type != DataType::STRING) return std::nullopt;
        try {
            current_val = std::stoll(it->second.str_val);
        } catch (...) {
            return std::nullopt;
        }
    }

    current_val += amount;
    ValueEntry entry(DataType::STRING);
    entry.str_val = std::to_string(current_val);
    shards_[sidx].entries[key] = std::move(entry);
    return current_val;
}

std::vector<std::optional<std::string>> StorageEngine::mget(const std::vector<std::string>& keys) {
    std::vector<std::optional<std::string>> results;
    results.reserve(keys.size());
    for (const auto& k : keys) {
        results.push_back(get(k));
    }
    return results;
}

bool StorageEngine::mset(const std::vector<std::pair<std::string, std::string>>& kvs) {
    for (const auto& kv : kvs) {
        set(kv.first, kv.second);
    }
    return true;
}

// --- Lists ---
int64_t StorageEngine::lpush(const std::string& key, const std::vector<std::string>& values) {
    size_t sidx = get_shard_index(key);
    std::unique_lock<std::mutex> lock(shards_[sidx].mutex);

    check_and_erase_expired_locked(sidx, key);
    auto it = shards_[sidx].entries.find(key);
    if (it == shards_[sidx].entries.end()) {
        ValueEntry entry(DataType::LIST);
        for (const auto& v : values) {
            entry.list_val.push_front(v);
        }
        size_t sz = entry.list_val.size();
        shards_[sidx].entries[key] = std::move(entry);
        return static_cast<int64_t>(sz);
    }

    if (it->second.type != DataType::LIST) return -1;
    for (const auto& v : values) {
        it->second.list_val.push_front(v);
    }
    return static_cast<int64_t>(it->second.list_val.size());
}

int64_t StorageEngine::rpush(const std::string& key, const std::vector<std::string>& values) {
    size_t sidx = get_shard_index(key);
    std::unique_lock<std::mutex> lock(shards_[sidx].mutex);

    check_and_erase_expired_locked(sidx, key);
    auto it = shards_[sidx].entries.find(key);
    if (it == shards_[sidx].entries.end()) {
        ValueEntry entry(DataType::LIST);
        for (const auto& v : values) {
            entry.list_val.push_back(v);
        }
        size_t sz = entry.list_val.size();
        shards_[sidx].entries[key] = std::move(entry);
        return static_cast<int64_t>(sz);
    }

    if (it->second.type != DataType::LIST) return -1;
    for (const auto& v : values) {
        it->second.list_val.push_back(v);
    }
    return static_cast<int64_t>(it->second.list_val.size());
}

std::optional<std::string> StorageEngine::lpop(const std::string& key) {
    size_t sidx = get_shard_index(key);
    std::unique_lock<std::mutex> lock(shards_[sidx].mutex);

    if (check_and_erase_expired_locked(sidx, key)) return std::nullopt;
    auto it = shards_[sidx].entries.find(key);
    if (it == shards_[sidx].entries.end() || it->second.type != DataType::LIST) return std::nullopt;
    if (it->second.list_val.empty()) return std::nullopt;

    std::string val = std::move(it->second.list_val.front());
    it->second.list_val.pop_front();
    if (it->second.list_val.empty()) {
        shards_[sidx].entries.erase(it);
        ttl_manager_.remove_key(key);
    }
    return val;
}

std::optional<std::string> StorageEngine::rpop(const std::string& key) {
    size_t sidx = get_shard_index(key);
    std::unique_lock<std::mutex> lock(shards_[sidx].mutex);

    if (check_and_erase_expired_locked(sidx, key)) return std::nullopt;
    auto it = shards_[sidx].entries.find(key);
    if (it == shards_[sidx].entries.end() || it->second.type != DataType::LIST) return std::nullopt;
    if (it->second.list_val.empty()) return std::nullopt;

    std::string val = std::move(it->second.list_val.back());
    it->second.list_val.pop_back();
    if (it->second.list_val.empty()) {
        shards_[sidx].entries.erase(it);
        ttl_manager_.remove_key(key);
    }
    return val;
}

std::vector<std::string> StorageEngine::lrange(const std::string& key, int64_t start, int64_t stop) {
    size_t sidx = get_shard_index(key);
    std::unique_lock<std::mutex> lock(shards_[sidx].mutex);

    if (check_and_erase_expired_locked(sidx, key)) return {};
    auto it = shards_[sidx].entries.find(key);
    if (it == shards_[sidx].entries.end() || it->second.type != DataType::LIST) return {};

    int64_t len = static_cast<int64_t>(it->second.list_val.size());
    if (start < 0) start = std::max<int64_t>(0, len + start);
    if (stop < 0) stop = len + stop;
    if (start >= len || start > stop) return {};
    stop = std::min<int64_t>(stop, len - 1);

    std::vector<std::string> res;
    res.reserve(stop - start + 1);
    for (int64_t i = start; i <= stop; ++i) {
        res.push_back(it->second.list_val[i]);
    }
    return res;
}

int64_t StorageEngine::llen(const std::string& key) {
    size_t sidx = get_shard_index(key);
    std::unique_lock<std::mutex> lock(shards_[sidx].mutex);

    if (check_and_erase_expired_locked(sidx, key)) return 0;
    auto it = shards_[sidx].entries.find(key);
    if (it == shards_[sidx].entries.end() || it->second.type != DataType::LIST) return 0;
    return static_cast<int64_t>(it->second.list_val.size());
}

std::optional<std::string> StorageEngine::lindex(const std::string& key, int64_t index) {
    size_t sidx = get_shard_index(key);
    std::unique_lock<std::mutex> lock(shards_[sidx].mutex);

    if (check_and_erase_expired_locked(sidx, key)) return std::nullopt;
    auto it = shards_[sidx].entries.find(key);
    if (it == shards_[sidx].entries.end() || it->second.type != DataType::LIST) return std::nullopt;

    int64_t len = static_cast<int64_t>(it->second.list_val.size());
    if (index < 0) index = len + index;
    if (index < 0 || index >= len) return std::nullopt;
    return it->second.list_val[index];
}

// --- Sets ---
int64_t StorageEngine::sadd(const std::string& key, const std::vector<std::string>& members) {
    size_t sidx = get_shard_index(key);
    std::unique_lock<std::mutex> lock(shards_[sidx].mutex);

    check_and_erase_expired_locked(sidx, key);
    auto it = shards_[sidx].entries.find(key);
    if (it == shards_[sidx].entries.end()) {
        ValueEntry entry(DataType::SET);
        int64_t added = 0;
        for (const auto& m : members) {
            if (entry.set_val.insert(m).second) ++added;
        }
        shards_[sidx].entries[key] = std::move(entry);
        return added;
    }

    if (it->second.type != DataType::SET) return -1;
    int64_t added = 0;
    for (const auto& m : members) {
        if (it->second.set_val.insert(m).second) ++added;
    }
    return added;
}

int64_t StorageEngine::srem(const std::string& key, const std::vector<std::string>& members) {
    size_t sidx = get_shard_index(key);
    std::unique_lock<std::mutex> lock(shards_[sidx].mutex);

    if (check_and_erase_expired_locked(sidx, key)) return 0;
    auto it = shards_[sidx].entries.find(key);
    if (it == shards_[sidx].entries.end() || it->second.type != DataType::SET) return 0;

    int64_t removed = 0;
    for (const auto& m : members) {
        removed += it->second.set_val.erase(m);
    }
    if (it->second.set_val.empty()) {
        shards_[sidx].entries.erase(it);
        ttl_manager_.remove_key(key);
    }
    return removed;
}

std::vector<std::string> StorageEngine::smembers(const std::string& key) {
    size_t sidx = get_shard_index(key);
    std::unique_lock<std::mutex> lock(shards_[sidx].mutex);

    if (check_and_erase_expired_locked(sidx, key)) return {};
    auto it = shards_[sidx].entries.find(key);
    if (it == shards_[sidx].entries.end() || it->second.type != DataType::SET) return {};

    return std::vector<std::string>(it->second.set_val.begin(), it->second.set_val.end());
}

bool StorageEngine::sismember(const std::string& key, const std::string& member) {
    size_t sidx = get_shard_index(key);
    std::unique_lock<std::mutex> lock(shards_[sidx].mutex);

    if (check_and_erase_expired_locked(sidx, key)) return false;
    auto it = shards_[sidx].entries.find(key);
    if (it == shards_[sidx].entries.end() || it->second.type != DataType::SET) return false;

    return it->second.set_val.find(member) != it->second.set_val.end();
}

int64_t StorageEngine::scard(const std::string& key) {
    size_t sidx = get_shard_index(key);
    std::unique_lock<std::mutex> lock(shards_[sidx].mutex);

    if (check_and_erase_expired_locked(sidx, key)) return 0;
    auto it = shards_[sidx].entries.find(key);
    if (it == shards_[sidx].entries.end() || it->second.type != DataType::SET) return 0;

    return static_cast<int64_t>(it->second.set_val.size());
}

std::vector<std::string> StorageEngine::sunion(const std::vector<std::string>& keys) {
    std::unordered_set<std::string> res_set;
    for (const auto& k : keys) {
        auto members = smembers(k);
        res_set.insert(members.begin(), members.end());
    }
    return std::vector<std::string>(res_set.begin(), res_set.end());
}

std::vector<std::string> StorageEngine::sinter(const std::vector<std::string>& keys) {
    if (keys.empty()) return {};
    auto base_members = smembers(keys[0]);
    std::unordered_set<std::string> res_set(base_members.begin(), base_members.end());

    for (size_t i = 1; i < keys.size(); ++i) {
        auto curr = smembers(keys[i]);
        std::unordered_set<std::string> curr_set(curr.begin(), curr.end());
        for (auto it = res_set.begin(); it != res_set.end();) {
            if (curr_set.find(*it) == curr_set.end()) {
                it = res_set.erase(it);
            } else {
                ++it;
            }
        }
    }
    return std::vector<std::string>(res_set.begin(), res_set.end());
}

std::vector<std::string> StorageEngine::sdiff(const std::vector<std::string>& keys) {
    if (keys.empty()) return {};
    auto base_members = smembers(keys[0]);
    std::unordered_set<std::string> res_set(base_members.begin(), base_members.end());

    for (size_t i = 1; i < keys.size(); ++i) {
        auto curr = smembers(keys[i]);
        for (const auto& m : curr) {
            res_set.erase(m);
        }
    }
    return std::vector<std::string>(res_set.begin(), res_set.end());
}

// --- Hashes ---
int64_t StorageEngine::hset(const std::string& key, const std::string& field, const std::string& val) {
    size_t sidx = get_shard_index(key);
    std::unique_lock<std::mutex> lock(shards_[sidx].mutex);

    check_and_erase_expired_locked(sidx, key);
    auto it = shards_[sidx].entries.find(key);
    if (it == shards_[sidx].entries.end()) {
        ValueEntry entry(DataType::HASH);
        entry.hash_val[field] = val;
        shards_[sidx].entries[key] = std::move(entry);
        return 1;
    }

    if (it->second.type != DataType::HASH) return -1;
    bool is_new = it->second.hash_val.find(field) == it->second.hash_val.end();
    it->second.hash_val[field] = val;
    return is_new ? 1 : 0;
}

std::optional<std::string> StorageEngine::hget(const std::string& key, const std::string& field) {
    size_t sidx = get_shard_index(key);
    std::unique_lock<std::mutex> lock(shards_[sidx].mutex);

    if (check_and_erase_expired_locked(sidx, key)) return std::nullopt;
    auto it = shards_[sidx].entries.find(key);
    if (it == shards_[sidx].entries.end() || it->second.type != DataType::HASH) return std::nullopt;

    auto fit = it->second.hash_val.find(field);
    if (fit == it->second.hash_val.end()) return std::nullopt;
    return fit->second;
}

int64_t StorageEngine::hdel(const std::string& key, const std::vector<std::string>& fields) {
    size_t sidx = get_shard_index(key);
    std::unique_lock<std::mutex> lock(shards_[sidx].mutex);

    if (check_and_erase_expired_locked(sidx, key)) return 0;
    auto it = shards_[sidx].entries.find(key);
    if (it == shards_[sidx].entries.end() || it->second.type != DataType::HASH) return 0;

    int64_t removed = 0;
    for (const auto& f : fields) {
        removed += it->second.hash_val.erase(f);
    }
    if (it->second.hash_val.empty()) {
        shards_[sidx].entries.erase(it);
        ttl_manager_.remove_key(key);
    }
    return removed;
}

std::vector<std::pair<std::string, std::string>> StorageEngine::hgetall(const std::string& key) {
    size_t sidx = get_shard_index(key);
    std::unique_lock<std::mutex> lock(shards_[sidx].mutex);

    if (check_and_erase_expired_locked(sidx, key)) return {};
    auto it = shards_[sidx].entries.find(key);
    if (it == shards_[sidx].entries.end() || it->second.type != DataType::HASH) return {};

    std::vector<std::pair<std::string, std::string>> res;
    res.reserve(it->second.hash_val.size());
    for (const auto& pair : it->second.hash_val) {
        res.push_back(pair);
    }
    return res;
}

bool StorageEngine::hexists(const std::string& key, const std::string& field) {
    size_t sidx = get_shard_index(key);
    std::unique_lock<std::mutex> lock(shards_[sidx].mutex);

    if (check_and_erase_expired_locked(sidx, key)) return false;
    auto it = shards_[sidx].entries.find(key);
    if (it == shards_[sidx].entries.end() || it->second.type != DataType::HASH) return false;
    return it->second.hash_val.find(field) != it->second.hash_val.end();
}

std::vector<std::string> StorageEngine::hkeys(const std::string& key) {
    size_t sidx = get_shard_index(key);
    std::unique_lock<std::mutex> lock(shards_[sidx].mutex);

    if (check_and_erase_expired_locked(sidx, key)) return {};
    auto it = shards_[sidx].entries.find(key);
    if (it == shards_[sidx].entries.end() || it->second.type != DataType::HASH) return {};

    std::vector<std::string> res;
    for (const auto& pair : it->second.hash_val) {
        res.push_back(pair.first);
    }
    return res;
}

std::vector<std::string> StorageEngine::hvals(const std::string& key) {
    size_t sidx = get_shard_index(key);
    std::unique_lock<std::mutex> lock(shards_[sidx].mutex);

    if (check_and_erase_expired_locked(sidx, key)) return {};
    auto it = shards_[sidx].entries.find(key);
    if (it == shards_[sidx].entries.end() || it->second.type != DataType::HASH) return {};

    std::vector<std::string> res;
    for (const auto& pair : it->second.hash_val) {
        res.push_back(pair.second);
    }
    return res;
}

int64_t StorageEngine::hlen(const std::string& key) {
    size_t sidx = get_shard_index(key);
    std::unique_lock<std::mutex> lock(shards_[sidx].mutex);

    if (check_and_erase_expired_locked(sidx, key)) return 0;
    auto it = shards_[sidx].entries.find(key);
    if (it == shards_[sidx].entries.end() || it->second.type != DataType::HASH) return 0;

    return static_cast<int64_t>(it->second.hash_val.size());
}

// --- Sorted Set (AVL Tree) ---
int64_t StorageEngine::zadd(const std::string& key, double score, const std::string& member) {
    size_t sidx = get_shard_index(key);
    std::unique_lock<std::mutex> lock(shards_[sidx].mutex);

    check_and_erase_expired_locked(sidx, key);
    auto it = shards_[sidx].entries.find(key);
    if (it == shards_[sidx].entries.end()) {
        ValueEntry entry(DataType::AVL_ZSET);
        entry.zset_val->member_scores[member] = score;
        entry.zset_val->avl_tree.insert(SortedSetData::ScoreMember{score, member}, 1);
        shards_[sidx].entries[key] = std::move(entry);
        return 1;
    }

    if (it->second.type != DataType::AVL_ZSET) return -1;
    auto& zset = *it->second.zset_val;
    auto mit = zset.member_scores.find(member);
    if (mit != zset.member_scores.end()) {
        if (mit->second == score) return 0; // Unchanged
        // Remove old score from AVL tree
        zset.avl_tree.remove(SortedSetData::ScoreMember{mit->second, member});
        mit->second = score;
        zset.avl_tree.insert(SortedSetData::ScoreMember{score, member}, 1);
        return 0; // Updated
    }

    zset.member_scores[member] = score;
    zset.avl_tree.insert(SortedSetData::ScoreMember{score, member}, 1);
    return 1; // Newly added
}

std::optional<double> StorageEngine::zscore(const std::string& key, const std::string& member) {
    size_t sidx = get_shard_index(key);
    std::unique_lock<std::mutex> lock(shards_[sidx].mutex);

    if (check_and_erase_expired_locked(sidx, key)) return std::nullopt;
    auto it = shards_[sidx].entries.find(key);
    if (it == shards_[sidx].entries.end() || it->second.type != DataType::AVL_ZSET) return std::nullopt;

    auto mit = it->second.zset_val->member_scores.find(member);
    if (mit == it->second.zset_val->member_scores.end()) return std::nullopt;
    return mit->second;
}

std::vector<std::pair<std::string, double>> StorageEngine::zrange(const std::string& key, int64_t start, int64_t stop) {
    size_t sidx = get_shard_index(key);
    std::unique_lock<std::mutex> lock(shards_[sidx].mutex);

    if (check_and_erase_expired_locked(sidx, key)) return {};
    auto it = shards_[sidx].entries.find(key);
    if (it == shards_[sidx].entries.end() || it->second.type != DataType::AVL_ZSET) return {};

    // In-order traversal yields sorted order by (score, member)
    auto sorted_pairs = it->second.zset_val->avl_tree.in_order();
    int64_t len = static_cast<int64_t>(sorted_pairs.size());
    if (start < 0) start = std::max<int64_t>(0, len + start);
    if (stop < 0) stop = len + stop;
    if (start >= len || start > stop) return {};
    stop = std::min<int64_t>(stop, len - 1);

    std::vector<std::pair<std::string, double>> res;
    res.reserve(stop - start + 1);
    for (int64_t i = start; i <= stop; ++i) {
        res.emplace_back(sorted_pairs[i].first.member, sorted_pairs[i].first.score);
    }
    return res;
}

std::vector<std::pair<std::string, double>> StorageEngine::zrangebyscore(
    const std::string& key, double min_score, double max_score) {
    size_t sidx = get_shard_index(key);
    std::unique_lock<std::mutex> lock(shards_[sidx].mutex);

    if (check_and_erase_expired_locked(sidx, key)) return {};
    auto it = shards_[sidx].entries.find(key);
    if (it == shards_[sidx].entries.end() || it->second.type != DataType::AVL_ZSET) return {};

    // Efficient logarithmic range query on AVL Tree
    SortedSetData::ScoreMember min_node{min_score, ""};
    SortedSetData::ScoreMember max_node{max_score, "\x7F\x7F\x7F\x7F"};

    auto in_range = it->second.zset_val->avl_tree.range_query(min_node, max_node);
    std::vector<std::pair<std::string, double>> res;
    res.reserve(in_range.size());
    for (const auto& item : in_range) {
        res.emplace_back(item.first.member, item.first.score);
    }
    return res;
}

int64_t StorageEngine::zrem(const std::string& key, const std::string& member) {
    size_t sidx = get_shard_index(key);
    std::unique_lock<std::mutex> lock(shards_[sidx].mutex);

    if (check_and_erase_expired_locked(sidx, key)) return 0;
    auto it = shards_[sidx].entries.find(key);
    if (it == shards_[sidx].entries.end() || it->second.type != DataType::AVL_ZSET) return 0;

    auto& zset = *it->second.zset_val;
    auto mit = zset.member_scores.find(member);
    if (mit == zset.member_scores.end()) return 0;

    double score = mit->second;
    zset.member_scores.erase(mit);
    zset.avl_tree.remove(SortedSetData::ScoreMember{score, member});

    if (zset.member_scores.empty()) {
        shards_[sidx].entries.erase(it);
        ttl_manager_.remove_key(key);
    }
    return 1;
}

int64_t StorageEngine::zcard(const std::string& key) {
    size_t sidx = get_shard_index(key);
    std::unique_lock<std::mutex> lock(shards_[sidx].mutex);

    if (check_and_erase_expired_locked(sidx, key)) return 0;
    auto it = shards_[sidx].entries.find(key);
    if (it == shards_[sidx].entries.end() || it->second.type != DataType::AVL_ZSET) return 0;

    return static_cast<int64_t>(it->second.zset_val->member_scores.size());
}

// --- Generic ---
int64_t StorageEngine::del(const std::vector<std::string>& keys) {
    int64_t deleted = 0;
    for (const auto& key : keys) {
        size_t sidx = get_shard_index(key);
        std::unique_lock<std::mutex> lock(shards_[sidx].mutex);
        if (shards_[sidx].entries.erase(key) > 0) {
            ttl_manager_.remove_key(key);
            ++deleted;
        }
    }
    return deleted;
}

bool StorageEngine::exists(const std::string& key) {
    size_t sidx = get_shard_index(key);
    std::unique_lock<std::mutex> lock(shards_[sidx].mutex);
    if (check_and_erase_expired_locked(sidx, key)) return false;
    return shards_[sidx].entries.find(key) != shards_[sidx].entries.end();
}

std::string StorageEngine::type(const std::string& key) {
    size_t sidx = get_shard_index(key);
    std::unique_lock<std::mutex> lock(shards_[sidx].mutex);
    if (check_and_erase_expired_locked(sidx, key)) return "none";

    auto it = shards_[sidx].entries.find(key);
    if (it == shards_[sidx].entries.end()) return "none";

    switch (it->second.type) {
        case DataType::STRING: return "string";
        case DataType::LIST: return "list";
        case DataType::SET: return "set";
        case DataType::HASH: return "hash";
        case DataType::AVL_ZSET: return "zset";
        default: return "none";
    }
}

std::vector<std::string> StorageEngine::keys(const std::string& pattern) {
    std::vector<std::string> matched_keys;

    // Convert simple glob pattern (e.g. "user:*", "*") to regex
    std::string regex_str;
    for (char c : pattern) {
        if (c == '*') regex_str += ".*";
        else if (c == '?') regex_str += ".";
        else if (c == '.' || c == '+' || c == '(' || c == ')' || c == '[' || c == ']' || c == '^' || c == '$') {
            regex_str += '\\';
            regex_str += c;
        } else {
            regex_str += c;
        }
    }
    std::regex reg(regex_str);

    for (size_t s = 0; s < NUM_SHARDS; ++s) {
        std::unique_lock<std::mutex> lock(shards_[s].mutex);
        for (auto it = shards_[s].entries.begin(); it != shards_[s].entries.end();) {
            if (ttl_manager_.is_expired(it->first)) {
                ttl_manager_.remove_key(it->first);
                it = shards_[s].entries.erase(it);
            } else {
                if (std::regex_match(it->first, reg)) {
                    matched_keys.push_back(it->first);
                }
                ++it;
            }
        }
    }
    return matched_keys;
}

size_t StorageEngine::dbsize() {
    size_t count = 0;
    for (size_t s = 0; s < NUM_SHARDS; ++s) {
        std::unique_lock<std::mutex> lock(shards_[s].mutex);
        for (auto it = shards_[s].entries.begin(); it != shards_[s].entries.end();) {
            if (ttl_manager_.is_expired(it->first)) {
                ttl_manager_.remove_key(it->first);
                it = shards_[s].entries.erase(it);
            } else {
                ++count;
                ++it;
            }
        }
    }
    return count;
}

void StorageEngine::flushall() {
    for (size_t s = 0; s < NUM_SHARDS; ++s) {
        std::unique_lock<std::mutex> lock(shards_[s].mutex);
        shards_[s].entries.clear();
    }
    ttl_manager_.clear();
}

// --- Expiration ---
bool StorageEngine::expire(const std::string& key, uint64_t seconds) {
    if (!exists(key)) return false;
    ttl_manager_.set_ttl(key, seconds);
    return true;
}

bool StorageEngine::pexpire(const std::string& key, uint64_t millis) {
    if (!exists(key)) return false;
    ttl_manager_.set_pttl(key, millis);
    return true;
}

int64_t StorageEngine::ttl(const std::string& key) {
    if (!exists(key)) return -2;
    return ttl_manager_.get_ttl(key);
}

int64_t StorageEngine::pttl(const std::string& key) {
    if (!exists(key)) return -2;
    return ttl_manager_.get_pttl(key);
}

bool StorageEngine::persist(const std::string& key) {
    if (!exists(key)) return false;
    return ttl_manager_.persist(key);
}

size_t StorageEngine::purge_expired(size_t max_keys) {
    auto expired_keys = ttl_manager_.evict_expired(max_keys);
    for (const auto& key : expired_keys) {
        size_t sidx = get_shard_index(key);
        std::unique_lock<std::mutex> lock(shards_[sidx].mutex);
        shards_[sidx].entries.erase(key);
    }
    return expired_keys.size();
}

StorageEngine::DatabaseSnapshot StorageEngine::create_snapshot() {
    DatabaseSnapshot snap;
    for (size_t s = 0; s < NUM_SHARDS; ++s) {
        std::unique_lock<std::mutex> lock(shards_[s].mutex);
        for (const auto& kv : shards_[s].entries) {
            const auto& key = kv.first;
            if (ttl_manager_.is_expired(key)) continue;

            const auto& entry = kv.second;
            switch (entry.type) {
                case DataType::STRING:
                    snap.strings.emplace_back(key, entry.str_val);
                    break;
                case DataType::LIST:
                    snap.lists.emplace_back(key, std::vector<std::string>(entry.list_val.begin(), entry.list_val.end()));
                    break;
                case DataType::SET:
                    snap.sets.emplace_back(key, std::vector<std::string>(entry.set_val.begin(), entry.set_val.end()));
                    break;
                case DataType::HASH: {
                    std::vector<std::pair<std::string, std::string>> hvec;
                    for (const auto& hkv : entry.hash_val) hvec.push_back(hkv);
                    snap.hashes.emplace_back(key, std::move(hvec));
                    break;
                }
                case DataType::AVL_ZSET: {
                    auto in_order = entry.zset_val->avl_tree.in_order();
                    std::vector<std::pair<std::string, double>> zvec;
                    for (const auto& zitem : in_order) {
                        zvec.emplace_back(zitem.first.member, zitem.first.score);
                    }
                    snap.zsets.emplace_back(key, std::move(zvec));
                    break;
                }
                default:
                    break;
            }

            int64_t rem_pttl = ttl_manager_.get_pttl(key);
            if (rem_pttl > 0) {
                snap.ttls_ms.emplace_back(key, static_cast<uint64_t>(rem_pttl));
            }
        }
    }
    return snap;
}

void StorageEngine::load_snapshot(const DatabaseSnapshot& snapshot) {
    flushall();
    for (const auto& s : snapshot.strings) {
        set(s.first, s.second);
    }
    for (const auto& l : snapshot.lists) {
        rpush(l.first, l.second);
    }
    for (const auto& s : snapshot.sets) {
        sadd(s.first, s.second);
    }
    for (const auto& h : snapshot.hashes) {
        for (const auto& fv : h.second) {
            hset(h.first, fv.first, fv.second);
        }
    }
    for (const auto& z : snapshot.zsets) {
        for (const auto& item : z.second) {
            zadd(z.first, item.second, item.first);
        }
    }
    for (const auto& ttl_item : snapshot.ttls_ms) {
        pexpire(ttl_item.first, ttl_item.second);
    }
}

} // namespace storage
