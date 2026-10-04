#pragma once
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

#include "config.hpp"
#include "node.hpp"

namespace soilgw {

struct NodeInfo {
    uint8_t id;
    NodeState state;
    MoistureLevel level;
    double raw, average;
    uint16_t battery_mv;
    uint64_t frames, lost;
    long last_seen_ms;  // age; -1 if never
};

// Thread-safe registry of nodes; nodes are created on first frame.
class NodeManager {
public:
    explicit NodeManager(const Config& cfg) : cfg_(cfg) {}

    std::vector<Alert> handleFrame(const Frame& f, Clock::time_point now);
    std::vector<Alert> tick(Clock::time_point now);
    std::vector<NodeInfo> snapshot(Clock::time_point now = Clock::now()) const;
    std::optional<NodeInfo> info(uint8_t id, Clock::time_point now = Clock::now()) const;
    void applyConfig(const Config& cfg);

private:
    NodeInfo makeInfo(const Node& n, Clock::time_point now) const;

    mutable std::mutex m_;
    Config cfg_;
    std::map<uint8_t, std::unique_ptr<Node>> nodes_;
};

}  // namespace soilgw
