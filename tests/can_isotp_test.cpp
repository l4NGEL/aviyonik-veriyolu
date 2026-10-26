#include <gtest/gtest.h>

#include <deque>
#include <vector>

#include "avb/can/isotp.hpp"

using namespace avb::can;
using namespace avb::can::isotp;

namespace {

struct Kayit {
    std::int64_t t = 0;
    Frame f;
};

// Iki uc arasinda bellekte cerceve tasiyan kucuk duzenek; zaman µs.
struct Hat {
    std::int64_t now = 0;
    std::vector<Kayit> gonderenCikis;  // gonderen -> alici
    std::vector<Kayit> aliciCikis;     // alici -> gonderen (akis kontrolu)
    std::deque<Frame> bekleyenG;
    std::deque<Frame> bekleyenA;
    bool akisKontroluDusur = false;

    static bool gonderenden(void* ctx, const Frame& f) {
        auto* h = static_cast<Hat*>(ctx);
        h->gonderenCikis.push_back({h->now, f});
        h->bekleyenA.push_back(f);
        return true;
    }
    static bool aliciden(void* ctx, const Frame& f) {
        auto* h = static_cast<Hat*>(ctx);
        h->aliciCikis.push_back({h->now, f});
        if (!h->akisKontroluDusur) {
            h->bekleyenG.push_back(f);
        }
        return true;
    }
};

const Addressing kGonderen{0x7E0, 0x7E8, false, true};
const Addressing kAlici{0x7E8, 0x7E0, false, true};

std::vector<std::uint8_t> yuk(std::size_t n) {
    std::vector<std::uint8_t> v(n);
    for (std::size_t i = 0; i < n; ++i) {
        v[i] = static_cast<std::uint8_t>(i * 7 + 3);
    }
    return v;
}

// Teslim edilene ya da adim sinirina kadar zamani 100 µs adimlarla ilerletir
void kostur(Hat& h, Sender& s, Receiver& r, int adim = 200'000) {
    for (int i = 0; i < adim && !r.hasMessage(); ++i) {
        while (!h.bekleyenA.empty()) {
            const Frame f = h.bekleyenA.front();
            h.bekleyenA.pop_front();
            r.onFrame(f, h.now);
        }
        while (!h.bekleyenG.empty()) {
            const Frame f = h.bekleyenG.front();
            h.bekleyenG.pop_front();
            s.onFrame(f, h.now);
        }
        s.tick(h.now);
        r.tick(h.now);
        if (s.state() == Sender::State::Failed || r.lastError() != Error::None) {
            return;
        }
        h.now += 100;
    }
}

}  // namespace

// Dogrular: LLR-CAN-020, LLR-CAN-021
TEST(IsoTp, TekCerceve) {
    Hat h;
    Sender s(kGonderen, &Hat::gonderenden, &h);
    Receiver r(kAlici, &Hat::aliciden, &h);
    const auto v = yuk(5);
    ASSERT_TRUE(s.start(v.data(), v.size(), 0));
    EXPECT_EQ(s.state(), Sender::State::Done);
    ASSERT_EQ(h.gonderenCikis.size(), 1U);
    EXPECT_EQ(h.gonderenCikis[0].f.data[0], 0x05);
    EXPECT_EQ(h.gonderenCikis[0].f.dlc, 8);
    EXPECT_EQ(h.gonderenCikis[0].f.data[7], 0xCC);  // dolgu
    kostur(h, s, r);
    ASSERT_TRUE(r.hasMessage());
    EXPECT_EQ(std::vector<std::uint8_t>(r.data(), r.data() + r.length()), v);
}

// Dogrular: LLR-CAN-020, LLR-CAN-021, LLR-CAN-024
TEST(IsoTp, EnBuyukYukBloklarVeStMin) {
    Hat h;
    Sender s(kGonderen, &Hat::gonderenden, &h);
    Receiver r(kAlici, &Hat::aliciden, &h);
    r.blockSize = 8;
    r.stMin = 2;  // 2 ms
    const auto v = yuk(kMaxPayload);
    ASSERT_TRUE(s.start(v.data(), v.size(), 0));
    EXPECT_EQ(h.gonderenCikis[0].f.data[0], 0x1F);  // FF, uzunluk ust yarim 0xF
    EXPECT_EQ(h.gonderenCikis[0].f.data[1], 0xFF);
    kostur(h, s, r);

    ASSERT_TRUE(r.hasMessage());
    EXPECT_EQ(std::vector<std::uint8_t>(r.data(), r.data() + r.length()), v);
    EXPECT_EQ(s.state(), Sender::State::Done);
    // 6 + 584*7 >= 4095: 585 ardisik cerceve, her 8'inde bir akis kontrolu
    const std::size_t cf = h.gonderenCikis.size() - 1;
    EXPECT_EQ(cf, 585U);
    EXPECT_EQ(h.aliciCikis.size(), 1U + (cf - 1) / 8);
    // ardisik cerceveler arasi en az STmin; sira numarasi 15'ten sonra 0
    for (std::size_t i = 2; i < h.gonderenCikis.size(); ++i) {
        const bool blokBasi = (i - 1) % 8 == 0;
        if (!blokBasi) {
            EXPECT_GE(h.gonderenCikis[i].t - h.gonderenCikis[i - 1].t, 2000) << i;
        }
        EXPECT_EQ(h.gonderenCikis[i].f.data[0] & 0x0F, i % 16) << i;
    }
}

