#include "storage/ttl_manager.hpp"

namespace storage {

TTLManager::TTLManager() = default;

void TTLManager::set_ttl(const std::string& key, uint64_t ttl_seconds) {
    set_pttl(key, ttl_seconds * 1000);
}

void TTLManager::set_pttl(const std::string& key, uint64_t ttl_millis) {
    uint64_t expire_at = current_time_ms() + ttl_millis;
    set_expire_at(key, expire_at);
}

void TTLManager::set_expire_at(const std::string& key, uint64_t expire_at_ms) {
    std::unique_lock<std::mutex> lock(mutex_);
    uint64_t version = ++next_version_;
    key_info_[key] = KeyInfo{expire_at_ms, version};
    min_heap_.push(TTLEntry{key, expire_at_ms, version});
}

bool TTLManager::persist(const std::string& key) {
    std::unique_lock<std::mutex> lock(mutex_);
    auto it = key_info_.find(key);
    if (it == key_info_.end()) {
        return false;
    }
    key_info_.erase(it);
    return true;
}

int64_t TTLManager::get_ttl(const std::string& key) {
    int64_t pttl = get_pttl(key);
    if (pttl < 0) return pttl;
    return (pttl + 999) / 1000;
}

int64_t TTLManager::get_pttl(const std::string& key) {
    std::unique_lock<std::mutex> lock(mutex_);
    auto it = key_info_.find(key);
    if (it == key_info_.end()) {
        return -1; // No TTL associated
    }

    uint64_t now = current_time_ms();
    if (now >= it->second.expire_at_ms) {
        return -2; // Expired
    }
    return static_cast<int64_t>(it->second.expire_at_ms - now);
}

bool TTLManager::has_ttl(const std::string& key) {
    std::unique_lock<std::mutex> lock(mutex_);
    return key_info_.find(key) != key_info_.end();
}

bool TTLManager::is_expired(const std::string& key) {
    std::unique_lock<std::mutex> lock(mutex_);
    auto it = key_info_.find(key);
    if (it == key_info_.end()) {
        return false;
    }
    return current_time_ms() >= it->second.expire_at_ms;
}

void TTLManager::remove_key(const std::string& key) {
    std::unique_lock<std::mutex> lock(mutex_);
    key_info_.erase(key);
}

void TTLManager::clear() {
    std::unique_lock<std::mutex> lock(mutex_);
    key_info_.clear();
    min_heap_.clear();
}

size_t TTLManager::size() const {
    std::unique_lock<std::mutex> lock(mutex_);
    return key_info_.size();
}

std::vector<std::string> TTLManager::evict_expired(size_t batch_size) {
    std::vector<std::string> expired_keys;
    std::unique_lock<std::mutex> lock(mutex_);

    uint64_t now = current_time_ms();
    size_t count = 0;

    while (!min_heap_.empty() && count < batch_size) {
        const TTLEntry& top = min_heap_.top();
        if (top.expire_at_ms > now) {
            // Min-heap property: if root is not expired, none are expired!
            break;
        }

        TTLEntry entry = top;
        min_heap_.pop();

        auto it = key_info_.find(entry.key);
        if (it != key_info_.end() && it->second.version == entry.version) {
            // Valid active expiration
            expired_keys.push_back(entry.key);
            key_info_.erase(it);
            ++count;
        }
        // Stale entries (where version differs or key was removed/re-keyed) are naturally discarded.
    }

    return expired_keys;
}

} // namespace storage
