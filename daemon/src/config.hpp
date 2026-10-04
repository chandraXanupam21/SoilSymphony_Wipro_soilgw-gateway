#pragma once
#include <istream>
#include <map>
#include <optional>
#include <string>

#include "node.hpp"

namespace soilgw {

struct Config {
    std::string device = "/dev/soilgw";
    std::string log_dir = "./logs";
    std::string socket_path = "/tmp/soilgw.sock";
    std::string pid_file;
    int offline_timeout_ms = 5000;
    size_t window = 5;
    Thresholds defaults;

    struct Override {
        std::optional<double> low, high, hysteresis;
    };
    std::map<int, Override> overrides;

    Thresholds thresholdsFor(int node) const;

    // Throws std::runtime_error on malformed input.
    static Config fromStream(std::istream& in);
    static Config fromFile(const std::string& path);
};

}  // namespace soilgw
