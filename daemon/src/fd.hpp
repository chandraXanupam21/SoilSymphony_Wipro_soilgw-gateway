#pragma once
#include <unistd.h>

#include <utility>

namespace soilgw {

// RAII wrapper for a POSIX file descriptor.
class UniqueFd {
public:
    explicit UniqueFd(int fd = -1) noexcept : fd_(fd) {}
    ~UniqueFd() { reset(); }
    UniqueFd(UniqueFd&& o) noexcept : fd_(o.release()) {}
    UniqueFd& operator=(UniqueFd&& o) noexcept {
        if (this != &o) reset(o.release());
        return *this;
    }
    UniqueFd(const UniqueFd&) = delete;
    UniqueFd& operator=(const UniqueFd&) = delete;

    int get() const noexcept { return fd_; }
    bool valid() const noexcept { return fd_ >= 0; }
    int release() noexcept { return std::exchange(fd_, -1); }
    void reset(int fd = -1) noexcept {
        if (fd_ >= 0) ::close(fd_);
        fd_ = fd;
    }

private:
    int fd_;
};

}  // namespace soilgw
