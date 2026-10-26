#pragma once
// Veri yolu denetleyicisi (BC) ve çerçeve zamanlayıcısı.
//
// BC mesajı seçili hatta yürütür, yanıtı doğrular; yanıt gelmez ya da bozuk
// gelirse yedek hatta yeniden dener. Zamanlayıcı mesajları küçük çerçevelere
// (ör. 20 ms / 50 Hz) yerleştirir; her mesaj N küçük çerçevede bir koşar.

#include <array>
#include <cstddef>
#include <cstdint>

#include "avb/m1553/bus.hpp"
#include "avb/m1553/word.hpp"

namespace avb::m1553 {

struct Message {
    enum class Kind : std::uint8_t { BcToRt, RtToBc, RtToRt, ModeCode };

    Kind kind = Kind::BcToRt;
    std::uint8_t rt = 1;          // RtToRt'de alıcı RT
    std::uint8_t subaddress = 1;  // RtToRt'de alıcı alt adres
    std::uint8_t wordCount = 1;   // 1..32
    std::uint8_t txRt = 2;        // yalnız RtToRt: gönderen RT
    std::uint8_t txSubaddress = 1;
    std::uint8_t modeCode = 0;    // yalnız ModeCode
    BusId bus = BusId::A;
    std::array<std::uint16_t, kMaxDataWords> data{};  // BcToRt verisi ya da mod kodu veri kelimesi

    static Message bcToRt(std::uint8_t rt, std::uint8_t sa, const std::uint16_t* d, std::uint8_t n);
    static Message rtToBc(std::uint8_t rt, std::uint8_t sa, std::uint8_t n);
    static Message rtToRt(std::uint8_t rxRt, std::uint8_t rxSa, std::uint8_t txRt, std::uint8_t txSa, std::uint8_t n);
    static Message modeCodeMsg(std::uint8_t rt, std::uint8_t code, std::uint16_t dataWord = 0);
};

enum class Outcome : std::uint8_t {
    Ok,
    BroadcastSent,  // yayın: yanıt beklenmez
    NoResponse,
    ResponseError,  // bozuk kelime, eksik/fazla veri, çakışma
    AddressMismatch,
    MessageError,   // RT durum kelimesinde ME
    Busy,
};
const char* toString(Outcome o);

struct Result {
    Outcome outcome = Outcome::NoResponse;
    BusId busUsed = BusId::A;
    int retries = 0;
    StatusWord status;       // RtToRt'de alıcının durumu
    StatusWord txStatus;     // yalnız RtToRt: gönderenin durumu
    std::array<std::uint16_t, kMaxDataWords> data{};
    std::uint8_t dataCount = 0;
    std::int64_t startNs = 0;
    std::int64_t endNs = 0;
};

struct BcStats {
    std::uint64_t messages = 0;
    std::uint64_t ok = 0;
    std::uint64_t noResponse = 0;
    std::uint64_t responseError = 0;
    std::uint64_t messageError = 0;
    std::uint64_t busy = 0;
    std::uint64_t retries = 0;
    std::uint64_t recoveredOnAlternateBus = 0;
    std::uint64_t frameOverruns = 0;
};

// En kötü durum mesaj süresi: kelimeler + en uzun yanıt süresi + mesajlar arası boşluk.  LLR-1553-061
std::int64_t worstCaseDurationNs(const Message& m, std::int64_t gapNs = Timing::kMinIntermessageGapNs);

class Schedule {
public:
    struct Entry {
        Message message;
        std::uint8_t period = 1;  // her kaç küçük çerçevede bir
        std::uint8_t phase = 0;   // 0..period-1
    };
    static constexpr std::size_t kMaxEntries = 64;
    static constexpr int kMaxMajorFrame = 64;

    explicit Schedule(std::int64_t minorFrameNs) : minorFrameNs_(minorFrameNs) {}

    bool add(const Entry& e);  // LLR-1553-060
    std::size_t size() const { return count_; }
    const Entry& entry(std::size_t i) const { return entries_[i]; }
    std::int64_t minorFrameNs() const { return minorFrameNs_; }
    int majorFrameLength() const;  // periyotların EKOK'u
    bool runsIn(std::size_t i, int minorIndex) const;

    struct Check {
        bool ok = true;
        int worstMinor = 0;
        std::int64_t worstNs = 0;
        double worstUtilization = 0.0;
    };
    Check validate() const;  // her küçük çerçeve en kötü durumda sığıyor mu  LLR-1553-061

private:
    std::int64_t minorFrameNs_;
    std::array<Entry, kMaxEntries> entries_{};
    std::size_t count_ = 0;
};

class BusController {
public:
    BusController(SimClock& clock, Bus& busA, Bus& busB);

    void setMaxRetries(int n) { maxRetries_ = n; }
    void setRetryOnAlternateBus(bool v) { alternateOnRetry_ = v; }
    void setIntermessageGapNs(std::int64_t ns) { gapNs_ = ns < Timing::kMinIntermessageGapNs ? Timing::kMinIntermessageGapNs : ns; }

    Result execute(const Message& m);  // LLR-1553-012

    // Bir küçük çerçeveyi yürütür; sonuçlar giriş sırasıyla results'a yazılır.
    // Dönüş: koşan mesaj sayısı. Çerçeve sonunda saat çerçeve sınırına ilerletilir.
    std::size_t runMinorFrame(const Schedule& s, int minorIndex, Result* results, std::size_t capacity);

    const BcStats& stats() const { return stats_; }

private:
    Bus& bus(BusId id) { return id == BusId::A ? busA_ : busB_; }
    Result attempt(const Message& m, BusId id);
    Outcome collect(Bus& b, std::uint8_t expectedRt, int expectedData, StatusWord& status,
                    std::array<std::uint16_t, kMaxDataWords>* data, std::uint8_t* dataCount);
    static bool retryable(Outcome o) { return o == Outcome::NoResponse || o == Outcome::ResponseError; }

    SimClock& clock_;
    Bus& busA_;
    Bus& busB_;
    int maxRetries_ = 1;
    bool alternateOnRetry_ = true;
    std::int64_t gapNs_ = Timing::kMinIntermessageGapNs;
    BcStats stats_;
};

}  // namespace avb::m1553
