#include "logger.hpp"

#include <syslog.h>

#include <cstdio>
#include <filesystem>
#include <stdexcept>

namespace soilgw {

namespace {
bool g_syslog = false;
}

void setSyslog(bool on) {
    g_syslog = on;
    if (on) openlog("soilgwd", LOG_PID, LOG_DAEMON);
}

void sysLog(int priority, const char* fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (g_syslog) {
        ::syslog(priority, "%s", buf);
    } else {
        const char* lvl = priority <= LOG_ERR ? "ERROR" : priority == LOG_WARNING ? "WARN" : "INFO";
        std::fprintf(stderr, "[%s] [%s] %s\n", formatTime(std::time(nullptr)).c_str(), lvl, buf);
    }
}

std::string formatTime(std::time_t t) {
    char buf[32];
    std::tm tm{};
    localtime_r(&t, &tm);
    std::strftime(buf, sizeof buf, "%Y-%m-%d %H:%M:%S", &tm);
    return buf;
}

Logger::Logger(const std::string& dir) {
    if (dir.empty()) return;
    std::filesystem::create_directories(dir);
    const std::string rp = dir + "/readings.csv";
    const bool fresh = !std::filesystem::exists(rp) || std::filesystem::file_size(rp) == 0;
    readings_.open(rp, std::ios::app);
    alerts_.open(dir + "/alerts.log", std::ios::app);
    if (!readings_ || !alerts_) throw std::runtime_error("cannot open log files in " + dir);
    if (fresh) readings_ << "timestamp,node,seq,moisture_raw,moisture_avg,battery_mv,state\n";
}

void Logger::reading(const Frame& f, double average, NodeState state) {
    std::lock_guard<std::mutex> lk(m_);
    if (!readings_.is_open()) return;
    char buf[160];
    std::snprintf(buf, sizeof buf, "%s,%u,%u,%.2f,%.2f,%u,%s\n", formatTime(std::time(nullptr)).c_str(),
                  f.node, f.seq, f.moisture, average, f.battery_mv, toString(state));
    readings_ << buf;
    readings_.flush();
}

void Logger::alert(const Alert& a) {
    std::lock_guard<std::mutex> lk(m_);
    if (!alerts_.is_open()) return;
    alerts_ << formatTime(a.ts) << " [" << toString(a.type) << "] node=" << int(a.node) << " " << a.message << "\n";
    alerts_.flush();
}

}  // namespace soilgw
