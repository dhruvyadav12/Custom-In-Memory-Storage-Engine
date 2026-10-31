#pragma once

#include "protocol/tlv.hpp"
#include "storage/persistence.hpp"
#include "storage/storage_engine.hpp"
#include "threadpool/thread_pool.hpp"
#include <memory>
#include <string>
#include <vector>

namespace storage {

enum class ResponseFormat {
    TEXT,
    TLV
};

struct CommandResponse {
    std::string text_output;
    TLVMessage tlv_output;

    std::vector<uint8_t> serialize(ResponseFormat format) const {
        if (format == ResponseFormat::TLV) {
            return TLVSerializer::serialize(tlv_output);
        }
        std::vector<uint8_t> bytes(text_output.begin(), text_output.end());
        return bytes;
    }
};

class CommandDispatcher {
public:
    CommandDispatcher(StorageEngine& engine, PersistenceManager& persistence, ThreadPool& pool);

    // Parses a line of text (e.g. "SET key val") into tokenized arguments
    static std::vector<std::string> parse_text_line(const std::string& line);

    // Executes a command given tokenized arguments
    CommandResponse execute(const std::vector<std::string>& args);

    // Helper to format text responses in Redis/RESP-like human readable output
    static CommandResponse make_ok(const std::string& msg = "OK");
    static CommandResponse make_err(const std::string& err_msg);
    static CommandResponse make_int(int64_t val);
    static CommandResponse make_str(const std::string& str);
    static CommandResponse make_nil();
    static CommandResponse make_array(const std::vector<std::string>& items);
    static CommandResponse make_map(const std::vector<std::pair<std::string, std::string>>& items);
    static CommandResponse make_avl_range(const std::vector<std::pair<std::string, double>>& items);

private:
    StorageEngine& engine_;
    PersistenceManager& persistence_;
    ThreadPool& pool_;

    CommandResponse handle_set(const std::vector<std::string>& args);
    CommandResponse handle_get(const std::vector<std::string>& args);
    CommandResponse handle_del(const std::vector<std::string>& args);
    CommandResponse handle_mget(const std::vector<std::string>& args);
    CommandResponse handle_mset(const std::vector<std::string>& args);
    CommandResponse handle_incr(const std::vector<std::string>& args, int64_t step);

    CommandResponse handle_lpush(const std::vector<std::string>& args);
    CommandResponse handle_rpush(const std::vector<std::string>& args);
    CommandResponse handle_lpop(const std::vector<std::string>& args);
    CommandResponse handle_rpop(const std::vector<std::string>& args);
    CommandResponse handle_lrange(const std::vector<std::string>& args);

    CommandResponse handle_sadd(const std::vector<std::string>& args);
    CommandResponse handle_srem(const std::vector<std::string>& args);
    CommandResponse handle_smembers(const std::vector<std::string>& args);
    CommandResponse handle_sismember(const std::vector<std::string>& args);
    CommandResponse handle_scard(const std::vector<std::string>& args);
    CommandResponse handle_set_ops(const std::vector<std::string>& args, const std::string& op);

    CommandResponse handle_hset(const std::vector<std::string>& args);
    CommandResponse handle_hget(const std::vector<std::string>& args);
    CommandResponse handle_hdel(const std::vector<std::string>& args);
    CommandResponse handle_hgetall(const std::vector<std::string>& args);

    CommandResponse handle_zadd(const std::vector<std::string>& args);
    CommandResponse handle_zrange(const std::vector<std::string>& args);
    CommandResponse handle_zrangebyscore(const std::vector<std::string>& args);
    CommandResponse handle_zscore(const std::vector<std::string>& args);
    CommandResponse handle_zrem(const std::vector<std::string>& args);
    CommandResponse handle_zcard(const std::vector<std::string>& args);

    CommandResponse handle_expire(const std::vector<std::string>& args, bool millis);
    CommandResponse handle_ttl(const std::vector<std::string>& args, bool millis);
    CommandResponse handle_persist(const std::vector<std::string>& args);

    CommandResponse handle_keys(const std::vector<std::string>& args);
    CommandResponse handle_info();
};

} // namespace storage
