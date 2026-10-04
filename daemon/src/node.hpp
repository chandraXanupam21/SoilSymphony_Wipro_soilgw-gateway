#pragma once
#include <chrono>
#include <cstdint>
#include <ctime>
#include <deque>
#include <string>
#include <vector>

#include "protocol.hpp"

namespace soilgw {

using Clock = std::chrono::steady_clock;

// Sliding-window moving average (generic).
template <typename T>
class MovingAverage {
public:
    explicit MovingAverage(size_t window = 5) : window_(window ? window : 1) {}
    T add(T v) {
        q_.push_back(v);
        if (q_.size() > window_) q_.pop_front();
        return value();
    }
    T value() const {
        if (q_.empty()) return T{};
        T s{};
        for (const T& x : q_) s += x;
        return s / static_cast<T>(q_.size());
    }
    size_t size() const { return q_.size(); }
    void reset() { q_.clear(); }

private:
    size_t window_;
    std::deque<T> q_;
};

enum class NodeState { Unknown, Online, Offline, Faulty };
enum class MoistureLevel { Normal, Low, High };
enum class AlertType { LowMoisture, HighMoisture, MoistureNormal, NodeOffline, NodeFaulty, NodeRecovered };

const char* toString(NodeState s);
const char* toString(MoistureLevel l);
const char* toString(AlertType t);

struct Thresholds {
    double low = 25.0;
    double high = 80.0;
    double hysteresis = 5.0;
};

struct Alert {
    std::time_t ts = 0;
    uint8_t node = 0;
    AlertType type = AlertType::LowMoisture;
    std::string message;
};

// One field sensor node: validation, filtering, state machine, alert logic.
// Time is injected so the class is fully unit-testable.
class Node {
public:
    static constexpr int kFaultyStreak = 3;

    Node(uint8_t id, Thresholds th, size_t window, std::chrono::milliseconds offlineTimeout);

    std::vector<Alert> onFrame(const Frame& f, Clock::time_point now);
    std::vector<Alert> onTick(Clock::time_point now);
    void configure(Thresholds th, std::chrono::milliseconds offlineTimeout);

    uint8_t id() const { return id_; }
    NodeState state() const { return state_; }
    MoistureLevel level() const { return level_; }
    double raw() const { return raw_; }
    double average() const { return filter_.value(); }
    uint16_t batteryMv() const { return battery_mv_; }
    uint64_t frames() const { return frames_; }
    uint64_t lost() const { return lost_; }
    Clock::time_point lastSeen() const { return last_seen_; }
    const Thresholds& thresholds() const { return th_; }

    static bool validMoisture(double m) { return m >= 0.0 && m <= 100.0; }

private:
    Alert make(AlertType t, std::string msg) const;
    void evaluateLevel(std::vector<Alert>& out);

    uint8_t id_;
    Thresholds th_;
    std::chrono::milliseconds timeout_;
    MovingAverage<double> filter_;
    NodeState state_ = NodeState::Unknown;
    MoistureLevel level_ = MoistureLevel::Normal;
    double raw_ = 0.0;
    uint16_t battery_mv_ = 0;
    uint64_t frames_ = 0, lost_ = 0;
    uint8_t last_seq_ = 0;
    bool have_seq_ = false;
    int bad_streak_ = 0;
    Clock::time_point last_seen_{};
};

}  // namespace soilgw
