#include "avb/can/socketcan.hpp"

#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>

namespace avb::can {

SocketCan::~SocketCan() { close(); }

SocketCan::SocketCan(SocketCan&& o) noexcept : fd_(o.fd_), errno_(o.errno_) { o.fd_ = -1; }

SocketCan& SocketCan::operator=(SocketCan&& o) noexcept {
    if (this != &o) {
        close();
        fd_ = o.fd_;
        errno_ = o.errno_;
        o.fd_ = -1;
    }
    return *this;
}

bool SocketCan::interfaceExists(const char* ifname) { return if_nametoindex(ifname) != 0; }

bool SocketCan::open(const char* ifname) {
    close();
    const unsigned int index = if_nametoindex(ifname);
    if (index == 0) {
        errno_ = errno;
        return false;
    }
    fd_ = ::socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (fd_ < 0) {
        errno_ = errno;
        return false;
    }
    sockaddr_can addr{};
    addr.can_family = AF_CAN;
    addr.can_ifindex = static_cast<int>(index);
    // sockaddr_can -> sockaddr dönüşümü soket API'sinin gereği
    if (::bind(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0) {  // NOLINT
        errno_ = errno;
        close();
        return false;
    }
    return true;
}

void SocketCan::close() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

bool SocketCan::send(const Frame& f) {
    if (fd_ < 0 || !f.valid()) {
        return false;
    }
    can_frame raw{};
    raw.can_id = f.extended ? (f.id | CAN_EFF_FLAG) : f.id;
    raw.len = f.dlc;
    std::copy_n(f.data.begin(), f.dlc, raw.data);
    const ssize_t n = ::write(fd_, &raw, sizeof raw);
    if (n != static_cast<ssize_t>(sizeof raw)) {
        errno_ = errno;
        return false;
    }
    return true;
}

SocketCan::Recv SocketCan::receive(Frame& out, int timeoutMs) {
    if (fd_ < 0) {
        return Recv::Error;
    }
    pollfd p{fd_, POLLIN, 0};
    const int r = ::poll(&p, 1, timeoutMs);
    if (r == 0) {
        return Recv::Timeout;
    }
    if (r < 0) {
        errno_ = errno;
        return Recv::Error;
    }
    can_frame raw{};
    const ssize_t n = ::read(fd_, &raw, sizeof raw);
    if (n != static_cast<ssize_t>(sizeof raw)) {
        errno_ = errno;
        return Recv::Error;
    }
    out.extended = (raw.can_id & CAN_EFF_FLAG) != 0;
    out.id = raw.can_id & (out.extended ? CAN_EFF_MASK : CAN_SFF_MASK);
    out.dlc = std::min<std::uint8_t>(raw.len, 8);
    std::copy_n(raw.data, out.dlc, out.data.begin());
    return Recv::Ok;
}

bool SocketCan::setFilters(const std::uint32_t* ids, std::size_t count, bool extended) {
    if (fd_ < 0 || count > 32) {
        return false;
    }
    std::array<can_filter, 32> filters{};
    for (std::size_t i = 0; i < count; ++i) {
        filters[i].can_id = extended ? (ids[i] | CAN_EFF_FLAG) : ids[i];
        filters[i].can_mask = (extended ? CAN_EFF_MASK : CAN_SFF_MASK) | CAN_EFF_FLAG | CAN_RTR_FLAG;
    }
    const auto bytes = static_cast<socklen_t>(count * sizeof(can_filter));
    if (::setsockopt(fd_, SOL_CAN_RAW, CAN_RAW_FILTER, filters.data(), bytes) != 0) {
        errno_ = errno;
        return false;
    }
    return true;
}

}  // namespace avb::can
