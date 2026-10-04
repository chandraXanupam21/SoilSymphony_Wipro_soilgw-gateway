#include "config.hpp"

#include <fstream>
#include <stdexcept>

namespace soilgw {

namespace {
std::string trim(const std::string& s) {
    const char* ws = " \t\r\n";
    auto b = s.find_first_not_of(ws);
    if (b == std::string::npos) return "";
    auto e = s.find_last_not_of(ws);
    return s.substr(b, e - b + 1);
}
}  // namespace

Thresholds Config::thresholdsFor(int node) const {
    Thresholds t = defaults;
    auto it = overrides.find(node);
    if (it != overrides.end()) {
        if (it->second.low) t.low = *it->second.low;
        if (it->second.high) t.high = *it->second.high;
        if (it->second.hysteresis) t.hysteresis = *it->second.hysteresis;
    }
    return t;
}

Config Config::fromStream(std::istream& in) {
    Config c;
    std::string line;
    int ln = 0;
    while (std::getline(in, line)) {
        ++ln;
        if (auto h = line.find('#'); h != std::string::npos) line.erase(h);
        line = trim(line);
        if (line.empty()) continue;
        const std::string where = "config line " + std::to_string(ln) + ": ";
        auto eq = line.find('=');
        if (eq == std::string::npos) throw std::runtime_error(where + "expected key=value");
        const std::string key = trim(line.substr(0, eq));
        const std::string val = trim(line.substr(eq + 1));
        try {
            if (key == "device") c.device = val;
            else if (key == "log_dir") c.log_dir = val;
            else if (key == "socket") c.socket_path = val;
            else if (key == "pid_file") c.pid_file = val;
            else if (key == "offline_timeout_ms") c.offline_timeout_ms = std::stoi(val);
            else if (key == "window") c.window = std::stoul(val);
            else if (key == "low") c.defaults.low = std::stod(val);
            else if (key == "high") c.defaults.high = std::stod(val);
            else if (key == "hysteresis") c.defaults.hysteresis = std::stod(val);
            else if (key.rfind("node.", 0) == 0) {
                auto dot = key.find('.', 5);
                if (dot == std::string::npos) throw std::runtime_error(where + "bad node key '" + key + "'");
                int id = std::stoi(key.substr(5, dot - 5));
                std::string field = key.substr(dot + 1);
                double v = std::stod(val);
                if (field == "low") c.overrides[id].low = v;
                else if (field == "high") c.overrides[id].high = v;
                else if (field == "hysteresis") c.overrides[id].hysteresis = v;
                else throw std::runtime_error(where + "unknown node field '" + field + "'");
            } else {
                throw std::runtime_error(where + "unknown key '" + key + "'");
            }
        } catch (const std::invalid_argument&) {
            throw std::runtime_error(where + "invalid value for '" + key + "'");
        } catch (const std::out_of_range&) {
            throw std::runtime_error(where + "value out of range for '" + key + "'");
        }
    }
    if (c.window == 0) throw std::runtime_error("window must be >= 1");
    if (c.defaults.low >= c.defaults.high) throw std::runtime_error("low must be < high");
    return c;
}

Config Config::fromFile(const std::string& path) {
    std::ifstream f(path);
    if (!f) throw std::runtime_error("cannot open config file: " + path);
    return fromStream(f);
}

}  // namespace soilgw
