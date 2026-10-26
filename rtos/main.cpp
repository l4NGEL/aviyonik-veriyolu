// FreeRTOS üzerinde aviyonik veri akışı ve öncelik tersinmesi deneyi.
//
// Varsayılan kip (--sure N): dört görev N saniye koşar
//   Sensör   (öncelik 3, 100 Hz) INS RT'sinin alt adres verisini günceller
//   VeriYolu (öncelik 4,  50 Hz) BC küçük çerçevesini yürütür (INS -> gösterge RT->RT),
//                                gösterge verisini bütünlük kontrolüyle kuyruğa koyar
//   CAN      (öncelik 2)         kuyruktan alıp sinyalleri CAN çerçevesine paketler, vcan0'a yazar
//   İzleyici (öncelik 1)         süre sonunda vcan0'dan geri okur, ölçütleri değerlendirir
// RT nesneleri iki görevden kullanıldığı için bir FreeRTOS mutex'iyle korunur.
//
// --oncelik: aynı senaryo önce ikili semafor, sonra mutex ile koşar; yüksek
// öncelikli görevin bekleme süresi ölçülür (öncelik kalıtımının etkisi).
//
// Dikkat: POSIX portunda görevler Linux iş parçacıklarıdır; ölçülen sapmalar
// hedef donanımdaki gerçek zamanlı davranışı temsil etmez.

#include <unistd.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include "FreeRTOS.h"
#include "queue.h"
#include "semphr.h"
#include "task.h"

#include "avb/can/signal.hpp"
#include "avb/can/socketcan.hpp"
#include "avb/m1553/bus.hpp"
#include "avb/m1553/bus_controller.hpp"
#include "avb/m1553/remote_terminal.hpp"

using namespace avb;

extern "C" void vAssertCalled(const char* file, unsigned long line) {
    std::fprintf(stderr, "configASSERT: %s:%lu\n", file, line);
    std::fflush(stderr);
    _exit(3);
}

extern "C" void vApplicationMallocFailedHook() {
    std::fprintf(stderr, "FreeRTOS yigini tukendi\n");
    std::fflush(stderr);
    _exit(4);
}

namespace {

std::int64_t simdiUs() {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<std::int64_t>(ts.tv_sec) * 1'000'000 + ts.tv_nsec / 1000;
}

void bitir(int kod) {
    std::fflush(stdout);
    std::fflush(stderr);
    _exit(kod);  // diğer görev iş parçacıkları açıkken exit() yıkıcıları güvenli değil
}

// ------------------------------------------------------------ periyot ölçümü

struct Periyot {
    std::uint32_t tur = 0;
    std::uint32_t kacirilan = 0;     // xTaskDelayUntil gecikmeden döndü: son tarih kaçtı
    std::int64_t maksSapmaUs = 0;    // ardışık iki aktivasyon arası süre - hedef periyot
    std::int64_t onceki = 0;

    void kaydet(BaseType_t gecikti, std::int64_t periyotUs) {
        const std::int64_t t = simdiUs();
        if (gecikti == pdFALSE) {
            ++kacirilan;
        }
        if (onceki != 0) {
            const std::int64_t sapma = std::llabs((t - onceki) - periyotUs);
            if (sapma > maksSapmaUs) {
                maksSapmaUs = sapma;
            }
        }
        onceki = t;
        ++tur;
    }
};

// ------------------------------------------------------------ paylaşılan durum

constexpr std::uint32_t kCanKimlik = 0x101;
const can::Signal kSiraSinyali{0, 16, can::Signal::ByteOrder::Intel};
const can::Signal kYatisSinyali{16, 16, can::Signal::ByteOrder::Intel, true, 0.01, 0.0, -180.0, 180.0};
const can::Signal kYunuslamaSinyali{32, 16, can::Signal::ByteOrder::Intel, true, 0.01, 0.0, -90.0, 90.0};

struct Telemetri {
    std::uint16_t sira;
    std::int16_t yatisCdeg;
    std::int16_t yunuslamaCdeg;
};

struct Sistem {
    m1553::SimClock clock;
    m1553::Bus a{m1553::BusId::A, clock};
    m1553::Bus b{m1553::BusId::B, clock};
    m1553::RemoteTerminal ins{3};
    m1553::RemoteTerminal gosterge{7};
    m1553::BusController bc{clock, a, b};
    m1553::Schedule cizelge{20'000'000};
    SemaphoreHandle_t kilit = nullptr;
    QueueHandle_t kuyruk = nullptr;
    can::SocketCan canTx;
    can::SocketCan canRx;

