#include "avb/m1553/manchester.hpp"

namespace avb::m1553 {

namespace {

constexpr HalfBits kCommandSync = 0b000111;  // ilk üç yarım bit yüksek (bit 0..2)
constexpr HalfBits kDataSync = 0b111000;     // ilk üç yarım bit düşük
constexpr HalfBits kSyncMask = 0b111111;

void putBit(HalfBits& hb, int index, bool one) {
    // 1: yüksek sonra düşük; 0: düşük sonra yüksek
    const int first = 6 + index * 2;
    if (one) {
        hb |= HalfBits{1} << first;
    } else {
        hb |= HalfBits{1} << (first + 1);
    }
}

}  // namespace

HalfBits encodeManchester(const Word& w) {
    HalfBits hb = (w.sync == Sync::CommandStatus) ? kCommandSync : kDataSync;
    for (int i = 0; i < 16; ++i) {
        const bool one = ((w.value >> (15 - i)) & 1U) != 0;
        putBit(hb, i, one);
    }
    putBit(hb, 16, parityBitFor(w.value));
    return hb;
}

DecodeResult decodeManchester(HalfBits hb) {
    DecodeResult r;
    const HalfBits sync = hb & kSyncMask;
    if (sync == kCommandSync) {
        r.word.sync = Sync::CommandStatus;
    } else if (sync == kDataSync) {
        r.word.sync = Sync::Data;
    } else {
        r.error = DecodeError::InvalidSync;
        return r;
    }

    std::uint16_t value = 0;
    bool parity = false;
    for (int i = 0; i < 17; ++i) {
        const int first = 6 + i * 2;
        const bool hi = ((hb >> first) & 1U) != 0;
        const bool lo = ((hb >> (first + 1)) & 1U) != 0;
        if (hi == lo) {
            // Bit ortasında geçiş yok: Manchester ihlali
            r.error = DecodeError::Manchester;
            return r;
        }
        if (i < 16) {
            value = static_cast<std::uint16_t>((static_cast<unsigned>(value) << 1U) | (hi ? 1U : 0U));
        } else {
            parity = hi;
        }
    }
    r.word.value = value;
    if (parity != parityBitFor(value)) {
        r.error = DecodeError::Parity;
    }
    return r;
}

const char* toString(DecodeError e) {
    switch (e) {
        case DecodeError::None:
            return "yok";
        case DecodeError::InvalidSync:
            return "gecersiz-senkron";
        case DecodeError::Manchester:
            return "manchester";
        case DecodeError::Parity:
            return "eslik";
    }
    return "?";
}

}  // namespace avb::m1553
