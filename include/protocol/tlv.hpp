#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <optional>

namespace storage {

constexpr uint8_t TLV_MAGIC = 0xA5;

enum class TLVType : uint8_t {
    CMD_REQUEST       = 0x01,
    RESP_OK           = 0x02,
    RESP_ERR          = 0x03,
    RESP_INT          = 0x04,
    RESP_STR          = 0x05,
    RESP_ARRAY        = 0x06,
    RESP_NIL          = 0x07,
    RESP_MAP          = 0x08,
    RESP_AVL_RANGE    = 0x09,
    DATA_STRING       = 0x10,
    DATA_LIST         = 0x11,
    DATA_SET          = 0x12,
    DATA_HASH         = 0x13,
    DATA_AVL          = 0x14
};

struct TLVMessage {
    uint8_t magic = TLV_MAGIC;
    TLVType type;
    uint32_t length = 0;
    std::vector<uint8_t> value;

    TLVMessage() : type(TLVType::RESP_OK) {}
    TLVMessage(TLVType t, std::vector<uint8_t> val)
        : type(t), length(static_cast<uint32_t>(val.size())), value(std::move(val)) {}
    TLVMessage(TLVType t, const std::string& str)
        : type(t), length(static_cast<uint32_t>(str.size())),
          value(str.begin(), str.end()) {}

    std::string as_string() const {
        return std::string(value.begin(), value.end());
    }

    int64_t as_int64() const;
    std::vector<std::string> as_string_list() const;
    std::vector<TLVMessage> as_nested_tlvs() const;
};

class TLVSerializer {
public:
    static std::vector<uint8_t> serialize(const TLVMessage& msg);
    static std::vector<uint8_t> serialize_ok(const std::string& msg = "OK");
    static std::vector<uint8_t> serialize_error(const std::string& err_msg);
    static std::vector<uint8_t> serialize_int(int64_t val);
    static std::vector<uint8_t> serialize_string(const std::string& str);
    static std::vector<uint8_t> serialize_nil();
    static std::vector<uint8_t> serialize_array(const std::vector<TLVMessage>& items);
    static std::vector<uint8_t> serialize_string_list(const std::vector<std::string>& list);
    static std::vector<uint8_t> serialize_command(const std::vector<std::string>& args);
    static std::vector<uint8_t> serialize_key_values(const std::vector<std::pair<std::string, std::string>>& kvs);

    // Deserialization
    // Returns true if a full valid TLV frame was parsed, setting bytes_consumed.
    // If not enough bytes are available in the buffer, returns false.
    static bool deserialize(const uint8_t* buffer, size_t size,
                            TLVMessage& out_msg, size_t& bytes_consumed);

    static uint32_t to_network_u32(uint32_t val);
    static uint32_t from_network_u32(uint32_t val);
    static uint64_t to_network_u64(uint64_t val);
    static uint64_t from_network_u64(uint64_t val);
};

} // namespace storage
