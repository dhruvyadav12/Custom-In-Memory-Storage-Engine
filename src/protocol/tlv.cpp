#include "protocol/tlv.hpp"
#include <cstring>

#if defined(_WIN32) || defined(_WIN64)
#include <winsock2.h>
#else
#include <arpa/inet.h>
#endif

namespace storage {

uint32_t TLVSerializer::to_network_u32(uint32_t val) {
    return htonl(val);
}

uint32_t TLVSerializer::from_network_u32(uint32_t val) {
    return ntohl(val);
}

uint64_t TLVSerializer::to_network_u64(uint64_t val) {
#if defined(__linux__) || defined(__APPLE__)
    // Standard POSIX 64-bit conversion
    uint32_t high = htonl(static_cast<uint32_t>(val >> 32));
    uint32_t low = htonl(static_cast<uint32_t>(val & 0xFFFFFFFFLL));
    return (static_cast<uint64_t>(low) << 32) | high;
#else
    // Portable big-endian conversion
    uint8_t bytes[8];
    bytes[0] = (val >> 56) & 0xFF;
    bytes[1] = (val >> 48) & 0xFF;
    bytes[2] = (val >> 40) & 0xFF;
    bytes[3] = (val >> 32) & 0xFF;
    bytes[4] = (val >> 24) & 0xFF;
    bytes[5] = (val >> 16) & 0xFF;
    bytes[6] = (val >> 8) & 0xFF;
    bytes[7] = val & 0xFF;
    uint64_t result;
    std::memcpy(&result, bytes, 8);
    return result;
#endif
}

uint64_t TLVSerializer::from_network_u64(uint64_t val) {
    return to_network_u64(val); // Symmetrical
}

int64_t TLVMessage::as_int64() const {
    if (value.size() < 8) return 0;
    uint64_t net_val;
    std::memcpy(&net_val, value.data(), 8);
    uint64_t host_val = TLVSerializer::from_network_u64(net_val);
    return static_cast<int64_t>(host_val);
}

std::vector<std::string> TLVMessage::as_string_list() const {
    std::vector<std::string> list;
    size_t offset = 0;
    while (offset + 4 <= value.size()) {
        uint32_t str_len_net;
        std::memcpy(&str_len_net, value.data() + offset, 4);
        uint32_t str_len = TLVSerializer::from_network_u32(str_len_net);
        offset += 4;
        if (offset + str_len > value.size()) break;
        list.emplace_back(reinterpret_cast<const char*>(value.data() + offset), str_len);
        offset += str_len;
    }
    return list;
}

std::vector<TLVMessage> TLVMessage::as_nested_tlvs() const {
    std::vector<TLVMessage> result;
    size_t offset = 0;
    while (offset < value.size()) {
        TLVMessage child;
        size_t consumed = 0;
        if (TLVSerializer::deserialize(value.data() + offset, value.size() - offset, child, consumed)) {
            result.push_back(std::move(child));
            offset += consumed;
        } else {
            break;
        }
    }
    return result;
}

std::vector<uint8_t> TLVSerializer::serialize(const TLVMessage& msg) {
    std::vector<uint8_t> buffer;
    buffer.reserve(1 + 1 + 4 + msg.value.size());

    // Magic (1 byte)
    buffer.push_back(TLV_MAGIC);
    // Type (1 byte)
    buffer.push_back(static_cast<uint8_t>(msg.type));
    // Length (4 bytes big-endian)
    uint32_t len_net = to_network_u32(static_cast<uint32_t>(msg.value.size()));
    const uint8_t* len_bytes = reinterpret_cast<const uint8_t*>(&len_net);
    buffer.insert(buffer.end(), len_bytes, len_bytes + 4);
    // Value (N bytes)
    buffer.insert(buffer.end(), msg.value.begin(), msg.value.end());

    return buffer;
}

std::vector<uint8_t> TLVSerializer::serialize_ok(const std::string& msg) {
    TLVMessage tlv(TLVType::RESP_OK, msg);
    return serialize(tlv);
}

std::vector<uint8_t> TLVSerializer::serialize_error(const std::string& err_msg) {
    TLVMessage tlv(TLVType::RESP_ERR, err_msg);
    return serialize(tlv);
}

std::vector<uint8_t> TLVSerializer::serialize_int(int64_t val) {
    uint64_t net_val = to_network_u64(static_cast<uint64_t>(val));
    std::vector<uint8_t> bytes(8);
    std::memcpy(bytes.data(), &net_val, 8);
    TLVMessage tlv(TLVType::RESP_INT, std::move(bytes));
    return serialize(tlv);
}

std::vector<uint8_t> TLVSerializer::serialize_string(const std::string& str) {
    TLVMessage tlv(TLVType::RESP_STR, str);
    return serialize(tlv);
}

std::vector<uint8_t> TLVSerializer::serialize_nil() {
    TLVMessage tlv(TLVType::RESP_NIL, std::vector<uint8_t>{});
    return serialize(tlv);
}

std::vector<uint8_t> TLVSerializer::serialize_string_list(const std::vector<std::string>& list) {
    std::vector<uint8_t> val_buffer;
    for (const auto& s : list) {
        uint32_t len_net = to_network_u32(static_cast<uint32_t>(s.size()));
        const uint8_t* p = reinterpret_cast<const uint8_t*>(&len_net);
        val_buffer.insert(val_buffer.end(), p, p + 4);
        val_buffer.insert(val_buffer.end(), s.begin(), s.end());
    }
    TLVMessage tlv(TLVType::RESP_ARRAY, std::move(val_buffer));
    return serialize(tlv);
}

std::vector<uint8_t> TLVSerializer::serialize_command(const std::vector<std::string>& args) {
    std::vector<uint8_t> val_buffer;
    for (const auto& arg : args) {
        uint32_t len_net = to_network_u32(static_cast<uint32_t>(arg.size()));
        const uint8_t* p = reinterpret_cast<const uint8_t*>(&len_net);
        val_buffer.insert(val_buffer.end(), p, p + 4);
        val_buffer.insert(val_buffer.end(), arg.begin(), arg.end());
    }
    TLVMessage tlv(TLVType::CMD_REQUEST, std::move(val_buffer));
    return serialize(tlv);
}

std::vector<uint8_t> TLVSerializer::serialize_array(const std::vector<TLVMessage>& items) {
    std::vector<uint8_t> val_buffer;
    for (const auto& item : items) {
        auto item_bytes = serialize(item);
        val_buffer.insert(val_buffer.end(), item_bytes.begin(), item_bytes.end());
    }
    TLVMessage tlv(TLVType::RESP_ARRAY, std::move(val_buffer));
    return serialize(tlv);
}

std::vector<uint8_t> TLVSerializer::serialize_key_values(const std::vector<std::pair<std::string, std::string>>& kvs) {
    std::vector<uint8_t> val_buffer;
    for (const auto& kv : kvs) {
        uint32_t klen = to_network_u32(static_cast<uint32_t>(kv.first.size()));
        const uint8_t* pk = reinterpret_cast<const uint8_t*>(&klen);
        val_buffer.insert(val_buffer.end(), pk, pk + 4);
        val_buffer.insert(val_buffer.end(), kv.first.begin(), kv.first.end());

        uint32_t vlen = to_network_u32(static_cast<uint32_t>(kv.second.size()));
        const uint8_t* pv = reinterpret_cast<const uint8_t*>(&vlen);
        val_buffer.insert(val_buffer.end(), pv, pv + 4);
        val_buffer.insert(val_buffer.end(), kv.second.begin(), kv.second.end());
    }
    TLVMessage tlv(TLVType::RESP_MAP, std::move(val_buffer));
    return serialize(tlv);
}

bool TLVSerializer::deserialize(const uint8_t* buffer, size_t size,
                                TLVMessage& out_msg, size_t& bytes_consumed) {
    if (!buffer || size < 6) {
        return false; // Header is at least 6 bytes: Magic(1) + Type(1) + Length(4)
    }

    if (buffer[0] != TLV_MAGIC) {
        return false;
    }

    uint8_t raw_type = buffer[1];
    uint32_t len_net = 0;
    std::memcpy(&len_net, buffer + 2, 4);
    uint32_t length = from_network_u32(len_net);

    size_t total_size = 6 + length;
    if (size < total_size) {
        return false; // Incomplete packet, wait for more data
    }

    out_msg.magic = TLV_MAGIC;
    out_msg.type = static_cast<TLVType>(raw_type);
    out_msg.length = length;
    out_msg.value.assign(buffer + 6, buffer + 6 + length);
    bytes_consumed = total_size;
    return true;
}

} // namespace storage
