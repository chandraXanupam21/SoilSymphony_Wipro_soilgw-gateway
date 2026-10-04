// Minimal dependency-free unit/component test suite for the gateway library.
#include <cstdio>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

#include "config.hpp"
#include "node.hpp"
#include "node_manager.hpp"
#include "protocol.hpp"
#include "queue.hpp"

using namespace soilgw;
using std::chrono::milliseconds;

static int g_total = 0, g_fail = 0;
static std::vector<std::pair<std::string, std::function<void()>>>& registry() {
    static std::vector<std::pair<std::string, std::function<void()>>> r;
    return r;
}
struct Reg { Reg(const char* n, std::function<void()> f) { registry().emplace_back(n, std::move(f)); } };
#define TEST(name) static void name(); static Reg reg_##name(#name, name); static void name()
#define CHECK(c) do { ++g_total; if (!(c)) { ++g_fail; std::printf("    FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)
#define NEAR(a, b) CHECK(std::abs((a) - (b)) < 1e-6)

static Frame mk(uint8_t node, uint8_t seq, double m, uint16_t batt = 3900) {
    Frame f; f.node = node; f.seq = seq; f.moisture = m; f.battery_mv = batt; return f;
}
static std::vector<Frame> feedAll(FrameParser& p, const RawFrame& r) { return p.feed(r.data(), r.size()); }
static Config smallCfg() { Config c; c.window = 1; c.offline_timeout_ms = 1000; return c; }

// ---------------- protocol ----------------
TEST(crc_detects_single_byte_change) {
    RawFrame r = encode(mk(3, 1, 42.5));
    uint8_t good = crc8(&r[1], 6);
    CHECK(good == r[7]);
    for (size_t i = 1; i < 7; ++i) { RawFrame b = r; b[i] ^= 0x10; CHECK(crc8(&b[1], 6) != b[7]); }
}
TEST(encode_decode_roundtrip) {
    FrameParser p;
    auto v = feedAll(p, encode(mk(7, 200, 37.25, 4012)));
    CHECK(v.size() == 1);
    CHECK(v[0].node == 7); CHECK(v[0].seq == 200); NEAR(v[0].moisture, 37.25); CHECK(v[0].battery_mv == 4012);
}
TEST(parser_rejects_corrupt_frame) {
    FrameParser p; RawFrame r = encode(mk(1, 0, 50)); r[4] ^= 0x01;
    CHECK(feedAll(p, r).empty()); CHECK(p.framesBad() == 1);
}
TEST(parser_skips_garbage_before_frame) {
    FrameParser p; uint8_t junk[] = {0x01, 0x02, 0x55};
    CHECK(p.feed(junk, 3).empty()); CHECK(p.resyncBytes() == 3);
    CHECK(feedAll(p, encode(mk(2, 0, 10))).size() == 1);
}
TEST(parser_handles_split_frames) {
    FrameParser p; RawFrame r = encode(mk(4, 9, 33));
    CHECK(p.feed(r.data(), 3).empty());
    auto v = p.feed(r.data() + 3, 5);
    CHECK(v.size() == 1); CHECK(v[0].node == 4);
}
TEST(parser_back_to_back_frames) {
    FrameParser p; std::vector<uint8_t> s;
    for (int n = 1; n <= 5; ++n) { auto r = encode(mk(n, 0, 10.0 * n)); s.insert(s.end(), r.begin(), r.end()); }
    auto v = p.feed(s.data(), s.size());
    CHECK(v.size() == 5); CHECK(p.framesBad() == 0);
}
TEST(parser_recovers_after_truncated_frame) {
    // A frame cut short by 3 bytes, immediately followed by a good frame.
    FrameParser p; RawFrame a = encode(mk(1, 0, 20)), b = encode(mk(2, 0, 60));
    std::vector<uint8_t> s(a.begin(), a.begin() + 5);
    s.insert(s.end(), b.begin(), b.end());
    auto v = p.feed(s.data(), s.size());
    bool found = false; for (auto& f : v) if (f.node == 2) found = true;
    CHECK(found);
}
TEST(parser_rejects_invalid_node_id) {
    FrameParser p; CHECK(feedAll(p, encode(mk(0, 0, 10))).empty()); CHECK(feedAll(p, encode(mk(33, 0, 10))).empty());
}

