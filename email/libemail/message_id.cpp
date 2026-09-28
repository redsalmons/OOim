#include "message_id.h"

#include <chrono>
#include <cstdio>
#include <mutex>
#include <random>

std::string generate_x_message_id(const std::string& account) {
    static std::mutex s_mutex;
    static std::mt19937_64 s_rng{std::random_device{}()};

    uint64_t rnd;
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        rnd = s_rng();
    }

    auto now = std::chrono::system_clock::now().time_since_epoch();
    long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();

    std::string domain;
    size_t at = account.find('@');
    if (at != std::string::npos && at + 1 < account.size()) {
        domain = account.substr(at + 1);
    }
    if (domain.empty()) domain = "oim";

    char buf[128];
    snprintf(buf, sizeof(buf), "<%lld.%016llx@", ms, (unsigned long long)rnd);
    return std::string(buf) + domain + ">";
}
