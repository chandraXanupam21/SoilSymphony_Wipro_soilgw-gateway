#pragma once
#include <cstdarg>
#include <fstream>
#include <mutex>
#include <string>

#include "node.hpp"

namespace soilgw {

// Thread-safe CSV/text file logger. Empty directory => disabled (used in tests).
class Logger {
public:
    explicit Logger(const std::string& dir);
    void reading(const Frame& f, double average, NodeState state);
    void alert(const Alert& a);

private:
    std::mutex m_;
    std::ofstream readings_, alerts_;
};

// Process-wide diagnostic log (stderr, or syslog once daemonised).
void setSyslog(bool on);
void sysLog(int priority, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
std::string formatTime(std::time_t t);

}  // namespace soilgw
