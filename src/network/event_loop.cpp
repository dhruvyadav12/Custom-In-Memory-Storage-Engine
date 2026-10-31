#include "network/event_loop.hpp"
#include "storage/ttl_manager.hpp"
#include "utils/logger.hpp"
#include <chrono>
#include <thread>

namespace storage {

EventLoop::EventLoop(StorageEngine& engine, PersistenceManager& persistence, ThreadPool& pool)
    : engine_(engine), persistence_(persistence), pool_(pool),
      dispatcher_(engine, persistence, pool) {}

EventLoop::~EventLoop() {
    stop();
}

bool EventLoop::init(const std::string& host, int port) {
    if (!SocketUtil::init_network_subsystem()) {
        return false;
    }

    listen_fd_ = SocketUtil::create_tcp_server(host, port);
    if (listen_fd_ == INVALID_SOCKET_FD) {
        LOG_ERROR("EventLoop: Failed to create server socket on ", host, ":", port);
        return false;
    }

#if defined(__linux__)
    epoll_fd_ = epoll_create1(0);
    if (epoll_fd_ < 0) {
        LOG_ERROR("EventLoop: Failed to create epoll instance");
        SocketUtil::close_socket(listen_fd_);
        listen_fd_ = INVALID_SOCKET_FD;
        return false;
    }
    add_fd_epoll(listen_fd_, EPOLLIN);
    LOG_INFO("EventLoop: Initialized Linux epoll multiplexing engine");
#else
    LOG_INFO("EventLoop: Initialized portable I/O multiplexing engine");
#endif

    running_.store(true);
    return true;
}

void EventLoop::stop() {
    bool expected = true;
    if (running_.compare_exchange_strong(expected, false)) {
        LOG_INFO("EventLoop: Shutting down event loop...");
        for (auto& pair : connections_) {
            pair.second->close();
        }
        connections_.clear();

        if (listen_fd_ != INVALID_SOCKET_FD) {
            SocketUtil::close_socket(listen_fd_);
            listen_fd_ = INVALID_SOCKET_FD;
        }

#if defined(__linux__)
        if (epoll_fd_ >= 0) {
            close(epoll_fd_);
            epoll_fd_ = -1;
        }
#endif
        SocketUtil::cleanup_network_subsystem();
    }
}

void EventLoop::run() {
#if defined(__linux__)
    run_epoll();
#else
    run_select();
#endif
}

#if defined(__linux__)
void EventLoop::add_fd_epoll(socket_t fd, uint32_t events) {
    epoll_event ev;
    ev.events = events;
    ev.data.fd = fd;
    epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &ev);
}

void EventLoop::mod_fd_epoll(socket_t fd, uint32_t events) {
    epoll_event ev;
    ev.events = events;
    ev.data.fd = fd;
    epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, fd, &ev);
}

void EventLoop::del_fd_epoll(socket_t fd) {
    epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr);
}

void EventLoop::run_epoll() {
    constexpr int MAX_EVENTS = 1024;
    epoll_event events[MAX_EVENTS];
    auto last_tick = std::chrono::steady_clock::now();

    LOG_INFO("EventLoop: Epoll event loop started");

    while (running_.load()) {
        int nfds = epoll_wait(epoll_fd_, events, MAX_EVENTS, 100 /* 100ms timeout for timers */);
        if (nfds < 0) {
            if (errno == EINTR) continue;
            LOG_ERROR("EventLoop: epoll_wait returned error: ", errno);
            break;
        }

        for (int i = 0; i < nfds; ++i) {
            socket_t fd = events[i].data.fd;
            uint32_t ev = events[i].events;

            if (fd == listen_fd_) {
                handle_new_connection();
            } else {
                if (ev & (EPOLLERR | EPOLLHUP)) {
                    close_connection(fd);
                    continue;
                }
                if (ev & EPOLLIN) {
                    handle_client_read(fd);
                }
                if (ev & EPOLLOUT) {
                    handle_client_write(fd);
                }
            }
        }

        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - last_tick).count() >= 100) {
            on_periodic_tick();
            last_tick = now;
        }
    }
}
#else
void EventLoop::run_select() {
    auto last_tick = std::chrono::steady_clock::now();
    LOG_INFO("EventLoop: Portable select-based event loop started");

    while (running_.load()) {
        fd_set read_fds, write_fds, err_fds;
        FD_ZERO(&read_fds);
        FD_ZERO(&write_fds);
        FD_ZERO(&err_fds);

        socket_t max_fd = listen_fd_;
        FD_SET(listen_fd_, &read_fds);
        FD_SET(listen_fd_, &err_fds);

        for (const auto& pair : connections_) {
            socket_t cfd = pair.first;
            FD_SET(cfd, &read_fds);
            FD_SET(cfd, &err_fds);
            if (pair.second->has_pending_write()) {
                FD_SET(cfd, &write_fds);
            }
            if (cfd > max_fd) {
                max_fd = cfd;
            }
        }

        timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 50000; // 50ms

#if defined(_WIN32) || defined(_WIN64)
        int activity = select(0, &read_fds, &write_fds, &err_fds, &tv);
#else
        int activity = select(max_fd + 1, &read_fds, &write_fds, &err_fds, &tv);
#endif

        if (activity < 0) {
            if (SocketUtil::is_would_block()) continue;
            break;
        }

        if (activity > 0) {
            if (FD_ISSET(listen_fd_, &read_fds)) {
                handle_new_connection();
            }

            std::vector<socket_t> to_read, to_write, to_close;
            for (const auto& pair : connections_) {
                socket_t cfd = pair.first;
                if (FD_ISSET(cfd, &err_fds)) {
                    to_close.push_back(cfd);
                } else {
                    if (FD_ISSET(cfd, &read_fds)) to_read.push_back(cfd);
                    if (FD_ISSET(cfd, &write_fds)) to_write.push_back(cfd);
                }
            }

            for (socket_t fd : to_close) close_connection(fd);
            for (socket_t fd : to_read) handle_client_read(fd);
            for (socket_t fd : to_write) handle_client_write(fd);
        }

        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - last_tick).count() >= 100) {
            on_periodic_tick();
            last_tick = now;
        }
    }
}
#endif

