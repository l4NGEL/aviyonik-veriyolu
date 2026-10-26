#include <gtest/gtest.h>

#include <vector>

#include "m1553_fixture.hpp"

using namespace avb::m1553;

// Dogrular: LLR-1553-012, LLR-1553-021, LLR-1553-010
TEST_F(VeriYolu, BcdenRtyeVeriSaklanirVeSureStandartta) {
    const std::uint16_t data[] = {0x1111, 0x2222};
    const Result r = bc.execute(Message::bcToRt(5, 3, data, 2));

    EXPECT_EQ(r.outcome, Outcome::Ok);
    EXPECT_EQ(r.status.rt, 5);
    EXPECT_FALSE(r.status.messageError);
    std::uint32_t seq = 0;
    EXPECT_EQ(received(rt5, 3, &seq), (std::vector<std::uint16_t>{0x1111, 0x2222}));
    EXPECT_EQ(seq, 1U);
    // komut + 2 veri (60 us) + RT yanit suresi 8 us + durum 20 us
    EXPECT_EQ(r.endNs - r.startNs, 88'000);
    EXPECT_TRUE(received(rt6, 3).empty());  // adreslenmeyen RT dokunmaz  LLR-1553-020
}

// Dogrular: LLR-1553-012, LLR-1553-021
TEST_F(VeriYolu, RtdenBcyeAltAdresVerisiDoner) {
    const std::uint16_t data[] = {7, 8, 9};
    ASSERT_TRUE(rt6.setTransmitData(4, data, 3));
    const Result r = bc.execute(Message::rtToBc(6, 4, 3));

    EXPECT_EQ(r.outcome, Outcome::Ok);
    ASSERT_EQ(r.dataCount, 3);
    EXPECT_EQ(r.data[0], 7);
    EXPECT_EQ(r.data[2], 9);
    EXPECT_EQ(r.endNs - r.startNs, 20'000 + 8'000 + 4 * 20'000);
}

// Dogrular: LLR-1553-021
TEST_F(VeriYolu, AltAdresSinirlariReddedilir) {
    const std::uint16_t d[33] = {};
    EXPECT_FALSE(rt5.setTransmitData(0, d, 1));
    EXPECT_FALSE(rt5.setTransmitData(31, d, 1));
    EXPECT_FALSE(rt5.setTransmitData(1, d, 33));
    EXPECT_FALSE(rt5.setTransmitData(1, nullptr, 2));
    EXPECT_TRUE(rt5.setTransmitData(30, d, 32));
}

// Dogrular: LLR-1553-026
TEST_F(VeriYolu, RtdenRtyeAktarim) {
    const std::uint16_t data[] = {0xAAAA, 0xBBBB, 0xCCCC, 0xDDDD};
    ASSERT_TRUE(rt6.setTransmitData(2, data, 4));
    const Result r = bc.execute(Message::rtToRt(5, 7, 6, 2, 4));

    EXPECT_EQ(r.outcome, Outcome::Ok);
    EXPECT_EQ(r.txStatus.rt, 6);
    EXPECT_EQ(r.status.rt, 5);
    EXPECT_EQ(received(rt5, 7), (std::vector<std::uint16_t>{0xAAAA, 0xBBBB, 0xCCCC, 0xDDDD}));
}

// Dogrular: LLR-1553-026
TEST_F(VeriYolu, RtdenRtyeGonderenSusarsaAliciMesajiGecersizSayar) {
    rt6.setSilent(true);
    bc.setMaxRetries(0);
    const Result r = bc.execute(Message::rtToRt(5, 7, 6, 2, 4));
    EXPECT_EQ(r.outcome, Outcome::NoResponse);
    EXPECT_TRUE(received(rt5, 7).empty());
    EXPECT_TRUE(statusOf(5).messageError);  // sonraki mesaj sinirinda zaman asimi
}

// Dogrular: LLR-1553-024, LLR-1553-032
TEST_F(VeriYolu, YayinYanitsizSaklanirVeBcrBitiDurumdaGorunur) {
    const std::uint16_t data[] = {0x0042};
    const Result r = bc.execute(Message::bcToRt(kBroadcastAddress, 9, data, 1));

    EXPECT_EQ(r.outcome, Outcome::BroadcastSent);
    EXPECT_EQ(received(rt5, 9), (std::vector<std::uint16_t>{0x0042}));
    EXPECT_EQ(received(rt6, 9), (std::vector<std::uint16_t>{0x0042}));
    // yalniz BC kelimeleri hatta: durum kelimesi yok
    EXPECT_EQ(monitor.size(), 2U);

    EXPECT_TRUE(statusOf(5).broadcastReceived);
    EXPECT_TRUE(statusOf(5).broadcastReceived);  // durum istegi biti silmez
    bc.execute(Message::rtToBc(5, 1, 1));
    EXPECT_FALSE(statusOf(5).broadcastReceived);  // gecerli yeni komut siler
}

// Dogrular: LLR-1553-022, LLR-1553-014
TEST_F(VeriYolu, BozukVeriKelimesindeYanitBastirilirBHattindaTekrar) {
    // A hattinda BC'nin 2. veri kelimesini (iletimdeki 3. kelime) boz
    faults.add({BusId::A, true, 0, 2, FaultInjector::Action::FlipHalfBit, 20, 1});
    const std::uint16_t data[] = {1, 2, 3};
    const Result r = bc.execute(Message::bcToRt(5, 1, data, 3));

    EXPECT_EQ(r.outcome, Outcome::Ok);
    EXPECT_EQ(r.retries, 1);
    EXPECT_EQ(r.busUsed, BusId::B);
    EXPECT_EQ(bc.stats().recoveredOnAlternateBus, 1U);
    EXPECT_EQ(rt5.invalidMessageCount(), 1U);
    EXPECT_EQ(received(rt5, 1), (std::vector<std::uint16_t>{1, 2, 3}));
    // B'deki gecerli mesaj ME'yi temizledi
    EXPECT_FALSE(statusOf(5).messageError);
}

// Dogrular: LLR-1553-022, LLR-1553-032
TEST_F(VeriYolu, TekrarYoksaYanitYokVeMeSonrakiDurumIsteginde) {
    bc.setMaxRetries(0);
    faults.add({BusId::A, true, 0, 1, FaultInjector::Action::FlipHalfBit, 9, 1});
    const std::uint16_t data[] = {1, 2};
    const Result r = bc.execute(Message::bcToRt(5, 1, data, 2));
    EXPECT_EQ(r.outcome, Outcome::NoResponse);
    EXPECT_TRUE(received(rt5, 1).empty());
    EXPECT_TRUE(statusOf(5).messageError);
}

// Dogrular: LLR-1553-022
TEST_F(VeriYolu, EksikVeriKelimesiGecersizMesajdir) {
    // Komut 4 kelime diyor, BC 3 kelime gonderip susuyor
    busA.startOfMessage();
    busA.transmit({Sync::CommandStatus, CommandWord{5, false, 2, 4}.encode()}, Source::bc(), 0);
    for (int i = 0; i < 3; ++i) {
        busA.transmit({Sync::Data, 0x5555}, Source::bc(), i + 1);
    }
    busA.endOfTransmission(Source::bc());
    bool collision = false;
    EXPECT_EQ(busA.responder(collision), nullptr);
    EXPECT_EQ(rt5.invalidMessageCount(), 1U);
    EXPECT_TRUE(received(rt5, 2).empty());
}

// Dogrular: LLR-1553-022
TEST_F(VeriYolu, FazlaVeriKelimesiGecersizMesajdir) {
    busA.startOfMessage();
    busA.transmit({Sync::CommandStatus, CommandWord{5, false, 2, 2}.encode()}, Source::bc(), 0);
    for (int i = 0; i < 3; ++i) {
        busA.transmit({Sync::Data, 0x5555}, Source::bc(), i + 1);
    }
    busA.endOfTransmission(Source::bc());
    bool collision = false;
    EXPECT_EQ(busA.responder(collision), nullptr);
    EXPECT_EQ(rt5.invalidMessageCount(), 1U);
}

// Dogrular: LLR-1553-023
TEST_F(VeriYolu, YeniMesajBaslarkenYarimKalanMesajZamanAsimina) {
    busA.startOfMessage();
    busA.transmit({Sync::CommandStatus, CommandWord{5, false, 2, 2}.encode()}, Source::bc(), 0);
    // BC susmadan yeni mesaj sinirina gecildi
    busA.startOfMessage();
    EXPECT_EQ(rt5.invalidMessageCount(), 1U);
}

// Dogrular: LLR-1553-011, LLR-1553-014
TEST_F(VeriYolu, SusanRtIkiHattaDaYanitYok) {
    rt5.setSilent(true);
    const Result r = bc.execute(Message::rtToBc(5, 1, 1));
    EXPECT_EQ(r.outcome, Outcome::NoResponse);
    EXPECT_EQ(r.retries, 1);
    EXPECT_EQ(r.busUsed, BusId::B);
    EXPECT_EQ(bc.stats().noResponse, 1U);
    // komut 20 us + 14 us bekleme
    EXPECT_EQ(r.endNs - r.startNs, 34'000);
}

// Dogrular: LLR-1553-011
TEST_F(VeriYolu, BcYanitBeklemeSuresiSiniri) {
    bc.setMaxRetries(0);
    rt5.setResponseDelayNs(14'000);  // sinirda: kabul
    EXPECT_EQ(bc.execute(Message::rtToBc(5, 1, 1)).outcome, Outcome::Ok);
    rt5.setResponseDelayNs(14'001);  // gec: yok sayilir
    EXPECT_EQ(bc.execute(Message::rtToBc(5, 1, 1)).outcome, Outcome::NoResponse);
}

// Dogrular: LLR-1553-013, LLR-1553-014
TEST_F(VeriYolu, BozukDurumKelimesiYanitHatasiVeTekrar) {
    faults.add({BusId::A, false, 5, 0, FaultInjector::Action::FlipHalfBit, 15, 1});
    const Result r = bc.execute(Message::rtToBc(5, 1, 2));
    EXPECT_EQ(r.outcome, Outcome::Ok);
    EXPECT_EQ(r.retries, 1);
}

// Dogrular: LLR-1553-013
TEST_F(VeriYolu, DusenVeriKelimesiYanitHatasi) {
    bc.setMaxRetries(0);
    faults.add({BusId::A, false, 5, 2, FaultInjector::Action::Drop, 0, 1});
    const Result r = bc.execute(Message::rtToBc(5, 1, 3));
    EXPECT_EQ(r.outcome, Outcome::ResponseError);
    EXPECT_EQ(r.dataCount, 2);
}

// Dogrular: LLR-1553-013
TEST_F(VeriYolu, AyniAdresliIkiRtCakisir) {
    RemoteTerminal ikiz{5};
    busA.attach(ikiz);
    bc.setMaxRetries(0);
    EXPECT_EQ(bc.execute(Message::rtToBc(5, 1, 1)).outcome, Outcome::ResponseError);
}

// Dogrular: LLR-1553-028
TEST_F(VeriYolu, MesgulRtVeriVermezVeAlmaz) {
    rt5.setBusy(true);
    const Result tx = bc.execute(Message::rtToBc(5, 1, 4));
    EXPECT_EQ(tx.outcome, Outcome::Busy);
    EXPECT_EQ(tx.dataCount, 0);
    const std::uint16_t data[] = {1};
    EXPECT_EQ(bc.execute(Message::bcToRt(5, 2, data, 1)).outcome, Outcome::Busy);
    EXPECT_TRUE(received(rt5, 2).empty());
}

// Dogrular: LLR-1553-007
TEST_F(VeriYolu, AltSistemBayraklariDurumdaGorunur) {
    rt5.setServiceRequest(true);
    rt5.setSubsystemFlag(true);
    rt5.setTerminalFlag(true);
    const StatusWord s = bc.execute(Message::rtToBc(5, 1, 1)).status;
    EXPECT_TRUE(s.serviceRequest);
    EXPECT_TRUE(s.subsystemFlag);
    EXPECT_TRUE(s.terminalFlag);
}

// Dogrular: LLR-1553-025
TEST_F(VeriYolu, YayinaGonderKomutuGecersiz) {
    busA.startOfMessage();
    busA.transmit({Sync::CommandStatus, CommandWord{kBroadcastAddress, true, 1, 1}.encode()}, Source::bc(), 0);
    busA.endOfTransmission(Source::bc());
    bool collision = false;
    EXPECT_EQ(busA.responder(collision), nullptr);
    EXPECT_TRUE(statusOf(5).messageError);
    EXPECT_TRUE(statusOf(6).messageError);
}

// ------------------------------------------------------------- mod kodlari

// Dogrular: LLR-1553-031
TEST_F(VeriYolu, SenkronizasyonModKodlari) {
    EXPECT_EQ(bc.execute(Message::modeCodeMsg(5, mode::kSynchronize)).outcome, Outcome::Ok);
    EXPECT_EQ(rt5.syncCount(), 1U);
    EXPECT_EQ(bc.execute(Message::modeCodeMsg(5, mode::kSynchronizeWithData, 0xBEEF)).outcome, Outcome::Ok);
    EXPECT_EQ(rt5.syncCount(), 2U);
    EXPECT_EQ(rt5.syncData(), 0xBEEF);
    // yayin senkronu: yanit yok, iki RT de sayar
    EXPECT_EQ(bc.execute(Message::modeCodeMsg(kBroadcastAddress, mode::kSynchronize)).outcome, Outcome::BroadcastSent);
    EXPECT_EQ(rt5.syncCount(), 3U);
    EXPECT_EQ(rt6.syncCount(), 1U);
}

// Dogrular: LLR-1553-031
TEST_F(VeriYolu, VektorVeBitKelimesi) {
    rt5.setVectorWord(0x0F0F);
    rt5.setBitWord(0x8001);
    const Result v = bc.execute(Message::modeCodeMsg(5, mode::kTransmitVectorWord));
    ASSERT_EQ(v.outcome, Outcome::Ok);
    EXPECT_EQ(v.data[0], 0x0F0F);
    const Result b = bc.execute(Message::modeCodeMsg(5, mode::kTransmitBitWord));
    ASSERT_EQ(b.outcome, Outcome::Ok);
    EXPECT_EQ(b.data[0], 0x8001);
}

// Dogrular: LLR-1553-033
TEST_F(VeriYolu, SonKomutuGonderOncekiKomutuDondururVeDegistirmez) {
    bc.execute(Message::rtToBc(5, 12, 3));
    const std::uint16_t expected = CommandWord{5, true, 12, 3}.encode();
    for (int i = 0; i < 2; ++i) {
        const Result r = bc.execute(Message::modeCodeMsg(5, mode::kTransmitLastCommand));
        ASSERT_EQ(r.outcome, Outcome::Ok);
        EXPECT_EQ(r.data[0], expected);
    }
}

// Dogrular: LLR-1553-030
TEST_F(VeriYolu, DesteklenmeyenModKoduGecersizKomut) {
    const Result r = bc.execute(Message::modeCodeMsg(5, 3));  // kendi kendini test baslat: desteklenmiyor
    EXPECT_EQ(r.outcome, Outcome::MessageError);
    EXPECT_TRUE(r.status.messageError);
    EXPECT_EQ(rt5.invalidMessageCount(), 1U);
}

// Dogrular: LLR-1553-030
TEST_F(VeriYolu, YayindaVeriDonduruculModKoduGecersiz) {
    EXPECT_EQ(bc.execute(Message::modeCodeMsg(kBroadcastAddress, mode::kTransmitVectorWord)).outcome,
              Outcome::BroadcastSent);
    EXPECT_TRUE(statusOf(5).messageError);
}

// Dogrular: LLR-1553-030
TEST_F(VeriYolu, ModKoduYanlisYonluKomutGecersiz) {
    // 17 (veriyle senkron) alma yonlu olmali; gonder yonlu gelirse gecersiz
    busA.startOfMessage();
    busA.transmit({Sync::CommandStatus, CommandWord{5, true, 31, mode::kSynchronizeWithData}.encode()}, Source::bc(), 0);
    busA.endOfTransmission(Source::bc());
    bool collision = false;
    RemoteTerminal* rt = busA.responder(collision);
    ASSERT_EQ(rt, &rt5);
    EXPECT_TRUE(StatusWord::decode(rt5.response().words[0].value).messageError);
}

// Dogrular: LLR-1553-034
TEST_F(VeriYolu, VericiKapatmaDigerHattiSusturur) {
    bc.setMaxRetries(0);
    Message kapat = Message::modeCodeMsg(5, mode::kTransmitterShutdown);
    kapat.bus = BusId::A;
    ASSERT_EQ(bc.execute(kapat).outcome, Outcome::Ok);
    EXPECT_FALSE(rt5.transmitterEnabled(BusId::B));

    Message uzerinden = Message::rtToBc(5, 1, 1);
    uzerinden.bus = BusId::B;
    EXPECT_EQ(bc.execute(uzerinden).outcome, Outcome::NoResponse);

    Message ac = Message::modeCodeMsg(5, mode::kOverrideTransmitterShutdown);
    ASSERT_EQ(bc.execute(ac).outcome, Outcome::Ok);
    EXPECT_EQ(bc.execute(uzerinden).outcome, Outcome::Ok);
}

// Dogrular: LLR-1553-035
TEST_F(VeriYolu, RtSifirlamaVericileriAcarVeYanitVerir) {
    bc.execute(Message::modeCodeMsg(5, mode::kTransmitterShutdown));
    const Result r = bc.execute(Message::modeCodeMsg(5, mode::kResetRemoteTerminal));
    EXPECT_EQ(r.outcome, Outcome::Ok);
    EXPECT_EQ(rt5.resetCount(), 1U);
    EXPECT_TRUE(rt5.transmitterEnabled(BusId::B));
}

// Dogrular: LLR-1553-040
TEST_F(VeriYolu, MonitorHerKelimeyiZamanVeKaynakIleKaydeder) {
    const std::uint16_t data[] = {0xCAFE};
    bc.execute(Message::bcToRt(6, 1, data, 1));
    ASSERT_EQ(monitor.size(), 3U);  // komut, veri, durum
    EXPECT_TRUE(monitor.at(0).source.isBc);
    EXPECT_EQ(monitor.at(1).decoded.word.value, 0xCAFE);
    EXPECT_FALSE(monitor.at(2).source.isBc);
    EXPECT_EQ(monitor.at(2).source.rt, 6);
    EXPECT_LT(monitor.at(0).timeNs, monitor.at(2).timeNs);
    EXPECT_NE(monitor.format(0).find("KMT/DRM"), std::string::npos);
    EXPECT_NE(monitor.format(1).find("0xCAFE"), std::string::npos);
}

// Dogrular: LLR-1553-040
TEST(Monitor, HalkaTamponTasincaEnEskiyiSiler) {
    BusMonitor m;
    for (std::size_t i = 0; i < BusMonitor::kCapacity + 5; ++i) {
        MonitorEntry e;
        e.timeNs = static_cast<std::int64_t>(i);
        m.record(e);
    }
    EXPECT_EQ(m.size(), BusMonitor::kCapacity);
    EXPECT_EQ(m.totalRecorded(), BusMonitor::kCapacity + 5);
    EXPECT_EQ(m.at(0).timeNs, 5);
}