// ---------------- filter ----------------
TEST(moving_average_window) {
    MovingAverage<double> m(3);
    NEAR(m.add(3), 3.0); NEAR(m.add(6), 4.5); NEAR(m.add(9), 6.0); NEAR(m.add(12), 9.0);
    CHECK(m.size() == 3);
}

// ---------------- node state machine ----------------
TEST(node_unknown_to_online) {
    Node n(1, {}, 1, milliseconds(1000)); auto t = Clock::now();
    CHECK(n.state() == NodeState::Unknown);
    CHECK(n.onFrame(mk(1, 0, 50), t).empty()); CHECK(n.state() == NodeState::Online);
}
TEST(node_goes_offline_and_recovers) {
    Node n(1, {}, 1, milliseconds(1000)); auto t = Clock::now();
    n.onFrame(mk(1, 0, 50), t);
    CHECK(n.onTick(t + milliseconds(500)).empty()); CHECK(n.state() == NodeState::Online);
    auto a = n.onTick(t + milliseconds(1500));
    CHECK(a.size() == 1 && a[0].type == AlertType::NodeOffline); CHECK(n.state() == NodeState::Offline);
    CHECK(n.onTick(t + milliseconds(3000)).empty());  // no repeated alert
    auto r = n.onFrame(mk(1, 1, 50), t + milliseconds(3100));
    CHECK(r.size() == 1 && r[0].type == AlertType::NodeRecovered); CHECK(n.state() == NodeState::Online);
}
TEST(node_faulty_after_three_invalid_and_recovers) {
    Node n(1, {}, 1, milliseconds(1000)); auto t = Clock::now();
    n.onFrame(mk(1, 0, 50), t);
    CHECK(n.onFrame(mk(1, 1, 655.35), t).empty()); CHECK(n.onFrame(mk(1, 2, 655.35), t).empty());
    auto a = n.onFrame(mk(1, 3, 655.35), t);
    CHECK(a.size() == 1 && a[0].type == AlertType::NodeFaulty); CHECK(n.state() == NodeState::Faulty);
    NEAR(n.average(), 50.0);  // invalid data never enters the filter
    auto r = n.onFrame(mk(1, 4, 48), t);
    CHECK(r.size() == 1 && r[0].type == AlertType::NodeRecovered); CHECK(n.state() == NodeState::Online);
}
TEST(node_invalid_streak_resets_on_good_reading) {
    Node n(1, {}, 1, milliseconds(1000)); auto t = Clock::now();
    n.onFrame(mk(1, 0, 50), t); n.onFrame(mk(1, 1, 999), t); n.onFrame(mk(1, 2, 999), t);
    n.onFrame(mk(1, 3, 50), t); n.onFrame(mk(1, 4, 999), t); n.onFrame(mk(1, 5, 999), t);
    CHECK(n.state() == NodeState::Online);
}
TEST(node_counts_lost_frames_with_wraparound) {
    Node n(1, {}, 1, milliseconds(1000)); auto t = Clock::now();
    n.onFrame(mk(1, 254, 50), t); n.onFrame(mk(1, 255, 50), t); n.onFrame(mk(1, 2, 50), t);  // 0,1 missing
    CHECK(n.lost() == 2);
}

// ---------------- alerts ----------------
TEST(low_alert_fires_once_with_hysteresis) {
    Node n(1, {25, 80, 5}, 1, milliseconds(1000)); auto t = Clock::now(); int low = 0, normal = 0;
    auto count = [&](const std::vector<Alert>& v) {
        for (auto& a : v) { if (a.type == AlertType::LowMoisture) ++low; if (a.type == AlertType::MoistureNormal) ++normal; }
    };
    count(n.onFrame(mk(1, 0, 40), t));
    count(n.onFrame(mk(1, 1, 24), t)); count(n.onFrame(mk(1, 2, 23), t)); count(n.onFrame(mk(1, 3, 26), t));
    CHECK(low == 1); CHECK(normal == 0); CHECK(n.level() == MoistureLevel::Low);  // 26 < 25+5: still low
    count(n.onFrame(mk(1, 4, 31), t));
    CHECK(normal == 1); CHECK(n.level() == MoistureLevel::Normal);
}
TEST(high_alert_and_clear) {
    Node n(1, {25, 80, 5}, 1, milliseconds(1000)); auto t = Clock::now();
    auto a = n.onFrame(mk(1, 0, 85), t);
    CHECK(a.size() == 1 && a[0].type == AlertType::HighMoisture);
    CHECK(n.onFrame(mk(1, 1, 78), t).empty());
    auto b = n.onFrame(mk(1, 2, 70), t);
    CHECK(b.size() == 1 && b[0].type == AlertType::MoistureNormal);
}
TEST(alert_message_names_zone) {
    Node n(3, {25, 80, 5}, 1, milliseconds(1000));
    auto a = n.onFrame(mk(3, 0, 20), Clock::now());
    CHECK(a.size() == 1 && a[0].message.find("Zone 3") != std::string::npos);
}
TEST(averaging_smooths_single_dip) {
    Node n(1, {25, 80, 5}, 5, milliseconds(1000)); auto t = Clock::now();
    for (int i = 0; i < 4; ++i) n.onFrame(mk(1, i, 40), t);
    CHECK(n.onFrame(mk(1, 4, 5), t).empty());  // avg = 33 -> no alert
}

