#pragma once
// ISO 15765-2 (ISO-TP) taşıma katmanı, klasik CAN (8 bayt).
//
// 7 bayttan uzun yükler İlk Çerçeve + Ardışık Çerçevelere bölünür; alıcı Akış
// Kontrolü ile blok boyunu ve çerçeveler arası en kısa süreyi (STmin) bildirir.
// Durum makineleri taşımadan bağımsızdır: çerçeveler onFrame ile verilir,
// gönderilecekler kayıtlı fonksiyona yazılır, zaman tick ile ilerler (µs).
// Sabit 4095 baytlık tampon; dinamik bellek kullanılmaz.

#include <array>
#include <cstddef>
#include <cstdint>

#include "avb/can/frame.hpp"

namespace avb::can::isotp {

constexpr std::size_t kMaxPayload = 4095;

enum class Error : std::uint8_t {
    None,
    TimeoutBs,      // gönderen akış kontrolü beklerken zaman aşımı (N_Bs)
    TimeoutCr,      // alıcı ardışık çerçeve beklerken zaman aşımı (N_Cr)
    WrongSequence,  // ardışık çerçeve sıra numarası hatalı
    Overflow,       // alıcı tamponu yetersiz (FC OVFLW)
    InvalidFlowStatus,
    WaitLimit,      // çok fazla FC WAIT
    Busy,           // gönderim sürerken yeni gönderim
};
const char* toString(Error e);

using SendFn = bool (*)(void* ctx, const Frame& f);

// STmin bayt kodlaması: 0x00-0x7F ms, 0xF1-0xF9 100-900 µs; diğerleri 127 ms sayılır.  LLR-CAN-024
std::int64_t stMinToUs(std::uint8_t code);

struct Addressing {
    std::uint32_t txId = 0x7E0;
    std::uint32_t rxId = 0x7E8;
    bool extended = false;
    bool padding = true;  // çerçeveleri 8 bayta 0xCC ile doldur
};

class Sender {
public:
    enum class State : std::uint8_t { Idle, WaitFlowControl, Sending, Done, Failed };

    Sender(const Addressing& a, SendFn fn, void* ctx) : addr_(a), send_(fn), ctx_(ctx) {}

    std::int64_t timeoutBsUs = 1'000'000;
    int maxWait = 10;

    bool start(const std::uint8_t* payload, std::size_t len, std::int64_t nowUs);  // LLR-CAN-020
    void onFrame(const Frame& f, std::int64_t nowUs);
    void tick(std::int64_t nowUs);

    State state() const { return state_; }
    Error error() const { return error_; }
    void reset() { state_ = State::Idle; }

private:
    void emit(const std::uint8_t* pci, std::size_t pciLen, const std::uint8_t* d, std::size_t n);
    void fail(Error e);

    Addressing addr_;
    SendFn send_;
    void* ctx_;
    std::array<std::uint8_t, kMaxPayload> buf_{};
    std::size_t len_ = 0;
    std::size_t offset_ = 0;
    std::uint8_t seq_ = 0;
    std::uint8_t blockSize_ = 0;
    int blockRemaining_ = 0;
    std::int64_t stMinUs_ = 0;
    std::int64_t nextSendUs_ = 0;
    std::int64_t deadlineUs_ = 0;
    int waits_ = 0;
    State state_ = State::Idle;
    Error error_ = Error::None;
};

class Receiver {
public:
    Receiver(const Addressing& a, SendFn fn, void* ctx) : addr_(a), send_(fn), ctx_(ctx) {}

    std::uint8_t blockSize = 8;     // 0: tüm mesaj tek blok
    std::uint8_t stMin = 0;         // STmin bayt kodu
    std::size_t capacity = kMaxPayload;
    std::int64_t timeoutCrUs = 1'000'000;

    void onFrame(const Frame& f, std::int64_t nowUs);  // LLR-CAN-021
    void tick(std::int64_t nowUs);

    bool hasMessage() const { return complete_; }
    const std::uint8_t* data() const { return buf_.data(); }
    std::size_t length() const { return len_; }
    void consume() { complete_ = false; }

    Error lastError() const { return error_; }
    std::uint32_t errorCount() const { return errors_; }

private:
    void sendFlowControl(std::uint8_t status);
    void fail(Error e);

    Addressing addr_;
    SendFn send_;
    void* ctx_;
    std::array<std::uint8_t, kMaxPayload> buf_{};
    std::size_t len_ = 0;
    std::size_t got_ = 0;
    std::uint8_t expectedSeq_ = 0;
    int blockCount_ = 0;
    std::int64_t deadlineUs_ = 0;
    bool receiving_ = false;
    bool complete_ = false;
    Error error_ = Error::None;
    std::uint32_t errors_ = 0;
};

}  // namespace avb::can::isotp
