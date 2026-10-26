#include <gtest/gtest.h>

#include "avb/m1553/word.hpp"

using namespace avb::m1553;

// Dogrular: LLR-1553-003
TEST(KomutKelimesi, AlanlarBilinenDegereKodlanir) {
    // RT 5, gonder, alt adres 3, 4 kelime -> 00101 1 00011 00100
    const CommandWord c{5, true, 3, 4};
    EXPECT_EQ(c.encode(), 0x2C64);
    const CommandWord d = CommandWord::decode(0x2C64);
    EXPECT_EQ(d.rt, 5);
    EXPECT_TRUE(d.transmit);
    EXPECT_EQ(d.subaddress, 3);
    EXPECT_EQ(d.countOrCode, 4);
}

// Dogrular: LLR-1553-003
TEST(KomutKelimesi, TumDegerlerdeGidisDonus) {
    for (std::uint32_t v = 0; v <= 0xFFFF; ++v) {
        EXPECT_EQ(CommandWord::decode(static_cast<std::uint16_t>(v)).encode(), v);
    }
}

// Dogrular: LLR-1553-003
TEST(KomutKelimesi, SifirKelimeSayisiOtuzIkiDemektir) {
    EXPECT_EQ((CommandWord{1, false, 1, 0}.dataWordCount()), 32);
    EXPECT_EQ((CommandWord{1, false, 1, 31}.dataWordCount()), 31);
}

// Dogrular: LLR-1553-004
TEST(KomutKelimesi, AltAdresSifirVeOtuzBirModKodudur) {
    EXPECT_TRUE((CommandWord{1, true, 0, 2}.isModeCode()));
    EXPECT_TRUE((CommandWord{1, true, 31, 2}.isModeCode()));
    EXPECT_FALSE((CommandWord{1, true, 30, 2}.isModeCode()));
    // 0-15 verisiz, 16-31 tek veri kelimeli
    EXPECT_EQ((CommandWord{1, true, 31, 15}.dataWordCount()), 0);
    EXPECT_EQ((CommandWord{1, true, 31, 16}.dataWordCount()), 1);
    EXPECT_EQ((CommandWord{1, false, 31, 17}.dataWordCount()), 1);
}

// Dogrular: LLR-1553-005
TEST(KomutKelimesi, Adres31Yayindir) {
    EXPECT_TRUE((CommandWord{31, false, 1, 1}.isBroadcast()));
    EXPECT_FALSE((CommandWord{30, false, 1, 1}.isBroadcast()));
}

// Dogrular: LLR-1553-007
TEST(DurumKelimesi, HerBayrakStandarttakiBitKonumunda) {
    StatusWord s;
    s.messageError = true;
    EXPECT_EQ(s.encode(), 0x0400);
    s = {};
    s.instrumentation = true;
    EXPECT_EQ(s.encode(), 0x0200);
    s = {};
    s.serviceRequest = true;
    EXPECT_EQ(s.encode(), 0x0100);
    s = {};
    s.broadcastReceived = true;
    EXPECT_EQ(s.encode(), 0x0010);
    s = {};
    s.busy = true;
    EXPECT_EQ(s.encode(), 0x0008);
    s = {};
    s.subsystemFlag = true;
    EXPECT_EQ(s.encode(), 0x0004);
    s = {};
    s.dynamicBusControlAccept = true;
    EXPECT_EQ(s.encode(), 0x0002);
    s = {};
    s.terminalFlag = true;
    EXPECT_EQ(s.encode(), 0x0001);
    s = {};
    s.rt = 31;
    EXPECT_EQ(s.encode(), 0xF800);
}

// Dogrular: LLR-1553-007
TEST(DurumKelimesi, AyrilmisBitlerHaricGidisDonus) {
    for (std::uint32_t v = 0; v <= 0xFFFF; ++v) {
        const auto masked = static_cast<std::uint16_t>(v & ~0x00E0U);  // bit 7-5 ayrilmis
        EXPECT_EQ(StatusWord::decode(masked).encode(), masked);
    }
}

// Dogrular: LLR-1553-002
TEST(Eslik, TekEslik) {
    EXPECT_TRUE(parityBitFor(0x0000));   // sifir bir -> eslik biti 1
    EXPECT_FALSE(parityBitFor(0x0001));  // bir adet bir -> eslik biti 0
    EXPECT_TRUE(parityBitFor(0x0003));
    EXPECT_FALSE(parityBitFor(0xFFFE));  // 15 bir
    EXPECT_TRUE(parityBitFor(0xFFFF));   // 16 bir
}
