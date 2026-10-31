#include "protocol/tlv.hpp"
#include <cassert>
#include <iostream>
#include <string>
#include <vector>

void test_tlv_string() {
    std::cout << "[Test] TLV: String & Status Serialization... ";
    auto bytes = storage::TLVSerializer::serialize_string("Hello World");

    storage::TLVMessage msg;
    size_t consumed = 0;
    assert(storage::TLVSerializer::deserialize(bytes.data(), bytes.size(), msg, consumed));
    assert(consumed == bytes.size());
    assert(msg.type == storage::TLVType::RESP_STR);
    assert(msg.as_string() == "Hello World");

    auto ok_bytes = storage::TLVSerializer::serialize_ok("PONG");
    assert(storage::TLVSerializer::deserialize(ok_bytes.data(), ok_bytes.size(), msg, consumed));
    assert(msg.type == storage::TLVType::RESP_OK);
    assert(msg.as_string() == "PONG");
    std::cout << "PASSED\n";
}

void test_tlv_int() {
    std::cout << "[Test] TLV: Integer Serialization... ";
    int64_t original = 9876543210LL;
    auto bytes = storage::TLVSerializer::serialize_int(original);

    storage::TLVMessage msg;
    size_t consumed = 0;
    assert(storage::TLVSerializer::deserialize(bytes.data(), bytes.size(), msg, consumed));
    assert(msg.type == storage::TLVType::RESP_INT);
    assert(msg.as_int64() == original);
    std::cout << "PASSED\n";
}

void test_tlv_command() {
    std::cout << "[Test] TLV: Command Request Encoding & Decoding... ";
    std::vector<std::string> args = {"SET", "username", "dhruvyadav12", "EX", "3600"};
    auto bytes = storage::TLVSerializer::serialize_command(args);

    storage::TLVMessage msg;
    size_t consumed = 0;
    assert(storage::TLVSerializer::deserialize(bytes.data(), bytes.size(), msg, consumed));
    assert(msg.type == storage::TLVType::CMD_REQUEST);

    auto decoded_args = msg.as_string_list();
    assert(decoded_args.size() == args.size());
    for (size_t i = 0; i < args.size(); ++i) {
        assert(decoded_args[i] == args[i]);
    }
    std::cout << "PASSED\n";
}

void test_tlv_partial_framing() {
    std::cout << "[Test] TLV: Partial Network Buffer Framing... ";
    auto bytes = storage::TLVSerializer::serialize_string("Complete Packet");

    storage::TLVMessage msg;
    size_t consumed = 0;

    // Incomplete header (< 6 bytes)
    assert(!storage::TLVSerializer::deserialize(bytes.data(), 4, msg, consumed));

    // Incomplete payload
    assert(!storage::TLVSerializer::deserialize(bytes.data(), bytes.size() - 2, msg, consumed));

    // Full packet
    assert(storage::TLVSerializer::deserialize(bytes.data(), bytes.size(), msg, consumed));
    assert(consumed == bytes.size());
    assert(msg.as_string() == "Complete Packet");
    std::cout << "PASSED\n";
}

int main() {
    std::cout << "=== Running TLV Protocol Unit Tests ===\n";
    test_tlv_string();
    test_tlv_int();
    test_tlv_command();
    test_tlv_partial_framing();
    std::cout << "ALL TLV Protocol Tests Passed Successfully!\n";
    return 0;
}
