#include "network/connection.hpp"
#include "storage/ttl_manager.hpp"
#include "utils/logger.hpp"
#include <algorithm>
#include <cstring>

namespace storage {

Connection::Connection(socket_t fd, std::string ip, int port)
    : fd_(fd), ip_(std::move(ip)), port_(port),
      last_active_ms_(TTLManager::current_time_ms()) {
    read_buffer_.reserve(4096);
    write_buffer_.reserve(4096);
}

Connection::~Connection() {
    close();
}

void Connection::update_active_time() {
    last_active_ms_ = TTLManager::current_time_ms();
}

int Connection::read_from_socket() {
    if (closed_) return -1;
    update_active_time();

    char temp_buf[8192];
    int total_read = 0;

    while (true) {
#if defined(_WIN32) || defined(_WIN64)
        int n = recv(fd_, temp_buf, sizeof(temp_buf), 0);
#else
        ssize_t n = recv(fd_, temp_buf, sizeof(temp_buf), 0);
#endif
        if (n > 0) {
            read_buffer_.insert(read_buffer_.end(), temp_buf, temp_buf + n);
            total_read += n;
            if (static_cast<size_t>(n) < sizeof(temp_buf)) {
                break; // Read all currently available data
            }
        } else if (n == 0) {
            // Client closed connection
            close();
            return 0;
        } else {
            if (SocketUtil::is_would_block()) {
                break; // Done reading available non-blocking chunks
            }
            // Error occurred
            close();
            return -1;
        }
    }

    return total_read;
}

int Connection::write_to_socket() {
    if (closed_ || write_buffer_.empty()) return 0;
    update_active_time();

    int total_written = 0;
    while (!write_buffer_.empty()) {
#if defined(_WIN32) || defined(_WIN64)
        int n = send(fd_, reinterpret_cast<const char*>(write_buffer_.data()),
                     static_cast<int>(write_buffer_.size()), 0);
#else
        ssize_t n = send(fd_, write_buffer_.data(), write_buffer_.size(), 0);
#endif
        if (n > 0) {
            total_written += n;
            write_buffer_.erase(write_buffer_.begin(), write_buffer_.begin() + n);
        } else if (n < 0) {
            if (SocketUtil::is_would_block()) {
                break; // Socket send buffer is full, wait for next writable event
            }
            close();
            return -1;
        } else {
            break;
        }
    }

    return total_written;
}

void Connection::append_response(const std::vector<uint8_t>& data) {
    write_buffer_.insert(write_buffer_.end(), data.begin(), data.end());
}

bool Connection::parse_next_command(std::vector<std::string>& out_cmd, ResponseFormat& out_format) {
    if (read_buffer_.empty()) return false;

    // Check if packet starts with TLV Magic Byte (0xA5)
    if (read_buffer_[0] == TLV_MAGIC) {
        TLVMessage msg;
        size_t consumed = 0;
        if (TLVSerializer::deserialize(read_buffer_.data(), read_buffer_.size(), msg, consumed)) {
            out_format = ResponseFormat::TLV;
            if (msg.type == TLVType::CMD_REQUEST) {
                out_cmd = msg.as_string_list();
            } else {
                out_cmd = { msg.as_string() };
            }
            read_buffer_.erase(read_buffer_.begin(), read_buffer_.begin() + consumed);
            return true;
        }
        return false; // Wait for complete TLV packet
    }

    // Otherwise, parse Text Protocol (terminated by '\n')
    auto it = std::find(read_buffer_.begin(), read_buffer_.end(), '\n');
    if (it == read_buffer_.end()) {
        return false; // Incomplete line, wait for more data
    }

    size_t line_len = std::distance(read_buffer_.begin(), it) + 1;
    std::string line(read_buffer_.begin(), read_buffer_.begin() + line_len);
    read_buffer_.erase(read_buffer_.begin(), read_buffer_.begin() + line_len);

    // Strip trailing \r and \n
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) {
        line.pop_back();
    }

    if (line.empty()) {
        return false;
    }

    out_format = ResponseFormat::TEXT;
    out_cmd = CommandDispatcher::parse_text_line(line);
    return !out_cmd.empty();
}

void Connection::close() {
    if (!closed_) {
        closed_ = true;
        SocketUtil::close_socket(fd_);
        fd_ = INVALID_SOCKET_FD;
    }
}

} // namespace storage
