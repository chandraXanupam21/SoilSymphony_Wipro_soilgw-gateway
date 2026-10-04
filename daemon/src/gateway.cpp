#include "gateway.hpp"

#include <fcntl.h>
#include <poll.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <syslog.h>

#include <cerrno>
#include <cstring>
#include <filesystem>
#include <sstream>
#include <stdexcept>
#include <thread>

#include "soilgw_proto.h"

namespace soilgw {

Gateway::Gateway(Config cfg, std::string cfgPath)
    : cfg_(std::move(cfg)),
      cfg_path_(std::move(cfgPath)),
      logger_(cfg_.log_dir),
      mgr_(cfg_),
      start_(Clock::now()) {
    stop_fd_.reset(::eventfd(0, EFD_CLOEXEC));
    if (!stop_fd_.valid()) throw std::runtime_error(std::string("eventfd: ") + std::strerror(errno));
}

Gateway::~Gateway() {
    if (!cfg_.socket_path.empty() && sock_fd_.valid()) ::unlink(cfg_.socket_path.c_str());
}

void Gateway::requestStop() noexcept {
    stop_ = true;
    uint64_t one = 1;
    ssize_t r = ::write(stop_fd_.get(), &one, sizeof one);
    (void)r;
}

void Gateway::requestReload() noexcept { reload_ = true; }

void Gateway::openDevice() {
    struct stat st{};
    if (::stat(cfg_.device.c_str(), &st) != 0)
        throw std::runtime_error("cannot stat device " + cfg_.device + ": " + std::strerror(errno));
    dev_is_chr_ = S_ISCHR(st.st_mode);
    // A FIFO is opened O_RDWR so the daemon itself is a writer and never sees EOF
    // when the simulator exits (virtual-device mode without the kernel module).
    int flags = (S_ISFIFO(st.st_mode) ? O_RDWR : O_RDONLY) | O_NONBLOCK | O_CLOEXEC;
    dev_fd_.reset(::open(cfg_.device.c_str(), flags));
    if (!dev_fd_.valid())
        throw std::runtime_error("cannot open device " + cfg_.device + ": " + std::strerror(errno));
    sysLog(LOG_INFO, "opened %s (%s)", cfg_.device.c_str(), dev_is_chr_ ? "char device" : "fifo/file");
}

void Gateway::openSocket() {
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    if (cfg_.socket_path.size() >= sizeof addr.sun_path) throw std::runtime_error("socket path too long");
    std::strncpy(addr.sun_path, cfg_.socket_path.c_str(), sizeof addr.sun_path - 1);
    ::unlink(cfg_.socket_path.c_str());
    sock_fd_.reset(::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0));
    if (!sock_fd_.valid()) throw std::runtime_error(std::string("socket: ") + std::strerror(errno));
    if (::bind(sock_fd_.get(), reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0)
        throw std::runtime_error("bind " + cfg_.socket_path + ": " + std::strerror(errno));
    ::chmod(cfg_.socket_path.c_str(), 0660);
    if (::listen(sock_fd_.get(), 8) != 0) throw std::runtime_error(std::string("listen: ") + std::strerror(errno));
}

void Gateway::run() {
    openDevice();
    openSocket();
    sysLog(LOG_INFO, "gateway started: timeout=%dms window=%zu low=%.0f high=%.0f", cfg_.offline_timeout_ms,
           cfg_.window, cfg_.defaults.low, cfg_.defaults.high);
    std::thread reader(&Gateway::readerLoop, this);
    std::thread processor(&Gateway::processorLoop, this);
    std::thread ipc(&Gateway::ipcLoop, this);
    reader.join();
    processor.join();
    ipc.join();
    sysLog(LOG_INFO, "gateway stopped");
}

void Gateway::readerLoop() {
    UniqueFd ep(::epoll_create1(EPOLL_CLOEXEC));
    epoll_event ev{};
    ev.events = EPOLLIN;
    ev.data.fd = dev_fd_.get();
    ::epoll_ctl(ep.get(), EPOLL_CTL_ADD, dev_fd_.get(), &ev);
    ev.data.fd = stop_fd_.get();
    ::epoll_ctl(ep.get(), EPOLL_CTL_ADD, stop_fd_.get(), &ev);

    uint8_t buf[512];
    epoll_event events[4];
    while (!stop_) {
        int n = ::epoll_wait(ep.get(), events, 4, 500);
        if (n < 0) {
            if (errno == EINTR) continue;
            sysLog(LOG_ERR, "epoll_wait: %s", std::strerror(errno));
            break;
        }
        for (int i = 0; i < n; ++i) {
            if (events[i].data.fd == stop_fd_.get()) return;
            for (int rounds = 0; rounds < 16; ++rounds) {  // bounded drain keeps us fair
                ssize_t r = ::read(dev_fd_.get(), buf, sizeof buf);
                if (r > 0) {
                    for (const Frame& f : parser_.feed(buf, static_cast<size_t>(r))) queue_.push(f);
                    frames_ok_ = parser_.framesOk();
                    frames_bad_ = parser_.framesBad();
                    resync_ = parser_.resyncBytes();
                    continue;
                }
                if (r < 0 && (errno == EAGAIN || errno == EINTR)) break;
                if (r == 0) {  // EOF (regular file) - nothing more to read
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    break;
                }
                sysLog(LOG_ERR, "read(%s): %s", cfg_.device.c_str(), std::strerror(errno));
                requestStop();
                return;
            }
        }
    }
}

void Gateway::onAlerts(const std::vector<Alert>& alerts) {
    for (const Alert& a : alerts) {
        logger_.alert(a);
        sysLog(a.type == AlertType::LowMoisture || a.type == AlertType::NodeFaulty || a.type == AlertType::NodeOffline
                   ? LOG_WARNING
                   : LOG_INFO,
               "ALERT [%s] %s", toString(a.type), a.message.c_str());
        std::lock_guard<std::mutex> lk(alert_m_);
        recent_.push_back(a);
        if (recent_.size() > 50) recent_.pop_front();
    }
}

void Gateway::processorLoop() {
    auto lastTick = Clock::now();
    while (!stop_) {
        Frame f;
        if (queue_.popFor(f, std::chrono::milliseconds(100))) {
            const auto now = Clock::now();
            onAlerts(mgr_.handleFrame(f, now));
            if (auto info = mgr_.info(f.node, now)) logger_.reading(f, info->average, info->state);
            ++processed_;
        }
        const auto now = Clock::now();
        if (now - lastTick >= std::chrono::milliseconds(100)) {
            lastTick = now;
            onAlerts(mgr_.tick(now));
        }
        if (reload_.exchange(false)) {
            if (cfg_path_.empty()) {
                sysLog(LOG_WARNING, "SIGHUP ignored: no config file given");
            } else {
                try {
                    Config c = Config::fromFile(cfg_path_);
                    mgr_.applyConfig(c);
                    sysLog(LOG_INFO, "configuration reloaded from %s", cfg_path_.c_str());
                } catch (const std::exception& e) {
                    sysLog(LOG_ERR, "reload failed (keeping old config): %s", e.what());
                }
            }
        }
    }
}

void Gateway::ipcLoop() {
    pollfd fds[2] = {{sock_fd_.get(), POLLIN, 0}, {stop_fd_.get(), POLLIN, 0}};
    while (!stop_) {
        int n = ::poll(fds, 2, 500);
        if (n < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (fds[1].revents & POLLIN) return;
        if (!(fds[0].revents & POLLIN)) continue;

        UniqueFd cl(::accept4(sock_fd_.get(), nullptr, nullptr, SOCK_CLOEXEC));
        if (!cl.valid()) continue;
        timeval tv{1, 0};
        ::setsockopt(cl.get(), SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
        char line[64];
        ssize_t r = ::read(cl.get(), line, sizeof line - 1);
        if (r <= 0) continue;
        line[r] = '\0';
        std::string cmd(line);
        while (!cmd.empty() && (cmd.back() == '\n' || cmd.back() == '\r' || cmd.back() == ' ')) cmd.pop_back();
        const std::string reply = handleCommand(cmd);
        size_t off = 0;
        while (off < reply.size()) {
            ssize_t w = ::send(cl.get(), reply.data() + off, reply.size() - off, MSG_NOSIGNAL);
            if (w <= 0) break;
            off += static_cast<size_t>(w);
        }
    }
}

std::string Gateway::handleCommand(const std::string& cmd) {
    if (cmd == "status") return statusText();
    if (cmd == "alerts") return alertsText();
    if (cmd == "stats") return statsText();
    return "commands: status | alerts | stats\n";
}

std::string Gateway::statusText() {
    std::ostringstream os;
    char buf[200];
    std::snprintf(buf, sizeof buf, "%-5s %-8s %-9s %-8s %-8s %-7s %-6s %-7s %s\n", "NODE", "STATE", "MOIST(%)",
                  "AVG(%)", "BATT(mV)", "FRAMES", "LOST", "LEVEL", "LAST SEEN");
    os << buf;
    for (const NodeInfo& n : mgr_.snapshot()) {
        char seen[24];
        if (n.last_seen_ms < 0) std::snprintf(seen, sizeof seen, "never");
        else std::snprintf(seen, sizeof seen, "%.1fs ago", n.last_seen_ms / 1000.0);
        std::snprintf(buf, sizeof buf, "%-5u %-8s %-9.2f %-8.2f %-8u %-7llu %-6llu %-7s %s\n", n.id, toString(n.state),
                      n.raw, n.average, n.battery_mv, static_cast<unsigned long long>(n.frames),
                      static_cast<unsigned long long>(n.lost), toString(n.level), seen);
        os << buf;
    }
    return os.str();
}

std::string Gateway::alertsText() {
    std::lock_guard<std::mutex> lk(alert_m_);
    if (recent_.empty()) return "no alerts\n";
    std::ostringstream os;
    for (const Alert& a : recent_)
        os << formatTime(a.ts) << " [" << toString(a.type) << "] " << a.message << "\n";
    return os.str();
}

std::string Gateway::statsText() {
    std::ostringstream os;
    const auto up = std::chrono::duration_cast<std::chrono::seconds>(Clock::now() - start_).count();
    os << "uptime_s:        " << up << "\n"
       << "device:          " << cfg_.device << "\n"
       << "frames_ok:       " << frames_ok_ << "\n"
       << "frames_bad:      " << frames_bad_ << "\n"
       << "resync_bytes:    " << resync_ << "\n"
       << "processed:       " << processed_ << "\n"
       << "queue_depth:     " << queue_.size() << "\n"
       << "queue_dropped:   " << queue_.dropped() << "\n"
       << "nodes:           " << mgr_.snapshot().size() << "\n";
    if (dev_is_chr_) {
        soilgw_stats ks{};
        if (::ioctl(dev_fd_.get(), SOILGW_IOC_GET_STATS, &ks) == 0)
            os << "kernel.frames_ok:       " << ks.frames_ok << "\n"
               << "kernel.frames_bad:      " << ks.frames_bad << "\n"
               << "kernel.frames_filtered: " << ks.frames_filtered << "\n"
               << "kernel.overruns:        " << ks.overruns << "\n"
               << "kernel.queued:          " << ks.queued << "\n";
    }
    return os.str();
}

}  // namespace soilgw
