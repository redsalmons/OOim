#ifndef LOGGER_H
#define LOGGER_H

#include <string>
#include <cstdio>
#include <cstdarg>
#include <fstream>
#include <mutex>
#include <memory>
#include <sstream>
#include <chrono>
#include <iomanip>
#include <filesystem>
#include <iostream>
#include <cstdlib>

namespace oemail {

class Logger {
public:
    static Logger& getInstance() {
        static Logger instance;
        return instance;
    }

    void init(const std::string& log_dir) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (log_stream_.is_open()) {
            log_stream_.close();
        }
        std::filesystem::create_directories(log_dir);
        log_file_path_ = log_dir + "/oim.log";
        // Open once and keep open
        log_stream_.open(log_file_path_, std::ios::out | std::ios::app);
        if (!log_stream_.is_open()) {
            std::cerr << "Logger: failed to open " << log_file_path_ << ", aborting" << std::endl;
            std::abort();
        }
        initialized_ = true;
    }

    void logf(const char* level, const char* fmt, ...) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!initialized_ || !log_stream_.is_open()) return;
        char buf[4096];
        va_list args;
        va_start(args, fmt);
        vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        std::tm tm = *std::localtime(&time_t);
        log_stream_ << "[" << std::put_time(&tm, "%Y-%m-%d %H:%M:%S") << "] " << buf << std::endl;
        log_stream_.flush();
    }

    void log(const std::string& level, const std::string& message) {
        logf(level.c_str(), "%s", message.c_str());
    }

    void debug(const std::string& message) {
        log("DEBUG", message);
    }

    void info(const std::string& message) {
        log("INFO", message);
    }

    void warning(const std::string& message) {
        log("WARNING", message);
    }

    void error(const std::string& message) {
        log("ERROR", message);
    }

    std::string get_log_file_path() const {
        return log_file_path_;
    }

private:
    Logger() = default;
    ~Logger() = default;
    
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    std::string log_file_path_;
    std::ofstream log_stream_;
    bool initialized_ = false;
    std::mutex mutex_;
};

} // namespace oemail

// Convenience macros - printf-style (outside namespace so usable everywhere)
#define LOG_DEBUG(fmt, ...) oemail::Logger::getInstance().logf("DEBUG", fmt, ##__VA_ARGS__)
#define LOG_INFO(fmt, ...) oemail::Logger::getInstance().logf("INFO", fmt, ##__VA_ARGS__)
#define LOG_WARNING(fmt, ...) oemail::Logger::getInstance().logf("WARNING", fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) oemail::Logger::getInstance().logf("ERROR", fmt, ##__VA_ARGS__)

#endif // LOGGER_H
