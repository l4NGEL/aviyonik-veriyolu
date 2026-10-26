#pragma once
// Çift yedekli (A/B) 1553 veri yolunun ayrık zamanlı modeli.
//
// Gerçek zaman yerine bir benzetim saati ilerletilir: her kelime 20 µs, RT
// yanıt süresi 4-12 µs, BC yanıt bekleme süresi 14 µs. Bir vericinin koyduğu
// her kelime Manchester olarak kodlanır, hata enjektöründen geçer, yeniden
// çözülür ve hattaki diğer tüm terminallere aynı biçimde ulaşır (tek hat, çok
// dinleyici). Bu dosyadaki hiçbir yol kararlı durumda dinamik bellek ayırmaz.

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

#include "avb/m1553/manchester.hpp"
#include "avb/m1553/word.hpp"

namespace avb::m1553 {

class RemoteTerminal;

enum class BusId : std::uint8_t { A = 0, B = 1 };
constexpr BusId other(BusId b) { return b == BusId::A ? BusId::B : BusId::A; }
const char* toString(BusId b);

// Zamanlama sabitleri (ns).  LLR-1553-010
struct Timing {
    static constexpr std::int64_t kWordNs = 20'000;
    static constexpr std::int64_t kMinResponseNs = 4'000;
    static constexpr std::int64_t kMaxResponseNs = 12'000;
    static constexpr std::int64_t kNoResponseTimeoutNs = 14'000;
    static constexpr std::int64_t kMinIntermessageGapNs = 4'000;
};

class SimClock {
public:
    std::int64_t nowNs() const { return now_; }
    void advance(std::int64_t ns) { now_ += ns; }
    void advanceTo(std::int64_t t) {
        if (t > now_) {
            now_ = t;
        }
    }

private:
    std::int64_t now_ = 0;
};

// Kelimeyi kim koydu: BC ya da adresli RT.
struct Source {
    bool isBc = true;
    std::uint8_t rt = 0;

    static Source bc() { return {true, 0}; }
    static Source terminal(std::uint8_t a) { return {false, a}; }
};

// Veri yolu izleyicisi (bus monitor): sabit kapasiteli halka tampon.  LLR-1553-040
struct MonitorEntry {
    std::int64_t timeNs = 0;
    BusId bus = BusId::A;
    Source source;
    bool dropped = false;
    DecodeResult decoded;
};

class BusMonitor {
public:
    static constexpr std::size_t kCapacity = 2048;

    void record(const MonitorEntry& e);
    std::size_t size() const { return count_; }
    std::uint64_t totalRecorded() const { return total_; }
    const MonitorEntry& at(std::size_t i) const;  // 0 = en eski
    void clear();

    std::string format(std::size_t i) const;  // tanı amaçlı; bellek ayırır

private:
    std::array<MonitorEntry, kCapacity> ring_{};
    std::size_t head_ = 0;
    std::size_t count_ = 0;
    std::uint64_t total_ = 0;
};

// Hata enjektörü: belirli kelimeleri bozar ya da düşürür.  LLR-1553-050
class FaultInjector {
public:
    enum class Action : std::uint8_t { FlipHalfBit, CorruptSync, Drop };

    struct Rule {
        BusId bus = BusId::A;
        bool fromBc = true;
        std::uint8_t rt = 0;           // fromBc=false ise vericinin adresi
        int wordIndex = 0;             // iletim içindeki sıra (0 = ilk kelime)
        Action action = Action::FlipHalfBit;
        int halfBit = 10;              // FlipHalfBit için
        int remaining = 1;             // kaç kez uygulanacak (<0: sınırsız)
    };

    static constexpr std::size_t kMaxRules = 16;

    bool add(const Rule& r);
    void clear();

    // Rastgele yarım bit hatası (iletilen her yarım bit için olasılık). Tekrarlanabilir tohum.
    void setRandomHalfBitErrorRate(double p, std::uint32_t seed);

    struct Effect {
        HalfBits halfBits = 0;
        bool dropped = false;
    };
    Effect apply(BusId bus, Source src, int wordIndex, HalfBits hb);

    std::uint64_t injectedCount() const { return injected_; }

private:
    std::uint32_t nextRandom();

    std::array<Rule, kMaxRules> rules_{};
    std::size_t count_ = 0;
    double halfBitErrorRate_ = 0.0;
    std::uint32_t rng_ = 1;
    std::uint64_t injected_ = 0;
};

class Bus {
public:
    static constexpr std::size_t kMaxTerminals = 31;

    Bus(BusId id, SimClock& clock, BusMonitor* monitor = nullptr, FaultInjector* faults = nullptr);

    BusId id() const { return id_; }
    bool attach(RemoteTerminal& rt);

    // Mesaj başlangıcı: önceki mesajı yarım bırakan terminaller zaman aşımına düşer.
    void startOfMessage();

    // Tek kelimeyi hatta koyar, 20 µs ilerletir. Düşürülürse ok()=false ve dropped=true döner.
    struct Heard {
        bool dropped = false;
        DecodeResult decoded;
    };
    Heard transmit(const Word& w, Source src, int wordIndex);

    // Verici sustu (hat boşluğu): terminaller mesaj sonunu buradan anlar.
    void endOfTransmission(Source src);

    // Bu hatta yanıt bekleyen terminal; birden fazlaysa çakışma.
    RemoteTerminal* responder(bool& collision) const;

private:
    BusId id_;
    SimClock& clock_;
    BusMonitor* monitor_;
    FaultInjector* faults_;
    std::array<RemoteTerminal*, kMaxTerminals> terminals_{};
    std::size_t count_ = 0;
};

}  // namespace avb::m1553
