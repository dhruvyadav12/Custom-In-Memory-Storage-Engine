#include "protocol/command.hpp"
#include <algorithm>
#include <cctype>
#include <sstream>

namespace storage {

CommandDispatcher::CommandDispatcher(StorageEngine& engine, PersistenceManager& persistence, ThreadPool& pool)
    : engine_(engine), persistence_(persistence), pool_(pool) {}

std::vector<std::string> CommandDispatcher::parse_text_line(const std::string& line) {
    std::vector<std::string> tokens;
    std::string token;
    bool in_quote = false;
    char quote_char = '\0';

    for (size_t i = 0; i < line.size(); ++i) {
        char c = line[i];
        if (in_quote) {
            if (c == quote_char) {
                in_quote = false;
            } else if (c == '\\' && i + 1 < line.size()) {
                token += line[++i];
            } else {
                token += c;
            }
        } else {
            if (c == '\'' || c == '\"') {
                in_quote = true;
                quote_char = c;
            } else if (std::isspace(static_cast<unsigned char>(c))) {
                if (!token.empty()) {
                    tokens.push_back(token);
                    token.clear();
                }
            } else {
                token += c;
            }
        }
    }

    if (!token.empty()) {
        tokens.push_back(token);
    }
    return tokens;
}

CommandResponse CommandDispatcher::make_ok(const std::string& msg) {
    CommandResponse res;
    res.text_output = "+ " + msg + "\r\n";
    res.tlv_output = TLVMessage(TLVType::RESP_OK, msg);
    return res;
}

CommandResponse CommandDispatcher::make_err(const std::string& err_msg) {
    CommandResponse res;
    res.text_output = "-ERR " + err_msg + "\r\n";
    res.tlv_output = TLVMessage(TLVType::RESP_ERR, err_msg);
    return res;
}

CommandResponse CommandDispatcher::make_int(int64_t val) {
    CommandResponse res;
    res.text_output = ":" + std::to_string(val) + "\r\n";
    uint64_t net_val = TLVSerializer::to_network_u64(static_cast<uint64_t>(val));
    std::vector<uint8_t> bytes(8);
    std::memcpy(bytes.data(), &net_val, 8);
    res.tlv_output = TLVMessage(TLVType::RESP_INT, std::move(bytes));
    return res;
}

CommandResponse CommandDispatcher::make_str(const std::string& str) {
    CommandResponse res;
    res.text_output = "$" + std::to_string(str.size()) + "\r\n" + str + "\r\n";
    res.tlv_output = TLVMessage(TLVType::RESP_STR, str);
    return res;
}

CommandResponse CommandDispatcher::make_nil() {
    CommandResponse res;
    res.text_output = "$-1\r\n";
    res.tlv_output = TLVMessage(TLVType::RESP_NIL, std::vector<uint8_t>{});
    return res;
}

CommandResponse CommandDispatcher::make_array(const std::vector<std::string>& items) {
    CommandResponse res;
    std::ostringstream oss;
    oss << "*" << items.size() << "\r\n";
    for (const auto& item : items) {
        oss << "$" << item.size() << "\r\n" << item << "\r\n";
    }
    res.text_output = oss.str();

    std::vector<uint8_t> val_buffer;
    for (const auto& s : items) {
        uint32_t len_net = TLVSerializer::to_network_u32(static_cast<uint32_t>(s.size()));
        const uint8_t* p = reinterpret_cast<const uint8_t*>(&len_net);
        val_buffer.insert(val_buffer.end(), p, p + 4);
        val_buffer.insert(val_buffer.end(), s.begin(), s.end());
    }
    res.tlv_output = TLVMessage(TLVType::RESP_ARRAY, std::move(val_buffer));
    return res;
}

CommandResponse CommandDispatcher::make_map(const std::vector<std::pair<std::string, std::string>>& items) {
    CommandResponse res;
    std::ostringstream oss;
    oss << "*" << (items.size() * 2) << "\r\n";
    for (const auto& kv : items) {
        oss << "$" << kv.first.size() << "\r\n" << kv.first << "\r\n";
        oss << "$" << kv.second.size() << "\r\n" << kv.second << "\r\n";
    }
    res.text_output = oss.str();

    std::vector<uint8_t> val_buffer;
    for (const auto& kv : items) {
        uint32_t klen = TLVSerializer::to_network_u32(static_cast<uint32_t>(kv.first.size()));
        const uint8_t* pk = reinterpret_cast<const uint8_t*>(&klen);
        val_buffer.insert(val_buffer.end(), pk, pk + 4);
        val_buffer.insert(val_buffer.end(), kv.first.begin(), kv.first.end());

        uint32_t vlen = TLVSerializer::to_network_u32(static_cast<uint32_t>(kv.second.size()));
        const uint8_t* pv = reinterpret_cast<const uint8_t*>(&vlen);
        val_buffer.insert(val_buffer.end(), pv, pv + 4);
        val_buffer.insert(val_buffer.end(), kv.second.begin(), kv.second.end());
    }
    res.tlv_output = TLVMessage(TLVType::RESP_MAP, std::move(val_buffer));
    return res;
}

CommandResponse CommandDispatcher::make_avl_range(const std::vector<std::pair<std::string, double>>& items) {
    CommandResponse res;
    std::ostringstream oss;
    oss << "*" << (items.size() * 2) << "\r\n";
    for (const auto& item : items) {
        oss << "$" << item.first.size() << "\r\n" << item.first << "\r\n";
        std::string s_str = std::to_string(item.second);
        oss << "$" << s_str.size() << "\r\n" << s_str << "\r\n";
    }
    res.text_output = oss.str();

    std::vector<uint8_t> val_buffer;
    for (const auto& item : items) {
        uint32_t mlen = TLVSerializer::to_network_u32(static_cast<uint32_t>(item.first.size()));
        const uint8_t* pm = reinterpret_cast<const uint8_t*>(&mlen);
        val_buffer.insert(val_buffer.end(), pm, pm + 4);
        val_buffer.insert(val_buffer.end(), item.first.begin(), item.first.end());

        std::string s_str = std::to_string(item.second);
        uint32_t slen = TLVSerializer::to_network_u32(static_cast<uint32_t>(s_str.size()));
        const uint8_t* ps = reinterpret_cast<const uint8_t*>(&slen);
        val_buffer.insert(val_buffer.end(), ps, ps + 4);
        val_buffer.insert(val_buffer.end(), s_str.begin(), s_str.end());
    }
    res.tlv_output = TLVMessage(TLVType::RESP_AVL_RANGE, std::move(val_buffer));
    return res;
}

CommandResponse CommandDispatcher::execute(const std::vector<std::string>& args) {
    if (args.empty()) {
        return make_err("Empty command");
    }

    std::string cmd = args[0];
    std::transform(cmd.begin(), cmd.end(), cmd.begin(), [](unsigned char c) {
        return std::toupper(c);
    });

    if (cmd == "PING") {
        return make_ok(args.size() > 1 ? args[1] : "PONG");
    }
    if (cmd == "ECHO") {
        return make_str(args.size() > 1 ? args[1] : "");
    }

    // --- Strings ---
    if (cmd == "SET") return handle_set(args);
    if (cmd == "GET") return handle_get(args);
    if (cmd == "DEL") return handle_del(args);
    if (cmd == "APPEND") {
        if (args.size() < 3) return make_err("wrong number of arguments for 'append'");
        return make_int(engine_.append(args[1], args[2]));
    }
    if (cmd == "STRLEN") {
        if (args.size() < 2) return make_err("wrong number of arguments for 'strlen'");
        return make_int(engine_.strlen_cmd(args[1]));
    }
    if (cmd == "INCR") return handle_incr(args, 1);
    if (cmd == "DECR") return handle_incr(args, -1);
    if (cmd == "INCRBY") {
        if (args.size() < 3) return make_err("wrong number of arguments for 'incrby'");
        try {
            int64_t step = std::stoll(args[2]);
            return handle_incr(args, step);
        } catch (...) {
            return make_err("value is not an integer or out of range");
        }
    }
    if (cmd == "MGET") return handle_mget(args);
    if (cmd == "MSET") return handle_mset(args);

    // --- Lists ---
    if (cmd == "LPUSH") return handle_lpush(args);
    if (cmd == "RPUSH") return handle_rpush(args);
    if (cmd == "LPOP") return handle_lpop(args);
    if (cmd == "RPOP") return handle_rpop(args);
    if (cmd == "LRANGE") return handle_lrange(args);
    if (cmd == "LLEN") {
        if (args.size() < 2) return make_err("wrong number of arguments for 'llen'");
        return make_int(engine_.llen(args[1]));
    }
    if (cmd == "LINDEX") {
        if (args.size() < 3) return make_err("wrong number of arguments for 'lindex'");
        try {
            int64_t idx = std::stoll(args[2]);
            auto val = engine_.lindex(args[1], idx);
            return val ? make_str(*val) : make_nil();
        } catch (...) {
            return make_err("value is not an integer or out of range");
        }
    }

    // --- Sets ---
    if (cmd == "SADD") return handle_sadd(args);
    if (cmd == "SREM") return handle_srem(args);
    if (cmd == "SMEMBERS") return handle_smembers(args);
    if (cmd == "SISMEMBER") return handle_sismember(args);
    if (cmd == "SCARD") return handle_scard(args);
    if (cmd == "SUNION" || cmd == "SINTER" || cmd == "SDIFF") {
        return handle_set_ops(args, cmd);
    }

    // --- Hashes ---
    if (cmd == "HSET") return handle_hset(args);
    if (cmd == "HGET") return handle_hget(args);
    if (cmd == "HDEL") return handle_hdel(args);
    if (cmd == "HGETALL") return handle_hgetall(args);
    if (cmd == "HEXISTS") {
        if (args.size() < 3) return make_err("wrong number of arguments for 'hexists'");
        return make_int(engine_.hexists(args[1], args[2]) ? 1 : 0);
    }
    if (cmd == "HKEYS") {
        if (args.size() < 2) return make_err("wrong number of arguments for 'hkeys'");
        return make_array(engine_.hkeys(args[1]));
    }
    if (cmd == "HVALS") {
        if (args.size() < 2) return make_err("wrong number of arguments for 'hvals'");
        return make_array(engine_.hvals(args[1]));
    }
    if (cmd == "HLEN") {
        if (args.size() < 2) return make_err("wrong number of arguments for 'hlen'");
        return make_int(engine_.hlen(args[1]));
    }

    // --- Sorted Sets (AVL Tree) ---
    if (cmd == "ZADD") return handle_zadd(args);
    if (cmd == "ZRANGE") return handle_zrange(args);
    if (cmd == "ZRANGEBYSCORE") return handle_zrangebyscore(args);
    if (cmd == "ZSCORE") return handle_zscore(args);
    if (cmd == "ZREM") return handle_zrem(args);
    if (cmd == "ZCARD") return handle_zcard(args);

    // --- Expiration ---
    if (cmd == "EXPIRE") return handle_expire(args, false);
    if (cmd == "PEXPIRE") return handle_expire(args, true);
    if (cmd == "TTL") return handle_ttl(args, false);
    if (cmd == "PTTL") return handle_ttl(args, true);
    if (cmd == "PERSIST") return handle_persist(args);

    // --- Generic & Server ---
    if (cmd == "EXISTS") {
        if (args.size() < 2) return make_err("wrong number of arguments for 'exists'");
        return make_int(engine_.exists(args[1]) ? 1 : 0);
    }
    if (cmd == "TYPE") {
        if (args.size() < 2) return make_err("wrong number of arguments for 'type'");
        return make_ok(engine_.type(args[1]));
    }
    if (cmd == "KEYS") return handle_keys(args);
    if (cmd == "DBSIZE") return make_int(static_cast<int64_t>(engine_.dbsize()));
    if (cmd == "FLUSHALL") {
        engine_.flushall();
        return make_ok();
    }
    if (cmd == "SAVE") {
        bool ok = persistence_.save(engine_);
        return ok ? make_ok("Snapshot saved successfully") : make_err("Failed to save snapshot");
    }
    if (cmd == "BGSAVE") {
        bool ok = persistence_.bgsave(engine_, pool_);
        return ok ? make_ok("Background saving started") : make_err("Background save already running");
    }
    if (cmd == "INFO") return handle_info();

    return make_err("unknown command '" + args[0] + "'");
}

CommandResponse CommandDispatcher::handle_set(const std::vector<std::string>& args) {
    if (args.size() < 3) return make_err("wrong number of arguments for 'set'");
    std::string key = args[1];
    std::string val = args[2];
    uint64_t ex_sec = 0;
    uint64_t px_ms = 0;

    for (size_t i = 3; i < args.size(); ++i) {
        std::string opt = args[i];
        std::transform(opt.begin(), opt.end(), opt.begin(), ::toupper);
        if (opt == "EX" && i + 1 < args.size()) {
            try { ex_sec = std::stoull(args[++i]); } catch (...) { return make_err("invalid expire time"); }
        } else if (opt == "PX" && i + 1 < args.size()) {
            try { px_ms = std::stoull(args[++i]); } catch (...) { return make_err("invalid expire time"); }
        }
    }

    engine_.set(key, val, ex_sec, px_ms);
    return make_ok();
}

CommandResponse CommandDispatcher::handle_get(const std::vector<std::string>& args) {
    if (args.size() < 2) return make_err("wrong number of arguments for 'get'");
    auto val = engine_.get(args[1]);
    return val ? make_str(*val) : make_nil();
}

CommandResponse CommandDispatcher::handle_del(const std::vector<std::string>& args) {
    if (args.size() < 2) return make_err("wrong number of arguments for 'del'");
    std::vector<std::string> keys(args.begin() + 1, args.end());
    return make_int(engine_.del(keys));
}

CommandResponse CommandDispatcher::handle_incr(const std::vector<std::string>& args, int64_t step) {
    if (args.size() < 2) return make_err("wrong number of arguments for increment");
    auto res = engine_.incrby(args[1], step);
    if (!res) return make_err("value is not an integer or out of range");
    return make_int(*res);
}

CommandResponse CommandDispatcher::handle_mget(const std::vector<std::string>& args) {
    if (args.size() < 2) return make_err("wrong number of arguments for 'mget'");
    std::vector<std::string> keys(args.begin() + 1, args.end());
    auto vals = engine_.mget(keys);
    std::vector<std::string> res;
    for (const auto& v : vals) {
        res.push_back(v ? *v : "(nil)");
    }
    return make_array(res);
}

CommandResponse CommandDispatcher::handle_mset(const std::vector<std::string>& args) {
    if (args.size() < 3 || (args.size() - 1) % 2 != 0) {
        return make_err("wrong number of arguments for 'mset'");
    }
    std::vector<std::pair<std::string, std::string>> kvs;
    for (size_t i = 1; i + 1 < args.size(); i += 2) {
        kvs.emplace_back(args[i], args[i + 1]);
    }
    engine_.mset(kvs);
    return make_ok();
}

CommandResponse CommandDispatcher::handle_lpush(const std::vector<std::string>& args) {
    if (args.size() < 3) return make_err("wrong number of arguments for 'lpush'");
    std::vector<std::string> vals(args.begin() + 2, args.end());
    int64_t len = engine_.lpush(args[1], vals);
    if (len < 0) return make_err("Operation against a key holding the wrong kind of value");
    return make_int(len);
}

CommandResponse CommandDispatcher::handle_rpush(const std::vector<std::string>& args) {
    if (args.size() < 3) return make_err("wrong number of arguments for 'rpush'");
    std::vector<std::string> vals(args.begin() + 2, args.end());
    int64_t len = engine_.rpush(args[1], vals);
    if (len < 0) return make_err("Operation against a key holding the wrong kind of value");
    return make_int(len);
}

CommandResponse CommandDispatcher::handle_lpop(const std::vector<std::string>& args) {
    if (args.size() < 2) return make_err("wrong number of arguments for 'lpop'");
    auto val = engine_.lpop(args[1]);
    return val ? make_str(*val) : make_nil();
}

CommandResponse CommandDispatcher::handle_rpop(const std::vector<std::string>& args) {
    if (args.size() < 2) return make_err("wrong number of arguments for 'rpop'");
    auto val = engine_.rpop(args[1]);
    return val ? make_str(*val) : make_nil();
}

CommandResponse CommandDispatcher::handle_lrange(const std::vector<std::string>& args) {
    if (args.size() < 4) return make_err("wrong number of arguments for 'lrange'");
    try {
        int64_t start = std::stoll(args[2]);
        int64_t stop = std::stoll(args[3]);
        return make_array(engine_.lrange(args[1], start, stop));
    } catch (...) {
        return make_err("value is not an integer or out of range");
    }
}

CommandResponse CommandDispatcher::handle_sadd(const std::vector<std::string>& args) {
    if (args.size() < 3) return make_err("wrong number of arguments for 'sadd'");
    std::vector<std::string> members(args.begin() + 2, args.end());
    int64_t added = engine_.sadd(args[1], members);
    if (added < 0) return make_err("Operation against a key holding the wrong kind of value");
    return make_int(added);
}

CommandResponse CommandDispatcher::handle_srem(const std::vector<std::string>& args) {
    if (args.size() < 3) return make_err("wrong number of arguments for 'srem'");
    std::vector<std::string> members(args.begin() + 2, args.end());
    return make_int(engine_.srem(args[1], members));
}

CommandResponse CommandDispatcher::handle_smembers(const std::vector<std::string>& args) {
    if (args.size() < 2) return make_err("wrong number of arguments for 'smembers'");
    return make_array(engine_.smembers(args[1]));
}

CommandResponse CommandDispatcher::handle_sismember(const std::vector<std::string>& args) {
    if (args.size() < 3) return make_err("wrong number of arguments for 'sismember'");
    return make_int(engine_.sismember(args[1], args[2]) ? 1 : 0);
}

CommandResponse CommandDispatcher::handle_scard(const std::vector<std::string>& args) {
    if (args.size() < 2) return make_err("wrong number of arguments for 'scard'");
    return make_int(engine_.scard(args[1]));
}

CommandResponse CommandDispatcher::handle_set_ops(const std::vector<std::string>& args, const std::string& op) {
    if (args.size() < 2) return make_err("wrong number of arguments for set operation");
    std::vector<std::string> keys(args.begin() + 1, args.end());
    if (op == "SUNION") return make_array(engine_.sunion(keys));
    if (op == "SINTER") return make_array(engine_.sinter(keys));
    if (op == "SDIFF") return make_array(engine_.sdiff(keys));
    return make_err("Unknown set operation");
}

CommandResponse CommandDispatcher::handle_hset(const std::vector<std::string>& args) {
    if (args.size() < 4) return make_err("wrong number of arguments for 'hset'");
    int64_t added = engine_.hset(args[1], args[2], args[3]);
    if (added < 0) return make_err("Operation against a key holding the wrong kind of value");
    return make_int(added);
}

CommandResponse CommandDispatcher::handle_hget(const std::vector<std::string>& args) {
    if (args.size() < 3) return make_err("wrong number of arguments for 'hget'");
    auto val = engine_.hget(args[1], args[2]);
    return val ? make_str(*val) : make_nil();
}

CommandResponse CommandDispatcher::handle_hdel(const std::vector<std::string>& args) {
    if (args.size() < 3) return make_err("wrong number of arguments for 'hdel'");
    std::vector<std::string> fields(args.begin() + 2, args.end());
    return make_int(engine_.hdel(args[1], fields));
}

CommandResponse CommandDispatcher::handle_hgetall(const std::vector<std::string>& args) {
    if (args.size() < 2) return make_err("wrong number of arguments for 'hgetall'");
    return make_map(engine_.hgetall(args[1]));
}

CommandResponse CommandDispatcher::handle_zadd(const std::vector<std::string>& args) {
    if (args.size() < 4) return make_err("wrong number of arguments for 'zadd'");
    try {
        double score = std::stod(args[2]);
        int64_t res = engine_.zadd(args[1], score, args[3]);
        if (res < 0) return make_err("Operation against a key holding the wrong kind of value");
        return make_int(res);
    } catch (...) {
        return make_err("value is not a valid float");
    }
}

CommandResponse CommandDispatcher::handle_zrange(const std::vector<std::string>& args) {
    if (args.size() < 4) return make_err("wrong number of arguments for 'zrange'");
    try {
        int64_t start = std::stoll(args[2]);
        int64_t stop = std::stoll(args[3]);
        return make_avl_range(engine_.zrange(args[1], start, stop));
    } catch (...) {
        return make_err("value is not an integer or out of range");
    }
}

CommandResponse CommandDispatcher::handle_zrangebyscore(const std::vector<std::string>& args) {
    if (args.size() < 4) return make_err("wrong number of arguments for 'zrangebyscore'");
    try {
        double min_score = std::stod(args[2]);
        double max_score = std::stod(args[3]);
        return make_avl_range(engine_.zrangebyscore(args[1], min_score, max_score));
    } catch (...) {
        return make_err("min or max is not a float");
    }
}

CommandResponse CommandDispatcher::handle_zscore(const std::vector<std::string>& args) {
    if (args.size() < 3) return make_err("wrong number of arguments for 'zscore'");
    auto score = engine_.zscore(args[1], args[2]);
    return score ? make_str(std::to_string(*score)) : make_nil();
}

CommandResponse CommandDispatcher::handle_zrem(const std::vector<std::string>& args) {
    if (args.size() < 3) return make_err("wrong number of arguments for 'zrem'");
    return make_int(engine_.zrem(args[1], args[2]));
}

CommandResponse CommandDispatcher::handle_zcard(const std::vector<std::string>& args) {
    if (args.size() < 2) return make_err("wrong number of arguments for 'zcard'");
    return make_int(engine_.zcard(args[1]));
}

CommandResponse CommandDispatcher::handle_expire(const std::vector<std::string>& args, bool millis) {
    if (args.size() < 3) return make_err("wrong number of arguments for expire");
    try {
        uint64_t t = std::stoull(args[2]);
        bool ok = millis ? engine_.pexpire(args[1], t) : engine_.expire(args[1], t);
        return make_int(ok ? 1 : 0);
    } catch (...) {
        return make_err("value is not an integer or out of range");
    }
}

CommandResponse CommandDispatcher::handle_ttl(const std::vector<std::string>& args, bool millis) {
    if (args.size() < 2) return make_err("wrong number of arguments for ttl");
    return make_int(millis ? engine_.pttl(args[1]) : engine_.ttl(args[1]));
}

CommandResponse CommandDispatcher::handle_persist(const std::vector<std::string>& args) {
    if (args.size() < 2) return make_err("wrong number of arguments for 'persist'");
    return make_int(engine_.persist(args[1]) ? 1 : 0);
}

CommandResponse CommandDispatcher::handle_keys(const std::vector<std::string>& args) {
    std::string pattern = args.size() > 1 ? args[1] : "*";
    return make_array(engine_.keys(pattern));
}

CommandResponse CommandDispatcher::handle_info() {
    std::ostringstream oss;
    oss << "# Server\r\n"
        << "server_name:Custom-In-Memory-Storage-Engine\r\n"
        << "version:1.0.0\r\n"
        << "concurrency_model:event-driven-io-with-worker-threadpool\r\n"
        << "active_workers:" << pool_.active_tasks() << "\r\n"
        << "pending_tasks:" << pool_.pending_tasks() << "\r\n"
        << "db_keys:" << engine_.dbsize() << "\r\n"
        << "persistence_saving:" << (persistence_.is_saving() ? 1 : 0) << "\r\n"
        << "last_bgsave_timestamp:" << persistence_.last_save_time() << "\r\n";
    return make_str(oss.str());
}

} // namespace storage
