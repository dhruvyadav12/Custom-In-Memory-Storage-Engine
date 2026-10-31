#pragma once

#include "protocol/tlv.hpp"
#include "storage/storage_engine.hpp"
#include "threadpool/thread_pool.hpp"
#include <atomic>
#include <string>

namespace storage {

class PersistenceManager {
public:
    explicit PersistenceManager(std::string dump_filepath = "dump.tlv");

    bool save(StorageEngine& engine);
    bool bgsave(StorageEngine& engine, ThreadPool& pool);
    bool load(StorageEngine& engine);

    bool is_saving() const { return is_bgsaving_.load(); }
    uint64_t last_save_time() const { return last_save_time_ms_.load(); }
    const std::string& filepath() const { return filepath_; }

private:
    std::string filepath_;
    std::atomic<bool> is_bgsaving_{false};
    std::atomic<uint64_t> last_save_time_ms_{0};

    bool write_snapshot_to_file(const StorageEngine::DatabaseSnapshot& snapshot, const std::string& path);
};

} // namespace storage