// ---------------- config ----------------
TEST(config_parse_and_overrides) {
    std::istringstream in("# comment\ndevice=/tmp/x\nlow=30\nhigh=70\nwindow=3\nnode.2.low=15 # dry crop\n");
    Config c = Config::fromStream(in);
    CHECK(c.device == "/tmp/x"); CHECK(c.window == 3);
    NEAR(c.thresholdsFor(1).low, 30.0); NEAR(c.thresholdsFor(2).low, 15.0); NEAR(c.thresholdsFor(2).high, 70.0);
}
TEST(config_rejects_bad_input) {
    auto bad = [](const char* s) { std::istringstream in(s); try { Config::fromStream(in); return false; } catch (const std::runtime_error&) { return true; } };
    CHECK(bad("nonsense")); CHECK(bad("low=abc")); CHECK(bad("bogus=1")); CHECK(bad("low=90\nhigh=10")); CHECK(bad("window=0"));
    CHECK(bad("node.1.color=3"));
}

// ---------------- node manager / queue ----------------
TEST(manager_tracks_multiple_nodes_independently) {
    NodeManager m(smallCfg()); auto t = Clock::now();
    for (int id = 1; id <= 8; ++id) m.handleFrame(mk(id, 0, 10.0 * id), t);
    auto snap = m.snapshot(t);
    CHECK(snap.size() == 8); CHECK(snap[0].level == MoistureLevel::Low); CHECK(snap[7].level == MoistureLevel::Normal);
}
TEST(manager_timeout_only_affects_silent_node) {
    NodeManager m(smallCfg()); auto t = Clock::now();
    m.handleFrame(mk(1, 0, 50), t); m.handleFrame(mk(2, 0, 50), t);
    m.handleFrame(mk(1, 1, 50), t + milliseconds(900));
    auto a = m.tick(t + milliseconds(1500));
    CHECK(a.size() == 1 && a[0].node == 2);
}
TEST(manager_applies_per_node_threshold_override) {
    Config c = smallCfg(); c.overrides[2].low = 10;
    NodeManager m(c); auto t = Clock::now();
    CHECK(!m.handleFrame(mk(1, 0, 20), t).empty());  // default low=25 -> alert
    CHECK(m.handleFrame(mk(2, 0, 20), t).empty());   // node 2 low=10 -> ok
}
TEST(manager_reconfigure_changes_thresholds) {
    NodeManager m(smallCfg()); auto t = Clock::now();
    m.handleFrame(mk(1, 0, 40), t);
    Config c = smallCfg(); c.defaults.low = 45; m.applyConfig(c);
    CHECK(!m.handleFrame(mk(1, 1, 40), t).empty());
}
TEST(queue_drops_when_full_and_never_blocks) {
    BoundedQueue<int> q(2); CHECK(q.push(1)); CHECK(q.push(2)); CHECK(!q.push(3)); CHECK(q.dropped() == 1);
    int v = 0; CHECK(q.popFor(v, milliseconds(10))); CHECK(v == 1);
    CHECK(q.popFor(v, milliseconds(10))); CHECK(!q.popFor(v, milliseconds(10)));
}

int main() {
    int failedTests = 0;
    for (auto& [name, fn] : registry()) {
        int before = g_fail;
        fn();
        bool ok = g_fail == before;
        std::printf("[%s] %s\n", ok ? " OK " : "FAIL", name.c_str());
        if (!ok) ++failedTests;
    }
    std::printf("\n%zu tests, %d failed; %d checks, %d failed\n", registry().size(), failedTests, g_total, g_fail);
    return g_fail ? 1 : 0;
}
