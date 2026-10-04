#pragma once
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>

namespace soilgw {

// Bounded producer/consumer queue. push() never blocks: when full the item is
// dropped and counted, so a slow consumer can never stall the device reader.
template <typename T>
class BoundedQueue {
public:
    explicit BoundedQueue(size_t cap) : cap_(cap) {}

    bool push(T v) {
        {
            std::lock_guard<std::mutex> lk(m_);
            if (q_.size() >= cap_) {
                ++dropped_;
                return false;
            }
            q_.push_back(std::move(v));
        }
        cv_.notify_one();
        return true;
    }

    bool popFor(T& out, std::chrono::milliseconds timeout) {
        std::unique_lock<std::mutex> lk(m_);
        if (!cv_.wait_for(lk, timeout, [this] { return !q_.empty(); })) return false;
        out = std::move(q_.front());
        q_.pop_front();
        return true;
    }

    uint64_t dropped() const {
        std::lock_guard<std::mutex> lk(m_);
        return dropped_;
    }
    size_t size() const {
        std::lock_guard<std::mutex> lk(m_);
        return q_.size();
    }

private:
    size_t cap_;
    mutable std::mutex m_;
    std::condition_variable cv_;
    std::deque<T> q_;
    uint64_t dropped_ = 0;
};

}  // namespace soilgw
