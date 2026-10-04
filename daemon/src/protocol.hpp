#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace soilgw {

constexpr size_t kFrameLen = 8;
constexpr uint8_t kMaxNodes = 32;

struct Frame {
    uint8_t node = 0;
    uint8_t seq = 0;
    double moisture = 0.0;      // percent
    uint16_t battery_mv = 0;
};

using RawFrame = std::array<uint8_t, kFrameLen>;

uint8_t crc8(const uint8_t* data, size_t n);
RawFrame encode(const Frame& f);

// Incremental byte-stream -> frame parser (same algorithm as the kernel driver).
class FrameParser {
public:
    std::vector<Frame> feed(const uint8_t* data, size_t len);
    uint64_t framesOk() const { return ok_; }
    uint64_t framesBad() const { return bad_; }
    uint64_t resyncBytes() const { return resync_; }

private:
    uint8_t buf_[kFrameLen]{};
    size_t len_ = 0;
    uint64_t ok_ = 0, bad_ = 0, resync_ = 0;
};

}  // namespace soilgw
