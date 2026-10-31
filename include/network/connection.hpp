#pragma once

#include "network/socket.hpp"
#include "protocol/command.hpp"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace storage {

class Connection {
public:
    Connection(socket_t fd, std::string ip, int port);
    ~Connection();

    socket_t fd() const { return fd_; }
    const std::string& ip() const { return ip_; }
    int port() const { return port_; }
    uint64_t last_active() const { return last_active_ms_; }

    void update_active_time();

    // Performs non-blocking read into internal buffer.
    // Returns: >0 bytes read, 0 on EOF (client disconnect), -1 on error/would-block
    int read_from_socket();

    // Performs non-blocking write from internal write buffer.
    // Returns: >=0 bytes written, -1 on error/would-block
    int write_to_socket();

    bool has_pending_write() const { return !write_buffer_.empty(); }
    void append_response(const std::vector<uint8_t>& data);

    // Parses next request from read buffer if available.
    // Returns true if a complete command was parsed.
    bool parse_next_command(std::vector<std::string>& out_cmd, ResponseFormat& out_format);

    void close();
    bool is_closed() const { return closed_; }

private:
    socket_t fd_;
    std::string ip_;
    int port_;
    uint64_t last_active_ms_;
    bool closed_ = false;

    std::vector<uint8_t> read_buffer_;
    std::vector<uint8_t> write_buffer_;
};

} // namespace storage
