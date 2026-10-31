#include "network/socket.hpp"
#include "protocol/command.hpp"
#include "protocol/tlv.hpp"
#include "utils/logger.hpp"

#include <iostream>
#include <sstream>
#include <string>
#include <vector>

void print_tlv_response(const storage::TLVMessage& msg) {
    switch (msg.type) {
        case storage::TLVType::RESP_OK:
            std::cout << "+ " << msg.as_string() << "\n";
            break;
        case storage::TLVType::RESP_ERR:
            std::cout << "-ERR " << msg.as_string() << "\n";
            break;
        case storage::TLVType::RESP_INT:
            std::cout << ":" << msg.as_int64() << "\n";
            break;
        case storage::TLVType::RESP_STR:
            std::cout << "\"" << msg.as_string() << "\"\n";
            break;
        case storage::TLVType::RESP_NIL:
            std::cout << "(nil)\n";
            break;
        case storage::TLVType::RESP_ARRAY:
        case storage::TLVType::RESP_MAP:
        case storage::TLVType::RESP_AVL_RANGE: {
            auto items = msg.as_string_list();
            if (items.empty()) {
                std::cout << "(empty array or set)\n";
            } else {
                for (size_t i = 0; i < items.size(); ++i) {
                    std::cout << (i + 1) << ") \"" << items[i] << "\"\n";
                }
            }
            break;
        }
        default:
            std::cout << "Unknown TLV Type: " << static_cast<int>(msg.type)
                      << " Payload len=" << msg.length << "\n";
            break;
    }
}

int main(int argc, char* argv[]) {
    std::string host = "127.0.0.1";
    int port = 6379;
    bool tlv_mode = false;
    std::vector<std::string> oneshot_cmd;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--host" && i + 1 < argc) {
            host = argv[++i];
        } else if (arg == "--port" && i + 1 < argc) {
            port = std::stoi(argv[++i]);
        } else if (arg == "--tlv") {
            tlv_mode = true;
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Storage CLI Client\n"
                      << "Usage: " << argv[0] << " [options] [command...]\n\n"
                      << "Options:\n"
                      << "  --host <ip>       Server IP (default: 127.0.0.1)\n"
                      << "  --port <port>     Server Port (default: 6379)\n"
                      << "  --tlv             Use Binary TLV wire protocol\n"
                      << "  --help, -h        Show help\n";
            return 0;
        } else {
            oneshot_cmd.push_back(arg);
        }
    }

    if (!storage::SocketUtil::init_network_subsystem()) {
        std::cerr << "Failed to init sockets.\n";
        return 1;
    }

    storage::socket_t sock = storage::SocketUtil::create_tcp_client(host, port);
    if (sock == storage::INVALID_SOCKET_FD) {
        std::cerr << "Could not connect to server at " << host << ":" << port << "\n";
        storage::SocketUtil::cleanup_network_subsystem();
        return 1;
    }

    auto send_command = [&](const std::vector<std::string>& cmd_tokens) -> bool {
        if (cmd_tokens.empty()) return true;

        if (tlv_mode) {
            auto packet = storage::TLVSerializer::serialize_command(cmd_tokens);
#if defined(_WIN32) || defined(_WIN64)
            send(sock, reinterpret_cast<const char*>(packet.data()), static_cast<int>(packet.size()), 0);
#else
            send(sock, packet.data(), packet.size(), 0);
#endif
            std::vector<uint8_t> resp_buf(16384);
#if defined(_WIN32) || defined(_WIN64)
            int n = recv(sock, reinterpret_cast<char*>(resp_buf.data()), static_cast<int>(resp_buf.size()), 0);
#else
            ssize_t n = recv(sock, resp_buf.data(), resp_buf.size(), 0);
#endif
            if (n <= 0) return false;

            storage::TLVMessage resp;
            size_t consumed = 0;
            if (storage::TLVSerializer::deserialize(resp_buf.data(), static_cast<size_t>(n), resp, consumed)) {
                print_tlv_response(resp);
            } else {
                std::cout << "Invalid TLV response from server.\n";
            }
        } else {
            // Text Protocol
            std::ostringstream oss;
            for (size_t i = 0; i < cmd_tokens.size(); ++i) {
                if (i > 0) oss << " ";
                oss << cmd_tokens[i];
            }
            oss << "\r\n";
            std::string req = oss.str();
#if defined(_WIN32) || defined(_WIN64)
            send(sock, req.c_str(), static_cast<int>(req.size()), 0);
#else
            send(sock, req.c_str(), req.size(), 0);
#endif
            char buf[8192];
#if defined(_WIN32) || defined(_WIN64)
            int n = recv(sock, buf, sizeof(buf) - 1, 0);
#else
            ssize_t n = recv(sock, buf, sizeof(buf) - 1, 0);
#endif
            if (n <= 0) return false;
            buf[n] = '\0';
            std::cout << buf;
        }
        return true;
    };

    if (!oneshot_cmd.empty()) {
        send_command(oneshot_cmd);
    } else {
        std::cout << "Connected to Custom In-Memory Storage Engine at " << host << ":" << port
                  << " (" << (tlv_mode ? "TLV Binary Mode" : "Text Mode") << ")\n"
                  << "Type commands (e.g. 'SET k v', 'GET k', 'ZADD z 10 a', 'QUIT')\n";

        std::string line;
        while (true) {
            std::cout << "storage-cli " << host << ":" << port << "> ";
            if (!std::getline(std::cin, line)) break;
            auto tokens = storage::CommandDispatcher::parse_text_line(line);
            if (tokens.empty()) continue;

            std::string upper_cmd = tokens[0];
            std::transform(upper_cmd.begin(), upper_cmd.end(), upper_cmd.begin(), ::toupper);
            if (upper_cmd == "QUIT" || upper_cmd == "EXIT") break;

            if (!send_command(tokens)) {
                std::cerr << "Server disconnected.\n";
                break;
            }
        }
    }

    storage::SocketUtil::close_socket(sock);
    storage::SocketUtil::cleanup_network_subsystem();
    return 0;
}
