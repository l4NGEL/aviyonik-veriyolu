#include <gtest/gtest.h>

#include "avb/m1553/manchester.hpp"

using namespace avb::m1553;

namespace {
bool level(HalfBits hb, int i) { return ((hb >> i) & 1U) != 0; }
}  // namespace

// Dogrular: LLR-1553-001
TEST(Manchester, KomutSenkronuUcYuksekUcDusuk) {
    const HalfBits hb = encodeManchester({Sync::CommandStatus, 0});
    EXPECT_TRUE(level(hb, 0) && level(hb, 1) && level(hb, 2));
    EXPECT_FALSE(level(hb, 3) || level(hb, 4) || level(hb, 5));
}

// Dogrular: LLR-1553-001
TEST(Manchester, VeriSenkronuTers) {
    const HalfBits hb = encodeManchester({Sync::Data, 0});
    EXPECT_FALSE(level(hb, 0) || level(hb, 1) || level(hb, 2));
    EXPECT_TRUE(level(hb, 3) && level(hb, 4) && level(hb, 5));
}

// Dogrular: LLR-1553-001
TEST(Manchester, BirYuksektenDusugeSifirDusuktenYuksege) {
    const HalfBits one = encodeManchester({Sync::Data, 0x8000});  // ilk bilgi biti 1
    EXPECT_TRUE(level(one, 6));
    EXPECT_FALSE(level(one, 7));
    const HalfBits zero = encodeManchester({Sync::Data, 0x0000});
    EXPECT_FALSE(level(zero, 6));
    EXPECT_TRUE(level(zero, 7));
}

// Dogrular: LLR-1553-001, LLR-1553-006
TEST(Manchester, TumKelimelerdeGidisDonus) {
    for (int s = 0; s < 2; ++s) {
        const Sync sync = s == 0 ? Sync::CommandStatus : Sync::Data;
        for (std::uint32_t v = 0; v <= 0xFFFF; ++v) {
            const Word w{sync, static_cast<std::uint16_t>(v)};
            const DecodeResult r = decodeManchester(encodeManchester(w));
            ASSERT_TRUE(r.ok());
            ASSERT_EQ(r.word, w);
        }
    }
}

// Dogrular: LLR-1553-006
TEST(Manchester, HerTekYarimBitHatasiYakalanir) {
    // Senkrondaki tek hata gecersiz senkron, bilgi alanindaki tek hata Manchester ihlali verir;
    // hicbir tek yarim bit hatasi baska gecerli bir kelimeye donusmez.
    const std::uint16_t values[] = {0x0000, 0xFFFF, 0xA5A5, 0x2C64, 0x8001};
    for (std::uint16_t v : values) {
        for (int s = 0; s < 2; ++s) {
            const Word w{s == 0 ? Sync::CommandStatus : Sync::Data, v};
            const HalfBits hb = encodeManchester(w);
            for (int i = 0; i < kHalfBitsPerWord; ++i) {
                const DecodeResult r = decodeManchester(hb ^ (HalfBits{1} << i));
                EXPECT_FALSE(r.ok()) << "deger=" << v << " yarim bit=" << i;
                EXPECT_EQ(r.error, i < 6 ? DecodeError::InvalidSync : DecodeError::Manchester);
            }
        }
    }
}

// Dogrular: LLR-1553-006
TEST(Manchester, TekBitTersineDonmesiEslikleYakalanir) {
    // Bir bitin iki yarisi birden ters: Manchester gecerli kalir, eslik yakalar
    const HalfBits hb = encodeManchester({Sync::Data, 0x1234});
    const HalfBits flipped = hb ^ (HalfBits{0b11} << (6 + 2 * 5));
    EXPECT_EQ(decodeManchester(flipped).error, DecodeError::Parity);
}

// Dogrular: LLR-1553-006
TEST(Manchester, IkiBitTersineDonmesiYakalanamaz) {
    // Bilinen sinir: tek eslik cift sayida bit hatasini goremez.
    const HalfBits hb = encodeManchester({Sync::Data, 0x1234});
    const HalfBits flipped = hb ^ (HalfBits{0b11} << (6 + 2 * 5)) ^ (HalfBits{0b11} << (6 + 2 * 9));
    const DecodeResult r = decodeManchester(flipped);
    EXPECT_TRUE(r.ok());
    EXPECT_NE(r.word.value, 0x1234);
}
