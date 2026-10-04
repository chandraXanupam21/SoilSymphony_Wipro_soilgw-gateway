#pragma once
#include <atomic>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

#include "config.hpp"
#include "fd.hpp"
#include "logger.hpp"
#include "node_manager.hpp"
#include "protocol.hpp"
#include "queue.hpp"

namespace soilgw {

// Gateway daemon core. Three threads:
//   reader    : epoll on the device -> FrameParser -> queue
//   processor : queue -> NodeManager (+ periodic timeout tick) -> logger/alerts
//   ipc       : UNIX socket server answering CLI commands
class Gateway {
public:
    explicit Gateway(Config cfg, std::string cfgPath = "");
    ~Gateway();
    Gateway(const Gateway&) = delete;
    Gateway& operator=(const Gateway&) = delete;

    void run();                          // blocks until requestStop()
    void requestStop() noexcept;         // async-signal-safe
    void requestReload() noexcept;       // async-signal-safe
    std::string handleCommand(const std::string& cmd);

private:
    void openDevice();
    void openSocket();
    void readerLoop();
    void processorLoop();
    void ipcLoop();
    void onAlerts(const std::vector<Alert>& alerts);
    std::string statusText();
    std::string alertsText();
    std::string statsText();

    Config cfg_;
    std::string cfg_path_;
    Logger logger_;
    NodeManager mgr_;
    BoundedQueue<Frame> queue_{1024};
    FrameParser parser_;  // reader thread only

    UniqueFd dev_fd_, stop_fd_, sock_fd_;
    bool dev_is_chr_ = false;
    std::atomic<bool> stop_{false}, reload_{false};
    std::atomic<uint64_t> frames_ok_{0}, frames_bad_{0}, resync_{0}, processed_{0};

    std::mutex alert_m_;
    std::deque<Alert> recent_;
    Clock::time_point start_;
};

}  // namespace soilgw
