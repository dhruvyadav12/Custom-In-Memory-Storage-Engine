#include "network/socket.hpp"
#include "protocol/command.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <string>
#include <thread>
#include <vector>

struct BenchResult {
    std::vector<double> latencies_us;
    size_t successful_ops = 0;
    size_t failed_ops = 0;
};

void run_client_worker(const std::string& host, int port, size_t ops_per_thread,
                       const std::string& test_type, BenchResult& result) {
    storage::socket_t sock = storage::SocketUtil::create_tcp_client(host, port);
    if (sock == storage::INVALID_SOCKET_FD) {
        result.failed_ops = ops_per_thread;
        return;
    }

    result.latencies_us.reserve(ops_per_thread);
    char buf[1024];

    for (size_t i = 0; i < ops_per_thread; ++i) {
        std::string cmd;
        if (test_type == "SET") {
            cmd = "SET bench_k_" + std::to_string(i) + " bench_v_" + std::to_string(i) + "\r\n";
        } else if (test_type == "GET") {
            cmd = "GET bench_k_" + std::to_string(i % 1000) + "\r\n";
        } else if (test_type == "LPUSH") {
            cmd = "LPUSH bench_list val_" + std::to_string(i) + "\r\n";
        } else if (test_type == "ZADD") {
            cmd = "ZADD bench_zset " + std::to_string(i % 500) + " m_" + std::to_string(i) + "\r\n";
        } else {
            cmd = "PING\r\n";
        }

        auto start = std::chrono::high_resolution_clock::now();
#if defined(_WIN32) || defined(_WIN64)
        send(sock, cmd.c_str(), static_cast<int>(cmd.size()), 0);
        int n = recv(sock, buf, sizeof(buf) - 1, 0);
#else
        send(sock, cmd.c_str(), cmd.size(), 0);
        ssize_t n = recv(sock, buf, sizeof(buf) - 1, 0);
#endif
        auto end = std::chrono::high_resolution_clock::now();

        if (n > 0) {
            double duration_us = std::chrono::duration<double, std::micro>(end - start).count();
            result.latencies_us.push_back(duration_us);
            result.successful_ops++;
        } else {
            result.failed_ops++;
            break;
        }
    }

    storage::SocketUtil::close_socket(sock);
}

void print_stats(const std::string& title, size_t num_conns, size_t total_requests,
                 double total_seconds, std::vector<double>& latencies) {
    if (latencies.empty()) {
        std::cout << "No successful requests recorded.\n";
        return;
    }

    std::sort(latencies.begin(), latencies.end());
    double sum = std::accumulate(latencies.begin(), latencies.end(), 0.0);
    double avg = sum / latencies.size();
    double p50 = latencies[latencies.size() * 50 / 100];
    double p95 = latencies[latencies.size() * 95 / 100];
    double p99 = latencies[latencies.size() * 99 / 100];
    double max = latencies.back();
    double qps = total_requests / total_seconds;

    std::cout << "\n=======================================================\n"
              << " Benchmark: " << title << "\n"
              << " Concurrent Clients: " << num_conns << "\n"
              << " Total Operations  : " << total_requests << "\n"
              << " Elapsed Time      : " << std::fixed << std::setprecision(3) << total_seconds << " s\n"
              << " Throughput (QPS)  : " << std::fixed << std::setprecision(1) << qps << " ops/sec\n"
              << "-------------------------------------------------------\n"
              << " Latency Metrics (microseconds / milliseconds):\n"
              << "   Avg Latency : " << std::setw(8) << avg << " us (" << (avg / 1000.0) << " ms)\n"
              << "   P50 Latency : " << std::setw(8) << p50 << " us (" << (p50 / 1000.0) << " ms)\n"
              << "   P95 Latency : " << std::setw(8) << p95 << " us (" << (p95 / 1000.0) << " ms)\n"
              << "   P99 Latency : " << std::setw(8) << p99 << " us (" << (p99 / 1000.0) << " ms)\n"
              << "   Max Latency : " << std::setw(8) << max << " us (" << (max / 1000.0) << " ms)\n"
              << "=======================================================\n";
}

int main(int argc, char* argv[]) {
    std::string host = "127.0.0.1";
    int port = 6379;
    size_t num_clients = 20;
    size_t requests_per_client = 2500;
    std::string test_type = "PING";

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--host" && i + 1 < argc) host = argv[++i];
        else if (arg == "--port" && i + 1 < argc) port = std::stoi(argv[++i]);
        else if (arg == "-c" && i + 1 < argc) num_clients = std::stoul(argv[++i]);
        else if (arg == "-n" && i + 1 < argc) requests_per_client = std::stoul(argv[++i]);
        else if (arg == "-t" && i + 1 < argc) test_type = argv[++i];
    }

    storage::SocketUtil::init_network_subsystem();

    std::cout << "Starting benchmark against " << host << ":" << port << " ...\n"
              << "Test: " << test_type << ", Clients: " << num_clients
              << ", Requests per client: " << requests_per_client
              << " (Total: " << (num_clients * requests_per_client) << ")\n";

    std::vector<std::thread> threads;
    std::vector<BenchResult> results(num_clients);

    auto start_time = std::chrono::high_resolution_clock::now();
    for (size_t i = 0; i < num_clients; ++i) {
        threads.emplace_back(run_client_worker, host, port, requests_per_client, test_type, std::ref(results[i]));
    }

    for (auto& t : threads) {
        t.join();
    }
    auto end_time = std::chrono::high_resolution_clock::now();
    double total_sec = std::chrono::duration<double>(end_time - start_time).count();

    std::vector<double> all_latencies;
    size_t total_success = 0;
    for (const auto& r : results) {
        total_success += r.successful_ops;
        all_latencies.insert(all_latencies.end(), r.latencies_us.begin(), r.latencies_us.end());
    }

    print_stats(test_type, num_clients, total_success, total_sec, all_latencies);
    storage::SocketUtil::cleanup_network_subsystem();
    return 0;
}
