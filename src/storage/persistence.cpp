#include "storage/persistence.hpp"
#include "utils/logger.hpp"
#include <chrono>
#include <fstream>
#include <sstream>

namespace storage {

constexpr uint32_t SNAPSHOT_MAGIC = 0x544C5631; // "TLV1"
constexpr uint32_t SNAPSHOT_VERSION = 1;

PersistenceManager::PersistenceManager(std::string dump_filepath)
    : filepath_(std::move(dump_filepath)) {}

bool PersistenceManager::write_snapshot_to_file(const StorageEngine::DatabaseSnapshot& snapshot, const std::string& path) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        LOG_ERROR("Persistence: Failed to open file for writing: ", path);
        return false;
    }

    // 1. Write Header
    uint32_t magic_net = TLVSerializer::to_network_u32(SNAPSHOT_MAGIC);
    uint32_t ver_net = TLVSerializer::to_network_u32(SNAPSHOT_VERSION);
    uint64_t now_ms = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count()
    );
    uint64_t now_net = TLVSerializer::to_network_u64(now_ms);

    out.write(reinterpret_cast<const char*>(&magic_net), 4);
    out.write(reinterpret_cast<const char*>(&ver_net), 4);
    out.write(reinterpret_cast<const char*>(&now_net), 8);

    // Build TTL lookup
    std::unordered_map<std::string, uint64_t> ttl_map;
    for (const auto& t : snapshot.ttls_ms) {
        ttl_map[t.first] = t.second;
    }

    auto write_record = [&](TLVType type, const std::string& key, const std::vector<uint8_t>& val_payload) {
        // TLV for Key
        auto key_bytes = TLVSerializer::serialize_string(key);
        // TLV for Value
        TLVMessage val_msg(type, val_payload);
        auto val_bytes = TLVSerializer::serialize(val_msg);
        // TLV for TTL
        uint64_t key_ttl = 0;
        auto tit = ttl_map.find(key);
        if (tit != ttl_map.end()) key_ttl = tit->second;
        auto ttl_bytes = TLVSerializer::serialize_int(static_cast<int64_t>(key_ttl));

        // Write Key, Val, TTL
        out.write(reinterpret_cast<const char*>(key_bytes.data()), key_bytes.size());
        out.write(reinterpret_cast<const char*>(val_bytes.data()), val_bytes.size());
        out.write(reinterpret_cast<const char*>(ttl_bytes.data()), ttl_bytes.size());
    };

    // 2. Write Strings
    for (const auto& kv : snapshot.strings) {
        std::vector<uint8_t> payload(kv.second.begin(), kv.second.end());
        write_record(TLVType::DATA_STRING, kv.first, payload);
    }

    // 3. Write Lists
    for (const auto& l : snapshot.lists) {
        std::vector<uint8_t> payload;
        for (const auto& item : l.second) {
            uint32_t len_net = TLVSerializer::to_network_u32(static_cast<uint32_t>(item.size()));
            const uint8_t* p = reinterpret_cast<const uint8_t*>(&len_net);
            payload.insert(payload.end(), p, p + 4);
            payload.insert(payload.end(), item.begin(), item.end());
        }
        write_record(TLVType::DATA_LIST, l.first, payload);
    }

    // 4. Write Sets
    for (const auto& s : snapshot.sets) {
        std::vector<uint8_t> payload;
        for (const auto& item : s.second) {
            uint32_t len_net = TLVSerializer::to_network_u32(static_cast<uint32_t>(item.size()));
            const uint8_t* p = reinterpret_cast<const uint8_t*>(&len_net);
            payload.insert(payload.end(), p, p + 4);
            payload.insert(payload.end(), item.begin(), item.end());
        }
        write_record(TLVType::DATA_SET, s.first, payload);
    }

    // 5. Write Hashes
    for (const auto& h : snapshot.hashes) {
        std::vector<uint8_t> payload;
        for (const auto& fv : h.second) {
            uint32_t flen = TLVSerializer::to_network_u32(static_cast<uint32_t>(fv.first.size()));
            const uint8_t* pf = reinterpret_cast<const uint8_t*>(&flen);
            payload.insert(payload.end(), pf, pf + 4);
            payload.insert(payload.end(), fv.first.begin(), fv.first.end());

            uint32_t vlen = TLVSerializer::to_network_u32(static_cast<uint32_t>(fv.second.size()));
            const uint8_t* pv = reinterpret_cast<const uint8_t*>(&vlen);
            payload.insert(payload.end(), pv, pv + 4);
            payload.insert(payload.end(), fv.second.begin(), fv.second.end());
        }
        write_record(TLVType::DATA_HASH, h.first, payload);
    }

    // 6. Write AVL Sorted Sets
    for (const auto& z : snapshot.zsets) {
        std::vector<uint8_t> payload;
        for (const auto& item : z.second) {
            // Member string
            uint32_t mlen = TLVSerializer::to_network_u32(static_cast<uint32_t>(item.first.size()));
            const uint8_t* pm = reinterpret_cast<const uint8_t*>(&mlen);
            payload.insert(payload.end(), pm, pm + 4);
            payload.insert(payload.end(), item.first.begin(), item.first.end());

            // Score double as string
            std::string score_str = std::to_string(item.second);
            uint32_t slen = TLVSerializer::to_network_u32(static_cast<uint32_t>(score_str.size()));
            const uint8_t* ps = reinterpret_cast<const uint8_t*>(&slen);
            payload.insert(payload.end(), ps, ps + 4);
            payload.insert(payload.end(), score_str.begin(), score_str.end());
        }
        write_record(TLVType::DATA_AVL, z.first, payload);
    }

    out.flush();
    out.close();
    return true;
}

