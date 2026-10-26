#include "avb/can/signal.hpp"

#include <cmath>

namespace avb::can {

namespace {

void setBit(std::array<std::uint8_t, 8>& d, int pos, bool v) {
    const auto byte = static_cast<std::size_t>(pos / 8);
    const auto mask = static_cast<std::uint8_t>(1U << (pos % 8));
    if (v) {
        d[byte] = static_cast<std::uint8_t>(d[byte] | mask);
    } else {
        d[byte] = static_cast<std::uint8_t>(d[byte] & static_cast<std::uint8_t>(~mask));
    }
}

bool getBit(const std::array<std::uint8_t, 8>& d, int pos) {
    return ((static_cast<unsigned>(d[static_cast<std::size_t>(pos / 8)]) >> static_cast<unsigned>(pos % 8)) & 1U) != 0;
}

// Motorola testere dişi: bayt içinde aşağı, bayt sonunda bir sonraki baytın en üst bitine
int nextMotorola(int pos) { return (pos % 8 == 0) ? pos + 15 : pos - 1; }

}  // namespace

bool Signal::fits() const {
    if (length == 0 || length > 64 || startBit > 63) {
        return false;
    }
    if (order == ByteOrder::Intel) {
        return startBit + length <= 64;
    }
    int pos = startBit;
    for (int i = 1; i < length; ++i) {
        pos = nextMotorola(pos);
        if (pos > 63) {
            return false;
        }
    }
    return true;
}

bool packRaw(const Signal& s, std::uint64_t raw, std::array<std::uint8_t, 8>& data) {
    if (!s.fits()) {
        return false;
    }
    if (s.order == Signal::ByteOrder::Intel) {
        for (int i = 0; i < s.length; ++i) {
            setBit(data, s.startBit + i, ((raw >> i) & 1U) != 0);
        }
    } else {
        int pos = s.startBit;
        for (int i = 0; i < s.length; ++i) {
            setBit(data, pos, ((raw >> (s.length - 1 - i)) & 1U) != 0);
            pos = nextMotorola(pos);
        }
    }
    return true;
}

std::int64_t unpackRaw(const Signal& s, const std::array<std::uint8_t, 8>& data) {
    if (!s.fits()) {
        return 0;
    }
    std::uint64_t raw = 0;
    if (s.order == Signal::ByteOrder::Intel) {
        for (int i = 0; i < s.length; ++i) {
            raw |= static_cast<std::uint64_t>(getBit(data, s.startBit + i)) << i;
        }
    } else {
        int pos = s.startBit;
        for (int i = 0; i < s.length; ++i) {
            raw = (raw << 1) | static_cast<std::uint64_t>(getBit(data, pos));
            pos = nextMotorola(pos);
        }
    }
    if (s.isSigned && s.length < 64 && ((raw >> (s.length - 1)) & 1U) != 0) {
        raw |= ~((std::uint64_t{1} << s.length) - 1);  // işaret genişletme
    }
    return static_cast<std::int64_t>(raw);
}

bool encodePhysical(const Signal& s, double value, std::array<std::uint8_t, 8>& data) {
    if (!s.fits() || s.scale == 0.0 || !std::isfinite(value)) {
        return false;
    }
    if (s.minimum != s.maximum && (value < s.minimum || value > s.maximum)) {
        return false;
    }
    const double rawD = std::round((value - s.offset) / s.scale);
    const double lo = s.isSigned ? -std::ldexp(1.0, s.length - 1) : 0.0;
    const double hi = s.isSigned ? std::ldexp(1.0, s.length - 1) - 1.0 : std::ldexp(1.0, s.length) - 1.0;
    if (rawD < lo || rawD > hi) {
        return false;  // ham alana sığmıyor
    }
    std::uint64_t raw = 0;
    if (s.isSigned) {
        raw = static_cast<std::uint64_t>(static_cast<std::int64_t>(rawD));
    } else {
        raw = static_cast<std::uint64_t>(rawD);
    }
    if (s.length < 64) {
        raw &= (std::uint64_t{1} << s.length) - 1;
    }
    return packRaw(s, raw, data);
}

double decodePhysical(const Signal& s, const std::array<std::uint8_t, 8>& data) {
    const std::int64_t raw = unpackRaw(s, data);
    const double rawD = s.isSigned ? static_cast<double>(raw) : static_cast<double>(static_cast<std::uint64_t>(raw));
    return rawD * s.scale + s.offset;
}

}  // namespace avb::can
