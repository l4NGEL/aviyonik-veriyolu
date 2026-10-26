// 1553 veri yolu izi: 50 Hz küçük çerçeveli bir çizelge, A hattında enjekte
// edilen hatalar ve BC'nin B hattında kurtarması. Çıktı bir veri yolu
// izleyicisinin (bus monitor) göreceği kelime akışıdır.

#include <cstdio>

#include "avb/m1553/bus.hpp"
#include "avb/m1553/bus_controller.hpp"
#include "avb/m1553/remote_terminal.hpp"

using namespace avb::m1553;

int main() {
    SimClock clock;
    BusMonitor monitor;
    FaultInjector faults;
    Bus a(BusId::A, clock, &monitor, &faults);
    Bus b(BusId::B, clock, &monitor, &faults);
    RemoteTerminal ins(3);   // ataletsel seyrüsefer
    RemoteTerminal disp(7);  // gösterge bilgisayarı
    for (auto* rt : {&ins, &disp}) {
        a.attach(*rt);
        b.attach(*rt);
    }
    BusController bc(clock, a, b);

    const std::uint16_t konum[6] = {0x3E8A, 0x0123, 0x1C20, 0x0456, 0x0BB8, 0x0002};
    ins.setTransmitData(1, konum, 6);

    Schedule s(20'000'000);
    s.add({Message::modeCodeMsg(kBroadcastAddress, mode::kSynchronize), 1, 0});
    s.add({Message::rtToRt(7, 2, 3, 1, 6), 1, 0});  // INS -> gösterge, 50 Hz
    s.add({Message::rtToBc(3, 4, 2), 2, 1});        // INS durum, 25 Hz
    const Schedule::Check c = s.validate();
    std::printf("cizelge: buyuk cerceve %d kucuk cerceve, en kotu doluluk %%%.2f (kucuk cerceve %d) -> %s\n\n",
                s.majorFrameLength(), c.worstUtilization * 100.0, c.worstMinor, c.ok ? "SIGAR" : "SIGMAZ");

    // 2. küçük çerçevede A hattında INS'in ilk veri kelimesini boz
    faults.add({BusId::A, false, 3, 1, FaultInjector::Action::FlipHalfBit, 21, 1});
    // 3. küçük çerçevede A hattında gösterge susar
    std::array<Result, 8> res{};
    for (int k = 0; k < 4; ++k) {
        if (k == 3) {
            disp.setSilent(true);
        }
        const std::size_t before = monitor.size();
        const std::size_t n = bc.runMinorFrame(s, k, res.data(), res.size());
        std::printf("--- kucuk cerceve %d ---\n", k);
        for (std::size_t i = before; i < monitor.size(); ++i) {
            std::printf("%s\n", monitor.format(i).c_str());
        }
        for (std::size_t i = 0; i < n; ++i) {
            std::printf("  sonuc: %-18s hat %s, tekrar %d\n", toString(res[i].outcome), toString(res[i].busUsed),
                        res[i].retries);
        }
        disp.setSilent(false);
    }
    const BcStats& st = bc.stats();
    std::printf("\nozet: %llu mesaj, %llu tamam, %llu yanit-yok, %llu yanit-hatali, %llu B hattinda kurtarildi\n",
                static_cast<unsigned long long>(st.messages), static_cast<unsigned long long>(st.ok),
                static_cast<unsigned long long>(st.noResponse), static_cast<unsigned long long>(st.responseError),
                static_cast<unsigned long long>(st.recoveredOnAlternateBus));
    return 0;
}
