// Kararli durumda dinamik bellek ayrilmadigini dogrular (LLR-GEN-001).
//
// Global operator new sayac tutar. Kurulum (nesnelerin olusturulmasi) bittikten
// sonra 1000 kucuk cerceve boyunca BC/RT/monitor/ISO-TP yollari kosulur ve
// sayacin degismedigi kontrol edilir. Gomulu hedeflerde yigin parcalanmasi ve
// belirsiz sureli ayirma kabul edilmez; bu test o tasarim kararinin kaniti.
// Dogrular: LLR-GEN-001

#include <cstdio>
#include <cstdlib>
#include <new>

#include "avb/can/isotp.hpp"
#include "avb/m1553/bus.hpp"
#include "avb/m1553/bus_controller.hpp"
#include "avb/m1553/remote_terminal.hpp"

namespace {
std::size_t gAllocations = 0;
}

void* operator new(std::size_t n) {
    ++gAllocations;
    if (void* p = std::malloc(n == 0 ? 1 : n)) {
        return p;
    }
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }

namespace {
bool cerceveYut(void*, const avb::can::Frame&) { return true; }
}  // namespace

int main() {
    using namespace avb::m1553;
    // ---- kurulum: burada ayirma serbest
    static SimClock clock;
    static BusMonitor monitor;
    static FaultInjector faults;
    static Bus a(BusId::A, clock, &monitor, &faults);
    static Bus b(BusId::B, clock, &monitor, &faults);
    static RemoteTerminal rt1(1);
    static RemoteTerminal rt2(2);
    for (auto* rt : {&rt1, &rt2}) {
        a.attach(*rt);
        b.attach(*rt);
    }
    static BusController bc(clock, a, b);
    static Schedule s(20'000'000);
    const std::uint16_t veri[4] = {1, 2, 3, 4};
    s.add({Message::bcToRt(1, 1, veri, 4), 1, 0});
    s.add({Message::rtToBc(2, 2, 8), 2, 0});
    s.add({Message::rtToRt(1, 3, 2, 4, 6), 4, 1});
    s.add({Message::modeCodeMsg(kBroadcastAddress, mode::kSynchronize), 8, 3});
    faults.setRandomHalfBitErrorRate(1e-3, 7);  // hata ve yeniden deneme yollari da kossun
    static avb::can::isotp::Sender tp({0x700, 0x708, false, true}, &cerceveYut, nullptr);
    static std::uint8_t yuk[200] = {};

    const std::size_t once = gAllocations;
    std::array<Result, 8> sonuc{};
    for (int k = 0; k < 1000; ++k) {
        bc.runMinorFrame(s, k % s.majorFrameLength(), sonuc.data(), sonuc.size());
        rt1.setTransmitData(5, veri, 4);
        tp.reset();
        tp.start(yuk, sizeof yuk, k);
    }
    const std::size_t fark = gAllocations - once;
    std::printf("kararli durum: %zu kucuk cerceve, %llu mesaj, %zu dinamik ayirma\n", static_cast<std::size_t>(1000),
                static_cast<unsigned long long>(bc.stats().messages), fark);
    return fark == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
