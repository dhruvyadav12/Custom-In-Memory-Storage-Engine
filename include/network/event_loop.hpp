#pragma once

#include "network/connection.hpp"
#include "network/socket.hpp"
#include "protocol/command.hpp"
#include "storage/storage_engine.hpp"
#include "threadpool/thread_pool.hpp"

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#if defined(__linux__)
#include <sys/epoll.h>
#endif

namespace storage {

class EventLoop {
public:
    EventLoop(StorageEngine& engine, PersistenceManager& persistence, ThreadPool& pool);
    ~EventLoop();

    bool init(const std::string& host, int port);
    void run();
    void stop();

    bool is_running() const { return running_.load(); }
    size_t connection_count() const { return connections_.size(); }

private:
    StorageEngine& engine_;
    PersistenceManager& persistence_;
    ThreadPool& pool_;
    CommandDispatcher dispatcher_;

    socket_t listen_fd_ = INVALID_SOCKET_FD;
    std::atomic<bool> running_{false};

    std::unordered_map<socket_t, std::unique_ptr<Connection>> connections_;

#if defined(__linux__)
    int epoll_fd_ = -1;
    void add_fd_epoll(socket_t fd, uint32_t events);
    void mod_fd_epoll(socket_t fd, uint32_t events);
    void del_fd_epoll(socket_t fd);
    void run_epoll();
#else
    void run_select();
#endif

    void handle_new_connection();
    void handle_client_read(socket_t fd);
    void handle_client_write(socket_t fd);
    void close_connection(socket_t fd);
    void on_periodic_tick();
};

} // namespace storage
