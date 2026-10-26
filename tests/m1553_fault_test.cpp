#include <gtest/gtest.h>

#include <cstdio>

#include "m1553_fixture.hpp"

using namespace avb::m1553;

// Dogrular: LLR-1553-050
TEST(HataEnjektoru, KuralBelirtilenSayidaUygulanir) {
    FaultInjector f;
    ASSERT_TRUE(f.add({BusId::A, true, 0, 0, FaultInjector::Action::FlipHalfBit, 7, 2}));
    const HalfBits hb = encodeManchester({Sync::Data, 0x1234});
    EXPECT_NE(f.apply(BusId::A, Source::bc(), 0, hb).halfBits, hb);
    EXPECT_NE(f.apply(BusId::A, Source::bc(), 0, hb).halfBits, hb);
    EXPECT_EQ(f.apply(BusId::A, Source::bc(), 0, hb).halfBits, hb);  // hak bitti
    EXPECT_EQ(f.apply(BusId::B, Source::bc(), 0, hb).halfBits, hb);  // baska hat
    EXPECT_EQ(f.injectedCount(), 2U);
}

// Dogrular: LLR-1553-050
TEST(HataEnjektoru, KapasiteSiniri) {
    FaultInjector f;
    for (std::size_t i = 0; i < FaultInjector::kMaxRules; ++i) {
        ASSERT_TRUE(f.add({}));
    }
    EXPECT_FALSE(f.add({}));
    f.clear();
    EXPECT_TRUE(f.add({}));
}

// Dogrular: LLR-1553-050, LLR-1553-006
TEST(HataEnjektoru, SenkronBozmaGecersizSenkronVerir) {
    FaultInjector f;
    f.add({BusId::A, true, 0, 0, FaultInjector::Action::CorruptSync, 0, -1});
    for (Sync s : {Sync::CommandStatus, Sync::Data}) {
        const auto e = f.apply(BusId::A, Source::bc(), 0, encodeManchester({s, 0xABCD}));
        EXPECT_EQ(decodeManchester(e.halfBits).error, DecodeError::InvalidSync);
    }
}

// Dogrular: LLR-1553-051, LLR-1553-014
TEST_F(VeriYolu, RastgeleBitHatalarindaBozukVeriKabulEdilmez) {
    // Yarim bit basina 2e-4 hata (kelime basina ~%0,8): her mesaj ya dogru veriyle
    // tamamlanir ya da hata olarak raporlanir; bozuk veri hicbir zaman "tamam" donmez.
    faults.setRandomHalfBitErrorRate(2e-4, 12345);
    std::array<std::uint16_t, 8> pattern{};
    for (std::size_t i = 0; i < pattern.size(); ++i) {
        pattern[i] = static_cast<std::uint16_t>(0x1000U + i * 0x0111U);
    }
    ASSERT_TRUE(rt6.setTransmitData(3, pattern.data(), pattern.size()));

    constexpr int kMessages = 20'000;
    int ok = 0;
    int okOnAlternate = 0;
    int corruptedButAccepted = 0;
    for (int i = 0; i < kMessages; ++i) {
        const Result r = bc.execute(Message::rtToBc(6, 3, 8));
        if (r.outcome != Outcome::Ok) {
            continue;
        }
        ++ok;
        okOnAlternate += r.retries > 0 ? 1 : 0;
        for (std::size_t k = 0; k < pattern.size(); ++k) {
            if (r.data[k] != pattern[k]) {
                ++corruptedButAccepted;
                break;
            }
        }
    }
    EXPECT_EQ(corruptedButAccepted, 0);
    EXPECT_GT(okOnAlternate, 0);
    EXPECT_GT(ok, kMessages * 99 / 100);  // tek yedek denemeyle %99+ teslim
    std::printf("[ bilgi ] %d/%d mesaj tamam, %d tanesi yedek hatta kurtarildi, enjekte edilen hata: %llu\n", ok,
                kMessages, okOnAlternate, static_cast<unsigned long long>(faults.injectedCount()));
}
