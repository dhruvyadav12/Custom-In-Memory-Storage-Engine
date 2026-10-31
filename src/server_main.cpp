#include "network/event_loop.hpp"
#include "storage/persistence.hpp"
#include "storage/storage_engine.hpp"
#include "threadpool/thread_pool.hpp"
#include "utils/logger.hpp"

#include <csignal>
#include <cstdlib>
#include <iostream>
#include <string>

static storage::EventLoop* g_loop = nullptr;

void signal_handler(int sig) {
    LOG_INFO("Received signal ", sig, ", initiating graceful server shutdown...");
    if (g_loop) {
        g_loop->stop();
    }
}

int main(int argc, char* argv[]) {
    std::string host = "0.0.0.0";
    int port = 6379;
    size_t num_threads = 4;
    std::string dump_file = "dump.tlv";

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--host" && i + 1 < argc) {
            host = argv[++i];
        } else if (arg == "--port" && i + 1 < argc) {
            port = std::stoi(argv[++i]);
        } else if (arg == "--threads" && i + 1 < argc) {
            num_threads = static_cast<size_t>(std::stoul(argv[++i]));
        } else if (arg == "--dump" && i + 1 < argc) {
            dump_file = argv[++i];
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Custom In-Memory Storage Engine\n"
                      << "Usage: " << argv[0] << " [options]\n\n"
                      << "Options:\n"
                      << "  --host <ip>       Bind host address (default: 0.0.0.0)\n"
                      << "  --port <port>     TCP listen port (default: 6379)\n"
                      << "  --threads <n>     ThreadPool worker count (default: 4)\n"
                      << "  --dump <file>     Snapshot file path (default: dump.tlv)\n"
                      << "  --help, -h        Show this help message\n";
            return 0;
        }
    }

    std::cout << R"(
  ____           _ _       _     _ _        ____  _                             
 |  _ \ ___   __| (_)___  | |   (_) | _____/ ___|| |_ ___  _ __ __ _  __ _  ___ 
 | |_) / _ \ / _` | / __| | |   | | |/ / _ \___ \| __/ _ \| '__/ _` |/ _` |/ _ \
 |  _ <  __/| (_| | \__ \ | |___| |   <  __/___) | || (_) | | | (_| | (_| |  __/
 |_| \_\___| \__,_|_|___/ |_____|_|_|\_\___|____/ \__\___/|_|  \__,_|\__, |\___|
                                                                     |___/      
    Custom In-Memory Storage Engine | C++17 | Epoll / Non-Blocking TCP
    TLV Binary Protocol | Min-Heap TTL Expiration | AVL Trees | ThreadPool
==============================================================================
)" << std::endl;

    LOG_INFO("Config: Host: ", host, " | Port: ", port, " | Worker Threads: ", num_threads);
    LOG_INFO("Config: Snapshot persistence target: ", dump_file);

    // Initialize Subsystems
    storage::StorageEngine engine;
    storage::PersistenceManager persistence(dump_file);
    storage::ThreadPool pool(num_threads);
    storage::EventLoop loop(engine, persistence, pool);
    g_loop = &loop;

    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    // Load Snapshot if present
    persistence.load(engine);

    // Start Event Loop
    if (!loop.init(host, port)) {
        LOG_ERROR("Fatal: Failed to start server event loop on ", host, ":", port);
        return 1;
    }

    LOG_INFO("Storage Engine Server is ready to accept client connections!");
    loop.run();

    // Graceful Shutdown: save snapshot to disk
    LOG_INFO("Saving final database snapshot before terminating...");
    persistence.save(engine);
    pool.shutdown();

    LOG_INFO("Custom In-Memory Storage Engine stopped cleanly.");
    return 0;
}
