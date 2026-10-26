#pragma once
// DBC tarzı sinyal paketleme.
//
// Intel (little-endian): başlangıç biti en düşük anlamlı bittir, bitler artan
// sırada ilerler. Motorola (big-endian): başlangıç biti en yüksek anlamlı
// bittir; bayt içinde azalan, bayt sınırında bir sonraki baytın 7. bitine
// geçen "testere dişi" numaralama kullanılır (DBC dosyalarındaki gösterim).
// Fiziksel değer = ham * ölçek + ofset.

#include <array>
#include <cstdint>

namespace avb::can {

struct Signal {
    enum class ByteOrder : std::uint8_t { Intel, Motorola };

    std::uint16_t startBit = 0;  // 0..63
    std::uint8_t length = 8;     // 1..64
    ByteOrder order = ByteOrder::Intel;
    bool isSigned = false;
    double scale = 1.0;
    double offset = 0.0;
    double minimum = 0.0;
    double maximum = 0.0;  // min == max ise sınır yok

    bool fits() const;  // sinyal 8 baytın dışına taşıyor mu  LLR-CAN-010
};

// Ham değeri bayt dizisine yazar; diğer bitler korunur.  LLR-CAN-011
bool packRaw(const Signal& s, std::uint64_t raw, std::array<std::uint8_t, 8>& data);
// Ham değeri okur; işaretli sinyalde işaret genişletilir.  LLR-CAN-012
std::int64_t unpackRaw(const Signal& s, const std::array<std::uint8_t, 8>& data);

// Fiziksel değer: aralık dışı ya da ham alana sığmayan değer false döner.  LLR-CAN-013
bool encodePhysical(const Signal& s, double value, std::array<std::uint8_t, 8>& data);
double decodePhysical(const Signal& s, const std::array<std::uint8_t, 8>& data);

}  // namespace avb::can
