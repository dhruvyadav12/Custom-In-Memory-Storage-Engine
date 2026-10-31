#pragma once

#include <cstdint>
#include <string>

#if defined(_WIN32) || defined(_WIN64)
#include <winsock2.h>
#include <ws2tcpip.h>
using socket_t = SOCKET;
constexpr socket_t INVALID_SOCKET_FD = INVALID_SOCKET;
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
using socket_t = int;
constexpr socket_t INVALID_SOCKET_FD = -1;
#endif

namespace storage {

class SocketUtil {
public:
    static bool init_network_subsystem();
    static void cleanup_network_subsystem();

    static socket_t create_tcp_server(const std::string& host, int port, int backlog = 1024);
    static socket_t create_tcp_client(const std::string& host, int port);

    static bool set_nonblocking(socket_t fd);
    static bool set_reuse_addr(socket_t fd);
    static bool set_tcp_nodelay(socket_t fd);
    static void close_socket(socket_t fd);

    static bool is_would_block();
    static std::string get_peer_ip_port(socket_t fd, int& port);
};

} // namespace storage
