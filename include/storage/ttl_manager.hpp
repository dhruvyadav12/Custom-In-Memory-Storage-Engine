#pragma once

#include "datastructures/min_heap.hpp"
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace storage {

class TTLManager {
public:
    TTLManager();
    ~TTLManager() = default;

    static uint64_t current_time_ms() {
        return static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()
            ).count()
        );
    }

    void set_ttl(const std::string& key, uint64_t ttl_seconds);
    void set_pttl(const std::string& key, uint64_t ttl_millis);
    void set_expire_at(const std::string& key, uint64_t expire_at_ms);

    bool persist(const std::string& key);
    int64_t get_ttl(const std::string& key);
    int64_t get_pttl(const std::string& key);

    bool has_ttl(const std::string& key);
    bool is_expired(const std::string& key);

    // Evicts up to batch_size expired keys by inspecting the min-heap root.
    // Returns list of keys that were evicted.
    std::vector<std::string> evict_expired(size_t batch_size = 100);

    void remove_key(const std::string& key);
    void clear();

    size_t size() const;

private:
    struct KeyInfo {
        uint64_t expire_at_ms;
        uint64_t version;
    };

    mutable std::mutex mutex_;
    TTLMinHeap min_heap_;
    std::unordered_map<std::string, KeyInfo> key_info_;
    uint64_t next_version_ = 1;
};

} // namespace storage
