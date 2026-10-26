#pragma once
// Klasik CAN (2.0A/2.0B) çerçevesi: 11 ya da 29 bit kimlik, en fazla 8 bayt.

#include <array>
#include <cstdint>

namespace avb::can {

struct Frame {
    std::uint32_t id = 0;
    bool extended = false;  // true: 29 bit kimlik
    std::uint8_t dlc = 0;
    std::array<std::uint8_t, 8> data{};

    static constexpr std::uint32_t kMaxStandardId = 0x7FF;
    static constexpr std::uint32_t kMaxExtendedId = 0x1FFFFFFF;

    // LLR-CAN-001
    bool valid() const { return dlc <= 8 && id <= (extended ? kMaxExtendedId : kMaxStandardId); }
};

}  // namespace avb::can
