// soilgw-cli - query the running gateway daemon over its UNIX control socket
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <string>

static int query(const std::string& path, const std::string& cmd, std::string& out) {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    sockaddr_un a{};
    a.sun_family = AF_UNIX;
    std::strncpy(a.sun_path, path.c_str(), sizeof a.sun_path - 1);
    if (connect(fd, reinterpret_cast<sockaddr*>(&a), sizeof a) != 0) { close(fd); return -1; }
    std::string msg = cmd + "\n";
    if (write(fd, msg.data(), msg.size()) < 0) { close(fd); return -1; }
    char buf[2048];
    ssize_t n;
    out.clear();
    while ((n = read(fd, buf, sizeof buf)) > 0) out.append(buf, static_cast<size_t>(n));
    close(fd);
    return 0;
}

int main(int argc, char** argv) {
    std::string sock = "/tmp/soilgw.sock", cmd;
    bool watch = false;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "-s" && i + 1 < argc) sock = argv[++i];
        else if (a == "-w") watch = true;
        else if (a == "-h" || a == "--help") cmd = "";
        else cmd = a;
    }
    if (cmd.empty()) {
        std::printf("Usage: %s [-s socket] [-w] status|alerts|stats\n  -w  refresh every second\n", argv[0]);
        return 2;
    }
    do {
        std::string out;
        if (query(sock, cmd, out) != 0) {
            std::fprintf(stderr, "cannot connect to %s: %s (is soilgwd running?)\n", sock.c_str(), std::strerror(errno));
            return 1;
        }
        if (watch) std::printf("\033[H\033[2J");
        std::fputs(out.c_str(), stdout);
        std::fflush(stdout);
        if (watch) sleep(1);
    } while (watch);
    return 0;
}