// Dogrular: LLR-CAN-021
TEST(IsoTp, YanlisSiraNumarasiAlimiIptalEder) {
    Hat h;
    Receiver r(kAlici, &Hat::aliciden, &h);
    Frame ff;
    ff.id = 0x7E0;
    ff.dlc = 8;
    ff.data = {0x10, 20, 1, 2, 3, 4, 5, 6};
    r.onFrame(ff, 0);
    Frame cf = ff;
    cf.data = {0x22, 0, 0, 0, 0, 0, 0, 0};  // 1 bekleniyordu
    r.onFrame(cf, 100);
    EXPECT_EQ(r.lastError(), Error::WrongSequence);
    EXPECT_FALSE(r.hasMessage());
}

// Dogrular: LLR-CAN-023
TEST(IsoTp, AkisKontroluGelmezseGonderenZamanAsimi) {
    Hat h;
    h.akisKontroluDusur = true;
    Sender s(kGonderen, &Hat::gonderenden, &h);
    Receiver r(kAlici, &Hat::aliciden, &h);
    const auto v = yuk(100);
    ASSERT_TRUE(s.start(v.data(), v.size(), 0));
    kostur(h, s, r, 20'000);
    EXPECT_EQ(s.state(), Sender::State::Failed);
    EXPECT_EQ(s.error(), Error::TimeoutBs);
    EXPECT_GT(h.now, 1'000'000);
}

// Dogrular: LLR-CAN-023
TEST(IsoTp, ArdisikCerceveGelmezseAliciZamanAsimi) {
    Hat h;
    Receiver r(kAlici, &Hat::aliciden, &h);
    Frame ff;
    ff.id = 0x7E0;
    ff.dlc = 8;
    ff.data = {0x10, 20, 1, 2, 3, 4, 5, 6};
    r.onFrame(ff, 0);
    r.tick(999'999);
    EXPECT_EQ(r.lastError(), Error::None);
    r.tick(1'000'001);
    EXPECT_EQ(r.lastError(), Error::TimeoutCr);
}

// Dogrular: LLR-CAN-022
TEST(IsoTp, TasmaVeBeklemeAkisDurumlari) {
    Hat h;
    Sender s(kGonderen, &Hat::gonderenden, &h);
    Receiver r(kAlici, &Hat::aliciden, &h);
    r.capacity = 64;
    const auto v = yuk(100);
    ASSERT_TRUE(s.start(v.data(), v.size(), 0));
    kostur(h, s, r);
    EXPECT_EQ(s.error(), Error::Overflow);
    EXPECT_EQ(r.lastError(), Error::Overflow);

    // WAIT: sinir asilinca vazgecer
    Sender s2(kGonderen, &Hat::gonderenden, &h);
    s2.maxWait = 2;
    ASSERT_TRUE(s2.start(v.data(), v.size(), 0));
    Frame wait;
    wait.id = 0x7E8;
    wait.dlc = 3;
    wait.data = {0x31, 0, 0};
    s2.onFrame(wait, 10);
    s2.onFrame(wait, 20);
    EXPECT_EQ(s2.state(), Sender::State::WaitFlowControl);
    s2.onFrame(wait, 30);
    EXPECT_EQ(s2.error(), Error::WaitLimit);
}

// Dogrular: LLR-CAN-020
TEST(IsoTp, SurenGonderimdeYeniGonderimReddedilir) {
    Hat h;
    Sender s(kGonderen, &Hat::gonderenden, &h);
    const auto v = yuk(50);
    ASSERT_TRUE(s.start(v.data(), v.size(), 0));
    EXPECT_FALSE(s.start(v.data(), v.size(), 1));
    EXPECT_EQ(s.error(), Error::Busy);
    EXPECT_FALSE(Sender(kGonderen, nullptr, nullptr).start(v.data(), kMaxPayload + 1, 0));
}

// Dogrular: LLR-CAN-021
TEST(IsoTp, GecersizCercevelerYokSayilir) {
    Hat h;
    Receiver r(kAlici, &Hat::aliciden, &h);
    Frame f;
    f.id = 0x7E0;
    f.dlc = 8;
    f.data = {0x00, 1, 2, 3, 4, 5, 6, 7};  // SF uzunluk 0
    r.onFrame(f, 0);
    f.data = {0x10, 5, 1, 2, 3, 4, 5, 6};  // FF ama 8'den kisa
    r.onFrame(f, 0);
    f.id = 0x123;  // baska kimlik
    f.data = {0x03, 1, 2, 3, 0, 0, 0, 0};
    r.onFrame(f, 0);
    f.id = 0x7E0;
    f.data = {0x21, 1, 2, 3, 4, 5, 6, 7};  // alim yokken CF
    r.onFrame(f, 0);
    EXPECT_FALSE(r.hasMessage());
    EXPECT_TRUE(h.aliciCikis.empty());
}

// Dogrular: LLR-CAN-024
TEST(IsoTp, StMinKodlamasi) {
    EXPECT_EQ(stMinToUs(0x00), 0);
    EXPECT_EQ(stMinToUs(0x7F), 127'000);
    EXPECT_EQ(stMinToUs(0xF1), 100);
    EXPECT_EQ(stMinToUs(0xF9), 900);
    EXPECT_EQ(stMinToUs(0x80), 127'000);  // ayrilmis
    EXPECT_EQ(stMinToUs(0xFA), 127'000);
}
