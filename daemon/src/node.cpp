#include "node.hpp"

#include <cstdio>

namespace soilgw {

const char* toString(NodeState s) {
    switch (s) {
        case NodeState::Unknown: return "UNKNOWN";
        case NodeState::Online: return "ONLINE";
        case NodeState::Offline: return "OFFLINE";
        case NodeState::Faulty: return "FAULTY";
    }
    return "?";
}
const char* toString(MoistureLevel l) {
    switch (l) {
        case MoistureLevel::Normal: return "NORMAL";
        case MoistureLevel::Low: return "LOW";
        case MoistureLevel::High: return "HIGH";
    }
    return "?";
}
const char* toString(AlertType t) {
    switch (t) {
        case AlertType::LowMoisture: return "LOW_MOISTURE";
        case AlertType::HighMoisture: return "HIGH_MOISTURE";
        case AlertType::MoistureNormal: return "MOISTURE_NORMAL";
        case AlertType::NodeOffline: return "NODE_OFFLINE";
        case AlertType::NodeFaulty: return "NODE_FAULTY";
        case AlertType::NodeRecovered: return "NODE_RECOVERED";
    }
    return "?";
}

Node::Node(uint8_t id, Thresholds th, size_t window, std::chrono::milliseconds offlineTimeout)
    : id_(id), th_(th), timeout_(offlineTimeout), filter_(window) {}

void Node::configure(Thresholds th, std::chrono::milliseconds offlineTimeout) {
    th_ = th;
    timeout_ = offlineTimeout;
}

Alert Node::make(AlertType t, std::string msg) const {
    Alert a;
    a.ts = std::time(nullptr);
    a.node = id_;
    a.type = t;
    a.message = std::move(msg);
    return a;
}

void Node::evaluateLevel(std::vector<Alert>& out) {
    const double avg = filter_.value();
    char buf[160];
    switch (level_) {
        case MoistureLevel::Normal:
            if (avg < th_.low) {
                level_ = MoistureLevel::Low;
                std::snprintf(buf, sizeof buf, "Zone %u moisture %.1f%% is below %.0f%% - irrigation needed",
                              id_, avg, th_.low);
                out.push_back(make(AlertType::LowMoisture, buf));
            } else if (avg > th_.high) {
                level_ = MoistureLevel::High;
                std::snprintf(buf, sizeof buf, "Zone %u moisture %.1f%% is above %.0f%% - over-watering risk",
                              id_, avg, th_.high);
                out.push_back(make(AlertType::HighMoisture, buf));
            }
            break;
        case MoistureLevel::Low:
            if (avg >= th_.low + th_.hysteresis) {
                level_ = MoistureLevel::Normal;
                std::snprintf(buf, sizeof buf, "Zone %u moisture back to normal (%.1f%%)", id_, avg);
                out.push_back(make(AlertType::MoistureNormal, buf));
            }
            break;
        case MoistureLevel::High:
            if (avg <= th_.high - th_.hysteresis) {
                level_ = MoistureLevel::Normal;
                std::snprintf(buf, sizeof buf, "Zone %u moisture back to normal (%.1f%%)", id_, avg);
                out.push_back(make(AlertType::MoistureNormal, buf));
            }
            break;
    }
}

std::vector<Alert> Node::onFrame(const Frame& f, Clock::time_point now) {
    std::vector<Alert> out;
    ++frames_;
    if (have_seq_) lost_ += static_cast<uint8_t>(f.seq - last_seq_ - 1);
    have_seq_ = true;
    last_seq_ = f.seq;
    last_seen_ = now;
    battery_mv_ = f.battery_mv;
    raw_ = f.moisture;

    if (!validMoisture(f.moisture)) {
        if (++bad_streak_ >= kFaultyStreak && state_ != NodeState::Faulty) {
            state_ = NodeState::Faulty;
            char buf[96];
            std::snprintf(buf, sizeof buf, "Node %u faulty: %d consecutive invalid readings", id_, bad_streak_);
            out.push_back(make(AlertType::NodeFaulty, buf));
        }
        return out;
    }

    bad_streak_ = 0;
    filter_.add(f.moisture);
    if (state_ != NodeState::Online) {
        const bool wasKnown = state_ != NodeState::Unknown;
        state_ = NodeState::Online;
        if (wasKnown) out.push_back(make(AlertType::NodeRecovered, "Node " + std::to_string(id_) + " recovered"));
    }
    evaluateLevel(out);
    return out;
}

std::vector<Alert> Node::onTick(Clock::time_point now) {
    std::vector<Alert> out;
    if ((state_ == NodeState::Online || state_ == NodeState::Faulty) && now - last_seen_ > timeout_) {
        state_ = NodeState::Offline;
        out.push_back(make(AlertType::NodeOffline,
                           "Node " + std::to_string(id_) + " offline (no data for > " +
                               std::to_string(timeout_.count()) + " ms)"));
    }
    return out;
}

}  // namespace soilgw
