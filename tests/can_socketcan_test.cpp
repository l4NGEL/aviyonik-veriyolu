// SocketCAN entegrasyon testleri: gercek cekirdek CAN yigini uzerinde (vcan).
// Arabirim yoksa atlanir; scripts/docker-test.sh vcan0'i olusturur.

#include <gtest/gtest.h>

#include <chrono>
#include <cstdlib>
#include <vector>

#include "avb/can/isotp.hpp"
#include "avb/can/socketcan.hpp"

using namespace avb::can;

namespace {

const char* arabirim() {
    const char* e = std::getenv("AVB_VCAN");
    return e != nullptr ? e : "vcan0";
}

#define VCAN_GEREKLI()                                                                  \
    if (!SocketCan::interfaceExists(arabirim())) {                                      \
        GTEST_SKIP() << arabirim() << " yok (ip link add dev vcan0 type vcan)";          \
    }

std::int64_t simdiUs() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

}  // namespace

// Dogrular: LLR-CAN-030
TEST(SocketCan, OlmayanArabirimAcilmaz) {
    SocketCan s;
    EXPECT_FALSE(s.open("yok_can9"));
    EXPECT_FALSE(s.isOpen());
    Frame f;
    EXPECT_FALSE(s.send(f));
    EXPECT_EQ(s.receive(f, 0), SocketCan::Recv::Error);
}

// Dogrular: LLR-CAN-030
TEST(SocketCan, StandartVeGenisletilmisCerceveGidipGelir) {
    VCAN_GEREKLI();
    SocketCan tx;
    SocketCan rx;
    ASSERT_TRUE(tx.open(arabirim()));
    ASSERT_TRUE(rx.open(arabirim()));

    Frame f;
    f.id = 0x123;
    f.dlc = 3;
    f.data = {1, 2, 3};
    ASSERT_TRUE(tx.send(f));
    Frame g;
    ASSERT_EQ(rx.receive(g, 500), SocketCan::Recv::Ok);
    EXPECT_EQ(g.id, 0x123U);
    EXPECT_FALSE(g.extended);
    EXPECT_EQ(g.dlc, 3);
    EXPECT_EQ(g.data[2], 3);

    f.id = 0x18DAF110;
    f.extended = true;
    ASSERT_TRUE(tx.send(f));
    ASSERT_EQ(rx.receive(g, 500), SocketCan::Recv::Ok);
    EXPECT_EQ(g.id, 0x18DAF110U);
    EXPECT_TRUE(g.extended);

    Frame gecersiz;
    gecersiz.id = 0x800;
    EXPECT_FALSE(tx.send(gecersiz));
}

// Dogrular: LLR-CAN-031
TEST(SocketCan, CekirdekFiltresiYalnizSecilenKimlikleriGecirir) {
    VCAN_GEREKLI();
    SocketCan tx;
    SocketCan rx;
    ASSERT_TRUE(tx.open(arabirim()));
    ASSERT_TRUE(rx.open(arabirim()));
    const std::uint32_t ids[] = {0x200};
    ASSERT_TRUE(rx.setFilters(ids, 1, false));

    Frame f;
    f.dlc = 1;
    f.id = 0x100;
    ASSERT_TRUE(tx.send(f));
    f.id = 0x200;
    ASSERT_TRUE(tx.send(f));
    Frame g;
    ASSERT_EQ(rx.receive(g, 500), SocketCan::Recv::Ok);
    EXPECT_EQ(g.id, 0x200U);
    EXPECT_EQ(rx.receive(g, 50), SocketCan::Recv::Timeout);
}

namespace {
bool soketeYaz(void* ctx, const Frame& f) { return static_cast<SocketCan*>(ctx)->send(f); }
}  // namespace

// Dogrular: LLR-CAN-020, LLR-CAN-021, LLR-CAN-030
TEST(SocketCan, IsoTpVcanUzerinden) {
    VCAN_GEREKLI();
    SocketCan gs;
    SocketCan as;
    ASSERT_TRUE(gs.open(arabirim()));
    ASSERT_TRUE(as.open(arabirim()));
    const std::uint32_t gIds[] = {0x7E8};
    const std::uint32_t aIds[] = {0x7E0};
    ASSERT_TRUE(gs.setFilters(gIds, 1, false));
    ASSERT_TRUE(as.setFilters(aIds, 1, false));

    isotp::Sender s({0x7E0, 0x7E8, false, true}, &soketeYaz, &gs);
    isotp::Receiver r({0x7E8, 0x7E0, false, true}, &soketeYaz, &as);
    r.blockSize = 4;
    r.stMin = 0xF5;  // 500 µs

    std::vector<std::uint8_t> v(1000);
    for (std::size_t i = 0; i < v.size(); ++i) {
        v[i] = static_cast<std::uint8_t>(i ^ 0x5A);
    }
    const std::int64_t t0 = simdiUs();
    ASSERT_TRUE(s.start(v.data(), v.size(), t0));
    while (!r.hasMessage() && simdiUs() - t0 < 5'000'000) {
        Frame f;
        while (as.receive(f, 0) == SocketCan::Recv::Ok) {
            r.onFrame(f, simdiUs());
        }
        while (gs.receive(f, 0) == SocketCan::Recv::Ok) {
            s.onFrame(f, simdiUs());
        }
        s.tick(simdiUs());
        r.tick(simdiUs());
        ASSERT_NE(s.state(), isotp::Sender::State::Failed) << isotp::toString(s.error());
        ASSERT_EQ(r.lastError(), isotp::Error::None);
    }
    ASSERT_TRUE(r.hasMessage());
    EXPECT_EQ(std::vector<std::uint8_t>(r.data(), r.data() + r.length()), v);
    // 142 ardisik cercevenin 36'si blok basi (bekleme yok), kalan 106'si en az 500 µs arayla
    EXPECT_GE(simdiUs() - t0, 106 * 500);
}
