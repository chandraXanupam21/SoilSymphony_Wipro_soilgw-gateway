// soilgw-sim - multi-node soil sensor simulator with fault injection.
// Writes raw 8-byte frames to /dev/soilgw (driver write path) or to a FIFO.
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "protocol.hpp"

struct Window { int node, from_s, to_s; };

static bool parseWindow(const char* s, Window& w) {
    return std::sscanf(s, "%d:%d:%d", &w.node, &w.from_s, &w.to_s) == 3;
}
static bool inWindow(const std::vector<Window>& v, int node, double t) {
    for (const auto& w : v) if (w.node == node && t >= w.from_s && t < w.to_s) return true;
    return false;
}

int main(int argc, char** argv) {
    std::string out = "/dev/soilgw";
    int nodes = 4, interval = 500, duration = 0;
    double decay = 0.4, corrupt = 0.0, garbage = 0.0, start = -1;
    unsigned seed = std::random_device{}();
    bool verbose = false;
    std::vector<Window> offline, faulty;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto val = [&]() -> const char* { return i + 1 < argc ? argv[++i] : (std::exit(2), nullptr); };
        if (a == "-o") out = val();
        else if (a == "-n") nodes = std::atoi(val());
        else if (a == "-i") interval = std::atoi(val());
        else if (a == "-t") duration = std::atoi(val());
        else if (a == "--decay") decay = std::atof(val());
        else if (a == "--start") start = std::atof(val());
        else if (a == "--corrupt") corrupt = std::atof(val());
        else if (a == "--garbage") garbage = std::atof(val());
        else if (a == "--seed") seed = static_cast<unsigned>(std::atoi(val()));
        else if (a == "--offline") { Window w; if (!parseWindow(val(), w)) return 2; offline.push_back(w); }
        else if (a == "--faulty") { Window w; if (!parseWindow(val(), w)) return 2; faulty.push_back(w); }
        else if (a == "-v") verbose = true;
        else {
            std::printf(
                "Usage: %s [-o device|fifo] [-n nodes] [-i interval_ms] [-t seconds] [-v]\n"
                "  --decay D          mean moisture drop per sample in %% (default 0.4)\n"
                "  --start M          initial moisture %% for all nodes (default random 35..70)\n"
                "  --corrupt P        probability of corrupting a frame (CRC error)\n"
                "  --garbage P        probability of injecting noise bytes before a frame\n"
                "  --offline N:S:E    node N is silent from second S to E\n"
                "  --faulty  N:S:E    node N reports invalid values (655.35%%) from S to E\n"
                "  --seed K           RNG seed\n",
                argv[0]);
            return 2;
        }
    }
    if (nodes < 1 || nodes > soilgw::kMaxNodes) { std::fprintf(stderr, "nodes must be 1..%d\n", soilgw::kMaxNodes); return 2; }
    signal(SIGPIPE, SIG_IGN);
    int fd = open(out.c_str(), O_WRONLY);
    if (fd < 0) { std::perror(("open " + out).c_str()); return 1; }

    std::mt19937 rng(seed);
    std::uniform_real_distribution<double> u01(0.0, 1.0), drop(0.3, 1.7), init(35.0, 70.0);
    std::normal_distribution<double> noise(0.0, 0.15);
    std::vector<double> moist(nodes + 1);
    std::vector<uint8_t> seq(nodes + 1, 0);
    for (int n = 1; n <= nodes; ++n) moist[n] = start >= 0 ? start : init(rng);

    const auto t0 = std::chrono::steady_clock::now();
    unsigned long sent = 0;
    for (long tick = 0;; ++tick) {
        double t = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        if (duration > 0 && t >= duration) break;
        for (int n = 1; n <= nodes; ++n) {
            moist[n] = std::max(0.0, moist[n] - decay * drop(rng) + noise(rng));
            if (moist[n] < 10.0) moist[n] = 60.0;  // irrigation / rain event
            if (inWindow(offline, n, t)) { std::this_thread::sleep_for(std::chrono::milliseconds(interval / nodes)); continue; }

            soilgw::Frame f;
            f.node = static_cast<uint8_t>(n);
            f.seq = seq[n]++;
            f.moisture = inWindow(faulty, n, t) ? 655.35 : moist[n];
            f.battery_mv = static_cast<uint16_t>(4100 - tick / 10 - n * 7);
            soilgw::RawFrame raw = soilgw::encode(f);

            std::vector<uint8_t> bytes;
            if (u01(rng) < garbage) {
                int k = 1 + static_cast<int>(u01(rng) * 4);
                for (int j = 0; j < k; ++j) bytes.push_back(static_cast<uint8_t>(u01(rng) * 0xA9));  // never SOF
            }
            if (u01(rng) < corrupt) raw[1 + static_cast<size_t>(u01(rng) * 6)] ^= static_cast<uint8_t>(1 + u01(rng) * 254);
            bytes.insert(bytes.end(), raw.begin(), raw.end());
            if (write(fd, bytes.data(), bytes.size()) < 0) { std::perror("write"); close(fd); return 1; }
            ++sent;
            if (verbose) std::printf("t=%6.1f node=%d seq=%u moisture=%.2f\n", t, n, f.seq, f.moisture);
            std::this_thread::sleep_for(std::chrono::milliseconds(interval / nodes));
        }
    }
    std::printf("simulator done: %lu frames sent\n", sent);
    close(fd);
    return 0;
}
