#pragma once
// Linux SocketCAN (CAN_RAW) için ince RAII sarmalayıcı.
//
// Gerçek bir arabirimle (can0) ya da sanal arabirimle (vcan0) çalışır.
// Bloklamayan alım poll() ile zaman aşımlıdır; böylece RTOS görevleri ya da
// test döngüleri süresiz beklemez.

#include <cstddef>
#include <cstdint>

#include "avb/can/frame.hpp"

namespace avb::can {

class SocketCan {
public:
    SocketCan() = default;
    ~SocketCan();
    SocketCan(const SocketCan&) = delete;
    SocketCan& operator=(const SocketCan&) = delete;
    SocketCan(SocketCan&& o) noexcept;
    SocketCan& operator=(SocketCan&& o) noexcept;

    static bool interfaceExists(const char* ifname);

    bool open(const char* ifname);  // LLR-CAN-030
    void close();
    bool isOpen() const { return fd_ >= 0; }

    bool send(const Frame& f);
    enum class Recv : std::uint8_t { Ok, Timeout, Error };
    Recv receive(Frame& out, int timeoutMs);

    // Yalnız verilen kimlikleri geçir (çekirdek düzeyinde filtre).  LLR-CAN-031
    bool setFilters(const std::uint32_t* ids, std::size_t count, bool extended);

    int lastErrno() const { return errno_; }

private:
    int fd_ = -1;
    int errno_ = 0;
};

}  // namespace avb::can
