#include <gtest/gtest.h>

#include "avb/can/frame.hpp"
#include "avb/can/signal.hpp"

using namespace avb::can;
using Bytes = std::array<std::uint8_t, 8>;

// Dogrular: LLR-CAN-001
TEST(CanCerceve, KimlikVeUzunlukSinirlari) {
    Frame f;
    f.id = 0x7FF;
    f.dlc = 8;
    EXPECT_TRUE(f.valid());
    f.id = 0x800;
    EXPECT_FALSE(f.valid());
    f.extended = true;
    EXPECT_TRUE(f.valid());
    f.id = 0x20000000;
    EXPECT_FALSE(f.valid());
    f.id = 1;
    f.dlc = 9;
    EXPECT_FALSE(f.valid());
}

// Dogrular: LLR-CAN-011, LLR-CAN-012
TEST(CanSinyal, IntelBilinenYerlesim) {
    // 12 bit, bit 4'ten baslar: 0xABC -> bayt0 ust yarisi C, bayt1 = AB
    const Signal s{4, 12, Signal::ByteOrder::Intel};
    Bytes d{};
    ASSERT_TRUE(packRaw(s, 0xABC, d));
    EXPECT_EQ(d[0], 0xC0);
    EXPECT_EQ(d[1], 0xAB);
    EXPECT_EQ(unpackRaw(s, d), 0xABC);
}

// Dogrular: LLR-CAN-011, LLR-CAN-012
TEST(CanSinyal, MotorolaBilinenYerlesim) {
    // DBC: baslangic biti 7 (bayt0'in MSB'si), 16 bit -> bayt0 = 0x12, bayt1 = 0x34
    const Signal s{7, 16, Signal::ByteOrder::Motorola};
    Bytes d{};
    ASSERT_TRUE(packRaw(s, 0x1234, d));
    EXPECT_EQ(d[0], 0x12);
    EXPECT_EQ(d[1], 0x34);
    EXPECT_EQ(unpackRaw(s, d), 0x1234);
}

// Dogrular: LLR-CAN-011
TEST(CanSinyal, MotorolaBaytOrtasindanBaslayan) {
    // baslangic 11 (bayt1 bit3), 8 bit: bayt1 bit3..0 + bayt2 bit7..4
    const Signal s{11, 8, Signal::ByteOrder::Motorola};
    Bytes d{};
    ASSERT_TRUE(packRaw(s, 0xA5, d));
    EXPECT_EQ(d[1], 0x0A);
    EXPECT_EQ(d[2], 0x50);
}

// Dogrular: LLR-CAN-011
TEST(CanSinyal, KomsuBitlerKorunur) {
    Bytes d{};
    d.fill(0xFF);
    const Signal s{8, 8, Signal::ByteOrder::Intel};
    ASSERT_TRUE(packRaw(s, 0x00, d));
    EXPECT_EQ(d[0], 0xFF);
    EXPECT_EQ(d[1], 0x00);
    EXPECT_EQ(d[2], 0xFF);
}

// Dogrular: LLR-CAN-012
TEST(CanSinyal, IsaretGenisletme) {
    Signal s{0, 12, Signal::ByteOrder::Intel, true};
    Bytes d{};
    ASSERT_TRUE(packRaw(s, 0xFFF, d));
    EXPECT_EQ(unpackRaw(s, d), -1);
    ASSERT_TRUE(packRaw(s, 0x800, d));
    EXPECT_EQ(unpackRaw(s, d), -2048);
}

// Dogrular: LLR-CAN-010
TEST(CanSinyal, TasanSinyalReddedilir) {
    Bytes d{};
    EXPECT_FALSE(packRaw(Signal{60, 8, Signal::ByteOrder::Intel}, 1, d));
    EXPECT_FALSE(packRaw(Signal{0, 0, Signal::ByteOrder::Intel}, 1, d));
    EXPECT_FALSE(packRaw(Signal{56, 16, Signal::ByteOrder::Motorola}, 1, d));  // bayt7'den sonrasi yok
    EXPECT_TRUE(packRaw(Signal{63, 8, Signal::ByteOrder::Motorola}, 1, d));
    EXPECT_TRUE(packRaw(Signal{0, 64, Signal::ByteOrder::Intel}, 1, d));
}

// Dogrular: LLR-CAN-013
TEST(CanSinyal, FizikselDegerOlcekVeOfset) {
    // irtifa: 0.5 m/bit, -1000 m ofset, 16 bit isaretsiz, 0..30000 m
    const Signal s{0, 16, Signal::ByteOrder::Intel, false, 0.5, -1000.0, 0.0, 30000.0};
    Bytes d{};
    ASSERT_TRUE(encodePhysical(s, 1234.5, d));
    EXPECT_EQ(unpackRaw(s, d), 4469);  // (1234.5 + 1000) / 0.5
    EXPECT_DOUBLE_EQ(decodePhysical(s, d), 1234.5);
    EXPECT_FALSE(encodePhysical(s, 30000.5, d));  // aralik disi
    EXPECT_FALSE(encodePhysical(s, -1.0, d));
}

// Dogrular: LLR-CAN-013
TEST(CanSinyal, HamAlanaSigmayanDegerReddedilir) {
    const Signal s{0, 8, Signal::ByteOrder::Intel, true, 1.0, 0.0};
    Bytes d{};
    EXPECT_TRUE(encodePhysical(s, -128.0, d));
    EXPECT_DOUBLE_EQ(decodePhysical(s, d), -128.0);
    EXPECT_TRUE(encodePhysical(s, 127.0, d));
    EXPECT_FALSE(encodePhysical(s, 128.0, d));
    EXPECT_FALSE(encodePhysical(s, -129.0, d));
    EXPECT_FALSE(encodePhysical(Signal{0, 8, Signal::ByteOrder::Intel, false, 0.0}, 1.0, d));  // olcek 0
}

// Dogrular: LLR-CAN-011, LLR-CAN-012
TEST(CanSinyal, TumKonumVeUzunluklardaGidisDonus) {
    for (int order = 0; order < 2; ++order) {
        for (int len = 1; len <= 32; ++len) {
            for (int start = 0; start < 64; ++start) {
                Signal s;
                s.order = order == 0 ? Signal::ByteOrder::Intel : Signal::ByteOrder::Motorola;
                s.startBit = static_cast<std::uint16_t>(start);
                s.length = static_cast<std::uint8_t>(len);
                if (!s.fits()) {
                    continue;
                }
                const std::uint64_t v = 0x5A5A5A5AULL & ((std::uint64_t{1} << len) - 1);
                Bytes d{};
                ASSERT_TRUE(packRaw(s, v, d));
                ASSERT_EQ(static_cast<std::uint64_t>(unpackRaw(s, d)), v) << "start=" << start << " len=" << len;
            }
        }
    }
}