void EventLoop::handle_new_connection() {
    while (true) {
        sockaddr_in client_addr;
#if defined(_WIN32) || defined(_WIN64)
        int addr_len = sizeof(client_addr);
#else
        socklen_t addr_len = sizeof(client_addr);
#endif
        socket_t client_fd = accept(listen_fd_, reinterpret_cast<sockaddr*>(&client_addr), &addr_len);
        if (client_fd == INVALID_SOCKET_FD) {
            if (SocketUtil::is_would_block()) {
                break; // All pending connections accepted
            }
            LOG_ERROR("EventLoop: Error accepting connection");
            break;
        }

        SocketUtil::set_nonblocking(client_fd);
        SocketUtil::set_tcp_nodelay(client_fd);

        int client_port = 0;
        std::string client_ip = SocketUtil::get_peer_ip_port(client_fd, client_port);
        LOG_INFO("EventLoop: New client connected: ", client_ip, ":", client_port, " (fd=", client_fd, ")");

        connections_[client_fd] = std::make_unique<Connection>(client_fd, client_ip, client_port);

#if defined(__linux__)
        add_fd_epoll(client_fd, EPOLLIN);
#endif
    }
}

void EventLoop::handle_client_read(socket_t fd) {
    auto it = connections_.find(fd);
    if (it == connections_.end()) return;

    Connection& conn = *it->second;
    int bytes_read = conn.read_from_socket();

    if (bytes_read <= 0 && conn.is_closed()) {
        close_connection(fd);
        return;
    }

    // Process all complete commands in read buffer
    std::vector<std::string> cmd_args;
    ResponseFormat format;
    while (conn.parse_next_command(cmd_args, format)) {
        CommandResponse resp = dispatcher_.execute(cmd_args);
        std::vector<uint8_t> resp_bytes = resp.serialize(format);
        conn.append_response(resp_bytes);
    }

    // Attempt immediate write
    conn.write_to_socket();

#if defined(__linux__)
    if (conn.has_pending_write()) {
        mod_fd_epoll(fd, EPOLLIN | EPOLLOUT);
    } else {
        mod_fd_epoll(fd, EPOLLIN);
    }
#endif
}

void EventLoop::handle_client_write(socket_t fd) {
    auto it = connections_.find(fd);
    if (it == connections_.end()) return;

    Connection& conn = *it->second;
    conn.write_to_socket();

#if defined(__linux__)
    if (!conn.has_pending_write()) {
        mod_fd_epoll(fd, EPOLLIN); // Drained, listen only for read events
    }
#endif
}

void EventLoop::close_connection(socket_t fd) {
    auto it = connections_.find(fd);
    if (it != connections_.end()) {
        LOG_INFO("EventLoop: Client disconnected: ", it->second->ip(), ":", it->second->port(), " (fd=", fd, ")");
#if defined(__linux__)
        del_fd_epoll(fd);
#endif
        it->second->close();
        connections_.erase(it);
    }
}

void EventLoop::on_periodic_tick() {
    // 1. Active TTL eviction using Min-Heap
    size_t evicted = engine_.purge_expired(100);
    if (evicted > 0) {
        LOG_DEBUG("EventLoop: Evicted ", evicted, " expired keys via Min-Heap");
    }

    // 2. Prune dead or heavily idle connections (> 600 seconds idle)
    uint64_t now_ms = TTLManager::current_time_ms();
    std::vector<socket_t> idle_fds;
    for (const auto& pair : connections_) {
        if (now_ms > pair.second->last_active() + 600000) {
            idle_fds.push_back(pair.first);
        }
    }
    for (socket_t fd : idle_fds) {
        LOG_INFO("EventLoop: Timing out idle connection fd=", fd);
        close_connection(fd);
    }
}

} // namespace storage
