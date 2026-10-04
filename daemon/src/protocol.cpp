#include "protocol.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "soilgw_proto.h"

namespace soilgw {

uint8_t crc8(const uint8_t* data, size_t n) {
    return soilgw_crc8(data, static_cast<unsigned int>(n));
}

RawFrame encode(const Frame& f) {
    RawFrame r{};
    double m = std::clamp(f.moisture, 0.0, 655.35);
    auto mv = static_cast<uint16_t>(std::lround(m * 100.0));
    r[0] = SOILGW_SOF;
    r[1] = f.node;
    r[2] = f.seq;
    r[3] = static_cast<uint8_t>(mv >> 8);
    r[4] = static_cast<uint8_t>(mv & 0xFF);
    r[5] = static_cast<uint8_t>(f.battery_mv >> 8);
    r[6] = static_cast<uint8_t>(f.battery_mv & 0xFF);
    r[7] = crc8(&r[1], 6);
    return r;
}

std::vector<Frame> FrameParser::feed(const uint8_t* data, size_t len) {
    std::vector<Frame> out;
    for (size_t k = 0; k < len; ++k) {
        uint8_t b = data[k];
        if (len_ == 0 && b != SOILGW_SOF) {
            ++resync_;
            continue;
        }
        buf_[len_++] = b;
        if (len_ < kFrameLen) continue;

        if (crc8(&buf_[1], 6) == buf_[7] && buf_[1] >= 1 && buf_[1] <= kMaxNodes) {
            Frame f;
            f.node = buf_[1];
            f.seq = buf_[2];
            f.moisture = ((buf_[3] << 8) | buf_[4]) / 100.0;
            f.battery_mv = static_cast<uint16_t>((buf_[5] << 8) | buf_[6]);
            out.push_back(f);
            ++ok_;
            len_ = 0;
            continue;
        }
        // Bad frame: resynchronise on next SOF inside the buffer.
        ++bad_;
        size_t i = 1;
        while (i < kFrameLen && buf_[i] != SOILGW_SOF) ++i;
        if (i < kFrameLen) {
            std::memmove(buf_, buf_ + i, kFrameLen - i);
            len_ = kFrameLen - i;
        } else {
            len_ = 0;
        }
    }
    return out;
}

}  // namespace soilgw
