#include "network/socket.hpp"
#include "utils/logger.hpp"
#include <cstring>

namespace storage {

bool SocketUtil::init_network_subsystem() {
#if defined(_WIN32) || defined(_WIN64)
    WSADATA wsa_data;
    int err = WSAStartup(MAKEWORD(2, 2), &wsa_data);
    if (err != 0) {
        LOG_ERROR("WSAStartup failed with error: ", err);
        return false;
    }
#endif
    return true;
}

void SocketUtil::cleanup_network_subsystem() {
#if defined(_WIN32) || defined(_WIN64)
    WSACleanup();
#endif
}

bool SocketUtil::set_nonblocking(socket_t fd) {
#if defined(_WIN32) || defined(_WIN64)
    u_long mode = 1;
    return ioctlsocket(fd, FIONBIO, &mode) == 0;
#else
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) return false;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
}

bool SocketUtil::set_reuse_addr(socket_t fd) {
    int opt = 1;
#if defined(_WIN32) || defined(_WIN64)
    return setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&opt), sizeof(opt)) == 0;
#else
    return setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == 0;
#endif
}

bool SocketUtil::set_tcp_nodelay(socket_t fd) {
    int opt = 1;
#if defined(_WIN32) || defined(_WIN64)
    return setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&opt), sizeof(opt)) == 0;
#else
    return setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt)) == 0;
#endif
}

void SocketUtil::close_socket(socket_t fd) {
    if (fd == INVALID_SOCKET_FD) return;
#if defined(_WIN32) || defined(_WIN64)
    closesocket(fd);
#else
    close(fd);
#endif
}

bool SocketUtil::is_would_block() {
#if defined(_WIN32) || defined(_WIN64)
    int err = WSAGetLastError();
    return err == WSAEWOULDBLOCK;
#else
    int err = errno;
    return err == EAGAIN || err == EWOULDBLOCK || err == EINTR;
#endif
}

socket_t SocketUtil::create_tcp_server(const std::string& host, int port, int backlog) {
    socket_t fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == INVALID_SOCKET_FD) {
        LOG_ERROR("SocketUtil: Failed to create socket");
        return INVALID_SOCKET_FD;
    }

    if (!set_reuse_addr(fd)) {
        LOG_WARN("SocketUtil: Failed to set SO_REUSEADDR");
    }

    if (!set_nonblocking(fd)) {
        LOG_ERROR("SocketUtil: Failed to set socket non-blocking");
        close_socket(fd);
        return INVALID_SOCKET_FD;
    }

    sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));

    if (host.empty() || host == "0.0.0.0") {
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
    } else {
        inet_pton(AF_INET, host.c_str(), &addr.sin_addr);
    }

    if (bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        LOG_ERROR("SocketUtil: Failed to bind socket to ", host, ":", port);
        close_socket(fd);
        return INVALID_SOCKET_FD;
    }

    if (listen(fd, backlog) != 0) {
        LOG_ERROR("SocketUtil: Failed to listen on socket");
        close_socket(fd);
        return INVALID_SOCKET_FD;
    }

    LOG_INFO("SocketUtil: TCP server listening on ", host, ":", port, " (non-blocking fd=", fd, ")");
    return fd;
}

socket_t SocketUtil::create_tcp_client(const std::string& host, int port) {
    socket_t fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == INVALID_SOCKET_FD) return INVALID_SOCKET_FD;

    set_tcp_nodelay(fd);

    sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    inet_pton(AF_INET, host.c_str(), &addr.sin_addr);

    if (connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        close_socket(fd);
        return INVALID_SOCKET_FD;
    }

    return fd;
}

std::string SocketUtil::get_peer_ip_port(socket_t fd, int& port) {
    sockaddr_in addr;
#if defined(_WIN32) || defined(_WIN64)
    int len = sizeof(addr);
#else
    socklen_t len = sizeof(addr);
#endif
    if (getpeername(fd, reinterpret_cast<sockaddr*>(&addr), &len) == 0) {
        char ip_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &addr.sin_addr, ip_str, INET_ADDRSTRLEN);
        port = ntohs(addr.sin_port);
        return std::string(ip_str);
    }
    port = 0;
    return "unknown";
}

} // namespace storage
