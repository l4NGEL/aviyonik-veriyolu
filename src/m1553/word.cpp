#include "avb/m1553/word.hpp"

namespace avb::m1553 {

namespace {
constexpr std::uint16_t field(std::uint16_t v, int shift, std::uint16_t mask) {
    return static_cast<std::uint16_t>((v >> shift) & mask);
}
constexpr std::uint16_t bit(bool b, int shift) { return static_cast<std::uint16_t>(b ? (1U << shift) : 0U); }
}  // namespace

CommandWord CommandWord::decode(std::uint16_t v) {
    CommandWord c;
    c.rt = static_cast<std::uint8_t>(field(v, 11, 0x1F));
    c.transmit = field(v, 10, 0x1) != 0;
    c.subaddress = static_cast<std::uint8_t>(field(v, 5, 0x1F));
    c.countOrCode = static_cast<std::uint8_t>(field(v, 0, 0x1F));
    return c;
}

std::uint16_t CommandWord::encode() const {
    return static_cast<std::uint16_t>(((rt & 0x1FU) << 11) | bit(transmit, 10) | ((subaddress & 0x1FU) << 5) |
                                      (countOrCode & 0x1FU));
}

int CommandWord::dataWordCount() const {
    if (isModeCode()) {
        return countOrCode >= 16 ? 1 : 0;
    }
    return countOrCode == 0 ? kMaxDataWords : countOrCode;
}

StatusWord StatusWord::decode(std::uint16_t v) {
    StatusWord s;
    s.rt = static_cast<std::uint8_t>(field(v, 11, 0x1F));
    s.messageError = field(v, 10, 1) != 0;
    s.instrumentation = field(v, 9, 1) != 0;
    s.serviceRequest = field(v, 8, 1) != 0;
    // bit 7-5 ayrılmış
    s.broadcastReceived = field(v, 4, 1) != 0;
    s.busy = field(v, 3, 1) != 0;
    s.subsystemFlag = field(v, 2, 1) != 0;
    s.dynamicBusControlAccept = field(v, 1, 1) != 0;
    s.terminalFlag = field(v, 0, 1) != 0;
    return s;
}

std::uint16_t StatusWord::encode() const {
    return static_cast<std::uint16_t>(((rt & 0x1FU) << 11) | bit(messageError, 10) | bit(instrumentation, 9) |
                                      bit(serviceRequest, 8) | bit(broadcastReceived, 4) | bit(busy, 3) |
                                      bit(subsystemFlag, 2) | bit(dynamicBusControlAccept, 1) |
                                      bit(terminalFlag, 0));
}

}  // namespace avb::m1553
