#pragma once

#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>

namespace storage {

enum class LogLevel {
    DEBUG = 0,
    INFO,
    WARN,
    ERROR
};

class Logger {
public:
    static Logger& instance() {
        static Logger instance_;
        return instance_;
    }

    void set_level(LogLevel level) {
        min_level_ = level;
    }

    template <typename... Args>
    void log(LogLevel level, Args&&... args) {
        if (level < min_level_) return;

        std::ostringstream oss;
        (oss << ... << std::forward<Args>(args));

        std::string level_str;
        const char* color_code = "";
        const char* reset_code = "\033[0m";

        switch (level) {
            case LogLevel::DEBUG:
                level_str = "[DEBUG]";
                color_code = "\033[36m"; // Cyan
                break;
            case LogLevel::INFO:
                level_str = "[INFO ]";
                color_code = "\033[32m"; // Green
                break;
            case LogLevel::WARN:
                level_str = "[WARN ]";
                color_code = "\033[33m"; // Yellow
                break;
            case LogLevel::ERROR:
                level_str = "[ERROR]";
                color_code = "\033[31m"; // Red
                break;
        }

        auto now = std::chrono::system_clock::now();
        auto in_time_t = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      now.time_since_epoch()) % 1000;

        std::tm tm_buf;
#if defined(_WIN32) || defined(_WIN64)
        localtime_s(&tm_buf, &in_time_t);
#else
        localtime_r(&in_time_t, &tm_buf);
#endif

        std::unique_lock<std::mutex> lock(mutex_);
        std::cout << color_code << "[" 
                  << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S")
                  << "." << std::setfill('0') << std::setw(3) << ms.count()
                  << "] " << level_str << " " << reset_code
                  << oss.str() << std::endl;
    }

private:
    Logger() : min_level_(LogLevel::INFO) {}
    LogLevel min_level_;
    std::mutex mutex_;
};

#define LOG_DEBUG(...) storage::Logger::instance().log(storage::LogLevel::DEBUG, __VA_ARGS__)
#define LOG_INFO(...)  storage::Logger::instance().log(storage::LogLevel::INFO, __VA_ARGS__)
#define LOG_WARN(...)  storage::Logger::instance().log(storage::LogLevel::WARN, __VA_ARGS__)
#define LOG_ERROR(...) storage::Logger::instance().log(storage::LogLevel::ERROR, __VA_ARGS__)

} // namespace storage
