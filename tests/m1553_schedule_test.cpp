#include <gtest/gtest.h>

#include "m1553_fixture.hpp"

using namespace avb::m1553;

// Dogrular: LLR-1553-061
TEST(EnKotuSure, MesajTurlerineGore) {
    // komut + 4 veri + 12 us yanit + durum + 4 us bosluk
    EXPECT_EQ(worstCaseDurationNs(Message::bcToRt(1, 1, nullptr, 4)), 4'000 + 5 * 20'000 + 12'000 + 20'000);
    EXPECT_EQ(worstCaseDurationNs(Message::rtToBc(1, 1, 4)), 4'000 + 20'000 + 12'000 + 5 * 20'000);
    EXPECT_EQ(worstCaseDurationNs(Message::rtToRt(1, 1, 2, 1, 4)),
              4'000 + 2 * 20'000 + 12'000 + 5 * 20'000 + 12'000 + 20'000);
    EXPECT_EQ(worstCaseDurationNs(Message::bcToRt(kBroadcastAddress, 1, nullptr, 4)), 4'000 + 5 * 20'000);
    EXPECT_EQ(worstCaseDurationNs(Message::modeCodeMsg(1, mode::kTransmitVectorWord)),
              4'000 + 20'000 + 12'000 + 2 * 20'000);
    EXPECT_EQ(worstCaseDurationNs(Message::modeCodeMsg(1, mode::kSynchronizeWithData)),
              4'000 + 2 * 20'000 + 12'000 + 20'000);
}

// Dogrular: LLR-1553-060
TEST(Cizelge, GecersizGirislerReddedilir) {
    Schedule s(20'000'000);
    EXPECT_FALSE(s.add({Message::rtToBc(1, 1, 1), 0, 0}));  // periyot 0
    EXPECT_FALSE(s.add({Message::rtToBc(1, 1, 1), 4, 4}));  // faz >= periyot
    EXPECT_TRUE(s.add({Message::rtToBc(1, 1, 1), 32, 0}));
    EXPECT_FALSE(s.add({Message::rtToBc(1, 1, 1), 3, 0}));  // EKOK 96 > 64
    EXPECT_EQ(s.majorFrameLength(), 32);
}

// Dogrular: LLR-1553-060
TEST(Cizelge, BuyukCerceveEkokVeFaz) {
    Schedule s(20'000'000);
    ASSERT_TRUE(s.add({Message::rtToBc(1, 1, 1), 1, 0}));  // 50 Hz
    ASSERT_TRUE(s.add({Message::rtToBc(1, 2, 1), 2, 1}));  // 25 Hz, tek cercevelerde
    ASSERT_TRUE(s.add({Message::rtToBc(1, 3, 1), 4, 2}));  // 12,5 Hz
    EXPECT_EQ(s.majorFrameLength(), 4);
    EXPECT_TRUE(s.runsIn(1, 1));
    EXPECT_FALSE(s.runsIn(1, 2));
    EXPECT_TRUE(s.runsIn(2, 2));
    EXPECT_FALSE(s.runsIn(2, 3));
    EXPECT_TRUE(s.runsIn(2, 6));
}

// Dogrular: LLR-1553-061
TEST(Cizelge, AsiriYukluCizelgeDogrulamadaKalir) {
    Schedule s(1'000'000);  // 1 ms kucuk cerceve
    // her biri en kotu 4+660+12+20 = 696 us: ikisi 1 ms'ye sigmaz
    ASSERT_TRUE(s.add({Message::bcToRt(1, 1, nullptr, 32), 1, 0}));
    EXPECT_TRUE(s.validate().ok);
    ASSERT_TRUE(s.add({Message::bcToRt(2, 1, nullptr, 32), 2, 1}));
    const Schedule::Check c = s.validate();
    EXPECT_FALSE(c.ok);
    EXPECT_EQ(c.worstMinor, 1);
    EXPECT_EQ(c.worstNs, 2 * 696'000);
    EXPECT_NEAR(c.worstUtilization, 1.392, 1e-9);
}

// Dogrular: LLR-1553-062
TEST_F(VeriYolu, KucukCerceveDogruMesajlariKosarVeSaatSinirdaDurur) {
    Schedule s(20'000'000);
    ASSERT_TRUE(s.add({Message::rtToBc(5, 1, 2), 1, 0}));
    ASSERT_TRUE(s.add({Message::rtToBc(6, 1, 2), 2, 1}));
    std::array<Result, 8> res{};
    EXPECT_EQ(bc.runMinorFrame(s, 0, res.data(), res.size()), 1U);
    EXPECT_EQ(clock.nowNs(), 20'000'000);
    EXPECT_EQ(bc.runMinorFrame(s, 1, res.data(), res.size()), 2U);
    EXPECT_EQ(res[1].status.rt, 6);
    EXPECT_EQ(clock.nowNs(), 40'000'000);
    EXPECT_EQ(bc.stats().frameOverruns, 0U);
}

// Dogrular: LLR-1553-062
TEST_F(VeriYolu, CerceveTasmasiSayilir) {
    Schedule s(100'000);  // 100 us: tek bir 32 kelimelik mesaja bile yetmez
    ASSERT_TRUE(s.add({Message::rtToBc(5, 1, 32), 1, 0}));
    bc.runMinorFrame(s, 0, nullptr, 0);
    EXPECT_EQ(bc.stats().frameOverruns, 1U);
}
