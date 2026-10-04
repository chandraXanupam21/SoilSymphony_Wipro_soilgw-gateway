// soilgwd - soil-moisture gateway daemon
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <syslog.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>

#include "gateway.hpp"

namespace {
soilgw::Gateway* g_gw = nullptr;
void onSignal(int sig) {
    if (!g_gw) return;
    if (sig == SIGHUP) g_gw->requestReload();
    else g_gw->requestStop();
}

void usage(const char* p) {
    std::printf(
        "Usage: %s [-c config] [-d device] [-s socket] [-l logdir] [--daemon] [-h]\n"
        "  -c FILE    configuration file (key=value)\n"
        "  -d DEV     device path (default /dev/soilgw; a FIFO works for simulation)\n"
        "  -s PATH    UNIX control socket path\n"
        "  -l DIR     log directory\n"
        "  --daemon   detach and run in the background (use absolute paths)\n"
        "Signals: SIGINT/SIGTERM stop, SIGHUP reloads the config file.\n",
        p);
}

void daemonize() {
    if (pid_t p = fork(); p < 0) std::exit(1); else if (p > 0) _exit(0);
    if (setsid() < 0) std::exit(1);
    if (pid_t p = fork(); p < 0) std::exit(1); else if (p > 0) _exit(0);
    umask(027);
    if (chdir("/") != 0) std::exit(1);
    int fd = open("/dev/null", O_RDWR);
    if (fd >= 0) {
        dup2(fd, 0); dup2(fd, 1); dup2(fd, 2);
        if (fd > 2) close(fd);
    }
    soilgw::setSyslog(true);
}
}  // namespace

int main(int argc, char** argv) {
    std::string cfgPath, device, sock, logdir;
    bool daemon = false;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&](std::string& dst) {
            if (i + 1 >= argc) { usage(argv[0]); std::exit(2); }
            dst = argv[++i];
        };
        if (a == "-c") next(cfgPath);
        else if (a == "-d") next(device);
        else if (a == "-s") next(sock);
        else if (a == "-l") next(logdir);
        else if (a == "--daemon") daemon = true;
        else if (a == "-h" || a == "--help") { usage(argv[0]); return 0; }
        else { usage(argv[0]); return 2; }
    }

    try {
        soilgw::Config cfg = cfgPath.empty() ? soilgw::Config{} : soilgw::Config::fromFile(cfgPath);
        if (!device.empty()) cfg.device = device;
        if (!sock.empty()) cfg.socket_path = sock;
        if (!logdir.empty()) cfg.log_dir = logdir;
        namespace fs = std::filesystem;
        if (daemon) {
            cfg.log_dir = fs::absolute(cfg.log_dir).string();
            cfg.socket_path = fs::absolute(cfg.socket_path).string();
            cfg.device = fs::absolute(cfg.device).string();
            if (!cfgPath.empty()) cfgPath = fs::absolute(cfgPath).string();
            if (!cfg.pid_file.empty()) cfg.pid_file = fs::absolute(cfg.pid_file).string();
            daemonize();
        }
        if (!cfg.pid_file.empty()) std::ofstream(cfg.pid_file) << getpid() << "\n";

        soilgw::Gateway gw(cfg, cfgPath);
        g_gw = &gw;
        struct sigaction sa{};
        sa.sa_handler = onSignal;
        sigemptyset(&sa.sa_mask);
        sigaction(SIGINT, &sa, nullptr);
        sigaction(SIGTERM, &sa, nullptr);
        sigaction(SIGHUP, &sa, nullptr);
        signal(SIGPIPE, SIG_IGN);
        gw.run();
        g_gw = nullptr;
        if (!cfg.pid_file.empty()) unlink(cfg.pid_file.c_str());
    } catch (const std::exception& e) {
        soilgw::sysLog(LOG_ERR, "fatal: %s", e.what());
        return 1;
    }
    return 0;
}
