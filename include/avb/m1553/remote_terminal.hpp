#pragma once
// Uzak terminal (RT).
//
// Hat tarafı (onWord / onEndOfTransmission) Bus tarafından çağrılır; alt
// sistem tarafı (setTransmitData, readReceivedData, bayraklar) uygulamaya
// açıktır. Sınıf tek iş parçacıklıdır: iki taraf farklı görevlerden
// kullanılacaksa çağıran kilitler (bkz. rtos/ örneği).

#include <array>
#include <cstddef>
#include <cstdint>

#include "avb/m1553/bus.hpp"
#include "avb/m1553/word.hpp"

namespace avb::m1553 {

class RemoteTerminal {
public:
    static constexpr std::uint8_t kSubaddressCount = 32;

    explicit RemoteTerminal(std::uint8_t address);

    std::uint8_t address() const { return address_; }

    // ---- alt sistem tarafı
    bool setTransmitData(std::uint8_t subaddress, const std::uint16_t* data, std::size_t count);
    // Son geçerli mesajın verisi; seq her yeni mesajda artar (0 = hiç gelmedi).
    std::size_t readReceivedData(std::uint8_t subaddress, std::uint16_t* out, std::size_t capacity,
                                 std::uint32_t* seq = nullptr) const;

    void setBusy(bool v) { busy_ = v; }
    void setServiceRequest(bool v) { serviceRequest_ = v; }
    void setSubsystemFlag(bool v) { subsystemFlag_ = v; }
    void setTerminalFlag(bool v) { terminalFlag_ = v; }
    void setVectorWord(std::uint16_t v) { vectorWord_ = v; }
    void setBitWord(std::uint16_t v) { bitWord_ = v; }

    // Hata senaryoları için: yanıt gecikmesi ve tamamen susma
    void setResponseDelayNs(std::int64_t ns) { responseDelayNs_ = ns; }
    void setSilent(bool v) { silent_ = v; }

    StatusWord currentStatus() const;
    bool transmitterEnabled(BusId b) const { return txEnabled_[static_cast<int>(b)]; }
    std::uint32_t syncCount() const { return syncCount_; }
    std::uint16_t syncData() const { return syncData_; }
    std::uint32_t resetCount() const { return resetCount_; }
    std::uint32_t invalidMessageCount() const { return invalidMessages_; }

    // ---- hat tarafı (Bus çağırır)
    void onWord(BusId bus, const DecodeResult& r);
    void onEndOfTransmission(BusId bus, Source src);
    void onMessageBoundary(BusId bus);

    struct Response {
        std::array<Word, kMaxDataWords + 1> words{};
        std::uint8_t count = 0;
        std::int64_t delayNs = 0;
        BusId bus = BusId::A;
        bool pending = false;
    };
    bool hasResponse(BusId bus) const { return response_.pending && response_.bus == bus; }
    const Response& response() const { return response_; }
    void clearResponse() { response_.pending = false; }

private:
    enum class State : std::uint8_t { Idle, Receiving, RtRtWaitStatus, RtRtReceiving };

    struct RxBuffer {
        std::array<std::uint16_t, kMaxDataWords> data{};
        std::uint8_t count = 0;
        std::uint32_t seq = 0;
    };
    struct TxBuffer {
        std::array<std::uint16_t, kMaxDataWords> data{};
        std::uint8_t count = 0;
    };

    void handleCommand(BusId bus, const CommandWord& c);
    void handleModeCode(BusId bus, const CommandWord& c);
    void completeReception(BusId bus);
    void abortMessage();
    void beginFreshStatus(const CommandWord& c);
    void respond(BusId bus, const std::uint16_t* data, std::size_t count);
    void resetTerminal();

    std::uint8_t address_;
    State state_ = State::Idle;
    BusId activeBus_ = BusId::A;
    CommandWord cmd_;
    CommandWord rtRtTransmitCmd_;
    int expected_ = 0;
    int received_ = 0;
    bool messageInvalid_ = false;
    std::array<std::uint16_t, kMaxDataWords> scratch_{};

    std::array<RxBuffer, kSubaddressCount> rx_{};
    std::array<TxBuffer, kSubaddressCount> tx_{};
    Response response_;

    // durum yazmacı
    bool messageError_ = false;
    bool broadcastReceived_ = false;
    bool busy_ = false;
    bool serviceRequest_ = false;
    bool subsystemFlag_ = false;
    bool terminalFlag_ = false;

    std::array<bool, 2> txEnabled_{{true, true}};
    std::uint16_t lastCommand_ = 0;
    std::uint16_t vectorWord_ = 0;
    std::uint16_t bitWord_ = 0;
    std::uint16_t syncData_ = 0;
    std::uint32_t syncCount_ = 0;
    std::uint32_t resetCount_ = 0;
    std::uint32_t invalidMessages_ = 0;
    std::int64_t responseDelayNs_ = 8'000;
    bool silent_ = false;
};

}  // namespace avb::m1553
