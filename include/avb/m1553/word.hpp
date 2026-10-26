#pragma once
// MIL-STD-1553B kelime biçimleri.
//
// Her kelime 20 bit zamanıdır: 3 bit senkron + 16 bit bilgi + 1 bit tek eşlik.
// Komut ve durum kelimeleri aynı senkronu (önce pozitif), veri kelimeleri ters
// senkronu (önce negatif) kullanır; alıcı kelime türünü senkrondan anlar.
// Bu dosyadaki alan yerleşimleri 16 bitlik bilgi alanına göredir (bit 15 = ilk
// iletilen bit, standarttaki bit zamanı 4).

#include <cstdint>

namespace avb::m1553 {

enum class Sync : std::uint8_t { CommandStatus, Data };

struct Word {
    Sync sync = Sync::Data;
    std::uint16_t value = 0;

    friend bool operator==(const Word& a, const Word& b) { return a.sync == b.sync && a.value == b.value; }
};

constexpr std::uint8_t kBroadcastAddress = 31;  // LLR-1553-005
constexpr std::uint8_t kMaxDataWords = 32;

// Tek eşlik: 16 bilgi biti + eşlik bitindeki 1'lerin sayısı tek olmalı.  LLR-1553-002
constexpr bool parityBitFor(std::uint16_t value) {
    unsigned ones = 0;
    for (unsigned i = 0; i < 16; ++i) {
        ones += (static_cast<unsigned>(value) >> i) & 1U;
    }
    return (ones % 2U) == 0;
}

// Komut kelimesi: RT adresi(5) | T/R(1) | alt adres/mod(5) | kelime sayısı/mod kodu(5)
struct CommandWord {
    std::uint8_t rt = 0;
    bool transmit = false;  // true: RT veri gönderir (RT->BC)
    std::uint8_t subaddress = 1;
    std::uint8_t countOrCode = 0;

    static CommandWord decode(std::uint16_t v);
    std::uint16_t encode() const;

    // Alt adres 0 ve 31 mod kodu demektir.  LLR-1553-004
    bool isModeCode() const { return subaddress == 0 || subaddress == 31; }
    bool isBroadcast() const { return rt == kBroadcastAddress; }

    // Kelime sayısı alanı 0 ise 32 veri kelimesi.  LLR-1553-003
    // Mod kodlarında 0-15 verisiz, 16-31 tek veri kelimeli.
    int dataWordCount() const;
};

// Durum kelimesi: RT adresi(5) | ME | Enstrümantasyon | Servis isteği | 3 ayrılmış |
// Yayın alındı | Meşgul | Alt sistem bayrağı | Dinamik veri yolu kabulü | Terminal bayrağı
struct StatusWord {
    std::uint8_t rt = 0;
    bool messageError = false;
    bool instrumentation = false;
    bool serviceRequest = false;
    bool broadcastReceived = false;
    bool busy = false;
    bool subsystemFlag = false;
    bool dynamicBusControlAccept = false;
    bool terminalFlag = false;

    static StatusWord decode(std::uint16_t v);
    std::uint16_t encode() const;

    friend bool operator==(const StatusWord& a, const StatusWord& b) { return a.encode() == b.encode(); }
};

// Bu uygulamada desteklenen mod kodları (standarttaki numaralar).
namespace mode {
constexpr std::uint8_t kSynchronize = 1;
constexpr std::uint8_t kTransmitStatusWord = 2;
constexpr std::uint8_t kTransmitterShutdown = 4;
constexpr std::uint8_t kOverrideTransmitterShutdown = 5;
constexpr std::uint8_t kResetRemoteTerminal = 8;
constexpr std::uint8_t kTransmitVectorWord = 16;
constexpr std::uint8_t kSynchronizeWithData = 17;
constexpr std::uint8_t kTransmitLastCommand = 18;
constexpr std::uint8_t kTransmitBitWord = 19;
}  // namespace mode

}  // namespace avb::m1553