bool PersistenceManager::save(StorageEngine& engine) {
    LOG_INFO("Persistence: Starting synchronous TLV snapshot to ", filepath_);
    auto snapshot = engine.create_snapshot();
    std::string temp_path = filepath_ + ".tmp";
    if (!write_snapshot_to_file(snapshot, temp_path)) {
        return false;
    }

    std::remove(filepath_.c_str());
    if (std::rename(temp_path.c_str(), filepath_.c_str()) != 0) {
        LOG_ERROR("Persistence: Failed to rename temporary snapshot file to ", filepath_);
        return false;
    }

    last_save_time_ms_.store(TTLManager::current_time_ms());
    LOG_INFO("Persistence: Snapshot saved successfully to ", filepath_);
    return true;
}

bool PersistenceManager::bgsave(StorageEngine& engine, ThreadPool& pool) {
    bool expected = false;
    if (!is_bgsaving_.compare_exchange_strong(expected, true)) {
        LOG_WARN("Persistence: Background save is already in progress");
        return false;
    }

    LOG_INFO("Persistence: Scheduling background snapshot via ThreadPool");
    // Snapshot state is captured synchronously in memory (fast)
    auto snapshot = engine.create_snapshot();

    pool.enqueue([this, snap = std::move(snapshot)]() {
        std::string temp_path = filepath_ + ".tmp";
        if (write_snapshot_to_file(snap, temp_path)) {
            std::remove(filepath_.c_str());
            std::rename(temp_path.c_str(), filepath_.c_str());
            last_save_time_ms_.store(TTLManager::current_time_ms());
            LOG_INFO("Persistence: Background snapshot completed successfully.");
        } else {
            LOG_ERROR("Persistence: Background snapshot failed.");
        }
        is_bgsaving_.store(false);
    });

    return true;
}

bool PersistenceManager::load(StorageEngine& engine) {
    std::ifstream in(filepath_, std::ios::binary);
    if (!in.is_open()) {
        LOG_INFO("Persistence: No existing snapshot file found at ", filepath_);
        return false;
    }

    // Read Header
    uint32_t magic_net = 0, ver_net = 0;
    uint64_t time_net = 0;
    in.read(reinterpret_cast<char*>(&magic_net), 4);
    in.read(reinterpret_cast<char*>(&ver_net), 4);
    in.read(reinterpret_cast<char*>(&time_net), 8);

    if (TLVSerializer::from_network_u32(magic_net) != SNAPSHOT_MAGIC) {
        LOG_ERROR("Persistence: Invalid magic header in snapshot file: ", filepath_);
        return false;
    }

    std::vector<uint8_t> buffer((std::istreambuf_iterator<char>(in)),
                                std::istreambuf_iterator<char>());
    in.close();

    StorageEngine::DatabaseSnapshot snapshot;
    size_t offset = 0;

    while (offset < buffer.size()) {
        TLVMessage key_msg, val_msg, ttl_msg;
        size_t c1 = 0, c2 = 0, c3 = 0;

        if (!TLVSerializer::deserialize(buffer.data() + offset, buffer.size() - offset, key_msg, c1)) break;
        offset += c1;

        if (!TLVSerializer::deserialize(buffer.data() + offset, buffer.size() - offset, val_msg, c2)) break;
        offset += c2;

        if (!TLVSerializer::deserialize(buffer.data() + offset, buffer.size() - offset, ttl_msg, c3)) break;
        offset += c3;

        std::string key = key_msg.as_string();
        int64_t ttl_val = ttl_msg.as_int64();
        if (ttl_val > 0) {
            snapshot.ttls_ms.emplace_back(key, static_cast<uint64_t>(ttl_val));
        }

        switch (val_msg.type) {
            case TLVType::DATA_STRING:
                snapshot.strings.emplace_back(key, val_msg.as_string());
                break;
            case TLVType::DATA_LIST:
                snapshot.lists.emplace_back(key, val_msg.as_string_list());
                break;
            case TLVType::DATA_SET:
                snapshot.sets.emplace_back(key, val_msg.as_string_list());
                break;
            case TLVType::DATA_HASH: {
                auto flist = val_msg.as_string_list();
                std::vector<std::pair<std::string, std::string>> hvec;
                for (size_t i = 0; i + 1 < flist.size(); i += 2) {
                    hvec.emplace_back(flist[i], flist[i + 1]);
                }
                snapshot.hashes.emplace_back(key, std::move(hvec));
                break;
            }
            case TLVType::DATA_AVL: {
                auto zlist = val_msg.as_string_list();
                std::vector<std::pair<std::string, double>> zvec;
                for (size_t i = 0; i + 1 < zlist.size(); i += 2) {
                    try {
                        zvec.emplace_back(zlist[i], std::stod(zlist[i + 1]));
                    } catch (...) {}
                }
                snapshot.zsets.emplace_back(key, std::move(zvec));
                break;
            }
            default:
                break;
        }
    }

    engine.load_snapshot(snapshot);
    LOG_INFO("Persistence: Successfully restored snapshot from ", filepath_);
    return true;
}

} // namespace storage
