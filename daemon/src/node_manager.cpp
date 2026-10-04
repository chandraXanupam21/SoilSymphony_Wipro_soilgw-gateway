#include "node_manager.hpp"

namespace soilgw {

std::vector<Alert> NodeManager::handleFrame(const Frame& f, Clock::time_point now) {
    std::lock_guard<std::mutex> lk(m_);
    auto& slot = nodes_[f.node];
    if (!slot)
        slot = std::make_unique<Node>(f.node, cfg_.thresholdsFor(f.node), cfg_.window,
                                      std::chrono::milliseconds(cfg_.offline_timeout_ms));
    return slot->onFrame(f, now);
}

std::vector<Alert> NodeManager::tick(Clock::time_point now) {
    std::lock_guard<std::mutex> lk(m_);
    std::vector<Alert> all;
    for (auto& [id, n] : nodes_) {
        auto a = n->onTick(now);
        all.insert(all.end(), a.begin(), a.end());
    }
    return all;
}

NodeInfo NodeManager::makeInfo(const Node& n, Clock::time_point now) const {
    long age = -1;
    if (n.frames() > 0)
        age = static_cast<long>(std::chrono::duration_cast<std::chrono::milliseconds>(now - n.lastSeen()).count());
    return NodeInfo{n.id(), n.state(), n.level(), n.raw(), n.average(), n.batteryMv(), n.frames(), n.lost(), age};
}

std::vector<NodeInfo> NodeManager::snapshot(Clock::time_point now) const {
    std::lock_guard<std::mutex> lk(m_);
    std::vector<NodeInfo> v;
    for (const auto& [id, n] : nodes_) v.push_back(makeInfo(*n, now));
    return v;
}

std::optional<NodeInfo> NodeManager::info(uint8_t id, Clock::time_point now) const {
    std::lock_guard<std::mutex> lk(m_);
    auto it = nodes_.find(id);
    if (it == nodes_.end()) return std::nullopt;
    return makeInfo(*it->second, now);
}

void NodeManager::applyConfig(const Config& cfg) {
    std::lock_guard<std::mutex> lk(m_);
    cfg_ = cfg;
    for (auto& [id, n] : nodes_)
        n->configure(cfg_.thresholdsFor(id), std::chrono::milliseconds(cfg_.offline_timeout_ms));
}

}  // namespace soilgw
