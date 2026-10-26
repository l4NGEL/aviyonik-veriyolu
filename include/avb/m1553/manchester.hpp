#pragma once
// Manchester II bi-faz kodlama.
//
// Bir kelime 40 yarım bit olarak modellenir (1 MHz bit hızında 0,5 µs'lik
// dilimler). 1 = yüksek->düşük, 0 = düşük->yüksek geçişi. Senkron alanı 3 bit
// zamanı boyunca geçişsizdir: komut/durum için 1,5 yüksek + 1,5 düşük, veri
// için tersi. Bu geçişsizlik geçerli bir Manchester bitiyle karıştırılamaz,
// alıcı kelime sınırını buradan yakalar.

#include <cstdint>

#include "avb/m1553/word.hpp"

namespace avb::m1553 {

constexpr int kHalfBitsPerWord = 40;

// Bit i (0..39) = i. yarım bitin seviyesi, 1 = yüksek. İlk iletilen yarım bit = bit 0.
using HalfBits = std::uint64_t;

enum class DecodeError : std::uint8_t { None, InvalidSync, Manchester, Parity };

struct DecodeResult {
    Word word;
    DecodeError error = DecodeError::None;

    bool ok() const { return error == DecodeError::None; }
};

HalfBits encodeManchester(const Word& w);   // LLR-1553-001
DecodeResult decodeManchester(HalfBits hb);  // LLR-1553-006

const char* toString(DecodeError e);

}  // namespace avb::m1553