    Periyot sensor;
    Periyot veriYolu;
    std::uint32_t yirtikVeri = 0;
    std::uint32_t kuyrukDolu = 0;
    std::uint32_t bcHata = 0;
    std::uint32_t kuyrugaGiren = 0;
    std::uint32_t canGonderilen = 0;
    std::uint32_t canGonderimHatasi = 0;
    std::uint16_t sonSira = 0;
    int sureSn = 5;
};

Sistem* gS = nullptr;

std::uint16_t sagla(const std::uint16_t* w) {
    return static_cast<std::uint16_t>(w[0] ^ w[1] ^ w[2] ^ w[3] ^ w[4]);
}

void sensorGorevi(void*) {
    TickType_t son = xTaskGetTickCount();
    std::uint16_t sira = 0;
    for (;;) {
        gS->sensor.kaydet(xTaskDelayUntil(&son, pdMS_TO_TICKS(10)), 10'000);
        ++sira;
        const double t = static_cast<double>(sira) * 0.01;
        std::uint16_t w[6] = {};
        w[0] = sira;
        w[1] = static_cast<std::uint16_t>(static_cast<std::int16_t>(std::lround(3000.0 * std::sin(t))));
        w[2] = static_cast<std::uint16_t>(static_cast<std::int16_t>(std::lround(1500.0 * std::cos(0.5 * t))));
        w[3] = 0x1234;
        w[4] = 0x5678;
        w[5] = sagla(w);
        xSemaphoreTake(gS->kilit, portMAX_DELAY);
        gS->ins.setTransmitData(1, w, 6);
        xSemaphoreGive(gS->kilit);
    }
}

void veriYoluGorevi(void*) {
    TickType_t son = xTaskGetTickCount();
    int kucuk = 0;
    std::uint32_t sonSeq = 0;
    std::array<m1553::Result, 4> sonuc{};
    for (;;) {
        gS->veriYolu.kaydet(xTaskDelayUntil(&son, pdMS_TO_TICKS(20)), 20'000);
        std::uint16_t w[6] = {};
        std::uint32_t seq = 0;
        xSemaphoreTake(gS->kilit, portMAX_DELAY);
        const std::size_t n = gS->bc.runMinorFrame(gS->cizelge, kucuk, sonuc.data(), sonuc.size());
        const std::size_t adet = gS->gosterge.readReceivedData(2, w, 6, &seq);
        xSemaphoreGive(gS->kilit);
        kucuk = (kucuk + 1) % gS->cizelge.majorFrameLength();

        for (std::size_t i = 0; i < n; ++i) {
            if (sonuc[i].outcome != m1553::Outcome::Ok && sonuc[i].outcome != m1553::Outcome::BroadcastSent) {
                ++gS->bcHata;
            }
        }
        if (adet != 6 || seq == sonSeq) {
            continue;
        }
        sonSeq = seq;
        if (sagla(w) != w[5]) {
            ++gS->yirtikVeri;  // RT'ye yazma ile 1553 okuması arasında yarım güncelleme
            continue;
        }
        const Telemetri t{w[0], static_cast<std::int16_t>(w[1]), static_cast<std::int16_t>(w[2])};
        if (xQueueSend(gS->kuyruk, &t, 0) != pdPASS) {
            ++gS->kuyrukDolu;
        } else {
            ++gS->kuyrugaGiren;
        }
    }
}

void canGorevi(void*) {
    for (;;) {
        Telemetri t{};
        if (xQueueReceive(gS->kuyruk, &t, pdMS_TO_TICKS(100)) != pdPASS) {
            continue;
        }
        can::Frame f;
        f.id = kCanKimlik;
        f.dlc = 6;
        bool ok = can::packRaw(kSiraSinyali, t.sira, f.data);
        ok = ok && can::encodePhysical(kYatisSinyali, t.yatisCdeg * 0.01, f.data);
        ok = ok && can::encodePhysical(kYunuslamaSinyali, t.yunuslamaCdeg * 0.01, f.data);
        gS->sonSira = t.sira;
        if (ok && gS->canTx.isOpen() && gS->canTx.send(f)) {
            ++gS->canGonderilen;
        } else if (gS->canTx.isOpen() || !ok) {
            ++gS->canGonderimHatasi;
        }
    }
}

struct CanAlim {
    std::uint32_t alinan = 0;
    std::uint32_t bozuk = 0;
    std::uint16_t sonSira = 0;

    void bosalt(can::SocketCan& rx) {
        can::Frame f;
        while (rx.receive(f, 0) == can::SocketCan::Recv::Ok) {
            const double yatis = can::decodePhysical(kYatisSinyali, f.data);
            if (f.id != kCanKimlik || f.dlc != 6 || std::fabs(yatis) > 30.0) {
                ++bozuk;
            }
            sonSira = static_cast<std::uint16_t>(can::unpackRaw(kSiraSinyali, f.data));
            ++alinan;
        }
    }
};

void izleyiciGorevi(void*) {
    // Soket alım tamponu (varsayılan SO_RCVBUF) ~220 çerçevede dolar ve yenileri
    // düşürür; bu yüzden çerçeveler süre sonunda değil, süre boyunca okunur.
    CanAlim alim;
    const TickType_t bitis = xTaskGetTickCount() + pdMS_TO_TICKS(static_cast<std::uint32_t>(gS->sureSn) * 1000U);
    while (xTaskGetTickCount() < bitis) {
        vTaskDelay(pdMS_TO_TICKS(50));
        if (gS->canRx.isOpen()) {
            alim.bosalt(gS->canRx);
        }
    }
    vTaskSuspendAll();  // ölçüm sırasında diğer görevler durur
    if (gS->canRx.isOpen()) {
        alim.bosalt(gS->canRx);
    }
    const std::uint32_t canAlinan = alim.alinan;
    const std::uint32_t canBozuk = alim.bozuk;
    const std::uint16_t sonAlinanSira = alim.sonSira;

    const Sistem& s = *gS;
    const auto beklenenSensor = static_cast<std::uint32_t>(s.sureSn * 100);
    const auto beklenenVeriYolu = static_cast<std::uint32_t>(s.sureSn * 50);
    std::printf("sure %d sn | sensor 100 Hz: %u tur, %u kacan son tarih, maks periyot sapmasi %lld us\n", s.sureSn,
                s.sensor.tur, s.sensor.kacirilan, static_cast<long long>(s.sensor.maksSapmaUs));
    std::printf("veri yolu 50 Hz: %u tur, %u kacan son tarih, maks periyot sapmasi %lld us, %llu 1553 mesaji, %u hata\n",
                s.veriYolu.tur, s.veriYolu.kacirilan, static_cast<long long>(s.veriYolu.maksSapmaUs),
                static_cast<unsigned long long>(s.bc.stats().messages), s.bcHata);
    std::printf("butunluk: %u yirtik veri | kuyruk: %u giren, %u dolu | CAN (%s): %u gonderilen, %u alinan, %u bozuk, "
                "son sira %u/%u\n",
                s.yirtikVeri, s.kuyrugaGiren, s.kuyrukDolu, s.canTx.isOpen() ? "vcan0" : "kapali", s.canGonderilen,
                canAlinan, canBozuk, sonAlinanSira, s.sonSira);

    bool ok = s.sensor.tur * 100 >= beklenenSensor * 98 && s.veriYolu.tur * 100 >= beklenenVeriYolu * 98;
    ok = ok && s.sensor.kacirilan * 100 <= s.sensor.tur && s.veriYolu.kacirilan * 100 <= s.veriYolu.tur;
    ok = ok && s.yirtikVeri == 0 && s.kuyrukDolu == 0 && s.bcHata == 0 && s.kuyrugaGiren > 0;
    if (s.canTx.isOpen()) {
        ok = ok && canAlinan == s.canGonderilen && canBozuk == 0 && s.canGonderimHatasi == 0 &&
             sonAlinanSira == s.sonSira;
    }
    std::printf("%s\n", ok ? "SONUC: GECTI" : "SONUC: KALDI");
    bitir(ok ? 0 : 1);
}

int gorevKipi(int sureSn) {
    static Sistem s;
    gS = &s;
    s.sureSn = sureSn;
    s.cizelge.add({m1553::Message::modeCodeMsg(m1553::kBroadcastAddress, m1553::mode::kSynchronize), 1, 0});
    s.cizelge.add({m1553::Message::rtToRt(7, 2, 3, 1, 6), 1, 0});
    s.cizelge.add({m1553::Message::rtToBc(3, 4, 2), 2, 1});
    for (auto* rt : {&s.ins, &s.gosterge}) {
        s.a.attach(*rt);
        s.b.attach(*rt);
    }
    const char* ifname = std::getenv("AVB_VCAN") != nullptr ? std::getenv("AVB_VCAN") : "vcan0";
    if (can::SocketCan::interfaceExists(ifname) && s.canTx.open(ifname) && s.canRx.open(ifname)) {
        const std::uint32_t ids[] = {kCanKimlik};
        s.canRx.setFilters(ids, 1, false);
    }
    s.kilit = xSemaphoreCreateMutex();
    s.kuyruk = xQueueCreate(8, sizeof(Telemetri));
    configASSERT(s.kilit != nullptr && s.kuyruk != nullptr);

    xTaskCreate(sensorGorevi, "sensor", 2048, nullptr, 3, nullptr);
    xTaskCreate(veriYoluGorevi, "veriyolu", 4096, nullptr, 4, nullptr);
    xTaskCreate(canGorevi, "can", 2048, nullptr, 2, nullptr);
    xTaskCreate(izleyiciGorevi, "izleyici", 2048, nullptr, 1, nullptr);
    vTaskStartScheduler();
    return 2;  // buraya dönülmez
}

// ------------------------------------------------------- öncelik tersinmesi

struct Deney {
    SemaphoreHandle_t kilit = nullptr;
    SemaphoreHandle_t bitti = nullptr;
    TickType_t yuksekBekleme = 0;
};
Deney gD;

void mesgulBekle(TickType_t tik) {
    const TickType_t t0 = xTaskGetTickCount();
    while (xTaskGetTickCount() - t0 < tik) {
        // işlemciyi bırakmadan çalış: yalnız daha yüksek öncelikli görev kesebilir
    }
}

void dusukGorev(void*) {
    xSemaphoreTake(gD.kilit, portMAX_DELAY);
    mesgulBekle(30);  // kritik bölge
    xSemaphoreGive(gD.kilit);
    xSemaphoreGive(gD.bitti);
    vTaskDelete(nullptr);
}

void ortaGorev(void*) {
    vTaskDelay(10);
    mesgulBekle(100);  // kilitle ilgisi olmayan uzun hesap
    xSemaphoreGive(gD.bitti);
    vTaskDelete(nullptr);
}

void yuksekGorev(void*) {
    vTaskDelay(5);
    const TickType_t t0 = xTaskGetTickCount();
    xSemaphoreTake(gD.kilit, portMAX_DELAY);
    gD.yuksekBekleme = xTaskGetTickCount() - t0;
    xSemaphoreGive(gD.kilit);
    xSemaphoreGive(gD.bitti);
    vTaskDelete(nullptr);
}

TickType_t senaryo(bool mutex) {
    if (mutex) {
        gD.kilit = xSemaphoreCreateMutex();  // öncelik kalıtımı var
    } else {
        gD.kilit = xSemaphoreCreateBinary();  // kalıtım yok
        xSemaphoreGive(gD.kilit);
    }
    gD.bitti = xSemaphoreCreateCounting(3, 0);
    xTaskCreate(dusukGorev, "dusuk", 2048, nullptr, 1, nullptr);
    xTaskCreate(ortaGorev, "orta", 2048, nullptr, 2, nullptr);
    xTaskCreate(yuksekGorev, "yuksek", 2048, nullptr, 3, nullptr);
    for (int i = 0; i < 3; ++i) {
        if (xSemaphoreTake(gD.bitti, pdMS_TO_TICKS(5000)) != pdPASS) {
            std::printf("zaman asimi: senaryo bitmedi\n");
            bitir(1);
        }
    }
    vTaskDelay(5);  // silinen görevlerin belleği boşta görevde geri alınır
    vSemaphoreDelete(gD.kilit);
    vSemaphoreDelete(gD.bitti);
    return gD.yuksekBekleme;
}

void deneyGorevi(void*) {
    const TickType_t semafor = senaryo(false);
    const TickType_t mutex = senaryo(true);
    std::printf("yuksek oncelikli gorevin kilit beklemesi: ikili semafor %u ms, mutex (oncelik kalitimi) %u ms\n",
                static_cast<unsigned>(semafor), static_cast<unsigned>(mutex));
    // Semaforda orta görev düşük görevi 100 ms boyunca keser; mutex'te düşük görev
    // yüksek önceliği devralır ve kritik bölgeyi ~25 ms'de bitirir.
    const bool ok = semafor >= 90 && mutex <= 40;
    std::printf("%s\n", ok ? "SONUC: GECTI" : "SONUC: KALDI");
    bitir(ok ? 0 : 1);
}

int oncelikKipi() {
    xTaskCreate(deneyGorevi, "deney", 2048, nullptr, 4, nullptr);
    vTaskStartScheduler();
    return 2;
}

}  // namespace

int main(int argc, char** argv) {
    int sure = 5;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--oncelik") == 0) {
            return oncelikKipi();
        }
        if (std::strcmp(argv[i], "--sure") == 0 && i + 1 < argc) {
            sure = std::atoi(argv[++i]);
        }
    }
    return gorevKipi(sure < 1 ? 1 : sure);
}
